#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include "debug.h"
#include "framebuffer.h"
#include "fbplot.h"
#include "util.h"


FT_Library library;
FT_Face face;


void init_freetype() {
    int error = FT_Init_FreeType(&library);

    if (error) {
        fprintf(stderr,"Freetype could not initialise.\n");
    }

    error = FT_New_Face(
            library,
            TEXT_FONT,
            0,
            &face);
    if (error == FT_Err_Unknown_File_Format) {
        fprintf(stderr,"Unsupported font format.\n");
    } else if (error) {
        fprintf(stderr,"Font can't be opened.\n");
    }
      
} 

rgb666 rgb_to_rgb666(rgba c) {
    rgb666 c2 = ((c.r >> 2) << 12) | ((c.g >> 2) << 6) | (c.b >> 2);
    return c2;
}

rgba rgb666_to_rgba(rgb666 c) {
    rgba c2;
    c2.r = (uint8_t)((c >> 12) << 2);
    c2.g = (uint8_t)(((c >> 6) & 0x40) << 2);
    c2.b = (uint8_t)((c & 0x40) << 2);
    return c2;
}

uint8_t clamp(double c) {
    // prevent overflow
    return (uint8_t)fmin(255, fmax(0, c));
}

rgba tinge_color(rgba c1, rgba c2, double alpha) {
    // tinge c1 with c2, c1 <- a * c2 + (1-a) * c1
    double y1 = 0.2126 * c1.r + 0.7152 * c1.g + 0.0722 * c1.b;
    //double y2 = 0.2126 * c2.r + 0.7152 * c2.g + 0.0722 * c2.b;
    c1.r = clamp((1.0 - alpha) * (double)c1.r + y1 * alpha * (double)c2.r);
    c1.g = clamp((1.0 - alpha) * (double)c1.g + y1 * alpha * (double)c2.g);
    c1.b = clamp((1.0 - alpha) * (double)c1.b + y1 * alpha * (double)c2.b);
    c1.a = clamp((1.0 - alpha) * (double)c1.a + y1 * alpha * (double)c2.a);
    return c1;
}

void bf_init(buffer *buff) {
    // set the buffer screen size and allocate memory for pixels for the buffer
    buff->w = FRAMEBUFFER_WIDTH;
    buff->h = FRAMEBUFFER_HEIGHT;
    buff->size = buff->h * buff->w;
    debug("allocating new buffer pixels\n");
    buff->pixels = (pixel*)calloc(buff->size, sizeof(pixel));
    init_freetype();
}

void bf_free_pixels(buffer *buff) {
    free(buff->pixels);
}

void bf_set_pixel(buffer buff, uint32_t x, uint32_t y, rgba c) {
    debug("i: %d\n", x * buff.h + y);
    if ((x < buff.w) && (y < buff.h)) {
        buff.pixels[y * buff.w + x] = rgba_to_pixel(c);
    } else {
        debug("set_pixel bounds broken, x: %d, y: %d\n", (int)x, (int)y);
    }
}

void bf_clear(const buffer buff) {
    memset(buff.pixels, 0, sizeof(pixel) * buff.size);
}

void bf_fill(const buffer buff, const rgba c) {
    // FIXME: write with stride of every fourth byte
    memset(buff.pixels, rgba_to_pixel(c), sizeof(pixel) * buff.size);
}

void bf_copy(const buffer buff1, const buffer buff2) {
    // copy buff2 into buff1
    memcpy(buff1.pixels, buff2.pixels, sizeof(pixel) * ((buff1.size < buff2.size) ? buff1.size : buff2.size));
}

void bf_check_col(buffer buff) {
    for (uint32_t i = 0; i < buff.size; i++) {
        buff.pixels[i] = rgba_to_pixel(pixel_to_rgba(buff.pixels[i]));
    }
}

void bf_blend(const buffer buff1, const buffer buff2, double alpha) {
    // fade b2 into b1, of same size
    // b1 = alpha*b1 + (1-alpha)*b2
    rgba c1, c2;
    for (uint32_t i = 0; i < buff1.size; i++) {
        // separate channels
        c1 = pixel_to_rgba(buff1.pixels[i]);
        c2 = pixel_to_rgba(buff2.pixels[i]);
        // blend
        c1.r = clamp(alpha * c1.r + (1.0 - alpha) * c2.r);
        c1.g = clamp(alpha * c1.g + (1.0 - alpha) * c2.g);
        c1.b = clamp(alpha * c1.b + (1.0 - alpha) * c2.b);
        c1.a = clamp(alpha * c1.a + (1.0 - alpha) * c2.a);
        // put back
        buff1.pixels[i] = rgba_to_pixel(c1);
    }
}

void bf_shade(const buffer buff, double alpha) {
    // shade buffer into black (alpha < 1.0) or brighter (alpha > 1.0)
    // buff = alpha*buff
    rgba c;
    for (uint32_t i = 0; i < buff.size; i++) {
        // separate channels
        c = pixel_to_rgba(buff.pixels[i]);
        // blend
        c.r = clamp(alpha * c.r);
        c.g = clamp(alpha * c.g);
        c.b = clamp(alpha * c.b);
        c.a = clamp(alpha * c.a);
        buff.pixels[i] = rgba_to_pixel(c);
    }
}

void bf_tinge(const buffer buff, const rgba tc, double alpha) {
    // introduce a tinge of alpha * tc in the active pixels
    rgba c;
    for (uint32_t i = 0; i < buff.size; i++) {
        c = pixel_to_rgba(buff.pixels[i]);
        c = tinge_color(c, tc, alpha);
        buff.pixels[i] = rgba_to_pixel(c);
    }
}

void bf_grayscale(const buffer buff) {
    double y;
    rgba c;
    for (uint32_t i = 0; i < buff.size; i++) {
        // separate channels
        c = pixel_to_rgba(buff.pixels[i]);
        // linear relative luminance in [0, 1]
        y = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
        // tint
        c.g = clamp(y * 255);
        c.g = clamp(y * 255);
        c.b = clamp(y * 255);
        // put back
        buff.pixels[i] = rgba_to_pixel(c);
    }
}

void bf_superpose(const buffer buff1, const buffer buff2) {
    // superpose buff2 over buff1 where buff2 is not 0
    for (uint32_t i = 0; i < buff1.size; i++) {
        if (buff2.pixels[i]) {
            buff1.pixels[i] = buff2.pixels[i];
        }
    }
}

void bf_text(buffer buff, char *text, int num_chars, int size, int center, uint32_t x, uint32_t y, rgba c) {
    // Write text to buff.
    uint8_t w = 0;
    int pen_x = 0;
    int width = 0;
    //int pen_y;
    int n, error;
    rgba c_text = c;

    error = FT_Set_Char_Size(
            face,               /* handle to face object           */
            0,                  /* char_width in 1/64th of points  */
            size*64,            /* char_height in 1/64th of points */
            DPI,                /* horizontal device resolution    */
            DPI);               /* vertical device resolution      */

    FT_GlyphSlot slot = face->glyph;

    // get extent of the rendered text
    for (n = 0; n < num_chars; n++) {
        // Load glyph image into the slot (erasing previous one).
        // We do this again in a minute, but they should be cached anyway.
        error = FT_Load_Char(face, text[n], FT_LOAD_RENDER);
        if (error)
            continue;  /* ignore errors */
        width += slot->advance.x >> 6;
    }
    if (center)
        x = buff.w / 2 - (uint32_t)(width / 2);

    for (n = 0; n < num_chars; n++) {
        /* load glyph image into the slot (erase previous one) */
        error = FT_Load_Char(face, text[n], FT_LOAD_RENDER);
        if (error)
            continue;  /* ignore errors */

        for (uint32_t dx=0; dx < slot->bitmap.width; dx++) {
            for (uint32_t dy=0; dy < slot->bitmap.rows; dy++) {
                // use grayscale hinting because text may be any colour
                w = (double)slot->bitmap.buffer[dy * slot->bitmap.pitch + dx];
                c_text.r = (uint8_t)((c.r * w) / 255);
                c_text.g = (uint8_t)((c.g * w) / 255);
                c_text.b = (uint8_t)((c.b * w) / 255);
                if (w != 0) {
                    // render to temp buffer
                    bf_set_pixel(buff,
                            x + dx + (uint32_t)pen_x,
                            y + (uint32_t)(slot->bitmap_top) - dy,
                            c_text);
                }
            }
        }

        /* increment pen position */
        pen_x += slot->advance.x >> 6;
    }
}

void bf_draw_line(const buffer buff, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, rgba c) {
    // draw a line to the buffer
    // look at Bresenham's algorithm, or even antialiasing
    int x, y;
    int dx = x1 - x0;
    int dy = y1 - y0;
    int l = dx > dy ? dx : dy;
    for (int i = 0; i <= l; i++) {
        x = x0 + i * dx / l;
        y = y0 + i * dy / l;
        bf_set_pixel(buff, x, y, c);
    }
}

void bf_xtick(const buffer buff, const axes ax, double x, const rgba c) {
    uint32_t screen_x = ax.screen_w * (x - ax.x_min) / (ax.x_max - ax.x_min) + ax.screen_x;
    bf_draw_line(buff, screen_x, ax.screen_y, screen_x, ax.screen_y + TICK_SIZE, c);
}

void bf_ytick(const buffer buff, const axes ax, double y, const rgba c) {
    uint32_t screen_y = ax.screen_h * (y - ax.y_min) / (ax.y_max - ax.y_min) + ax.screen_y;
    bf_draw_line(buff, ax.screen_x, screen_y, ax.screen_x + TICK_SIZE, screen_y, c);
}

void bf_plot_axes(const buffer buff, const axes ax, const rgba c1, const rgba c2) {
    // draw axes around a rectangle
    // outside bounds of ax
    //bf_draw_line(buff, ax.screen_x - 10, ax.screen_y - 1, ax.screen_x + ax.screen_w, ax.screen_y - 1, c);
    //bf_draw_line(buff, ax.screen_x - 1, ax.screen_y - 10, ax.screen_x - 1, ax.screen_y + ax.screen_h, c);

    for (int n=ax.y_max; n>=ax.y_min; n-=20) {
        // dB
        bf_ytick(buff, ax, n, c1);
    }

    for (int n=1; n<10; n++) {
        // powers of 10 Hz
        bf_xtick(buff, ax, log10(n * 10), c1);
        bf_xtick(buff, ax, log10(n * 100), c1);
        bf_xtick(buff, ax, log10(n * 1000), c1);
        bf_xtick(buff, ax, log10(n * 10000), c1);
    }

    // octaves around A4 (440)
    axes ax2 = ax;
    ax2.screen_y = ax2.screen_h - 10;
    bf_xtick(buff, ax2, log10(27.5), c2);
    bf_xtick(buff, ax2, log10(55), c2);
    bf_xtick(buff, ax2, log10(110), c2);
    bf_xtick(buff, ax2, log10(220), c2);
    bf_xtick(buff, ax2, log10(440), c2);
    bf_xtick(buff, ax2, log10(440 * 2), c2);
    bf_xtick(buff, ax2, log10(440 * 4), c2);
    bf_xtick(buff, ax2, log10(440 * 8), c2);
    bf_xtick(buff, ax2, log10(440 * 16), c2);
    bf_xtick(buff, ax2, log10(440 * 32), c2);
}

void bf_plot_data(const buffer buff, const axes ax, const int data[], uint32_t num_points, rgba c) {
    // plot some data to the buffer
    uint32_t x, y;
    double s;
    //uint32_t xm = ax.screen_x;
    //uint32_t ym = (uint32_t)(ax.screen_h * (20 * log10(data[0]) - ax.y_min) / (ax.y_max - ax.y_min)) + ax.screen_y;
    for (uint32_t i=1; i < num_points; i++) {
        y = (uint32_t)(ax.screen_h * (10 * log10(data[i]) - ax.y_min) / (ax.y_max - ax.y_min)) + ax.screen_y;
        if (y > ax.screen_y) {
            x = (uint32_t)((ax.screen_w * i) / num_points) + ax.screen_x;
            /*
            for (uint32_t dx=xm; dx < x; dx++) {
                bf_draw_line(buff, dx, ax.screen_y, dx, y, c);
            }
            xm = x;
            ym = y;
            */
            for (uint32_t dy = ax.screen_y; dy < y; dy ++) {
                s = 1.1 * (double)(dy - ax.screen_y) / (double)ax.screen_h;
                rgba c2 = {clamp(c.r * s), clamp(c.g * s), clamp(c.b * s), clamp(c.a * s)};
                bf_set_pixel(buff, x, dy, c2);
            }
            bf_set_pixel(buff, x,   y,   c);
            /*
            bf_set_pixel(buff, x-1, y,   c);
            bf_set_pixel(buff, x+1, y,   c);
            bf_set_pixel(buff, x,   y+1, c);
            bf_set_pixel(buff, x,   y-1, c);
            */
        }
    }
}

void bf_blit(buffer buff) {
    // blit buffer pixels to the framebuffer
    // relies on pixel format being the same
    fb_blit(buff.pixels, buff.w, buff.h);
}

void bf_render(buffer buff) {
    // push buffer pixels to the framebuffer
    //rgba c;
    pixel p;
    for (uint32_t x = 0; x < buff.w; x++) {
        for (uint32_t y = 0; y < buff.h; y++) {
            //DEBUG("x: %d, y: %d, i: %d, size: %d \n", x, y, i, buff.size);
            p = buff.pixels[y * buff.w + x];
            //c = pixel_to_rgba(buff.pixels[y * buff.w + x]);
            //fb_set_pixel(x, y, c);
            fb_set_raw_pixel(x, y, p);
        }
    }
}
