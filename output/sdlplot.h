/*
 * todo:
 *      map xy to screenx, screeny
 *      axis tick labels (log, lin)
 *      text labels
 */
// A S Sharma 2020

#pragma once

#include <inttypes.h>

#include "render.h"

#ifndef M_PI
#define M_PI 3.1415926535897932385
#endif

#define DPI 231
#define TICK_SIZE 3


typedef struct {
    // screen coordinates
    uint32_t screen_x;
    uint32_t screen_y;
    uint32_t screen_w;
    uint32_t screen_h;
    // plot data coordinates
    double x_min;
    double x_max;
    double y_min;
    double y_max;
} axes;

uint8_t clamp(double c);

uint32_t rgba_to_pixel(rgba c);
rgba pixel_to_rgba(uint32_t pixel);

rgba tinge_color(rgba c1, rgba c2, double persistence);

void freetype_init(char *text_font, char*audio_font);
void freetype_cleanup();

void bf_init(buffer *buff, int w, int h);
void bf_free_pixels(buffer *buff);

void bf_set_pixel(buffer buff, uint32_t x, uint32_t y, rgba c);

void bf_blit(buffer buff, int frame_time, int rotate);

void bf_render(buffer buff);

void bf_clear(const buffer buff);

void bf_fill(const buffer buff, const rgba c);

void bf_copy(const buffer buff1, const buffer buff2);

void bf_check_col(buffer buff);

void bf_blend(const buffer buff1, const buffer buff2, double persistence);

void bf_shade(const buffer buff, double persistence);

void bf_tinge(const buffer buff, const rgba tint_color, double persistence);

void bf_superpose(const buffer buff1, const buffer buff2);

void bf_blur(const buffer buff);

void bf_grayscale(const buffer buff);

void bf_text(buffer buff, char *text, int num_chars, int size, int center, uint32_t x, uint32_t y, int style, rgba c);

void bf_draw_line(const buffer buff, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, rgba c);

void bf_draw_arc(const buffer buff, int x0, int y0, int radius, double theta0, double theta1, int thickness, rgba c);

void bf_draw_ray(const buffer buff, int x0, int y0, int r0, int r1, double theta, int thickness, rgba c);

void bf_ray_xy(int x0, int y0, int radius, double theta, int *x, int *y);

void bf_xtick(const buffer buff, const axes ax, double x, const rgba c);
void bf_ytick(const buffer buff, const axes ax, double y, const rgba c);

void bf_plot_axes(const buffer buff, const axes ax, const rgba c1, const rgba c2);

void bf_plot_bars(const buffer buff, const axes ax, const int data[], uint32_t num_points, rgba c);
void bf_plot_line(const buffer buff, const axes ax, const double data[], uint32_t num_points, rgba c);
void bf_plot_polar(const buffer buff, const axes ax, const double data[], uint32_t num_points, uint32_t dx, rgba c);
void bf_plot_osc(const buffer buff, const axes ax, const double data_x[], const double data_y[], uint32_t num_points, rgba c);
