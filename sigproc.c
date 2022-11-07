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
void window(struct audio_data *audio, int type) {
    double a0, a1, a2, a3;
    if (type == RECT) {
        // rectangular window
        a0=1; a1=0.0; a2=0; a3=0;
        //a0=0; a1=1.0; a2=0; a3=0;
    } else if (type == HANN) {
        // Hann window
        a0=0.5; a1=0.5; a2=0; a3=0;
    } else if (type == BLAC) {
        // Blackman-Nuttall window
        a0=0.3635819; a1=0.4891775; a2=0.1365995; a3=0.0106411;
    } else {
        fprintf(stderr, "Windowing type not implemented");
        exit(EXIT_FAILURE);
    }
    // detrending makes things worse
    // since there is no instrument drift
    // and the measurements are naturally centred at zero.
    int n;
    double w;
    for (int i = 0; i < audio->FFTbufferSize; i++) {
        // it's reindexed somewhere else, from the look of the waveform
        //n = (audio->index + i) % audio->FFTbufferSize;
        n = i;
        w = a0 - a1 * cos(2 * M_PI * i / audio->FFTbufferSize) + a2 * cos(4 * M_PI * i / audio->FFTbufferSize) - a3 * cos(6 * M_PI * i / audio->FFTbufferSize);
        audio->windowed_l[i] = w * audio->in_l[n];
        audio->windowed_r[i] = w * audio->in_r[n];
    }
}


// bin together power spectrum in dB
int *make_bins(struct audio_data *audio, int number_of_bins, int channel) {
    int *bins;
    register int n, i;
    double power[number_of_bins];
    fftw_complex *out;
    static int bins_left[8192];
    static int bins_right[8192];

    if (channel == LEFT_CHANNEL) {
        bins = bins_left;
        out = audio->out_l;
    } else {
        bins = bins_right;
        out = audio->out_r;
    }

    // get total signal power in each bin,
    // and space bins logarithmically.
    // freq[i] = i * rate / FFTbufferSize;
    // so log[i](i) = log(freq[i] * FFTbufferSize / rate);
    // and log[i] equally spaced over bins
    int imin = floor(LOWER_CUTOFF_FREQ * audio->FFTbufferSize / audio->rate) + 1;
    int imax = fmin(floor(UPPER_CUTOFF_FREQ * audio->FFTbufferSize / audio->rate), (audio->FFTbufferSize / 2 + 1));

    memset(power, 0, sizeof(double) * number_of_bins);

    for (i = imin; i < imax; i++) {
        // signal power
        // log bin spacing, nearest bin
        n = (int)(number_of_bins * (log(i) - log(imin)) / (log(imax) - log(imin)));
        // integrating over bins, multiply by 1/f (i here) for log f ordinate

        // hack for testing
        //printf("i: %d, imin: %d, imax: %d, n: %d, N: %d\n", i, imin, imax, n, number_of_bins);

        power[n] += (out[i][0] * out[i][0] + out[i][1] * out[i][1]) / i;
    }

    for (n = 0; n < number_of_bins; n++) {
        bins[n] = (int)((power[n] * imax) / (audio->FFTbufferSize * audio->rate));
    }
    return bins;
}
