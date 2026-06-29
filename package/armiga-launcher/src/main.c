#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define SCREEN_W      640
#define SCREEN_H      480
#define FONT_PATH     "/root/.config/retroarch/assets/ozone/bold.ttf"
#define FONT_SIZE     24
#define FONT_SIZE_SM  18
#define DEBOUNCE_MS   200

#define ACTION_NONE    0
#define ACTION_ROMS    1
#define ACTION_UPDATE  2
#define ACTION_SHELL   3

static const char *MENU_ITEMS[] = {
    "Mis juegos",
    "Actualizar Armiga",
    "Salir al shell",
};
#define MENU_COUNT 3

static void render_text(SDL_Renderer *r, TTF_Font *font,
                        const char *text, SDL_Color color,
                        float x, float y)
{
    SDL_Surface *s = TTF_RenderText_Blended(font, text, 0, color);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
    SDL_RenderTexture(r, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_DestroySurface(s);
}

static void render_text_centered(SDL_Renderer *r, TTF_Font *font,
                                  const char *text, SDL_Color color, float y)
{
    SDL_Surface *s = TTF_RenderText_Blended(font, text, 0, color);
    if (!s) return;
    SDL_Texture *t = SDL_CreateTextureFromSurface(r, s);
    SDL_FRect dst = {(SCREEN_W - s->w) / 2.0f, y, (float)s->w, (float)s->h};
    SDL_RenderTexture(r, t, NULL, &dst);
    SDL_DestroyTexture(t);
    SDL_DestroySurface(s);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
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
    if (!window) { TTF_Quit(); SDL_Quit(); return 1; }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) { SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }

    TTF_Font *font    = TTF_OpenFont(FONT_PATH, FONT_SIZE);
    TTF_Font *font_sm = TTF_OpenFont(FONT_PATH, FONT_SIZE_SM);
    if (!font || !font_sm) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    /* Abrir joystick para leer hats */
    SDL_Joystick *joystick = NULL;
    int num_joysticks = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
    if (joysticks && num_joysticks > 0)
        joystick = SDL_OpenJoystick(joysticks[0]);
    SDL_free(joysticks);

    int selected = 0;
    int action   = ACTION_NONE;
    bool running = true;
    Uint64 last_move = 0;
    SDL_Event event;

    while (running) {
        Uint64 now = SDL_GetTicks();

        while (SDL_PollEvent(&event)) {
            /* Teclado */
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_UP && now - last_move > DEBOUNCE_MS) {
                    selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                    last_move = now;
                }
                if (event.key.key == SDLK_DOWN && now - last_move > DEBOUNCE_MS) {
                    selected = (selected + 1) % MENU_COUNT;
                    last_move = now;
                }
                if (event.key.key == SDLK_RETURN)
                    action = selected + 1;
                if (event.key.key == SDLK_ESCAPE)
                    running = false;
            }
            /* Hat (DPAD como ABS_HAT0X/Y) */
            if (event.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                if (now - last_move > DEBOUNCE_MS) {
                    if (event.jhat.value == SDL_HAT_UP) {
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        last_move = now;
                    } else if (event.jhat.value == SDL_HAT_DOWN) {
                        selected = (selected + 1) % MENU_COUNT;
                        last_move = now;
                    }
                }
            }
            /* Botones joystick — BTN_EAST es A físico (b0 en SDL) */
            if (event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                if (event.jbutton.button == 0) /* BTN_EAST = A */
                    action = selected + 1;
            }
            /* Analógico izquierdo como fallback */
            if (event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
                if (event.jaxis.axis == 1 && now - last_move > DEBOUNCE_MS) {
                    if (event.jaxis.value < -16000) {
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        last_move = now;
                    } else if (event.jaxis.value > 16000) {
                        selected = (selected + 1) % MENU_COUNT;
                        last_move = now;
                    }
                }
            }
        }

        /* Ejecutar acción */
        if (action != ACTION_NONE) {
            if (action == ACTION_SHELL) {
                running = false;
            } else if (action == ACTION_ROMS) {
                /* TODO: lanzar RetroArch con browser de ROMs */
            } else if (action == ACTION_UPDATE) {
                /* TODO: actualizar Armiga */
            }
            action = ACTION_NONE;
        }

        /* --- Render --- */
        SDL_Color white        = {255, 255, 255, 255};
        SDL_Color selected_col = {  0,  85, 170, 255};
        SDL_Color gray         = {160, 160, 160, 255};

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        /* Título */
        render_text_centered(renderer, font, "ARMIGA", white, 60.0f);

        /* Menú */
        float menu_y = 200.0f;
        float line_h = 40.0f;
        for (int i = 0; i < MENU_COUNT; i++) {
            SDL_Color col = (i == selected) ? selected_col : white;
            if (i == selected)
                render_text(renderer, font, "\xE2\x96\xBA", selected_col,
                            180.0f, menu_y + i * line_h);
            render_text(renderer, font, MENU_ITEMS[i], col,
                        210.0f, menu_y + i * line_h);
        }

        /* Ayuda inferior */
        render_text_centered(renderer, font_sm,
            "A: Seleccionar   DPAD: Navegar", gray, 440.0f);

        SDL_RenderPresent(renderer);
    }

    if (joystick) SDL_CloseJoystick(joystick);
    TTF_CloseFont(font_sm);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
