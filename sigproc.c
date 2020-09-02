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
void window(struct audio_data *audio) {
    // detrend
    double l_start = audio->in_l[audio->index];
    double l_end = audio->in_l[(audio->index + 1) % audio->FFTbufferSize];
    double r_start = audio->in_r[audio->index];
    double r_end = audio->in_r[(audio->index + 1) % audio->FFTbufferSize];
    int n;
    // Blackman-Nuttall window
    //double a0=0.3635819, a1=0.4891775, a2=0.1365995, a3=0.0106411;
    // Hann window
    double a0=0.5, a1=0.5, a2=0, a3=0;
    // rectangular window
    //double a0=1, a1=0.0, a2=0, a3=0;
    double w;
    for (int i = 0; i < audio->FFTbufferSize; i++) {
        n = (audio->index + i) % audio->FFTbufferSize;
        w = a0 - a1 * cos(2 * M_PI * n / audio->FFTbufferSize) + a2 * cos(4 * M_PI * n / audio->FFTbufferSize) - a3 * cos(6 * M_PI * n / audio->FFTbufferSize);
        audio->windowed_l[i] = w * (audio->in_l[n] - l_start - (l_end - l_start) * n / audio->FFTbufferSize);
        audio->windowed_r[i] = w * (audio->in_r[n] - r_start - (r_end - r_start) * n / audio->FFTbufferSize);
    }
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
        // log bin spacing, nearest bin
        n = (int)(number_of_bins * (log(i) - log(imin)) / log(imax / imin));
        // integrating over bins, multiply by 1/f (i here) for log f ordinate
        power[n] += (out[i][0] * out[i][0] + out[i][1] * out[i][1]) / i;
    }

    if (channel == LEFT_CHANNEL) {
        for (n = 0; n < number_of_bins; n++) {
            bins_left[n] = (int)((power[n] * imax) / (FFTbufferSize * rate));
        }
        return bins_left;
    } else {
        for (n = 0; n < number_of_bins; n++) {
            bins_right[n] = (int)((power[n] * imax) / (FFTbufferSize * rate));
        }
        return bins_right;
    }
}
