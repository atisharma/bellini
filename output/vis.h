#include <time.h>

#include "output/sdlplot.h"
#include "sigproc.h"
#include "config.h"
#include "input/common.h"


void vis_ppm(buffer buffer_final, struct audio_data *audio, int window_w, axes ax_l, rgba audio_c, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c);
void vis_osc(buffer buffer_final, struct audio_data *audio, axes ax_l, rgba osc_c);
void vis_pcm(buffer buffer_final, struct audio_data *audio, axes ax_l, axes ax_r, rgba plot_l_c, rgba plot_r_c);
void vis_fft(buffer buffer_final, struct audio_data *audio, fftw_plan p_l, fftw_plan p_r, struct config_params *p, axes ax_l, axes ax_r, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c);
void vis_clock(buffer buffer_final, buffer buffer_clock, int window_w, rgba text_c);
