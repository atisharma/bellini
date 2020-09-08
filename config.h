#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_ERROR_LEN 1024

#ifdef ALSA
#define HAS_ALSA true
#else
#define HAS_ALSA false
#endif

#ifdef PULSE
#define HAS_PULSE true
#else
#define HAS_PULSE false
#endif

#ifdef SNDIO
#define HAS_SNDIO true
#else
#define HAS_SNDIO false
#endif

// These are in order of least-favourable to most-favourable choices, in case
// multiple are supported and configured.
enum input_method {
    INPUT_FIFO,
    INPUT_ALSA,
    INPUT_PULSE,
    INPUT_SNDIO,
    INPUT_SHMEM,
    INPUT_MAX
};

struct config_params {
    char *plot_l_col, *plot_r_col, *ax_col, *ax_2_col, *text_col, *audio_col;
    char *audio_source, *text_font, *audio_font, *vis;
    double alpha, noise_floor;
    double *userEQ;
    enum input_method im;
    int col, bgcol, fifoSample, fifoSampleBits;
};

struct error_s {
    char message[MAX_ERROR_LEN];
    int length;
};

bool load_config(char configPath[PATH_MAX], struct config_params *p, struct error_s *error);
