#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "logo.h"

#define SCREEN_W  640
#define SCREEN_H  480

#define FONT_PATH    "/usr/share/armiga/fonts/JetBrainsMonoNL-ExtraBold.ttf"
#define FONT_TITLE   36
#define FONT_MED     13
#define FONT_SM      12

#define COL_BG       { 26,  26,  26, 255}
#define COL_GREEN    {  0, 255, 136, 255}
#define COL_DKGREEN  {  0, 119,  68, 255}
#define COL_WHITE    {220, 220, 220, 255}
#define COL_GRAY     {136, 136, 136, 255}
#define COL_SEL_BG   { 42,  42,  42, 255}
#define COL_RED      {200,  40,  40, 255}

#define ARMIGA_VERSION "armiga v1.0"

typedef enum {
    STATE_MENU,
    STATE_DEVMODE,
    STATE_CONFIRM
} AppState;

typedef enum {
    EXEC_NONE,
    EXEC_SHELL,
    EXEC_BTOP,
    EXEC_REBOOT,
    EXEC_SHUTDOWN
} ExecRequest;

#define ACTION_NONE    0
#define ACTION_ROMS    1
#define ACTION_UPDATE  2
#define ACTION_INFO    3
#define ACTION_SHELL   4

static const char *MENU_ICONS[] = {
    "[>]",
    "[~]",
    "[i]",
    "[$]",
};

static const char *MENU_ITEMS[] = {
    "Mis juegos",
    "Actualizar Armiga",
    "Informacion del sistema",
    "Salir al shell",
};
#define MENU_COUNT 4

#define DEV_ACTION_TERMINAL 0
#define DEV_ACTION_BTOP     1
#define DEV_ACTION_REBOOT   2
#define DEV_ACTION_SHUTDOWN 3

static const char *DEV_MENU_ITEMS[] = {
    "Terminal",
    "btop",
    "Reboot",
    "Shutdown",
};
#define DEV_MENU_COUNT 4

/* SDL button indices del H700 (confirmados en hardware, no kernel/evdev) */
#define BTN_SDL_B      0
#define BTN_SDL_A      1
#define BTN_SDL_L1     4
#define BTN_SDL_SELECT 8
#define BTN_SDL_START  9

#define DEVMODE_HOLD_MS 3000

#define ARMIGA_CONFIG_PATH "/media/amiga_data/armiga.cfg"

static void apply_timezone(void)
{
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return; /* sin config -> se queda en UTC (default del sistema) */

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "TZ")) {
                setenv("TZ", val, 1);
                tzset();
                break;
            }
        }
    }
    fclose(f);
}

static bool read_sysfs_str(const char *path, char *buf, size_t bufsize)
{
    FILE *f = fopen(path, "r");
    if (!f) return false;
    bool ok = (fgets(buf, (int)bufsize, f) != NULL);
    fclose(f);
    if (ok) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
    }
    return ok;
}

static bool read_sysfs_int(const char *path, int *out)
{
    char buf[16];
    if (!read_sysfs_str(path, buf, sizeof(buf))) return false;
    *out = atoi(buf);
    return true;
}

static void update_status(char *time_str, size_t time_str_sz,
                          bool *wifi_up, int *battery_pct)
{
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    if (lt) strftime(time_str, time_str_sz, "%H:%M", lt);
    else    strncpy(time_str, "--:--", time_str_sz);

    char operstate[16] = {0};
    *wifi_up = read_sysfs_str("/sys/class/net/wlan0/operstate", operstate, sizeof(operstate))
               && strncmp(operstate, "up", 2) == 0;

    int cap = -1;
    read_sysfs_int("/sys/class/power_supply/battery/capacity", &cap);
    *battery_pct = cap;
}

static void read_release(char *kernel, char *mesa, char *retroarch, char *sdl3)
{
    strncpy(kernel,    "?", 32);
    strncpy(mesa,      "?", 32);
    strncpy(retroarch, "?", 32);
    strncpy(sdl3,      "?", 32);
    FILE *f = fopen("/etc/armiga-release", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (!strcmp(key, "KERNEL_VERSION"))    strncpy(kernel,    val, 32);
            if (!strcmp(key, "MESA_VERSION"))      strncpy(mesa,      val, 32);
            if (!strcmp(key, "RETROARCH_VERSION")) strncpy(retroarch, val, 32);
            if (!strcmp(key, "SDL3_VERSION"))      strncpy(sdl3,      val, 32);
        }
    }
    fclose(f);
}

static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *t,
                      SDL_Color c, float x, float y)
{
    SDL_Surface *s = TTF_RenderText_Blended(f, t, 0, c);
    if (!s) return;
    SDL_Texture *tx = SDL_CreateTextureFromSurface(r, s);
    SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
    SDL_RenderTexture(r, tx, NULL, &dst);
    SDL_DestroyTexture(tx);
    SDL_DestroySurface(s);
}

static void draw_text_right(SDL_Renderer *r, TTF_Font *f, const char *t,
                            SDL_Color c, float right_x, float y)
{
    int w = 0, h = 0;
    TTF_GetStringSize(f, t, 0, &w, &h);
    draw_text(r, f, t, c, right_x - (float)w, y);
}

static void draw_text_centered(SDL_Renderer *r, TTF_Font *f, const char *t,
                               SDL_Color c, float center_x, float y)
{
    int w = 0, h = 0;
    TTF_GetStringSize(f, t, 0, &w, &h);
    draw_text(r, f, t, c, center_x - (float)w / 2.0f, y);
}

static void draw_rect_filled(SDL_Renderer *r, float x, float y,
                              float w, float h, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_FRect rect = {x, y, w, h};
    SDL_RenderFillRect(r, &rect);
}

static void draw_line(SDL_Renderer *r, float x1, float y1,
                      float x2, float y2, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderLine(r, x1, y1, x2, y2);
}

int main(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }

    apply_timezone();

    SDL_Window *win = SDL_CreateWindow("Armiga",
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!win) { TTF_Quit(); SDL_Quit(); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return 1; }

    TTF_Font *f_title = TTF_OpenFont(FONT_PATH, FONT_TITLE);
    TTF_Font *f_med   = TTF_OpenFont(FONT_PATH, FONT_MED);
    TTF_Font *f_sm    = TTF_OpenFont(FONT_PATH, FONT_SM);
    if (!f_title || !f_med || !f_sm) {
        fprintf(stderr, "TTF_OpenFont: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
        TTF_Quit(); SDL_Quit(); return 1;
    }

    /* Cargar logo desde array C */
    SDL_Texture *logo_tex = NULL;
    SDL_Surface *logo_surf = SDL_CreateSurface(LOGO_W, LOGO_H, SDL_PIXELFORMAT_RGBA32);
    if (logo_surf) {
        SDL_memcpy(logo_surf->pixels, logo_data, LOGO_W * LOGO_H * 4);
        logo_tex = SDL_CreateTextureFromSurface(ren, logo_surf);
        SDL_DestroySurface(logo_surf);
    }

    /* Leer versiones */
    char s_kernel[32], s_mesa[32], s_retroarch[32], s_sdl3[32];
    read_release(s_kernel, s_mesa, s_retroarch, s_sdl3);

    /* Joystick */
    SDL_Joystick *joy = NULL;
    int nj = 0;
    SDL_JoystickID *jids = SDL_GetJoysticks(&nj);
    if (jids && nj > 0) joy = SDL_OpenJoystick(jids[0]);
    SDL_free(jids);

    int selected = 0;
    int dev_selected = 0;
    int confirm_target = DEV_ACTION_REBOOT; /* cual de los dos confirm. */
    AppState state = STATE_MENU;
    ExecRequest exec_req = EXEC_NONE;
    int action   = ACTION_NONE;
    bool running = true;
    SDL_Event ev;

    Uint64 devmode_hold_start = 0; /* 0 = combo no presionado */
    bool devmode_combo_held = false;

    char status_time[8] = "--:--";
    bool status_wifi_up = false;
    int  status_battery = -1;
    Uint64 last_status_update = 0;

    SDL_Color c_bg      = COL_BG;
    SDL_Color c_green   = COL_GREEN;
    SDL_Color c_dkgreen = COL_DKGREEN;
    SDL_Color c_white   = COL_WHITE;
    SDL_Color c_gray    = COL_GRAY;
    SDL_Color c_selbg   = COL_SEL_BG;

    /* Layout */
    float mx     = 30.0f;
    float mw     = 390.0f;
    float sep_x  = 440.0f;
    float rx      = 458.0f;
    float sep_y  = 118.0f;
    float menu_y0 = 134.0f;
    float item_h  = 30.0f;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                if (state == STATE_MENU) running = false;
                else if (state == STATE_CONFIRM) state = STATE_DEVMODE;
                else if (state == STATE_DEVMODE) state = STATE_MENU;
            }

            if (state == STATE_MENU) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP)
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        selected = (selected + 1) % MENU_COUNT;
                    if (ev.key.key == SDLK_RETURN)
                        action = selected + 1;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        selected = (selected + 1) % MENU_COUNT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
                    ev.jaxis.axis == 1) {
                    static int axis_prev = 0;
                    int v = ev.jaxis.value;
                    int zone = (v < -16000) ? -1 : (v > 16000) ? 1 : 0;
                    if (zone != axis_prev) {
                        if (zone == -1)
                            selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        else if (zone == 1)
                            selected = (selected + 1) % MENU_COUNT;
                        axis_prev = zone;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A)
                    action = selected + 1;
            }
            else if (state == STATE_DEVMODE) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP)
                        dev_selected = (dev_selected - 1 + DEV_MENU_COUNT) % DEV_MENU_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        dev_selected = (dev_selected + 1) % DEV_MENU_COUNT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        dev_selected = (dev_selected - 1 + DEV_MENU_COUNT) % DEV_MENU_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        dev_selected = (dev_selected + 1) % DEV_MENU_COUNT;
                }
                if ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                     ev.jbutton.button == BTN_SDL_A)) {
                    if (dev_selected == DEV_ACTION_TERMINAL) {
                        running = false;
                        exec_req = EXEC_SHELL;
                    } else if (dev_selected == DEV_ACTION_BTOP) {
                        running = false;
                        exec_req = EXEC_BTOP;
                    } else if (dev_selected == DEV_ACTION_REBOOT) {
                        confirm_target = DEV_ACTION_REBOOT;
                        state = STATE_CONFIRM;
                    } else if (dev_selected == DEV_ACTION_SHUTDOWN) {
                        confirm_target = DEV_ACTION_SHUTDOWN;
                        state = STATE_CONFIRM;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
            }
            else if (state == STATE_CONFIRM) {
                if ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                     ev.jbutton.button == BTN_SDL_A)) {
                    running = false;
                    exec_req = (confirm_target == DEV_ACTION_REBOOT)
                               ? EXEC_REBOOT : EXEC_SHUTDOWN;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_DEVMODE;
            }
        }

        /* Deteccion de combo SELECT+START+L1 mantenido 3s (solo desde STATE_MENU) */
        if (state == STATE_MENU && joy) {
            bool sel_held   = SDL_GetJoystickButton(joy, BTN_SDL_SELECT);
            bool start_held = SDL_GetJoystickButton(joy, BTN_SDL_START);
            bool l1_held    = SDL_GetJoystickButton(joy, BTN_SDL_L1);
            bool combo_now  = sel_held && start_held && l1_held;

            if (combo_now && !devmode_combo_held) {
                devmode_hold_start = SDL_GetTicks();
                devmode_combo_held = true;
            } else if (!combo_now) {
                devmode_combo_held = false;
                devmode_hold_start = 0;
            } else if (devmode_combo_held && devmode_hold_start != 0 &&
                       SDL_GetTicks() - devmode_hold_start >= DEVMODE_HOLD_MS) {
                state = STATE_DEVMODE;
                dev_selected = 0;
                devmode_combo_held = false;
                devmode_hold_start = 0;
            }
        } else if (state != STATE_MENU) {
            devmode_combo_held = false;
            devmode_hold_start = 0;
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_SHELL) {
                running = false;
                exec_req = EXEC_SHELL;
            }
            action = ACTION_NONE;
        }

        Uint64 now_ticks = SDL_GetTicks();
        if (last_status_update == 0 || now_ticks - last_status_update > 3000) {
            update_status(status_time, sizeof(status_time),
                         &status_wifi_up, &status_battery);
            last_status_update = now_ticks;
        }

        /* RENDER */
        SDL_SetRenderDrawColor(ren, c_bg.r, c_bg.g, c_bg.b, 255);
        SDL_RenderClear(ren);

        if (state == STATE_MENU) {
        /* Logo */
        if (logo_tex) {
            SDL_FRect logo_dst = {mx, 14.0f, (float)LOGO_W, (float)LOGO_H};
            SDL_RenderTexture(ren, logo_tex, NULL, &logo_dst);
        }

        /* Slogan */
        draw_text(ren, f_sm, "68K SOUL, ARM64 HEART.", c_dkgreen, mx + 2.0f, 94.0f);

        /* Indicadores esquina superior derecha: reloj | WIFI | batería */
        {
            SDL_Color c_red = COL_RED;
            float corner_right = SCREEN_W - 20.0f;
            float corner_y     = 18.0f;
            float gap          = 14.0f;
            int wbuf, hbuf;
            char batt_buf[8];

            if (status_battery >= 0)
                snprintf(batt_buf, sizeof(batt_buf), "%d%%", status_battery);
            else
                strncpy(batt_buf, "--", sizeof(batt_buf));

            TTF_GetStringSize(f_sm, batt_buf, 0, &wbuf, &hbuf);
            float x_batt = corner_right - (float)wbuf;
            draw_text(ren, f_sm, batt_buf, c_white, x_batt, corner_y);

            TTF_GetStringSize(f_sm, "WIFI", 0, &wbuf, &hbuf);
            float x_wifi = x_batt - gap - (float)wbuf;
            draw_text(ren, f_sm, "WIFI", status_wifi_up ? c_green : c_red,
                      x_wifi, corner_y);

            TTF_GetStringSize(f_sm, status_time, 0, &wbuf, &hbuf);
            float x_clock = x_wifi - gap - (float)wbuf;
            draw_text(ren, f_sm, status_time, c_white, x_clock, corner_y);
        }

        /* Separador horizontal */
        draw_line(ren, mx, sep_y, SCREEN_W - 20.0f, sep_y, c_green);

        /* Separador vertical */
        draw_line(ren, sep_x, sep_y, sep_x, 438.0f, c_green);

        /* Menú */
        for (int i = 0; i < MENU_COUNT; i++) {
            float iy = menu_y0 + i * item_h;
            if (i == selected) {
                draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                 mw, item_h - 2.0f, c_selbg);
                draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                 4.0f, item_h - 2.0f, c_green);
                draw_text(ren, f_med, MENU_ICONS[i], c_green, mx + 8.0f, iy);
                draw_text(ren, f_med, MENU_ITEMS[i], c_green, mx + 46.0f, iy);
                draw_text(ren, f_med, ">", c_green, mx + mw - 20.0f, iy);
            } else {
                draw_text(ren, f_med, MENU_ICONS[i], c_gray, mx + 8.0f, iy);
                draw_text(ren, f_med, MENU_ITEMS[i], c_gray, mx + 46.0f, iy);
                draw_text(ren, f_med, ">", c_gray, mx + mw - 20.0f, iy);
            }
        }

        /* Panel derecho */
        float ry = menu_y0;
        draw_text(ren, f_sm,  "KERNEL",     c_green, rx, ry);
        draw_text(ren, f_med, s_kernel,     c_white, rx, ry + 14.0f);
        ry += 46.0f;
        draw_text(ren, f_sm,  "MESA",       c_green, rx, ry);
        draw_text(ren, f_med, s_mesa,       c_white, rx, ry + 14.0f);
        ry += 46.0f;
        draw_text(ren, f_sm,  "RETROARCH",  c_green, rx, ry);
        draw_text(ren, f_med, s_retroarch,  c_white, rx, ry + 14.0f);
        ry += 46.0f;
        draw_text(ren, f_sm,  "SDL3",       c_green, rx, ry);
        draw_text(ren, f_med, s_sdl3,       c_white, rx, ry + 14.0f);

        /* Barra inferior */
        draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
        draw_text(ren, f_sm, "[A] Seleccionar    [DPAD] Navegar",
                  c_gray, mx, 448.0f);
        draw_text_right(ren, f_sm, ARMIGA_VERSION, c_dkgreen,
                        SCREEN_W - 20.0f, 448.0f);

        /* Barra de progreso del hold de modo dev (si se está manteniendo) */
        if (devmode_combo_held && devmode_hold_start != 0) {
            Uint64 elapsed = SDL_GetTicks() - devmode_hold_start;
            float frac = (float)elapsed / (float)DEVMODE_HOLD_MS;
            if (frac > 1.0f) frac = 1.0f;
            float bar_w = 200.0f;
            float bar_x = (SCREEN_W - bar_w) / 2.0f;
            float bar_y = SCREEN_H - 14.0f;
            draw_rect_filled(ren, bar_x, bar_y, bar_w, 4.0f, c_gray);
            draw_rect_filled(ren, bar_x, bar_y, bar_w * frac, 4.0f, c_green);
        }

        } else if (state == STATE_DEVMODE) {
            draw_text_centered(ren, f_title, "MODO DESARROLLADOR", c_green,
                               SCREEN_W / 2.0f, 40.0f);

            float dev_item_h = 44.0f;
            float dev_y0 = 200.0f;
            float dev_w  = 220.0f;
            float dev_x  = (SCREEN_W - dev_w) / 2.0f;

            for (int i = 0; i < DEV_MENU_COUNT; i++) {
                float iy = dev_y0 + i * dev_item_h;
                if (i == dev_selected) {
                    draw_rect_filled(ren, dev_x, iy - 6.0f, dev_w, dev_item_h - 10.0f, c_selbg);
                    draw_text_centered(ren, f_med, DEV_MENU_ITEMS[i], c_green,
                                       SCREEN_W / 2.0f, iy);
                } else {
                    draw_text_centered(ren, f_med, DEV_MENU_ITEMS[i], c_gray,
                                       SCREEN_W / 2.0f, iy);
                }
            }

            draw_text_centered(ren, f_sm, "[A] Seleccionar   [B] Volver", c_gray,
                               SCREEN_W / 2.0f, SCREEN_H - 30.0f);

        } else if (state == STATE_CONFIRM) {
            const char *label = (confirm_target == DEV_ACTION_REBOOT)
                                 ? "Reiniciar el dispositivo?"
                                 : "Apagar el dispositivo?";
            draw_text_centered(ren, f_med, label, c_white,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f - 30.0f);
            draw_text_centered(ren, f_med, "[A] Si        [B] No", c_green,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f + 10.0f);
        }

        SDL_RenderPresent(ren);
    }

    if (logo_tex) SDL_DestroyTexture(logo_tex);
    if (joy) SDL_CloseJoystick(joy);
    TTF_CloseFont(f_sm);
    TTF_CloseFont(f_med);
    TTF_CloseFont(f_title);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    SDL_Quit();

    switch (exec_req) {
        case EXEC_SHELL: {
            const char *shell = getenv("SHELL");
            if (!shell || shell[0] == '\0') shell = "/bin/sh";
            execl(shell, shell, "-i", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar %s: %s\n",
                    shell, strerror(errno));
            return 1;
        }
        case EXEC_BTOP:
            execl("/usr/bin/btop", "btop", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar btop: %s\n",
                    strerror(errno));
            return 1;
        case EXEC_REBOOT:
            execl("/sbin/reboot", "reboot", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar reboot: %s\n",
                    strerror(errno));
            return 1;
        case EXEC_SHUTDOWN:
            execl("/sbin/poweroff", "poweroff", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar poweroff: %s\n",
                    strerror(errno));
            return 1;
        case EXEC_NONE:
        default:
            return 0;
    }
}
