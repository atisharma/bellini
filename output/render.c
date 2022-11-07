#include "render.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>


SDL_Window *gWindow = NULL;
SDL_Renderer *gRenderer = NULL;
SDL_Texture *gTarget = NULL;
//SDL_Texture *gBacking = NULL;

SDL_Event e;

SDL_Color fg_color = {0};
SDL_Color bg_color = {0};

// get the fullscreen size
//int w, h;
//SDL_GetRendererOutputSize(renderer, &w, &h);

void parse_color(char *color_string, SDL_Color *color) {
    if (color_string[0] == '#') {
        sscanf(++color_string, "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8,
                &color->r, &color->g, &color->b, &color->a);
    }
}

int load_font(TTF_Font* font, char* font_name, int ptsize) {
    TTF_OpenFont(font_name, ptsize);
    if (font == NULL) {
        printf("TTF could not load %s, point size %d! TTF_Error: %s\n", font_name, ptsize, TTF_GetError());
        return -1;
    }
    TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    TTF_SetFontOutline(font, 0);
    TTF_SetFontKerning(font, 1);
    TTF_SetFontHinting(font, TTF_HINTING_LIGHT);
    return 0;
}

// TODO: how does this differentiate between bg and fg col?
void sdl_init(int w, int h, rgba *fg_color, rgba *bg_color, int rotate, bool fullscreen) {

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    } else {
        if (rotate%2) {
            gWindow = SDL_CreateWindow("bellini", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, h, w, SDL_WINDOW_RESIZABLE);
        } else {
            gWindow = SDL_CreateWindow("bellini", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
        }
        if (gWindow == NULL) {
            printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        } else {
            gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
            if (gRenderer == NULL) {
                printf("Renderer could not be created! SDL Error: %s\n", SDL_GetError());
            }
        }
    }

    if (fullscreen) {
        SDL_SetWindowFullscreen(gWindow, SDL_WINDOW_FULLSCREEN);
    }
    //SDL_RenderSetVSync(gRenderer, 1);
    SDL_SetRenderDrawColor(gRenderer, bg_color->r, bg_color->g, bg_color->b, bg_color->a);
    SDL_RenderClear(gRenderer);

    SDL_SetRenderDrawColor(gRenderer, fg_color->r, fg_color->g, fg_color->b, bg_color->a);

    // pixel interpolation
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    // the main target and buffer texture, which matches pixel-to-pixel to the buffer object
    // so will have to be rotated when rendered
    gTarget = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);

    if (TTF_Init() < 0) {
        printf("TTF could not initialize! TTF_Error: %s\n", TTF_GetError());
    }
}

/*
int sdl_text(SDL_Texture *target, TTF_Font *font, char *text, int center, uint32_t x, uint32_t y, int style, rgba c) {

    int rc = 0;

    SDL_Surface* text_surface = TTF_RenderText_LCD(font, text, fg_color, bg_color);
    SDL_Texture *text_texture = SDL_CreateTextureFromSurface(gRenderer, text_surface);
    SDL_FreeSurface(text_surface);

    // render, split out into a function
    //SDL_SetRenderDrawColor(gRenderer, bg_color.r, bg_color.g, bg_color.b, bg_color.a);
    SDL_RenderClear(gRenderer);
    // last 2 args are the src and destination SDL_Rect
    SDL_RenderCopy(gRenderer, text_texture, NULL, NULL);
    SDL_RenderPresent(gRenderer);

    SDL_DestroyTexture(text_texture);

    SDL_Delay(FRAME_TIME_MSEC);

    SDL_PollEvent(&e);
    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        rc = -1;
    if (e.type == SDL_QUIT)
        rc = -2;

    return rc;
}
*/

int sdl_blit(const buffer buff, int frame_time_ms, int rotate) {

    // SDL_Delay doesn't seem to work?
    SDL_Delay(frame_time_ms);

    int rc = 0, window_w, window_h;
    SDL_GetWindowSize(gWindow, &window_w, &window_h);

    // texture matches buffer dimensions (not yet rotated)
    SDL_UpdateTexture(gTarget, NULL, buff.pixels, buff.w * sizeof(pixel));

    // work around https://stackoverflow.com/questions/28123292/sdl-rendersetscale-incorrectly-applies-to-rotated-bitmaps-in-sdl2-2-0-3
    if (rotate%2) {
        // scaling is applied before rotation
        // this works, but seems like a bug in SDL?
        double s = fmax(buff.w/buff.h, buff.h/buff.w);
        SDL_RenderSetScale(gRenderer, s * window_h/buff.w, s * window_h/buff.h);
    }
    SDL_RenderCopyExF(gRenderer, gTarget, NULL, NULL, rotate * 90, NULL, FLIP);

    SDL_RenderPresent(gRenderer);

    SDL_PollEvent(&e);
    if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
        rc = -1;
    if (e.type == SDL_QUIT)
        rc = -2;

    return rc;
}

void sdl_cleanup(void) {
    TTF_Quit();
    SDL_DestroyTexture(gTarget);
    //SDL_DestroyTexture(gBacking);
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
}
