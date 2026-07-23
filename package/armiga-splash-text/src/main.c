/* armiga-splash-text: dibuja version/build/fecha + "loading, please wait..."
 * sobre el framebuffer, esquina inferior izquierda, usando SDL3+TTF
 * (misma fuente que armiga-launcher). Se ejecuta antes del launcher y
 * queda animando los puntos de "loading" hasta recibir SIGTERM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>

#define RELEASE_PATH "/etc/armiga-release"
#define FONT_PATH    "/usr/share/armiga/fonts/JetBrainsMonoNL-ExtraBold.ttf"
#define BG_PATH      "/usr/share/armiga/splash.bmp"
#define FONT_SIZE    12

#define MARGIN_X     16
#define MARGIN_Y     14
#define LINE_GAP     4

static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }

static void read_release(char *version, size_t vsz, char *build, size_t bsz,
                          char *date, size_t dsz)
{
    snprintf(version, vsz, "1.0");
    snprintf(build, bsz, "?");
    snprintf(date, dsz, "?");
    FILE *f = fopen(RELEASE_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63[^\n]", key, val) == 2) {
            if (!strcmp(key, "ARMIGA_VERSION")) snprintf(version, vsz, "%s", val);
            if (!strcmp(key, "BUILD_NUMBER"))    snprintf(build, bsz, "%s", val);
            if (!strcmp(key, "BUILD_DATE"))      snprintf(date, dsz, "%s", val);
        }
    }
    fclose(f);
}

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *t,
                       SDL_Color c, float x, float y)
{
    SDL_Surface *s = TTF_RenderText_Blended(f, t, 0, c);
    if (!s) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, s);
    if (tex) {
        SDL_FRect dst = { x, y, (float)s->w, (float)s->h };
        SDL_RenderTexture(r, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(s);
}

static int text_height(TTF_Font *f)
{
    return TTF_GetFontHeight(f);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("armiga-splash-text", 640, 480,
                                        SDL_WINDOW_FULLSCREEN);
    if (!win) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); return 1; }

    TTF_Font *font = TTF_OpenFont(FONT_PATH, FONT_SIZE);
    if (!font) { fprintf(stderr, "TTF_OpenFont: %s\n", SDL_GetError()); return 1; }

    SDL_Texture *bg_tex = IMG_LoadTexture(ren, BG_PATH);
    if (!bg_tex) fprintf(stderr, "IMG_LoadTexture: %s\n", SDL_GetError());

    char version[32], build[16], date[24];
    read_release(version, sizeof(version), build, sizeof(build), date, sizeof(date));

    char line1[64], line2[64];
    snprintf(line1, sizeof(line1), "armiga v%s", version);
    snprintf(line2, sizeof(line2), "build %s  %s", build, date);

    SDL_Color col = {170, 170, 170, 255};

    int line_h = text_height(font) + LINE_GAP;
    int win_h = 480;
    float y_base = (float)(win_h - MARGIN_Y - line_h * 3 + LINE_GAP);
    float y3 = y_base + line_h * 2;

    signal(SIGTERM, on_sigterm);
    signal(SIGINT, on_sigterm);

    const char *base_txt = "loading, please wait";
    int dots = 0;

    while (!g_stop) {
        SDL_SetRenderDrawColor(ren, 10, 10, 10, 255);
        SDL_RenderClear(ren);

        if (bg_tex) SDL_RenderTexture(ren, bg_tex, NULL, NULL);

        draw_text(ren, font, line1, col, (float)MARGIN_X, y_base);
        draw_text(ren, font, line2, col, (float)MARGIN_X, y_base + line_h);

        char line3[40];
        char dotbuf[4] = {0};
        for (int i = 0; i < dots; i++) dotbuf[i] = '.';
        snprintf(line3, sizeof(line3), "%s%s", base_txt, dotbuf);
        draw_text(ren, font, line3, col, (float)MARGIN_X, y3);

        SDL_RenderPresent(ren);

        dots = (dots + 1) % 4;

        for (int i = 0; i < 5 && !g_stop; i++)
            usleep(100000);
    }

    TTF_CloseFont(font);
    if (bg_tex) SDL_DestroyTexture(bg_tex);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
