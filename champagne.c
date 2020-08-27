/*
 * TODO:
 *
 *  [x] fix loading correct config file
 *  [x] window(signal -> signal)                # time domain windowing (ZS)
 *  [x] window(signal -> signal)                # other window approaches
 *  [x] transform(signal -> power_spectrum)     # apply fft
 *  [ ] move transform, decay to sigproc.c
 *  [x] bin(power_spectrum -> histogram)        # gather fft in buckets (bars)
 *  [x] preplot(fft -> dB, freqs -> dB)         # turn fft data into plot data
 *  [x] render(image -> fb)                     # write array directly to framebuffer
 *  [x] plot(data -> image)                     # render plot data to array
 *  [ ] bars to be type double
 *  [x] remove noncurses output?
 *  [ ] remove raw output?
 *  [ ] remove bar style output
 *  [x] remove curses / tty output code
 *  [ ] plot raw audio signal (option)
 */

#include <locale.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else
#include <stdlib.h>
#endif

#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>

#include <ctype.h>
#include <dirent.h>
#include <fftw3.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "debug.h"
#include "util.h"

#include "input/alsa.h"
#include "input/common.h"
#include "input/fifo.h"
#include "input/portaudio.h"
#include "input/pulse.h"
#include "input/shmem.h"
#include "input/sndio.h"

#include "config.h"

#include "sigproc.h"
#include "output/framebuffer.h"
#include "output/fbplot.h"

#ifdef __GNUC__
// curses.h or other sources may already define
#undef GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif

// struct termios oldtio, newtio;
// int M = 8 * 1024;

// used by sig handler
// needs to know output mode in order to clean up terminal
int output_mode;
// whether we should reload the config or not
int should_reload = 0;
// whether we should only reload colors or not
int reload_colors = 0;

// these variables are used only in main, but making them global
// will allow us to not free them on exit without ASan complaining
struct config_params p;

fftw_complex *out_l, *out_r;
fftw_plan p_l, p_r;

// general: exit cleanly
void cleanup(void) {
    fb_cleanup();
}

// general: handle signals
void sig_handler(int sig_no) {
    if (sig_no == SIGUSR1) {
        should_reload = 1;
        return;
    }

    if (sig_no == SIGUSR2) {
        reload_colors = 1;
        return;
    }

    cleanup();
    if (sig_no == SIGINT) {
        printf("CTRL-C pressed -- goodbye\n");
    }
    signal(sig_no, SIG_DFL);
    raise(sig_no);
}

#ifdef ALSA
static bool is_loop_device_for_sure(const char *text) {
    const char *const LOOPBACK_DEVICE_PREFIX = "hw:Loopback,";
    return strncmp(text, LOOPBACK_DEVICE_PREFIX, strlen(LOOPBACK_DEVICE_PREFIX)) == 0;
}

static bool directory_exists(const char *path) {
    DIR *const dir = opendir(path);
    if (dir == NULL)
        return false;

    closedir(dir);
    return true;
}

#endif


// general: entry point
int main(int argc, char **argv) {

    // general: define variables
    pthread_t p_thread;
    int thr_id GCC_UNUSED;
    int bars[8192];
    int *bars_left, *bars_right;
    int previous_frame[8192];
    int n, c;
    struct timespec req = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec sleep_mode_timer = {.tv_sec = 0, .tv_nsec = 0};
    char configPath[PATH_MAX];
    char *usage = "\n\
Usage : " PACKAGE " [options]\n\
Visualize audio input in terminal. \n\
\n\
Options:\n\
	-p          path to config file\n\
	-v          print version\n\
\n\
Keys:\n\
        Up        Increase noise floor\n\
        Down      Decrease noise floor\n\
        Left      Decrease number of bars\n\
        Right     Increase number of bars\n\
        r         Reload config\n\
        c         Reload colors only\n\
        f         Cycle foreground color\n\
        b         Cycle background color\n\
        q         Quit\n\
\n\
as of 0.4.0 all options are specified in config file, see in '/home/username/.config/champagne/' \n";

    int number_of_bars = 25;
    int sourceIsAuto = 1;
    double peak_dB = 0;
    double dB = 0;

    struct audio_data audio;
    memset(&audio, 0, sizeof(audio));

    // framebuffer plotting init
    buffer buffer_final;
    bf_init(&buffer_final);
    bf_clear(buffer_final);
    buffer buffer_l;
    bf_init(&buffer_l);
    bf_clear(buffer_l);
    buffer buffer_r;
    bf_init(&buffer_r);
    bf_clear(buffer_r);

    // left channel axes
    axes ax_l;
    ax_l.screen_x = 0;
    ax_l.screen_y = 0;
    ax_l.screen_w = FRAMEBUFFER_WIDTH - 1;
    ax_l.screen_h = FRAMEBUFFER_HEIGHT;
    ax_l.x_min = log10(LOWER_CUTOFF_FREQ);
    ax_l.x_max = log10(UPPER_CUTOFF_FREQ);
    ax_l.y_min = -120;    // dB
    ax_l.y_max = 0;
    // right channel axes
    axes ax_r;
    ax_r.screen_x = 1;
    ax_r.screen_y = 0;
    ax_r.screen_w = FRAMEBUFFER_WIDTH - 1;
    ax_r.screen_h = FRAMEBUFFER_HEIGHT;
    ax_r.x_min = log10(LOWER_CUTOFF_FREQ);
    ax_r.x_max = log10(UPPER_CUTOFF_FREQ);
    ax_r.y_min = -120;    // dB
    ax_r.y_max = 0;

    //time_t plot_time = 0;

    /*
    foreground = '#56ff00'		# P1
    foreground = '#8cff00'		# P2
    foreground = '#ffb700' 	    # P3
    foreground = '#d2ff00' 	    # P4
    foreground = '#3300ff' 	    # P5
    foreground = '#007bff' 	    # P6
    foreground = '#b9ff00' 	    # P7
    foreground = '#007bff'		# P11
    foreground = '#ffdc00'		# P12
    foreground = '#ff2100'		# P13
    foreground = '#00ff61'		# P15
    foreground = '#610061'		# P16
    foreground = '#00ff61'		# P17
    foreground = '#92ff00'		# P18
    foreground = '#ffdf00'		# P19
    foreground = '#b3ff00'		# P20
    */
    rgba plot_c_l   = {0xFF, 0xD2, 0x00, 0x00};
    rgba plot_c_r   = {0x00, 0xFF, 0x61, 0x00};
    rgba ax_c       = {0x92, 0xFF, 0x00, 0x00};
    rgba ax_c2      = {0xF2, 0x22, 0x10, 0x00};

#ifndef NDEBUG
    int maxvalue = 0;
    int minvalue = 0;
#endif
    // general: console title
    printf("%c]0;%s%c", '\033', PACKAGE, '\007');

    configPath[0] = '\0';

    // general: handle Ctrl+C
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = &sig_handler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    // general: handle command-line arguments
    while ((c = getopt(argc, argv, "p:vh")) != -1) {
        switch (c) {
        case 'p': // argument: fifo path
            snprintf(configPath, sizeof(configPath), "%s", optarg);
            break;
        case 'h': // argument: print usage
            printf("%s", usage);
            return 1;
        case '?': // argument: print usage
            printf("%s", usage);
            return 1;
        case 'v': // argument: print version
            printf(PACKAGE " " VERSION "\n");
            return 0;
        default: // argument: no arguments; exit
            abort();
        }

        n = 0;
    }

    // general: main loop
    while (1) {

        debug("loading config\n");
        // config: load
        struct error_s error;
        error.length = 0;
        if (!load_config(configPath, &p, 0, &error)) {
            fprintf(stderr, "Error loading config. %s", error.message);
            exit(EXIT_FAILURE);
        }

        output_mode = p.om;

        // input: init
        audio.source = malloc(1 + strlen(p.audio_source));
        strcpy(audio.source, p.audio_source);

        audio.format = -1;
        audio.rate = 0;
        audio.FFTbufferSize = 8192;
        audio.terminate = 0;
        audio.channels = 2;
        audio.index = 0;
        audio.running = 1;

        audio.in_r = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
        audio.in_l = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
        memset(audio.in_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));
        memset(audio.in_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));

        out_l = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
        out_r = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
        memset(out_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
        memset(out_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));

        p_l = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.in_l, out_l, FFTW_MEASURE);
        p_r = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.in_r, out_r, FFTW_MEASURE);

        debug("got buffer size: %d, %d, %d", audio.FFTbufferSize);

        reset_output_buffers(&audio);

        debug("starting audio thread\n");
        switch (p.im) {
#ifdef ALSA
        case INPUT_ALSA:
            // input_alsa: wait for the input to be ready
            if (is_loop_device_for_sure(audio.source)) {
                if (directory_exists("/sys/")) {
                    if (!directory_exists("/sys/module/snd_aloop/")) {
                        cleanup();
                        fprintf(stderr,
                                "Linux kernel module \"snd_aloop\" does not seem to  be loaded.\n"
                                "Maybe run \"sudo modprobe snd_aloop\".\n");
                        exit(EXIT_FAILURE);
                    }
                }
            }

            thr_id = pthread_create(&p_thread, NULL, input_alsa,
                                    (void *)&audio); // starting alsamusic listener

            n = 0;

            while (audio.format == -1 || audio.rate == 0) {
                req.tv_sec = 0;
                req.tv_nsec = 1000000;
                nanosleep(&req, NULL);
                n++;
                if (n > 2000) {
                    cleanup();
                    fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
                                    "quiting...\n");
                    exit(EXIT_FAILURE);
                }
            }
            debug("got format: %d and rate %d\n", audio.format, audio.rate);
            break;
#endif
        case INPUT_FIFO:
            // starting fifomusic listener
            thr_id = pthread_create(&p_thread, NULL, input_fifo, (void *)&audio);
            audio.rate = p.fifoSample;
            audio.format = p.fifoSampleBits;
            break;
#ifdef PULSE
        case INPUT_PULSE:
            if (strcmp(audio.source, "auto") == 0) {
                getPulseDefaultSink((void *)&audio);
                sourceIsAuto = 1;
            } else
                sourceIsAuto = 0;
            // starting pulsemusic listener
            thr_id = pthread_create(&p_thread, NULL, input_pulse, (void *)&audio);
            audio.rate = 44100;
            break;
#endif
#ifdef SNDIO
        case INPUT_SNDIO:
            thr_id = pthread_create(&p_thread, NULL, input_sndio, (void *)&audio);
            audio.rate = 44100;
            break;
#endif
        case INPUT_SHMEM:
            thr_id = pthread_create(&p_thread, NULL, input_shmem, (void *)&audio);

            n = 0;

            while (audio.rate == 0) {
                req.tv_sec = 0;
                req.tv_nsec = 1000000;
                nanosleep(&req, NULL);
                n++;
                if (n > 2000) {
                    cleanup();
                    fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
                                    "quiting...\n");
                    exit(EXIT_FAILURE);
                }
            }
            debug("got format: %d and rate %d\n", audio.format, audio.rate);
            // audio.rate = 44100;
            break;
#ifdef PORTAUDIO
        case INPUT_PORTAUDIO:
            thr_id = pthread_create(&p_thread, NULL, input_portaudio, (void *)&audio);
            audio.rate = 44100;
            break;
#endif
        default:
            exit(EXIT_FAILURE); // Can't happen.
        }

        bool reloadConf = false;

        while (!reloadConf) { // jumping back to this loop means that you resized the screen

            fb_setup();
            fb_clear();

            // too many bins is noisy
            number_of_bars = ax_l.screen_w;

            bool resizeTerminal = false;

            if (p.framerate <= 1) {
                req.tv_sec = 1 / (float)p.framerate;
            } else {
                req.tv_sec = 0;
                req.tv_nsec = 1e9 / (float)p.framerate;
            }

            while (!resizeTerminal) {

                // may force reload through SIG
                if (should_reload) {
                    reloadConf = true;
                    resizeTerminal = true;
                    should_reload = 0;
                }

                if (reload_colors) {
                    struct error_s error;
                    error.length = 0;
                    if (!load_config(configPath, (void *)&p, 1, &error)) {
                        cleanup();
                        fprintf(stderr, "Error loading config. %s", error.message);
                        exit(EXIT_FAILURE);
                    }
                    resizeTerminal = true;
                    reload_colors = 0;
                }

#ifndef NDEBUG
                refresh();
#endif
                window(3, &audio);

                // process: if input was present for the last 5 frames apply FFT to it
                if (audio.running) {

                    // process: execute FFT and sort frequency bands
                    fftw_execute(p_l);
                    fftw_execute(p_r);

                    bars_left = make_bins(
                        audio.FFTbufferSize,
                        audio.rate,
                        out_l,
                        number_of_bars / 2,
                        LEFT_CHANNEL);

                    bars_right = make_bins(
                        audio.FFTbufferSize,
                        audio.rate,
                        out_r,
                        number_of_bars / 2,
                        RIGHT_CHANNEL);

                } else { // if in sleep mode wait and continue
                    // show a clock, screensaver or something
                    // for now, just fade
                    bf_shade(buffer_final, 0.999);
                    fb_vsync();
                    bf_blit(buffer_final);
                    // wait, then check if running again.
                    sleep_mode_timer.tv_sec = 0;
                    sleep_mode_timer.tv_nsec = 1e8;
                    nanosleep(&sleep_mode_timer, NULL);
                    continue;
                }


                // processing bars, after fft:
                // TODO: move these to a new function in sigproc.c     !!!
                for (n = 0; n < number_of_bars; n++) {
                    dB = 20 * log10(fmax(bars_left[number_of_bars / 2 - n - 1], bars_right[n - number_of_bars / 2]));
                    peak_dB = fmax(dB, peak_dB);
                    ax_l.y_max = peak_dB;
                    ax_l.y_min = peak_dB + p.noise_floor;
                    ax_r.y_max = peak_dB;
                    ax_r.y_min = peak_dB + p.noise_floor;

                }

#ifndef NDEBUG
                mvprintw(n + 2, 0, "min value: %d\n", minvalue); // checking maxvalue 10000
                mvprintw(n + 3, 0, "max value: %d\n", maxvalue); // checking maxvalue 10000
#endif

// output: draw processed input
#ifdef NDEBUG
                // plotting to framebuffer
                /*
                bf_clear(buffer_l);
                bf_clear(buffer_r);
                bf_plot_data(buffer_l, ax, bars_right, number_of_bars/2, plot_c_r);
                bf_plot_data(buffer_r, ax, bars_left, number_of_bars/2, plot_c_l);
                bf_blend(buffer_l, buffer_r, 0.5);
                bf_shade(buffer_l, 2);
                bf_blend(buffer_final, buffer_l, 0.5);
                bf_shade(buffer_final, 2 * p.alpha);
                */
                bf_shade(buffer_final, p.alpha);
                bf_plot_data(buffer_final, ax_l, bars_right, number_of_bars/2, plot_c_r);
                bf_plot_data(buffer_final, ax_r, bars_left, number_of_bars/2, plot_c_l);
                bf_plot_axes(buffer_final, ax_l, ax_c, ax_c2);
                fb_vsync();
                bf_blit(buffer_final);
                //printf("%3.2f FPS\r", 1.0 / (time(NULL) - plot_time));
                //plot_time = time(NULL);


#endif

                memcpy(previous_frame, bars, 8192 * sizeof(int));

                // checking if audio thread has exited unexpectedly
                if (audio.terminate == 1) {
                    cleanup();
                    fprintf(stderr, "Audio thread exited unexpectedly. %s\n", audio.error_message);
                    exit(EXIT_FAILURE);
                }

                nanosleep(&req, NULL);
            } // resize terminal

        } // reloading config
        req.tv_sec = 0;
        req.tv_nsec = 1000; // waiting some time to make sure audio is ready
        nanosleep(&req, NULL);

        //**tell audio thread to terminate**//
        audio.terminate = 1;
        pthread_join(p_thread, NULL);

        if (sourceIsAuto)
            free(audio.source);

        fftw_free(audio.in_r);
        fftw_free(audio.in_l);
        fftw_free(out_r);
        fftw_free(out_l);
        fftw_destroy_plan(p_l);
        fftw_destroy_plan(p_r);

        // free screen buffers
        bf_free_pixels(&buffer_l);
        bf_free_pixels(&buffer_r);
        bf_free_pixels(&buffer_final);

        cleanup();

        // fclose(fp);
    }
}
