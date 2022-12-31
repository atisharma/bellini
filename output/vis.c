#include "output/vis.h"


char textstr[50];
int length;
int rotate;

double peak_dB = -10.0;
double peak_l = 0, peak_r = 0;
double ppm_l = -60, ppm_r = -60;

double r = 0.5, theta = 7.0;

double fps = 30;
int dt_ms = 15;
clock_t fps_timer = 10;
clock_t last_fps_timer = 0;

buffer buffer_final;
buffer buffer_clock;


void axes_update(struct audio_data *audio, axes *ax_l, axes *ax_r) {
    double max=0, min=1e10;

    // update max, min based on peak, min audio signal
    // Alternatively, define limits using fact that audio is signed 16 bit int
    for (int n = 0; n < audio->FFTbufferSize; n++) {
        max = fmax(max, audio->in_l[n]);
        max = fmax(max, audio->in_r[n]);
        max = fmax(max, ax_l->y_max);
        max = fmax(max, ax_r->y_max);

        min = fmin(min, audio->in_l[n]);
        min = fmin(min, audio->in_r[n]);
        min = fmin(min, ax_l->y_min);
        min = fmin(min, ax_r->y_min);
    }

    ax_l->y_min = min;
    ax_r->y_min = min;
    ax_l->y_max = max;
    ax_r->y_max = max;

}


void vis_init(struct config_params *p, axes *ax_r, axes *ax_l, rgba text_c, rgba bg_c) {

    // config: font
    freetype_init(p->text_font, p->audio_font);

    /*** set up sdl display ***/
    rotate = p->rotate;

    // left channel axes
    ax_l->screen_x = 0;
    ax_l->screen_y = 0;
    ax_l->screen_w = p->width - 1;
    ax_l->screen_h = p->height;
    ax_l->x_min = log10(LOWER_CUTOFF_FREQ);
    ax_l->x_max = log10(UPPER_CUTOFF_FREQ);
    ax_l->y_min = -1000;    // dB
    ax_l->y_max = 500;

    // right channel axes are an offset copy of the left
    ax_r->screen_x = 1; // offset so l/r channels alternate pixels
    ax_r->screen_y = ax_l->screen_y;
    ax_r->screen_w = ax_l->screen_w;
    ax_r->screen_h = ax_l->screen_h;
    ax_r->x_min = ax_l->x_min;
    ax_r->x_max = ax_l->x_max;
    ax_r->y_min = ax_l->y_min;
    ax_r->y_max = ax_l->y_max;

    // plotting init
    sdl_init(p->width, p->height, &text_c, &bg_c, p->rotate, p->fullscreen);
    bf_init(&buffer_final, p->width, p->height);
    bf_clear(buffer_final);
    bf_init(&buffer_clock, p->width, p->height);

}


void vis_cleanup() {
    // free screen buffers
    bf_free_pixels(&buffer_final);
    bf_free_pixels(&buffer_clock);

    freetype_cleanup();
    sdl_cleanup();
}


void vis_sleep(long double nsec) {
    struct timespec req = {.tv_sec = 0, .tv_nsec = nsec};
    nanosleep(&req, NULL);
}


void vis_ppm(struct audio_data *audio, int window_w, axes ax_l, rgba audio_c, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c) {

    // PPM
    // peak_l and peak_r are averaged over last 5ms
    peak_l = 0;
    peak_r = 0;
    bool clip = false;
    int num_samples = (int)(5.0 * (double)audio->rate / 1000.0);
    for (int n = 0; n < num_samples; n++) {
        int i = (n + audio->index) % audio->FFTbufferSize;
        peak_l += fabs(audio->in_l[i]);
        peak_r += fabs(audio->in_r[i]);
        // clip if very very close to max possible value
        clip = clip || (fmax(fabs((double)audio->in_l[i]), fabs((double)audio->in_r[i])) >= ((2 << 15) - 2));
    }
    peak_l /= num_samples;
    peak_r /= num_samples;
    // Audio came from a signed 16-bit int, so clipping occurs at < 90.3dB.
    // The scale is defined with 0dB relative to 10dB headroom.
    // As such, subtract absolute 81dB so that instantaneous +10dB on
    // the scale is where clipping occurs.
    double max_angle = 45;
    double min_angle = 135;
    double max_dB = 5;
    double min_dB = -50;
    // Linear relation between dB and angle: theta = m*dB + c.
    double m = (max_angle - min_angle) / (max_dB - min_dB);
    double c = max_angle - m * max_dB;
    // ppm was designed with an absolute window of 800x480
    double ppm_scale = window_w / 800.0;
    // Physical dynamics of ppm meters:
    // the pole at -1.3545 corresponds to 20dB decay / 1.7s
    // as per Type I IEC 60268-10 (DIN PPM) spec.
    // ppm_l, ppm_r are the meter readings in dB.
    ppm_l = exp(-1.3545 * dt_ms / 1000) * ppm_l + fmax(20 * log10(peak_l) - 80.3, min_dB) * dt_ms / 1000;
    ppm_r = exp(-1.3545 * dt_ms / 1000) * ppm_r + fmax(20 * log10(peak_r) - 80.3, min_dB) * dt_ms / 1000;
    double angle_l = ppm_l * m + c;
    double angle_r = ppm_r * m + c;
    // Draw left and right needles on one dial.
    int r = (int)(ppm_scale * 320);     // needle radius
    int x0 = (int)buffer_final.w / 2;   // needle origin (x)
    int y0 = (int)ppm_scale * 60;       // needle origin (y)
    // render the dial to the buffer
    bf_clear(buffer_final);
    bf_text(buffer_final, "DIN PPM", 7, (int)(ppm_scale * 10), false, ax_l.screen_x + (int)(ppm_scale * 10), ax_l.screen_y + ax_l.screen_h - (int)(ppm_scale * 80), 0, audio_c);
    // dB scale markings
    for (double dB = min_dB; dB < 0; dB += 5) {
        bf_draw_ray(buffer_final, x0, y0, r+(int)(ppm_scale * 3), r+(int)(ppm_scale * 10), dB * m + c, (int)(ppm_scale * 4), ax_c);
    }
    for (double dB = min_dB; dB < 0; dB += 10) {
        bf_draw_ray(buffer_final, x0, y0, r+(int)(ppm_scale * 3), r+(int)(ppm_scale * 22), dB * m + c, (int)(ppm_scale * 4), ax_c);
    }
    // scale labels
    int x, y;
    bf_ray_xy(x0, y0, r + 30, -50 * m + c, &x, &y);
    bf_text(buffer_final, "-50", 3, (int)(ppm_scale * 8), false, x - (int)(ppm_scale * 24), y, 0, audio_c);
    bf_ray_xy(x0, y0, r + 30, c, &x, &y);
    bf_text(buffer_final, "0", 1, (int)(ppm_scale * 8), false, x + (int)(ppm_scale * 3), y + (int)(ppm_scale * 8), 0, audio_c);
    bf_ray_xy(x0, y0, r + 30, 5 * m + c, &x, &y);
    bf_text(buffer_final, "+5", 2, (int)(ppm_scale * 8), false, x, y, 0, ax2_c);
    // dB excess
    for (double dB = 0; dB <= max_dB; dB += 5) {
        bf_draw_ray(buffer_final, x0, y0, r+10, r+(int)(ppm_scale * 22), dB * m + c, (int)(ppm_scale * 4), ax2_c);
    }
    // main dial
    bf_draw_arc(buffer_final, x0, y0, r, min_dB * m + c, max_dB * m + c, (int)(ppm_scale * 2), ax_c);
    // dial excess; glow if hit
    if (ppm_l >= 0 || ppm_r >= 0 || clip) {
        rgba excess_c = ax2_c;
        excess_c.r = 255;
        bf_draw_arc(buffer_final, x0, y0, r+(int)(ppm_scale * 10), c, max_dB * m + c, (int)(ppm_scale * 6), excess_c);
    } else {
        bf_draw_arc(buffer_final, x0, y0, r+(int)(ppm_scale * 10), c, max_dB * m + c, (int)(ppm_scale * 5), ax2_c);
    }
    // readings
    bf_draw_ray(buffer_final, x0, y0, r - (int)(ppm_scale * 100), r + (int)(ppm_scale * 20), angle_l, (int)(ppm_scale * 5), plot_l_c);
    bf_draw_ray(buffer_final, x0, y0, r - (int)(ppm_scale * 100), r + (int)(ppm_scale * 20), angle_r, (int)(ppm_scale * 5), plot_r_c);
    sprintf(textstr, "%+03.0fdB", ppm_l);
    bf_text(buffer_final, textstr, 5, (int)(ppm_scale * 8), false, ax_l.screen_x + (int)(ppm_scale * 10), y0, 0, audio_c);
    sprintf(textstr, "%+03.0fdB", ppm_r);
    bf_text(buffer_final, textstr, 5, (int)(ppm_scale * 8), false, ax_l.screen_w - (int)(ppm_scale * 100), y0, 0, audio_c);
    bf_text(buffer_final, "dB", 2, (int)(ppm_scale * 16), true, 0, y0, 0, audio_c);

    // top right text: sampling rate
    sprintf(textstr, "%4.1fkHz", (double)audio->rate / 1000);
    bf_text(buffer_final, textstr, 7, (int)(ppm_scale * 10), false, ax_l.screen_x + ax_l.screen_w - (int)(ppm_scale * 120), ax_l.screen_y + ax_l.screen_h - (int)(ppm_scale * 80), 0, audio_c);

    vis_sleep(2e9 / 3000);

}


void vis_osc(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba osc_c) {

    // oscilliscope waveform plotter to framebuffer
    ax_l->y_min = -32766;
    ax_r->y_min = -32766;
    ax_l->y_max = 32766;
    ax_r->y_max = 32766;

    bf_blur(buffer_final);
    bf_shade(buffer_final, 0.8);    // 0.8 is not bad

    bf_plot_osc(buffer_final, *ax_l, audio->in_l, audio->in_r, audio->FFTbufferSize, osc_c);
    vis_sleep(2e9 / 3000);

}


void vis_pcm(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba plot_l_c, rgba plot_r_c) {

    // waveform plotter to framebuffer
    ax_l->y_min = -32766;
    ax_r->y_min = -32766;
    ax_l->y_max = 32766;
    ax_r->y_max = 32766;
    bf_clear(buffer_final);
    bf_plot_line(buffer_final, *ax_l, audio->in_l, audio->FFTbufferSize, plot_l_c);
    bf_plot_line(buffer_final, *ax_r, audio->in_r, audio->FFTbufferSize, plot_r_c);

}


void vis_fft(struct audio_data *audio, fftw_plan p_l, fftw_plan p_r, struct config_params *p, axes ax_l, axes ax_r, rgba ax_c, rgba ax2_c, rgba plot_l_c, rgba plot_r_c) {

    // window, execute FFT
    window(audio, HANN);
    fftw_execute(p_l);
    fftw_execute(p_r);

    // integrate power
    int number_of_bars = ax_l.screen_w / 2;
    int *bins_left = make_bins(audio, number_of_bars, LEFT_CHANNEL); 
    int *bins_right = make_bins(audio, number_of_bars, RIGHT_CHANNEL); 

    // FFT plotter to framebuffer
    // set plotting axes; persistent as based on bins_lr
    for (int n = 0; n < number_of_bars; n++) {
        double dB = 10 * log10(fmax(bins_left[n], bins_right[n]));
        peak_dB = fmax(dB, peak_dB);
    }
    ax_l.y_max = peak_dB;
    ax_l.y_min = peak_dB + p->noise_floor;
    ax_r.y_max = peak_dB;
    ax_r.y_min = peak_dB + p->noise_floor;

    bf_shade(buffer_final, p->persistence);
    // plot spectrum
    bf_plot_bars(buffer_final, ax_l, bins_right, number_of_bars, plot_l_c);
    bf_plot_bars(buffer_final, ax_r, bins_left, number_of_bars, plot_r_c);
    bf_plot_axes(buffer_final, ax_l, ax_c, ax2_c);

    // sleep to time with the shmem input refresh rate
    vis_sleep(2e9 / 3000);

}


void vis_polar(struct audio_data *audio, axes *ax_l, axes *ax_r, rgba plot_l_c, rgba plot_r_c) {

    // plot the sample in a polar plot
    bf_blur(buffer_final);
    bf_shade(buffer_final, 0.85);

    // last 75 ms
    double ms = 75.0;
    int num_samples = (int)fmin(audio->FFTbufferSize, (ms * (double)audio->rate / 1000.0));
    bf_plot_polar(buffer_final, *ax_l, audio->in_l, num_samples, plot_l_c);
    bf_plot_polar(buffer_final, *ax_r, audio->in_r, num_samples, plot_r_c);
    vis_sleep(2e9 / 3000);

}


void vis_julia(struct audio_data *audio, rgba col) {

    bf_blur(buffer_final);
    bf_shade(buffer_final, 0.999);

    double peak_l = 0;
    double peak_r = 0;
    // peak_l and peak_r are averaged over last 20ms
    int num_samples = (int)(20 * audio->rate / 1000.0);
    for (int n = 0; n < num_samples; n++) {
        int i = (n + audio->index) % audio->FFTbufferSize;
        peak_l += fabs(audio->in_l[i]);
        peak_r += fabs(audio->in_r[i]);
    }
    peak_l /= num_samples;
    peak_r /= num_samples;


    double a = 0.92;
    theta *= a;
    r *= a;
    theta += (1.0 - a) * log10(peak_l + peak_r + 1e-4) * 1.9;
    r += (1.0 - a) * (log10(fabs(peak_r - peak_l + 1e-4)) / 4);
    double cx = r * cos(theta);
    double cy = r * sin(theta);

    bf_plot_julia(buffer_final, cx, cy, col);

    //sprintf(textstr, "cx: %+3.2f, cy: %+3.2f", cx, cy);
    //bf_text(buffer_final, textstr, 20, 8, false, 10, 10, 0, col);
    //sprintf(textstr, "r: %+3.2f, theta: %+3.2f", r, theta);
    //bf_text(buffer_final, textstr, 22, 8, false, 10, 30, 0, col);

}


void vis_clock(int window_w, rgba text_c) {

    time_t now;
    time(&now);
    // if audio is paused wait and continue
    // show a clock, screensaver or something
    bf_clear(buffer_clock);
    double clock_scale = window_w / 800.0;
    length = strftime(textstr, sizeof(textstr), "%H:%M", localtime(&now));
    bf_text(buffer_clock, textstr, length, (int)(clock_scale * 64), true, 0, (int)(clock_scale * 200), 1, text_c);
    length = strftime(textstr, sizeof(textstr), "%a, %d %B %Y", localtime(&now));
    bf_text(buffer_clock, textstr, length, (int)(clock_scale * 14), true, 0, (int)(clock_scale * 80), 1, text_c);
    bf_blend(buffer_final, buffer_clock, 0.98);

    // wait, then check if running again.
    vis_sleep(1e8);

}


void vis_blit() {
    bf_blit(buffer_final, 15, rotate);
}
