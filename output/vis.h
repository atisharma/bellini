#include <time.h>

#include "output/sdlplot.h"
#include "sigproc.h"
#include "config.h"
#include "input/common.h"


void vis_init(struct config_params *p, axes *ax_r, axes *ax_l, rgba text_c, rgba bg_c);

void vis_cleanup();

void vis_ppm(struct audio_data *audio, int window_w, axes ax_l, rgba audio_c, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c);

void vis_osc(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba osc_c);

void vis_pcm(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba plot_l_c, rgba plot_r_c);

void vis_fft(struct audio_data *audio, fftw_plan p_l, fftw_plan p_r, struct config_params *p, axes ax_l, axes ax_r, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c);

void vis_polar(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba plot_l_c, rgba plot_r_c);

void vis_julia(struct audio_data *audio, rgba col);

void vis_clock(int window_w, rgba text_c);

void vis_blit();
