#include "input/common.h"

#include <string.h>

void reset_output_buffers(struct audio_data *data) {
    memset(data->in_r, 0, sizeof(double) * 2 * (data->FFTbufferSize / 2 + 1));
    memset(data->in_l, 0, sizeof(double) * 2 * (data->FFTbufferSize / 2 + 1));
}

int write_to_fftw_input_buffers(int16_t buf[], int16_t frames, void *data) {
    struct audio_data *audio = (struct audio_data *)data;

    for (uint16_t i = 0; i < frames * 2; i += 2) {
        // stereo storing channels in buffer
        // sadly I don't know how to memcpy every other element
        audio->in_l[audio->index] = buf[i];
        audio->in_r[audio->index] = buf[i + 1];

        audio->index++;
        if (audio->index == audio->FFTbufferSize - 1)
            audio->index = 0;
    }

    return 0;
}
