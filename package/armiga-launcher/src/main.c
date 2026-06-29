#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#define SCREEN_W 640
#define SCREEN_H 480
#define FONT_PATH "/root/.config/retroarch/assets/ozone/bold.ttf"
#define FONT_SIZE 24
int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Armiga",
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    TTF_Font *font = TTF_OpenFont(FONT_PATH, FONT_SIZE);
    if (!font) {
        fprintf(stderr, "TTF_OpenFont error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    /* Bucle principal */
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_KEY_DOWN &&
                event.key.key == SDLK_ESCAPE)
                running = false;
        }
        /* Fondo negro */
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        /* Texto de prueba centrado */
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface *surface = TTF_RenderText_Blended(font, "ARMIGA", 0, white);
        if (surface) {
            SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FRect dst = {
                (SCREEN_W - surface->w) / 2.0f,
                (SCREEN_H - surface->h) / 2.0f,
                (float)surface->w,
                (float)surface->h
            };
            SDL_RenderTexture(renderer, texture, NULL, &dst);
            SDL_DestroyTexture(texture);
            SDL_DestroySurface(surface);
        }
        SDL_RenderPresent(renderer);
    }
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
