#pragma once

#define LEFT_CHANNEL 1
#define RIGHT_CHANNEL 2


int window(int k, void *data);

int *make_bins(int FFTbufferSize,
        unsigned int rate,
        fftw_complex out[(FFTbufferSize / 2 + 1)],
        int number_of_bins,
        int channel);
