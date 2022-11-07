#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <stdbool.h>

// SDL_FLIP_HORIZONTAL, SDL_FLIP_VERTICAL, SDL_FLIP_NONE
#define FLIP SDL_FLIP_VERTICAL


typedef uint32_t pixel;
typedef SDL_Color rgba;

// a screen buffer, local RGBA8888 format
typedef struct {
    uint32_t h;
    uint32_t w;
    uint32_t size;
    pixel *pixels;
} buffer;


void parse_color(char *color_string, SDL_Color *color);
int load_font(TTF_Font* font, char* font_name, int ptsize);
void sdl_init(int w, int h, rgba *fg_color, rgba *bg_color, int rotate, bool fullscreen);
int sdl_text(char* text, int length);
int sdl_blit(const buffer buff, int frame_time_ms, int rotate);
void sdl_cleanup(void);

