/*
 * TODO:
 *
 *  [ ] fix loading correct config file
 *  [x] window(signal -> signal)                # time domain windowing (ZS)
 *  [x] window(signal -> signal)                # other window approaches
 *  [x] transform(signal -> power_spectrum)     # apply fft
 *  [ ] move transform to sigproc.c
 *  [x] bin(power_spectrum -> histogram)        # gather fft in buckets (bars)
 *  [ ] preplot(fft -> dB, freqs -> dB)         # turn fft data into plot data
 *  [ ] render(image -> fb)                     # write array directly to framebuffer
 *  [ ] plot(data -> image)                     # render plot data to array
 *  [ ] remove ncurses output
 *  [ ] remove noncurses output
 *  [ ] remove raw output?
 *  [ ] remove bar style output
 *  [ ] remove curses / tty output code
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

#ifdef NCURSES
#include "output/terminal_bcircle.h"
#include "output/terminal_ncurses.h"
#include <curses.h>
#endif

#include "output/raw.h"
#include "output/terminal_noncurses.h"

#include "input/alsa.h"
#include "input/common.h"
#include "input/fifo.h"
#include "input/portaudio.h"
#include "input/pulse.h"
#include "input/shmem.h"
#include "input/sndio.h"

#include "sigproc.h"

#include "config.h"

#ifdef __GNUC__
// curses.h or other sources may already define
#undef GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif

#define LEFT_CHANNEL 1
#define RIGHT_CHANNEL 2

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

// general: cleanup
void cleanup(void) {
    if (output_mode == OUTPUT_NCURSES) {
#ifdef NCURSES
        cleanup_terminal_ncurses();
#else
        ;
#endif
    } else if (output_mode == OUTPUT_NONCURSES) {
        cleanup_terminal_noncurses();
    }
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
    int bars[256];
    int *bars_left, *bars_right;
    int bars_mem[256];
    int previous_frame[256];
    int sleep = 0;
    int n, height, lines, width, c, rest, inAtty, fp, fptest, rc;
    bool silence;
    // int cont = 1;
    // float temp;
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

    char ch = '\0';
    int number_of_bars = 25;
    int sourceIsAuto = 1;

    struct audio_data audio;
    memset(&audio, 0, sizeof(audio));

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

        if (output_mode != OUTPUT_RAW) {
            // Check if we're running in a tty
            inAtty = 0;
            if (strncmp(ttyname(0), "/dev/tty", 8) == 0 || strcmp(ttyname(0), "/dev/console") == 0)
                inAtty = 1;

            // in macos vitual terminals are called ttys(xyz) and there are no ttys
            if (strncmp(ttyname(0), "/dev/ttys", 9) == 0)
                inAtty = 0;
            if (inAtty) {
                system("setfont champagne.psf  >/dev/null 2>&1");
                system("setterm -blank 0");
            }

            // We use unicode block characters to draw the bars and
            // the locale var LANG must be set to use unicode chars.
            // For some reason this var can't be retrieved with
            // setlocale(LANG, NULL), so we get it with getenv.
            // Also we can't set it with setlocale(LANG "") so we
            // must set LC_ALL instead.
            // Attempting to set to en_US if not set, if that lang
            // is not installed and LANG is not set there will be
            // no output, for more info see #109 #344
            if (!getenv("LANG"))
                setlocale(LC_ALL, "en_US.utf8");
            else
                setlocale(LC_ALL, "");
        }

        // input: init
        audio.source = malloc(1 + strlen(p.audio_source));
        strcpy(audio.source, p.audio_source);

        audio.format = -1;
        audio.rate = 0;
        audio.FFTbufferSize = 8192;
        audio.terminate = 0;
        audio.channels = 2;
        audio.index = 0;

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
            for (n = 0; n < 256; n++) {
                previous_frame[n] = 0;
                bars_mem[n] = 0;
                bars[n] = 0;
            }

            switch (output_mode) {
#ifdef NCURSES
            // output: start ncurses mode
            case OUTPUT_NCURSES:
                init_terminal_ncurses(p.color, p.bcolor, p.col, p.bgcol, p.gradient,
                                      p.gradient_count, p.gradient_colors, &width, &lines);
                // we have 8 times as much height due to using 1/8 block characters
                height = lines * 8;
                break;
#endif
            case OUTPUT_NONCURSES:
                get_terminal_dim_noncurses(&width, &lines);
                init_terminal_noncurses(inAtty, p.col, p.bgcol, width, lines, p.bar_width);
                height = (lines - 1) * 8;
                break;

            case OUTPUT_RAW:
                if (strcmp(p.raw_target, "/dev/stdout") != 0) {
                    // checking if file exists
                    if (access(p.raw_target, F_OK) != -1) {
                        // testopening in case it's a fifo
                        fptest = open(p.raw_target, O_RDONLY | O_NONBLOCK, 0644);

                        if (fptest == -1) {
                            printf("could not open file %s for writing\n", p.raw_target);
                            exit(1);
                        }
                    } else {
                        printf("creating fifo %s\n", p.raw_target);
                        if (mkfifo(p.raw_target, 0664) == -1) {
                            printf("could not create fifo %s\n", p.raw_target);
                            exit(1);
                        }
                        // fifo needs to be open for reading in order to write to it
                        fptest = open(p.raw_target, O_RDONLY | O_NONBLOCK, 0644);
                    }
                }

                fp = open(p.raw_target, O_WRONLY | O_NONBLOCK | O_CREAT, 0644);
                if (fp == -1) {
                    printf("could not open file %s for writing\n", p.raw_target);
                    exit(1);
                }
                printf("open file %s for writing raw output\n", p.raw_target);

                // width must be hardcoded for raw output.
                width = 256;

                if (strcmp(p.data_format, "binary") == 0) {
                    height = pow(2, p.bit_format) - 1;
                } else {
                    height = p.ascii_range;
                }
                break;

            default:
                exit(EXIT_FAILURE); // Can't happen.
            }

            // handle for user setting too many bars
            if (p.fixedbars) {
                p.autobars = 0;
                if (p.fixedbars * p.bar_width + p.fixedbars * p.bar_spacing - p.bar_spacing > width)
                    p.autobars = 1;
            }

            // getting original numbers of bars in case of resize
            if (p.autobars == 1) {
                number_of_bars = (width + p.bar_spacing) / (p.bar_width + p.bar_spacing);
                // if (p.bar_spacing != 0) number_of_bars = (width - number_of_bars * p.bar_spacing
                // + p.bar_spacing) / bar_width;
            } else
                number_of_bars = p.fixedbars;

            if (number_of_bars < 1)
                number_of_bars = 1; // must have at least 1 bars
            if (number_of_bars > 256)
                number_of_bars = 256; // can't have more than 256 bars

            // stereo must have even numbers of bars
            if (number_of_bars % 2 != 0)
                number_of_bars--;

            // checks if there is still extra room, will use this to center
            rest = (width - number_of_bars * p.bar_width - number_of_bars * p.bar_spacing +
                    p.bar_spacing) /
                   2;
            if (rest < 0)
                rest = 0;

#ifndef NDEBUG
            debug("height: %d width: %d bars:%d bar width: %d rest: %d\n", height, width,
                  number_of_bars, p.bar_width, rest);
#endif

            number_of_bars =
                number_of_bars / 2; // in stereo only half number of number_of_bars per channel

            // process
            double peak_dB = 0;

            for (n = 0; n < number_of_bars + 1; n++) {
                //double bar_distribution_coefficient = frequency_constant * (-1);
                //bar_distribution_coefficient +=
                    //((float)n + 1) / ((float)number_of_bars + 1) * frequency_constant;
            }

            number_of_bars = number_of_bars * 2;

            bool resizeTerminal = false;
            fcntl(0, F_SETFL, O_NONBLOCK);

            if (p.framerate <= 1) {
                req.tv_sec = 1 / (float)p.framerate;
            } else {
                req.tv_sec = 0;
                req.tv_nsec = 1e9 / (float)p.framerate;
            }

            while (!resizeTerminal) {

// general: keyboard controls
#ifdef NCURSES
                if (output_mode == OUTPUT_NCURSES)
                    ch = getch();
#endif
                if (output_mode == OUTPUT_NONCURSES)
                    ch = fgetc(stdin);

                switch (ch) {
                case 65: // key up
                    p.noise_floor = p.noise_floor + 5.0;
                    break;
                case 66: // key down
                    p.noise_floor = p.noise_floor - 5.0;
                    break;
                case 68: // key right
                    p.bar_width++;
                    resizeTerminal = true;
                    break;
                case 67: // key left
                    if (p.bar_width > 1)
                        p.bar_width--;
                    resizeTerminal = true;
                    break;
                case 'r': // reload config
                    should_reload = 1;
                    break;
                case 'c': // reload colors
                    reload_colors = 1;
                    break;
                case 'f': // change foreground color
                    if (p.col < 7)
                        p.col++;
                    else
                        p.col = 0;
                    resizeTerminal = true;
                    break;
                case 'b': // change background color
                    if (p.bgcol < 7)
                        p.bgcol++;
                    else
                        p.bgcol = 0;
                    resizeTerminal = true;
                    break;

                case 'q':
                    if (sourceIsAuto)
                        free(audio.source);
                    cleanup();
                    return EXIT_SUCCESS;
                }

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

                // if (cont == 0) break;

#ifndef NDEBUG
                // clear();
                refresh();
#endif

                // process: check if input is present
                silence = true;

                for (n = 0; n < audio.FFTbufferSize; n++) {
                    if (audio.in_l[n] || audio.in_r[n]) {
                        silence = false;
                        break;
                    }
                }

                window(3, &audio);

                if (silence)
                    sleep++;
                else
                    sleep = 0;

                // process: if input was present for the last 5 frames apply FFT to it
                if (sleep < p.framerate * 5) {

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

                } else { //**if in sleep mode wait and continue**//
#ifndef NDEBUG
                    printw("no sound detected for 5 frames, going to sleep mode\n");
#endif
                    // wait 0.1 sec, then check sound again.
                    sleep_mode_timer.tv_sec = 0;
                    sleep_mode_timer.tv_nsec = 1e8;
                    nanosleep(&sleep_mode_timer, NULL);
                    continue;
                }

                double dB = 0;
                peak_dB *= 0.9999;
                // processing bars, after fft:
                // TODO: move these to a new function in sigproc.c     !!!
                for (n = 0; n < number_of_bars; n++) {
                    // stereo channels mirrored
                    if (n < number_of_bars / 2) {
                        bars[n] = bars_left[number_of_bars / 2 - n - 1];
                    } else {
                        bars[n] = bars_right[n - number_of_bars / 2];
                    }

                    // freq domain smoothing: alpha decay
                    // may replace with Welch averaging or KS averaging
                    bars[n] = p.alpha * bars_mem[n] + (1.0 - p.alpha) * bars[n];
                    bars_mem[n] = bars[n];

                    // bar power in [0, 1] -> [peak_dB, noise_floor]dB -> bar height
                    dB = 20 * log10(bars[n]);
                    peak_dB = fmax(dB, peak_dB);
                    bars[n] = 0.9 * height * fmax((-dB + peak_dB) / p.noise_floor + 1.0, 0.0);

#ifndef NDEBUG
                    if (bars[n] < minvalue) {
                        minvalue = bars[n];
                        debug("min value: %d\n", minvalue); // checking maxvalue 10000
                    }
                    if (bars[n] > maxvalue) {
                        maxvalue = bars[n];
                    }
                    if (bars[n] < 0) {
                        debug("negative bar value!! %d\n", bars[n]);
                        //    exit(EXIT_FAILURE); // Can't happen.
                    }

#endif

                    // zero values causes divided by zero segfault (if not raw)
                    if (output_mode != OUTPUT_RAW && bars[n] < 1)
                        bars[n] = 1;

                }

#ifndef NDEBUG
                mvprintw(n + 2, 0, "min value: %d\n", minvalue); // checking maxvalue 10000
                mvprintw(n + 3, 0, "max value: %d\n", maxvalue); // checking maxvalue 10000
#endif

// output: draw processed input
#ifdef NDEBUG
                switch (output_mode) {
                case OUTPUT_NCURSES:
#ifdef NCURSES
                    rc = draw_terminal_ncurses(inAtty, lines, width, number_of_bars, p.bar_width,
                                               p.bar_spacing, rest, bars, previous_frame,
                                               p.gradient);
                    break;
#endif
                case OUTPUT_NONCURSES:
                    rc = draw_terminal_noncurses(inAtty, lines, width, number_of_bars, p.bar_width,
                                                 p.bar_spacing, rest, bars, previous_frame);
                    break;
                case OUTPUT_RAW:
                    rc = print_raw_out(number_of_bars, fp, p.is_bin, p.bit_format, p.ascii_range,
                                       p.bar_delim, p.frame_delim, bars);
                    break;

                default:
                    exit(EXIT_FAILURE); // Can't happen.
                }

                // terminal has been resized breaking to recalibrating values
                if (rc == -1)
                    resizeTerminal = true;

#endif

                memcpy(previous_frame, bars, 256 * sizeof(int));

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

        //**telling audio thread to terminate**//
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

        cleanup();

        // fclose(fp);
    }
}
