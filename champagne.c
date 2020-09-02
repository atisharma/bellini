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


// used by sig handler
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

    if (sig_no == SIGINT) {
        printf("CTRL-C pressed -- goodbye\n");
        fb_cleanup();
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
    int *bins_left, *bins_right;
    int n, c, l;
    struct timespec req = {.tv_sec = 0, .tv_nsec = 0};
    struct timespec sleep_mode_timer = {.tv_sec = 0, .tv_nsec = 0};
    time_t now;
    clock_t fps_timer = 10;
    clock_t last_fps_timer = 11;
    double fps = 30;
    double dt = 1.0;
    char textstr[40];
    char configPath[PATH_MAX];
    char *usage = "\n\
Usage : " PACKAGE " [options]\n\
Visualize audio input on the framebuffer. \n\
\n\
Options:\n\
	-p          path to config file\n\
	-v          print version\n\
\n\
as of 0.4.0 all options are specified in config file, see in '/home/username/.config/champagne/' \n";

    int number_of_bars = 25;    // bars per channel
    int sourceIsAuto = 1;
    double peak_dB = -10.0;
    double peak_l = 0, peak_r = 0, ppm_l = 0, ppm_r = 0;
    double dB = -100.0;

    struct audio_data audio;
    memset(&audio, 0, sizeof(audio));

    // left channel axes
    axes ax_l;
    ax_l.screen_x = 0;
    ax_l.screen_y = 0;
    ax_l.screen_w = FRAMEBUFFER_WIDTH - 1;
    ax_l.screen_h = FRAMEBUFFER_HEIGHT;
    ax_l.x_min = log10(LOWER_CUTOFF_FREQ);
    ax_l.x_max = log10(UPPER_CUTOFF_FREQ);
    ax_l.y_min = -1000;    // dB
    ax_l.y_max = 500;

    // right channel axes
    axes ax_r = ax_l;
    ax_r.screen_x = 1; // so l/r channels alternate pixels

    // framebuffer plotting init
    fb_setup();
    fb_clear();

    buffer buffer_final;
    bf_init(&buffer_final);
    bf_clear(buffer_final);
    buffer buffer_clock;
    bf_init(&buffer_clock);

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
    }

    // general: main loop
    while (true) {

        debug("loading config\n");
        // config: load
        struct error_s error;
        error.length = 0;
        if (!load_config(configPath, &p, 0, &error)) {
            fprintf(stderr, "Error loading config. %s", error.message);
            exit(EXIT_FAILURE);
        }

        // config: font
        init_freetype(p.font);

        // plot colours
        uint32_t r, g, b, a=0;
        sscanf(p.plot_l_col, "#%02x%02x%02x", &r, &g, &b);
        rgba plot_l_c   = {r, g, b, a};
        sscanf(p.plot_r_col, "#%02x%02x%02x", &r, &g, &b);
        rgba plot_r_c   = {r, g, b, a};
        sscanf(p.ax_col, "#%02x%02x%02x", &r, &g, &b);
        rgba ax_c   = {r, g, b, a};
        sscanf(p.ax_2_col, "#%02x%02x%02x", &r, &g, &b);
        rgba ax2_c   = {r, g, b, a};
        sscanf(p.text_col, "#%02x%02x%02x", &r, &g, &b);
        rgba text_c   = {r, g, b, a};
        sscanf(p.audio_col, "#%02x%02x%02x", &r, &g, &b);
        rgba audio_c   = {r, g, b, a};

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

        // allocate fft memory
        audio.in_r = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
        audio.in_l = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
        memset(audio.in_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));
        memset(audio.in_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(double));

        audio.windowed_r = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));
        audio.windowed_l = fftw_alloc_real(2 * (audio.FFTbufferSize / 2 + 1));

        out_l = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
        out_r = fftw_alloc_complex(2 * (audio.FFTbufferSize / 2 + 1));
        memset(out_l, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
        memset(out_r, 0, 2 * (audio.FFTbufferSize / 2 + 1) * sizeof(fftw_complex));

        p_l = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.windowed_l, out_l, FFTW_MEASURE);
        p_r = fftw_plan_dft_r2c_1d(audio.FFTbufferSize, audio.windowed_r, out_r, FFTW_MEASURE);

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
                        fb_cleanup();
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
                req.tv_nsec = 1e8;
                nanosleep(&req, NULL);
                n++;
                if (n > 2000) {
                    fb_cleanup();
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
                req.tv_nsec = 1e8;
                nanosleep(&req, NULL);
                n++;
                if (n > 2000) {
                    fb_cleanup();
                    fprintf(stderr, "could not get rate and/or format, problems with audio thread? "
                                    "quiting...\n");
                    exit(EXIT_FAILURE);
                }
            }
            debug("got format: %d and rate %d\n", audio.format, audio.rate);
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
        while (!reloadConf) {

            // alternating pixels for l/r channel
            number_of_bars = ax_l.screen_w / 2;

            bool breakMainLoop = false;
            while (!breakMainLoop) {

                // may force reload through SIG1
                if (should_reload) {
                    reloadConf = true;
                    breakMainLoop = true;
                    should_reload = 0;
                }

                if (reload_colors) {
                    struct error_s error;
                    error.length = 0;
                    if (!load_config(configPath, (void *)&p, 1, &error)) {
                        fprintf(stderr, "Error loading config. %s", error.message);
                        exit(EXIT_FAILURE);
                    }
                    breakMainLoop = true;
                    reload_colors = 0;
                }

                last_fps_timer = fps_timer;
                fps_timer = clock();
                dt = (double)(fps_timer - last_fps_timer) / CLOCKS_PER_SEC;

#ifdef NDEBUG
                // framebuffer vis

                // sync and blit
                fb_vsync();
                bf_blit(buffer_final);

                if (!audio.running) {
                    // if in sleep mode wait and continue
                    // show a clock, screensaver or something
                    bf_clear(buffer_clock);
                    time(&now);
                    l = strftime(textstr, sizeof(textstr), "%H:%M", localtime(&now));
                    bf_text(buffer_clock, textstr, l, 64, true, 0, 200, text_c);
                    l = strftime(textstr, sizeof(textstr), "%a, %d %B %Y", localtime(&now));
                    bf_text(buffer_clock, textstr, l, 14, true, 0, 80, text_c);
                    bf_blend(buffer_final, buffer_clock, 0.98);

                    // wait, then check if running again.
                    sleep_mode_timer.tv_sec = 0;
                    sleep_mode_timer.tv_nsec = 1e8;
                    nanosleep(&sleep_mode_timer, NULL);
                    continue;

                } else if (!strcmp("fft", p.vis)) {
                    // window, execute FFT
                    window(&audio);
                    fftw_execute(p_l);
                    fftw_execute(p_r);

                    // integrate power
                    bins_left = make_bins(
                        audio.FFTbufferSize,
                        audio.rate,
                        out_l,
                        number_of_bars,
                        LEFT_CHANNEL);

                    bins_right = make_bins(
                        audio.FFTbufferSize,
                        audio.rate,
                        out_r,
                        number_of_bars,
                        RIGHT_CHANNEL);

                    // FFT plotter to framebuffer
                    // set plotting axes
                    for (n = 0; n < number_of_bars; n++) {
                        dB = 10 * log10(fmax(bins_left[n], bins_right[n]));
                        peak_dB = fmax(dB, peak_dB);
                        ax_l.y_max = peak_dB;
                        ax_l.y_min = peak_dB + p.noise_floor;
                        ax_r.y_max = peak_dB;
                        ax_r.y_min = peak_dB + p.noise_floor;
                    }

                    bf_shade(buffer_final, p.alpha);
                    // plot spectrum
                    bf_plot_axes(buffer_final, ax_l, ax_c, ax2_c);
                    bf_plot_data(buffer_final, ax_l, bins_right, number_of_bars, plot_l_c);
                    bf_plot_data(buffer_final, ax_r, bins_left, number_of_bars, plot_r_c);

                } else {
                    // PPM
                    // peak over last 5ms
                    peak_l = 0;
                    peak_r = 0;
                    int i;
                    int num_samples = (int)(5.0 * (double)audio.rate / 1000.0);
                    for (n = 0; n < num_samples; n++) {
                        i = (n + audio.index) % (audio.FFTbufferSize - 1);
                        peak_l += fabs((double)audio.in_l[i]);
                        peak_r += fabs((double)audio.in_r[i]);
                    }
                    peak_l /= num_samples;
                    peak_r /= num_samples;
                    // came from a signed 16-bit int, so clipping at < 90.3dB
                    // indicate scale 0dB relative to absolute 81dB (10dB headroom)
                    // ppm in dB relative to headroom
                    double max_angle = 45;
                    double min_angle = 135;
                    double max_dB = 5;
                    double min_dB = -50;
                    double m = (max_angle - min_angle) / (max_dB - min_dB);
                    double c = max_angle - m * max_dB;
                    // physical dynamics of ppm meters
                    ppm_l = exp(-1.3545 * dt) * ppm_l + fmax(20 * log10(peak_l) - 80.3, min_dB) * dt;
                    ppm_r = exp(-1.3545 * dt) * ppm_r + fmax(20 * log10(peak_r) - 80.3, min_dB) * dt;
                    double angle_l = ppm_l * m + c;
                    double angle_r = ppm_r * m + c;
                    // draw left, right on one dial
                    int y0 = 60;
                    int r = 320;
                    int x0 = (int)buffer_final.w / 2;
                    bf_clear(buffer_final);
                    bf_text(buffer_final, "DIN PPM", 7, 8, false, ax_l.screen_x + 10, ax_l.screen_y + ax_l.screen_h - 80, audio_c);
                    // dB markings
                    for (double dB = min_dB; dB < 0; dB += 10) {
                        bf_draw_ray(buffer_final, x0, y0, r+8, r+22, dB * m + c, 3, ax_c);
                    }
                    // dB excess
                    for (double dB = 0; dB <= max_dB; dB += 5) {
                        bf_draw_ray(buffer_final, x0, y0, r+8, r+22, dB * m + c, 3, ax2_c);
                    }
                    // main dial
                    bf_draw_arc(buffer_final, x0, y0, r, min_dB * m + c, max_dB * m + c, 5, ax_c);
                    // dial excess; glow if hit
                    if ((ppm_l > -2) || (ppm_r > -2)) {
                        rgba excess_c = ax2_c;
                        excess_c.r = 255;
                        bf_draw_arc(buffer_final, x0, y0, r+10, c, max_dB * m + c, 5, excess_c);
                    } else {
                        bf_draw_arc(buffer_final, x0, y0, r+10, c, max_dB * m + c, 5, ax2_c);
                    }
                    // readings
                    bf_draw_ray(buffer_final, x0, y0, r - 100, r + 20, angle_l, 5, plot_l_c);
                    bf_draw_ray(buffer_final, x0, y0, r - 100, r + 20, angle_r, 5, plot_r_c);
                    sprintf(textstr, "%+03.0fdB", ppm_l);
                    bf_text(buffer_final, textstr, 5, 7, false, ax_l.screen_x + 10, y0, audio_c);
                    sprintf(textstr, "%+03.0fdB", ppm_r);
                    bf_text(buffer_final, textstr, 5, 7, false, ax_l.screen_w - 100, y0, audio_c);
                    bf_text(buffer_final, "dB", 2, 12, true, 0, y0, audio_c);
                }

                /* debugging info
                    sprintf(textstr, "%+7.2f peak_dB", peak_dB);
                    bf_text(buffer_final, textstr, 15, 8, false, ax_l.screen_x, ax_l.screen_y + ax_l.screen_h - 80, audio_c);
                    sprintf(textstr, "%+7.2f noise_floor", p.noise_floor);
                    bf_text(buffer_final, textstr, 19, 8, false, ax_l.screen_x, ax_l.screen_y + ax_l.screen_h - 110, audio_c);
                end debugging info */

                // stuff common to all vis
                fps = fps * 0.995 + (1.0 - 0.995) / dt;

                time(&now);
                if ((now % 20) > 15) {
                    // show FPS
                    sprintf(textstr, "%3.0ffps", fps);
                    bf_text(buffer_final, textstr, 6, 10, false, ax_l.screen_x + ax_l.screen_w - 150, ax_l.screen_y + ax_l.screen_h - 80, audio_c);
                } else if ((now % 20) > 10) {
                    // sampling rate
                    sprintf(textstr, "%4.1fkHz", (double)audio.rate / 1000);
                    bf_text(buffer_final, textstr, 7, 10, false, ax_l.screen_x + ax_l.screen_w - 150, ax_l.screen_y + ax_l.screen_h - 80, audio_c);
                } else {
                    // little clock
                    l = strftime(textstr, sizeof(textstr), "%H:%M", localtime(&now));
                    bf_text(buffer_final, textstr, l, 10, false, ax_l.screen_x + ax_l.screen_w - 130, ax_l.screen_y + ax_l.screen_h - 80, audio_c);
                }

#endif
                // check if audio thread has exited unexpectedly
                if (audio.terminate == 1) {
                    fb_cleanup();
                    fprintf(stderr, "Audio thread exited unexpectedly. %s\n", audio.error_message);
                    exit(EXIT_FAILURE);
                }
            }

        } // reload config

        req.tv_sec = 0;
        req.tv_nsec = 1000; // wait some time to make sure audio is ready
        nanosleep(&req, NULL);

        // tell input thread to terminate
        audio.terminate = 1;
        pthread_join(p_thread, NULL);

        if (sourceIsAuto)
            free(audio.source);

        // free fft working space
        fftw_free(audio.in_r);
        fftw_free(audio.in_l);
        fftw_free(audio.windowed_r);
        fftw_free(audio.windowed_l);
        fftw_free(out_r);
        fftw_free(out_l);
        fftw_destroy_plan(p_l);
        fftw_destroy_plan(p_r);

    } // config reload loop

    // free screen buffers
    bf_free_pixels(&buffer_final);
    bf_free_pixels(&buffer_clock);
    fb_cleanup();
}
