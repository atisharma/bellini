#pragma once

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fftw3.h>

struct audio_data {
    int FFTbufferSize;
    int index;
    double *in_r, *in_l, *windowed_l, *windowed_r;
    fftw_complex *out_l, *out_r;
    int format;
    unsigned int rate;
    char *source;   // alsa device, fifo path or pulse source
    int im;         // input mode alsa, fifo or pulse
    unsigned int channels;
    int terminate;  // shared variable used to terminate audio thread
    int running;    // for shmem input
    char error_message[1024];
};

void reset_output_buffers(struct audio_data *audio);

int write_to_fftw_input_buffers(int16_t buf[], int16_t frames, struct audio_data *audio);
