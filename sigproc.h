#pragma once

#define LEFT_CHANNEL 1
#define RIGHT_CHANNEL 2

#define LOWER_CUTOFF_FREQ 20
#define UPPER_CUTOFF_FREQ 20000


#include "input/common.h"

void window(struct audio_data *audio);

int *make_bins(int FFTbufferSize,
        unsigned int rate,
        fftw_complex out[FFTbufferSize / 2 + 1],
        int number_of_bins,
        int channel);
