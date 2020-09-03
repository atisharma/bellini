#pragma once

#define LEFT_CHANNEL 1
#define RIGHT_CHANNEL 2

#define LOWER_CUTOFF_FREQ 20
#define UPPER_CUTOFF_FREQ 20000

#define RECT 0
#define HANN 1
#define BLAC 2

#include "input/common.h"

void window(struct audio_data *audio, int type);

int *make_bins(struct audio_data *audio,
        int number_of_bins,
        int channel);
