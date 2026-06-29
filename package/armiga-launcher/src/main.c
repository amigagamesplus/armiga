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

/* Colores */
#define COL_BG        {  0,   0,   0, 255}
#define COL_WHITE     {255, 255, 255, 255}
#define COL_SELECTED  {  0,  85, 170, 255}  /* Azul Workbench */
#define COL_GRAY      {160, 160, 160, 255}

/* Opciones del menú */
static const char *MENU_ITEMS[] = {
    "Mis juegos",
    "Actualizar Armiga",
    "Salir al shell",
};
#define MENU_COUNT 3

/* Acciones */
#define ACTION_NONE    0
#define ACTION_ROMS    1
#define ACTION_UPDATE  2
#define ACTION_SHELL   3

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
    if (!window) { TTF_Quit(); SDL_Quit(); return 1; }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) { SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return 1; }

    TTF_Font *font    = TTF_OpenFont(FONT_PATH, FONT_SIZE);
    TTF_Font *font_sm = TTF_OpenFont(FONT_PATH, FONT_SIZE_SM);
    if (!font || !font_sm) {
        fprintf(stderr, "TTF_OpenFont error: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit(); SDL_Quit();
        return 1;
    }

    int selected = 0;
    int action   = ACTION_NONE;
    bool running = true;
    SDL_Event event;

    /* Abrir gamepad si está disponible */
    SDL_Gamepad *gamepad = NULL;
    int num_joysticks = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&num_joysticks);
    if (joysticks && num_joysticks > 0)
        gamepad = SDL_OpenGamepad(joysticks[0]);
    SDL_free(joysticks);

    while (running) {
        while (SDL_PollEvent(&event)) {
            /* Teclado (desarrollo) */
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_UP)
                    selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                if (event.key.key == SDLK_DOWN)
                    selected = (selected + 1) % MENU_COUNT;
                if (event.key.key == SDLK_RETURN || event.key.key == SDLK_SPACE)
                    action = selected + 1;
                if (event.key.key == SDLK_ESCAPE)
                    running = false;
            }
            /* Gamepad */
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
                    selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                    selected = (selected + 1) % MENU_COUNT;
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
                    action = selected + 1;
            }
            /* Hat (DPAD como ABS_HAT0X/Y) */
            if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
                if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    if (event.gaxis.value < -16000)
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                    else if (event.gaxis.value > 16000)
                        selected = (selected + 1) % MENU_COUNT;
                }
            }
        }

        /* Ejecutar acción */
        if (action != ACTION_NONE) {
            if (action == ACTION_SHELL) {
                /* Salir al shell */
                running = false;
            } else if (action == ACTION_ROMS) {
                /* TODO: lanzar RetroArch con browser de ROMs */
            } else if (action == ACTION_UPDATE) {
                /* TODO: actualizar Armiga */
            }
            action = ACTION_NONE;
        }

        /* --- Render --- */
        SDL_Color bg       = COL_BG;
        SDL_Color white    = COL_WHITE;
        SDL_Color selected_col = COL_SELECTED;
        SDL_Color gray     = COL_GRAY;

        SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
        SDL_RenderClear(renderer);

        /* Título */
        render_text_centered(renderer, font, "ARMIGA", white, 60.0f);

        /* Menú */
        float menu_y = 200.0f;
        float line_h = 40.0f;
        for (int i = 0; i < MENU_COUNT; i++) {
            SDL_Color col = (i == selected) ? selected_col : white;
            /* Indicador de selección */
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

    if (gamepad) SDL_CloseGamepad(gamepad);
    TTF_CloseFont(font_sm);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
