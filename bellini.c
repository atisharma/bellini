#include <locale.h>

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else
#include <stdlib.h>
#endif

//#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

//#include <ctype.h>
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
//#include <unistd.h>

#include "debug.h"
#include "config.h"
#include "sigproc.h"
#include "util.h"

#include "input/common.h"
#include "input/alsa.h"
#include "input/fifo.h"
#include "input/pulse.h"
#include "input/shmem.h"
#include "input/sndio.h"

#include "output/sdlplot.h"
#include "output/vis.h"

#ifdef __GNUC__
// curses.h or other sources may already define
#undef GCC_UNUSED
#define GCC_UNUSED __attribute__((unused))
#else
#define GCC_UNUSED /* nothing */
#endif


bool clean_exit = false;
struct config_params p;
SDL_Event event;

// general: handle signals
void sig_handler(int sig_no) {
    if (sig_no == SIGUSR1) {
        return;
    }

    if (sig_no == SIGUSR2) {
        return;
    }

    if (sig_no == SIGINT) {
        printf("CTRL-C pressed -- exiting\n");
        clean_exit = true;
        return;
    }

    signal(sig_no, SIG_DFL);
    raise(sig_no);
}

// config: reloader
// TODO: move to config.c
void check_config_changed(char *configPath,
        rgba *plot_l_c, rgba *plot_r_c,
        rgba *ax_c, rgba *ax2_c,
        rgba *text_c, rgba *audio_c, rgba *osc_c) {
    // reload if config file has been modified
    struct stat config_stat;
    static time_t last_time = 0;
    int err = stat(configPath, &config_stat);
    if (!err) {
        if (last_time != config_stat.st_mtime) {
            last_time = config_stat.st_mtime;
            debug("config file has been modified, reloading\n");
            struct error_s error;
            error.length = 0;
            if (!load_config(configPath, (void *)&p, &error)) {
                fprintf(stderr, "Error loading config. %s", error.message);
                exit(EXIT_FAILURE);
            } else {
                // config: font
                freetype_cleanup();
                freetype_init(p.text_font, p.audio_font);
                // config: plot colours
                uint32_t r, g, b;
                sscanf(p.plot_l_col, "#%02x%02x%02x", &r, &g, &b);
                plot_l_c->r = r; plot_l_c->g = g; plot_l_c->b = b;
                sscanf(p.plot_r_col, "#%02x%02x%02x", &r, &g, &b);
                plot_r_c->r = r; plot_r_c->g = g; plot_r_c->b = b;
                sscanf(p.ax_col, "#%02x%02x%02x", &r, &g, &b);
                ax_c->r = r; ax_c->g = g; ax_c->b = b;
                sscanf(p.ax_2_col, "#%02x%02x%02x", &r, &g, &b);
                ax2_c->r = r; ax2_c->g = g; ax2_c->b = b;
                sscanf(p.text_col, "#%02x%02x%02x", &r, &g, &b);
                text_c->r = r; text_c->g = g; text_c->b = b;
                sscanf(p.audio_col, "#%02x%02x%02x", &r, &g, &b);
                audio_c->r = r; audio_c->g = g; audio_c->b = b;
                sscanf(p.osc_col, "#%02x%02x%02x", &r, &g, &b);
                osc_c->r = r; osc_c->g = g; osc_c->b = b;
            }
        }
    }
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

int main(int argc, char **argv) {

    int exit_condition = EXIT_SUCCESS;

    char *usage = "\n\
Usage : " PACKAGE " [options]\n\
Visualize audio input. \n\
\n\
Options:\n\
	-p          path to config file\n\
	-v          print version\n\
\n\
All options are specified in config file, see in '/home/username/.config/bellini/' \n";

    // general: handle Ctrl+C and other signals
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = &sig_handler;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    // general: handle command-line arguments
    int c;
    char configPath[PATH_MAX];
    configPath[0] = '\0';
    while ((c = getopt(argc, argv, "p:vh")) != -1) {
        switch (c) {
        case 'p': // argument: fifo path
            snprintf(configPath, sizeof(configPath), "%s", optarg);
            break;
        case 'h': // argument: print usage
            printf("%s", usage);
            return EXIT_FAILURE;
        case '?': // argument: print usage
            printf("%s", usage);
            return EXIT_FAILURE;
        case 'v': // argument: print version
            printf(PACKAGE " " VERSION "\n");
            return EXIT_SUCCESS;
        default: // argument: no arguments; exit
            abort();
        }
    }

    // config: load the config file
    debug("loading config\n");
    struct error_s error;
    error.length = 0;
    if (!load_config(configPath, &p, &error)) {
        fprintf(stderr, "Error loading config. %s", error.message);
        exit(EXIT_FAILURE);
    }

    // config: plot colours
    // TODO: use parse_color
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
    sscanf(p.osc_col, "#%02x%02x%02x", &r, &g, &b);
    rgba osc_c   = {r, g, b, a};
    rgba bg_c   = {0, 0, 0, 0};

    /*** set up sdl display ***/

    // channel axes
    axes ax_l, ax_r;
    vis_init(&p, &ax_r, &ax_l, text_c, bg_c);

    /*** set up audio processing ***/

    struct audio_data audio;
    audio_init(&audio, p.audio_source);

    /*** set up audio input ***/

    debug("starting audio thread\n");
    pthread_t p_thread;
    int thr_id GCC_UNUSED;

    int sourceIsAuto = 1;
    switch (p.im) {
#ifdef ALSA
    case INPUT_ALSA:
        // input_alsa: wait for the input to be ready
        if (is_loop_device_for_sure(audio.source)) {
            if (directory_exists("/sys/")) {
                if (!directory_exists("/sys/module/snd_aloop/")) {
                    sdl_cleanup();
                    fprintf(stderr,
                            "Linux kernel module \"snd_aloop\" does not seem to  be loaded.\n"
                            "Maybe run \"sudo modprobe snd_aloop\".\n");
                    exit(EXIT_FAILURE);
                }
            }
        }

        thr_id = pthread_create(&p_thread, NULL, input_alsa,
                                (void *)&audio); // starting alsamusic listener

        int n = 0;

        while (audio.format == -1 || audio.rate == 0) {
            struct timespec req = {.tv_sec = 0, .tv_nsec = 1e8};
            nanosleep(&req, NULL);
            n++;
            if (n > 2000) {
                sdl_cleanup();
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
            struct timespec req = {.tv_sec = 0, .tv_nsec = 1e8};
            nanosleep(&req, NULL);
            n++;
            if (n > 2000) {
                sdl_cleanup();
                fprintf(stderr, "could not get rate problems with audio thread? "
                                "quiting...\n");
                exit(EXIT_FAILURE);
            }
        }
        debug("got audio rate %d\n", audio.rate);
        break;
    default:
        exit(EXIT_FAILURE); // Can't happen.
    }

    /*** main loop ***/

    // loop-scope variables
    time_t now;

    while (!clean_exit) {

        time(&now);

        // if config file is modified, reloads every 5s
        if ((now % 5) == 0) {
            check_config_changed(configPath,
                    &plot_l_c, &plot_r_c,
                    &ax_c, &ax2_c,
                    &text_c, &audio_c, &osc_c);
        }

#ifdef NDEBUG

        // check for keypresses
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_KEYDOWN:
                    switch( event.key.keysym.sym ) {
                        case SDLK_q:
                            clean_exit = true;
                            break;
                        case SDLK_v:
                            if (!strcmp("fft", p.vis)) {
                                p.vis = "ppm";
                            } else if (!strcmp("ppm", p.vis)) {
                                p.vis = "pcm";
                            } else if (!strcmp("pcm", p.vis)) {
                                p.vis = "osc";
                            } else if (!strcmp("osc", p.vis)) {
                                p.vis = "pol";
                            } else if (!strcmp("pol", p.vis)) {
                                p.vis = "fft";
                            }
                            break;
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (!strcmp("fft", p.vis)) {
                        p.vis = "ppm";
                    } else if (!strcmp("ppm", p.vis)) {
                        p.vis = "pcm";
                    } else if (!strcmp("pcm", p.vis)) {
                        p.vis = "osc";
                    } else if (!strcmp("osc", p.vis)) {
                        p.vis = "pol";
                    } else if (!strcmp("pol", p.vis)) {
                        p.vis = "fft";
                    }
                    break;
                case SDL_QUIT:
                    clean_exit = true;
                    break;
            }
        }

        vis_blit();
        if (!audio.running) {
            vis_clock(p.width, text_c);
        } else if (!strcmp("fft", p.vis)) {
            vis_fft(&audio, audio.p_l, audio.p_r, &p, ax_l, ax_r, ax_c, ax2_c, plot_l_c, plot_r_c);
        } else if (!strcmp("pcm", p.vis)) {
            vis_pcm(&audio, &ax_l, &ax_r, plot_l_c, plot_r_c);
        } else if (!strcmp("osc", p.vis)) {
            vis_osc(&audio, &ax_l, &ax_r, osc_c);
        } else if (!strcmp("pol", p.vis)) {
            vis_polar(&audio, &ax_l, &ax_r, plot_l_c, plot_r_c);
        } else {
            vis_ppm(&audio, p.width, ax_l, audio_c, ax_c, ax2_c, plot_l_c, plot_r_c);
        }

#endif
        // check if audio thread has exited unexpectedly
        if (audio.terminate == 1) {
            fprintf(stderr, "Audio thread exited unexpectedly. %s\n", audio.error_message);
            break;
            exit_condition = EXIT_FAILURE;
        }

    }

    /*** exit ***/

    // tell input thread to terminate
    audio.terminate = 1;
    pthread_join(p_thread, NULL);

    vis_cleanup();
    audio_cleanup(&audio, sourceIsAuto);

    return exit_condition;
}
