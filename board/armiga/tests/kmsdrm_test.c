/*
 * kmsdrm_test.c — Rectángulo rojo via SDL3 KMS/DRM
 * Compilar en el dispositivo:
 *   gcc kmsdrm_test.c -o kmsdrm_test $(sdl3-config --cflags --libs)
 * O cross-compilar:
 *   aarch64-linux-gnu-gcc kmsdrm_test.c -o kmsdrm_test \
 *     -I<sysroot>/usr/include/SDL3 -L<sysroot>/usr/lib -lSDL3
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;

    /* Forzar backend KMS/DRM */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "kmsdrm");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    printf("Video driver: %s\n", SDL_GetCurrentVideoDriver());

    window = SDL_CreateWindow("kmsdrm_test", 640, 480, SDL_WINDOW_FULLSCREEN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("Renderer: %s\n", SDL_GetRendererName(renderer));

    /* Rectángulo rojo 3 segundos */
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(3000);

    /* Verde — segunda comprobación */
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(2000);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("OK\n");
    return 0;
}
