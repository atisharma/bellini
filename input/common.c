#include "input/common.h"
#include "debug.h"

#include <string.h>


void audio_init(struct audio_data *audio, char *audio_source) {
    memset(audio, 0, sizeof(*audio));

    // input: init
    audio->source = malloc(1 + strlen(audio_source));
    strcpy(audio->source, audio_source);

    audio->format = -1;
    audio->rate = 0;
    audio->FFTbufferSize = 8192;
    audio->terminate = 0;
    audio->channels = 2;
    audio->index = 0;
    audio->running = 1;

    // allocate fft memory
    audio->in_r = fftw_alloc_real(2 * (audio->FFTbufferSize / 2 + 1));
    audio->in_l = fftw_alloc_real(2 * (audio->FFTbufferSize / 2 + 1));
    memset(audio->in_r, 0, 2 * (audio->FFTbufferSize / 2 + 1) * sizeof(double));
    memset(audio->in_l, 0, 2 * (audio->FFTbufferSize / 2 + 1) * sizeof(double));

    audio->windowed_r = fftw_alloc_real(2 * (audio->FFTbufferSize / 2 + 1));
    audio->windowed_l = fftw_alloc_real(2 * (audio->FFTbufferSize / 2 + 1));

    audio->out_l = fftw_alloc_complex(2 * (audio->FFTbufferSize / 2 + 1));
    audio->out_r = fftw_alloc_complex(2 * (audio->FFTbufferSize / 2 + 1));
    memset(audio->out_l, 0, 2 * (audio->FFTbufferSize / 2 + 1) * sizeof(fftw_complex));
    memset(audio->out_r, 0, 2 * (audio->FFTbufferSize / 2 + 1) * sizeof(fftw_complex));

    audio->p_l = fftw_plan_dft_r2c_1d(audio->FFTbufferSize, audio->windowed_l, audio->out_l, FFTW_MEASURE);
    audio->p_r = fftw_plan_dft_r2c_1d(audio->FFTbufferSize, audio->windowed_r, audio->out_r, FFTW_MEASURE);

    debug("got buffer size: %d", audio->FFTbufferSize);

    reset_output_buffers(audio);

}

void audio_cleanup(struct audio_data *audio, int sourceIsAuto) {

    if (sourceIsAuto)
        free(audio->source);

    // free fft working space
    fftw_free(audio->in_r);
    fftw_free(audio->in_l);
    fftw_free(audio->windowed_r);
    fftw_free(audio->windowed_l);
    fftw_free(audio->out_r);
    fftw_free(audio->out_l);
    fftw_destroy_plan(audio->p_l);
    fftw_destroy_plan(audio->p_r);
    fftw_cleanup();
}

void reset_output_buffers(struct audio_data *audio) {
    memset(audio->in_r, 0, sizeof(double) * 2 * (audio->FFTbufferSize / 2 + 1));
    memset(audio->in_l, 0, sizeof(double) * 2 * (audio->FFTbufferSize / 2 + 1));
}

int write_to_fftw_input_buffers(int16_t buf[], int16_t frames, struct audio_data *audio) {

    for (uint16_t i = 0; i < frames * 2; i += 2) {
        // separate the two stereo channels
        audio->in_l[audio->index] = buf[i];
        audio->in_r[audio->index] = buf[i + 1];

        audio->index++;
        if (audio->index == audio->FFTbufferSize - 1)
            audio->index = 0;
    }

    return 0;
}
