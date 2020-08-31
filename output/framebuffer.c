// A framebuffer plotter
// =====================
//
// framebuffer documentation:
// https://www.kernel.org/doc/Documentation/fb/api.txt
//
// example fb writer at:
// https://www.i-programmer.info/programming/cc/12839-applying-c-framebuffer-graphics.html
//
// A S Sharma 2020


//#define _POSIX_C_SOURCE  199309L

#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <fcntl.h> 
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "debug.h"
#include "framebuffer.h"

#include <unistd.h>


struct fb_fix_screeninfo finfo;
struct fb_var_screeninfo vinfo;

uint8_t *fbp;
int fd;


void fb_setup() {
    vinfo.grayscale = 0;
    vinfo.bits_per_pixel = 32;
    fd = open("/dev/fb0", O_RDWR);
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);
    // this pointer is global
    fbp = mmap(0, vinfo.yres * finfo.line_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
}

int fb_cleanup() {
    fb_clear();
    return munmap(fbp, vinfo.yres * finfo.line_length);
}

struct fb_var_screeninfo *get_vinfo() {
    return &vinfo;
};

uint32_t rgba_to_pixel(rgba c) {
    uint32_t pixel = (c.r << vinfo.red.offset)|
        (c.g << vinfo.green.offset) |
        (c.b << vinfo.blue.offset) |
        (c.a << vinfo.transp.offset);
    return pixel;
}

rgba pixel_to_rgba(pixel p) {
    rgba c;
    c.r = (p >> vinfo.red.offset) & 0xFF;
    c.g = (p >> vinfo.green.offset) & 0xFF;
    c.b = (p >> vinfo.blue.offset) & 0xFF;
    c.a = (p >> vinfo.transp.offset) & 0xFF;
    return c;
}

uint32_t fb_get_raw_pixel(uint32_t x, uint32_t y) {
    uint32_t location = x * (vinfo.bits_per_pixel / 8) + (vinfo.yres - y - 1) * finfo.line_length;
    return *((uint32_t*) (fbp + location));
}

void fb_set_raw_pixel(uint32_t x, uint32_t y, uint32_t pixel) {
    uint32_t location = x * (vinfo.bits_per_pixel / 8) + (vinfo.yres - y - 1) * finfo.line_length;
    *((uint32_t*) (fbp + location)) = pixel;
}

void fb_set_pixel(uint32_t x, uint32_t y, rgba c) {
    fb_set_raw_pixel(x, y, rgba_to_pixel(c));
}

void fb_blit(uint32_t *pixels, uint32_t line_length, uint32_t lines) {
    // Copy whole lines at a time.
    // Can be further optimised if *pixels is same size
    // as fb, to copy in one go.
    if ((finfo.line_length == line_length) && (vinfo.yres >= lines)) {
        // may be upside down here!
        memcpy(fbp, pixels, sizeof(uint32_t) * finfo.line_length * lines);
    } else {
        uint32_t location, pixels_loc;
        for (uint32_t y=0; y<lines; y++) {
            location = (vinfo.yres - y - 1) * finfo.line_length;
            pixels_loc = y * line_length;
            memcpy(fbp + location, pixels + pixels_loc, sizeof(uint32_t) * line_length);
        }
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t X, uint32_t Y, rgba c) {
    for (uint32_t i = 0; i < X; i++) {
        for (uint32_t j = 0; j < Y; j++) {
            fb_set_pixel(x + i, y + j, c);
        }
    }
}

void fb_clear() {
    memset(fbp, 0, finfo.line_length * vinfo.yres);
}

void fb_vsync() {
    ioctl(fd, FBIO_WAITFORVSYNC, 0);
}

void fb_draw_line_fb(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, rgba c) {
    // a rediscovery of Bresenham's algorithm
    uint32_t x, y;
    uint32_t dx = x1 - x0;
    uint32_t dy = y1 - y0;
    uint32_t l = dx > dy ? dx : dy;
    for (uint32_t i = 0; i <= l; i++) {
        x = x0 + i * dx / l;
        y = y0 + i * dy / l;
        fb_set_pixel(x, y, c);
    }
}
