#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#else
#include <stdlib.h>
#endif

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include <fftw3.h>
#include <sys/types.h>

#include "input/common.h"
#include "sigproc.h"

#include "debug.h"
#include "util.h"

// apply window in-place on audio data
int window(int k, void *data) {
    struct audio_data *audio = (struct audio_data *)data;
    double temp[audio->FFTbufferSize];

    // detrend
    double l_start = audio->in_l[0];
    double l_end = audio->in_l[audio->FFTbufferSize - 1];
    double r_start = audio->in_r[0];
    double r_end = audio->in_r[audio->FFTbufferSize - 1];
    for (int i = 0; i < audio->FFTbufferSize; i++) {
        audio->in_l[i] = (audio->in_l[i] - l_start) / (l_end - l_start);
        audio->in_r[i] = (audio->in_r[i] - r_start) / (r_end - r_start);
    }
    
    // Kolmogorov-Zurbenko (k, m=3) filter: repeated average of buffers
    for (int ki = 0; ki < k; ki++) {
        memcpy(temp, audio->in_l, sizeof(double) * audio->FFTbufferSize);
        for (int i = 1; i < audio->FFTbufferSize; i++) {    // careful with bounds checking for m
            audio->in_l[i] = (temp[i-1] + temp[i] + temp[i+1]) / 3.0;
        }
        memcpy(temp, audio->in_r, sizeof(double) * audio->FFTbufferSize);
        for (int i = 1; i < audio->FFTbufferSize; i++) {    // careful with bounds checking for m
            audio->in_r[i] = (temp[i-1] + temp[i] + temp[i+1]) / 3.0;
        }
    }
    return 0;
}


// bin together power spectrum in dB
int *make_bins(int FFTbufferSize,
        unsigned int rate,
        fftw_complex out[FFTbufferSize / 2 + 1],
        int number_of_bins,
        int channel) {
    register int n, i;
    double power[number_of_bins];
    static int bins_left[8192];
    static int bins_right[8192];
    int L = FFTbufferSize / 2 + 1;
    double y[L];
    register double w = 1.0 / (FFTbufferSize * rate);

    // get total signal power in each bin,
    // and space bins logarithmically.
    // freq[i] = i * rate / FFTbufferSize;
    // so log[i](i) = log(freq[i] * FFTbufferSize / rate);
    // and log[i] equally spaced over bins
    int imin = floor(LOWER_CUTOFF_FREQ * FFTbufferSize / rate);
    int imax = fmin(floor(UPPER_CUTOFF_FREQ * FFTbufferSize / rate), (FFTbufferSize / 2 + 1));

    memset(power, 0, sizeof(double) * number_of_bins);

    for (i = imin; i < imax; i++) {
        // signal power
        y[i] = out[i][0] * out[i][0] + out[i][1] * out[i][1];
        // log bin spacing, nearest bin
        n = (int)(number_of_bins * (log(i) - log(imin)) / log(imax / imin));
        // integrating over bins, multiply by 1/f for log f ordinate
        power[n] += y[i] * w * imax / i;
    }

    if (channel == LEFT_CHANNEL) {
        for (n = 0; n < number_of_bins; n++) {
            bins_left[n] = power[n];
        }
        return bins_left;
    } else {
        for (n = 0; n < number_of_bins; n++) {
            bins_right[n] = power[n];
        }
        return bins_right;
    }
}
