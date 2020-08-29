#pragma once

#include <linux/fb.h>
#include <inttypes.h>

#define CSI "\e["

typedef uint32_t pixel;

typedef struct  {
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t a;
} rgba;

typedef struct {
    uint32_t h;
    uint32_t w;
    int *pixels; // fix this
} image;


void fb_setup();

int fb_cleanup();

struct fb_var_screeninfo *get_vinfo();

uint32_t rgba_to_pixel(rgba c);

rgba pixel_to_rgba(pixel p);

uint32_t fb_get_raw_pixel(uint32_t x, uint32_t y);

void fb_set_raw_pixel(uint32_t x, uint32_t y, uint32_t pixel);

void fb_set_pixel(uint32_t x, uint32_t y, rgba c);

void fb_blit(uint32_t *pixels, uint32_t line_length, uint32_t lines);

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t X, uint32_t Y, rgba c);

void fb_clear();

void fb_vsync();

void fb_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, rgba c);
