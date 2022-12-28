#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "debug.h"
#include "util.h"

#include "sdlplot.h"

FT_Library library;
FT_Face text_face;
FT_Face audio_face;


uint32_t rgba_to_pixel(rgba c) {
    uint32_t pixel = ((uint32_t)c.r << 24) |
        ((uint32_t)c.g << 16) |
        ((uint32_t)c.b << 8) |
        ((uint32_t)c.a << 0);
    return pixel;
}

rgba pixel_to_rgba(uint32_t pixel) {
    rgba c = {
        (uint8_t)((pixel >> 24) & 0xFF),
        (uint8_t)((pixel >> 16) & 0xFF),
        (uint8_t)((pixel >> 8) & 0xFF),
        (uint8_t)((pixel >> 0) & 0xFF)
    };
    return c;
}

void freetype_init(char *text_font, char *audio_font) {
    int error = FT_Init_FreeType(&library);
    if (error) {
        fprintf(stderr,"Freetype could not initialise.\n");
    }

    int error1 = FT_New_Face(
            library,
            audio_font,
            0,
            &audio_face);

    int error2 = FT_New_Face(
            library,
            text_font,
            0,
            &text_face);

    if (error1 == FT_Err_Unknown_File_Format || error2 == FT_Err_Unknown_File_Format) {
        fprintf(stderr,"Unsupported font format.\n");
    } else if (error1 || error2) {
        fprintf(stderr,"Font can't be opened.\n");
    }
} 

void freetype_cleanup() {
    FT_Done_Face(text_face);
    FT_Done_Face(audio_face);
    FT_Done_FreeType(library);
}

uint8_t clamp(double c) {
    // prevent overflow
    return (uint8_t)min(255, max(0, c));
}

void bf_blend(const buffer buff1, const buffer buff2, double persistence) {
    // fade b2 into b1, of same size
    // b1 = persistence*b1 + (1-persistence)*b2
    register double r1, g1, b1, a1;
    register double r2, g2, b2, a2;
    register pixel p;
    for (uint32_t i = 0; i < buff1.size; i++) {
        // separate channels
        p = buff1.pixels[i];
        r1 = ((p >> 24) & 0xFF) * persistence;
        g1 = ((p >> 16) & 0xFF) * persistence;
        b1 = ((p >> 8) & 0xFF) * persistence;
        a1 = ((p >> 0) & 0xFF) * persistence;
        p = buff2.pixels[i];
        r2 = ((p >> 24) & 0xFF) * (1.0 - persistence);
        g2 = ((p >> 16) & 0xFF) * (1.0 - persistence);
        b2 = ((p >> 8) & 0xFF) * (1.0 - persistence);
        a2 = ((p >> 0) & 0xFF) * (1.0 - persistence);
        // blend
        p = ((int)r1 << 24)    |
            ((int)g1 << 16)    |
            ((int)b1 << 8)     |
            ((int)a1 << 0);
        p += ((int)r2 << 24)   |
             ((int)g2 << 16)   |
             ((int)b2 << 8)    |
             ((int)a2 << 0);
        // put back
        buff1.pixels[i] = p;
    }
}

void bf_shade(const buffer buff, double persistence) {
    // shade buffer into black (persistence < 1.0) or brighter (persistence > 1.0)
    // buff = persistence*buff
    register int r, g, b, a;
    register pixel p;
    for (uint32_t i = 0; i < buff.size; i++) {
        // separate channels
        p = buff.pixels[i];
        r = (p >> 24) & 0xFF;
        g = (p >> 16) & 0xFF;
        b = (p >> 8) & 0xFF;
        a = (p >> 0) & 0xFF;
        // blend
        r = (int)(persistence * r);
        g = (int)(persistence * g);
        b = (int)(persistence * b);
        a = (int)(persistence * a);
        p = (r << 24)    |
            (g << 16)  |
            (b << 8)   |
            (a << 0);
        buff.pixels[i] = p;
    }
}

void bf_init(buffer *buff, int w, int h) {
    // set the buffer screen size and allocate memory for pixels for the buffer
    buff->w = w;
    buff->h = h;
    buff->size = buff->h * buff->w;
    debug("allocating new buffer pixels\n");
    buff->pixels = (pixel*)calloc(buff->size, sizeof(pixel));
}

void bf_free_pixels(buffer *buff) {
    free(buff->pixels);
}

void bf_set_pixel(buffer buff, uint32_t x, uint32_t y, rgba c) {
    debug("i: %d\n", x * buff.h + y);
    buff.pixels[(y % (uint32_t)buff.h) * buff.w + (x % (uint32_t)buff.w)] = rgba_to_pixel(c);
}

rgba bf_get_pixel(buffer buff, uint32_t x, uint32_t y) {
    debug("i: %d\n", x * buff.h + y);
    return pixel_to_rgba(buff.pixels[(y % (uint32_t)buff.h) * buff.w + (x % (uint32_t)buff.w)]);
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

void bf_superpose(const buffer buff1, const buffer buff2) {
    // superpose buff2 over buff1 where buff2 is not 0
    for (uint32_t i = 0; i < buff1.size; i++) {
        if (buff2.pixels[i]) {
            buff1.pixels[i] = buff2.pixels[i];
        }
    }
}

void bf_blur(const buffer buff) {
    // ghetto blur
    for (uint32_t x1=1; x1 < buff.w - 1; x1++) {
        for (uint32_t y1=1; y1 < buff.h - 1; y1++) {
            uint32_t x2 = x1 + rand()%3 - 1;
            uint32_t y2 = y1 + rand()%3 - 1;
            rgba c1 = bf_get_pixel(buff, x1, y1);
            rgba c2 = bf_get_pixel(buff, x2, y2);
            rgba c3 = {
                c1.r/2 + c2.r/2,
                c1.g/2 + c2.g/2,
                c1.b/2 + c2.b/2,
                c1.a
            };
            bf_set_pixel(buff, x1, y1, c3);
            bf_set_pixel(buff, x2, y2, c3);
        }
    }
}

void bf_text(buffer buff, char *text, int num_chars, int size, int center, uint32_t x, uint32_t y, int style, rgba c) {
    // Write text to buff.
    int w = 0;
    int pen_x = 0;
    int width = 0;
    //int pen_y;
    int n, error;
    rgba c_text = c;

    FT_Face face;

    if (style) {
        face = text_face;
    } else {
        face = audio_face;
    }

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
                w = slot->bitmap.buffer[dy * slot->bitmap.pitch + dx];
                c_text.r = (uint8_t)(c.r * w >> 8);
                c_text.g = (uint8_t)(c.g * w >> 8);
                c_text.b = (uint8_t)(c.b * w >> 8);
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
    register int x, y;
    register int dx = x1 - x0;
    register int dy = y1 - y0;
    register int l = dx > dy ? dx : dy;
    for (register int i = 0; i <= l; i++) {
        x = x0 + i * dx / l;
        y = y0 + i * dy / l;
        bf_set_pixel(buff, x, y, c);
    }
}

void bf_draw_arc(const buffer buff, int x0, int y0, int radius, double theta0, double theta1, int thickness, rgba c) {
    // Draw an arc with origin x0, y0, from angle theta0 to theta1
    // NB: x0, y0 are int as they can be negative.
    int r, x, y, xmin, xmax;
    rgba c2 = c;
    c2.r /= 2;
    c2.g /= 2;
    c2.b /= 2;
    // sweep over all x1 < x < x2 and find y
    // smooth the edges
    r = radius;
    xmin = x0 + (int)(r * cos(theta0 * 2 * M_PI / 360));
    xmax = x0 + (int)(r * cos(theta1 * 2 * M_PI / 360));
    for (x = (int)xmin; x <= (int)xmax; x++) {
        y = y0 + sqrt(r * r - (x - x0) * (x - x0));
        bf_set_pixel(buff, (uint32_t)(x + 1), (uint32_t)(y - 1), c2);
        bf_set_pixel(buff, (uint32_t)(x - 1), (uint32_t)(y - 1), c2);
    }
    r = radius + thickness;
    xmin = x0 + (int)(r * cos(theta0 * 2 * M_PI / 360));
    xmax = x0 + (int)(r * cos(theta1 * 2 * M_PI / 360));
    for (x = (int)xmin; x <= (int)xmax; x++) {
        y = y0 + sqrt(r * r - (x - x0) * (x - x0));
        bf_set_pixel(buff, (uint32_t)(x + 1), (uint32_t)(y + 1), c2);
        bf_set_pixel(buff, (uint32_t)(x - 1), (uint32_t)(y + 1), c2);
    }
    // for those x, y, paint the pixels
    for (r = radius; r <= radius + thickness; r++) {
        xmin = x0 + (int)(r * cos(theta0 * 2 * M_PI / 360));
        xmax = x0 + (int)(r * cos(theta1 * 2 * M_PI / 360));
        for (x = (int)xmin; x <= (int)xmax; x++) {
            y = y0 + sqrt(r * r - (x - x0) * (x - x0));
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)y, c);
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)(y + 1), c);
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)(y - 1), c);
        }
    }
}

void bf_draw_ray(const buffer buff, int x0, int y0, int r0, int r1, double theta, int thickness, rgba c) {
    // Draw a ray with origin x0, y0, angle theta, from r0 to r1
    // NB: x0, y0 are int as they can be negative.
    int r, x, y, dx;
    rgba c2 = c;
    c2.r /= 2;
    c2.g /= 2;
    c2.b /= 2;
    // for those x, y, paint the pixels
    for (r = r0; r <= r1; r++) {
        for (dx = 0; dx < thickness; dx++) {
            x = dx + x0 + (int)(r * cos(theta * 2 * M_PI / 360));
            y = y0 + (int)(r * sin(theta * 2 * M_PI / 360));
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)(y - 1), c2);
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)(y + 1), c2);
        }
    }
    for (r = r0; r <= r1; r++) {
        for (dx = 0; dx < thickness; dx++) {
            x = dx + x0 + (int)(r * cos(theta * 2 * M_PI / 360));
            y = y0 + (int)(r * sin(theta * 2 * M_PI / 360));
            bf_set_pixel(buff, (uint32_t)x, (uint32_t)y, c);
        }
    }
}

void bf_ray_xy(int x0, int y0, int radius, double theta, int *x, int *y) {
    *x = x0 + (int)(radius * cos(theta * 2 * M_PI / 360));
    *y = y0 + (int)(radius * sin(theta * 2 * M_PI / 360));
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

void bf_plot_bars(const buffer buff, const axes ax, const int data[], uint32_t num_points, rgba c) {
    // plot some data to the buffer
    register uint32_t x, y, dy;
    register uint8_t r, g, b, a;
    register pixel p;
    for (uint32_t i=0; i < num_points; i++) {
        y = (uint32_t)(ax.screen_h * (10 * log10(data[i]) - ax.y_min) / (ax.y_max - ax.y_min)) + ax.screen_y;
        // y can overflow, so check bounds
        if (y > ax.screen_y && y < buff.h) {
            x = (uint32_t)((ax.screen_w * i) / num_points) + ax.screen_x;
            // draw peaks
            bf_set_pixel(buff, x,   y,   c);
            // draw faded lines up to y
            // raw pixels for speed
            for (dy = ax.screen_y; dy < y; dy++) {
                r = (c.r * (dy - ax.screen_y) / ax.screen_h);
                g = (c.g * (dy - ax.screen_y) / ax.screen_h);
                b = (c.b * (dy - ax.screen_y) / ax.screen_h);
                a = (c.a * (dy - ax.screen_y) / ax.screen_h);
                p = (r << 24) |
                    (g << 16) |
                    (b << 8) |
                    (a << 0);
                buff.pixels[(dy % (uint32_t)buff.h) * buff.w + (x % (uint32_t)buff.w)] = p;
            }
        }
    }
}

void bf_plot_line(const buffer buff, const axes ax, const double data[], uint32_t num_points, rgba c) {
    // plot some data to the buffer, cartesian plot
    register uint32_t x, y;
    for (uint32_t i=1; i < num_points; i++) {
        x = (uint32_t)((ax.screen_w * i) / num_points) + ax.screen_x;
        y = (uint32_t)((ax.screen_h * (data[i] - ax.y_min)) / (ax.y_max - ax.y_min)) + ax.screen_y;
        bf_set_pixel(buff, x, y, c);
    }
}

void bf_plot_polar(const buffer buff, const axes ax, const double data[], uint32_t num_points, rgba c) {
    // plot some data to the buffer, polar plot

    register uint32_t dx = (buff.w > buff.h) ? (buff.w - buff.h) / 2 : 0;
    register uint32_t dy = (buff.w < buff.h) ? (buff.h - buff.w) / 2 : 0;
    register uint32_t x, y;
    register double theta, r, l;
    for (uint32_t i=0; i < num_points; i++) {
        theta = 2.0 * M_PI * i / num_points;
        l = fmin(ax.screen_w, ax.screen_h) / 2;
        r = fabs(0.45 + (data[i] - ax.y_min) / (ax.y_max - ax.y_min) / 2.8);
        x = (uint32_t)(ax.screen_x + l * (r * cos(theta) + 1)) + dx;
        y = (uint32_t)(ax.screen_y + l * (r * sin(theta) + 1)) + dy;
        bf_set_pixel(buff, x, y,   c);
    }
}

void bf_plot_osc(const buffer buff, const axes ax, const double data_x[], const double data_y[], uint32_t num_points, rgba c) {
    // plot some data to the buffer like an oscilliscope

    // model afterimage with complementary colour
    rgba c_afterimage = {
        c.g/4 + c.b/4,
        c.r/4 + c.b/4,
        c.r/4 + c.g/4,
        c.a
    };

    register double h = fmin(ax.screen_w, ax.screen_h);
    register double range = ax.y_max - ax.y_min;

    register uint32_t x=(uint32_t)data_x[0], y=(uint32_t)data_y[0];
    register uint32_t dx = (buff.w > buff.h) ? (buff.w - buff.h) / 2 : 0;
    register uint32_t dy = (buff.w < buff.h) ? (buff.h - buff.w) / 2 : 0;

    // impersonate an afterimage
    // to get a really clean afterimage, you would have to do some additive trace tricks.
    for (uint32_t i=0; i < num_points - 1; i+=2) {
        x = (uint32_t)(h * (data_x[i] - ax.y_min) / range) + ax.screen_x + dx;
        y = (uint32_t)(h * (data_y[i] - ax.y_min) / range) + ax.screen_y + dy;

        bf_set_pixel(buff, x+1, y, c_afterimage);
        bf_set_pixel(buff, x-1, y, c_afterimage);
        bf_set_pixel(buff, x, y+1, c_afterimage);
        bf_set_pixel(buff, x, y-1, c_afterimage);
    }

    // draw the new image
    for (uint32_t i=0; i < num_points - 1; i++) {
        x = (uint32_t)(h * (data_x[i] - ax.y_min) / range) + ax.screen_x + dx;
        y = (uint32_t)(h * (data_y[i] - ax.y_min) / range) + ax.screen_y + dy;
        bf_set_pixel(buff, x, y, c);
    }

}

void bf_blit(buffer buff, int frame_time, int rotate) {
    // blit buffer pixels to the SDL texture
    // relies on pixel format being the same
    sdl_blit(buff, frame_time, rotate);
}

