#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
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
    STATE_CONFIRM,
    STATE_SYSINFO,
    STATE_UPDATE
} AppState;

typedef enum {
    EXEC_NONE,
    EXEC_SHELL,        /* "Apagar dispositivo" del menu principal: hereda stdio actual (SSH o consola) */
    EXEC_DEV_TERMINAL, /* "Terminal" del modo dev: fuerza consola local /dev/tty0 */
    EXEC_DEV_BTOP,     /* "btop" del modo dev: fuerza consola local /dev/tty0 */
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
    "[O]",
};

static const char *MENU_ITEMS[] = {
    "Catálogo Amiga",
    "Actualización de sistema",
    "Diagnóstico del sistema",
    "Apagar dispositivo",
};
static const char *MENU_DESC[] = {
    "Explora y lanza juegos\n" "Amiga desde tu biblioteca.",
    "Descarga e instala la\n" "ultima version de Armiga.",
    "Revisa el estado del\n" "hardware y el sistema.",
    "Apaga el dispositivo\n" "de forma segura.",
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
#define BTN_SDL_B      1
#define BTN_SDL_A      0
#define BTN_SDL_L1     4
#define BTN_SDL_R1     5
#define BTN_SDL_SELECT 8
#define BTN_SDL_START  9

#define DEVMODE_HOLD_MS 3000

#define ARMIGA_CONFIG_PATH "/media/amiga_data/armiga.cfg"

#define LOCAL_CONSOLE_PATH "/dev/tty0"

/* Redirige stdin/stdout/stderr a la consola local (pantalla+teclado del
 * dispositivo), en vez de heredar los descriptores actuales (que si el
 * launcher se lanzo por SSH, serian los de esa sesion remota). Necesario
 * para que Terminal/btop del modo desarrollador sean usables con un
 * teclado USB conectado directamente al Anbernic. */
static bool redirect_stdio_to_local_console(void)
{
    int fd = open(LOCAL_CONSOLE_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "armiga-launcher: no se pudo abrir %s: %s\n",
                LOCAL_CONSOLE_PATH, strerror(errno));
        return false;
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    /* SDL/DRM deja la consola en KD_GRAPHICS; sin esto el shell corre
     * pero no se ve nada en pantalla (cursor parpadeando, sin texto). */
    ioctl(fd, KDSETMODE, KD_TEXT);
    if (fd > STDERR_FILENO) close(fd);
    return true;
}

static void read_ip_address(char *buf, size_t bufsize)
{
    strncpy(buf, "sin red", bufsize);
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "wlan0") != 0) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, bufsize);
        break;
    }
    freeifaddrs(ifaddr);
}

static void read_uptime(char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return;
    double secs = 0.0;
    if (fscanf(f, "%lf", &secs) == 1) {
        int h = (int)(secs / 3600);
        int m = (int)(secs / 60) % 60;
        snprintf(buf, bufsize, "%dh %dm", h, m);
    }
    fclose(f);
}

static void read_ram_usage(char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return;
    long mem_total = -1, mem_avail = -1;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "MemTotal:", 9))     sscanf(line, "MemTotal: %ld", &mem_total);
        if (!strncmp(line, "MemAvailable:", 13)) sscanf(line, "MemAvailable: %ld", &mem_avail);
    }
    fclose(f);
    if (mem_total > 0 && mem_avail >= 0) {
        long used_mb = (mem_total - mem_avail) / 1024;
        long total_mb = mem_total / 1024;
        snprintf(buf, bufsize, "%ld/%ld MB", used_mb, total_mb);
    }
}

static void read_disk_usage(const char *path, char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    struct statvfs st;
    if (statvfs(path, &st) != 0) return;
    unsigned long long total_mb = (unsigned long long)st.f_blocks * st.f_frsize / (1024 * 1024);
    unsigned long long free_mb  = (unsigned long long)st.f_bfree  * st.f_frsize / (1024 * 1024);
    unsigned long long used_mb  = total_mb - free_mb;
    snprintf(buf, bufsize, "%llu/%llu MB", used_mb, total_mb);
}

static void read_cpu_temp(char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return;
    int millideg = 0;
    if (fscanf(f, "%d", &millideg) == 1) {
        snprintf(buf, bufsize, "%.1f C", millideg / 1000.0);
    }
    fclose(f);
}

/* Guarda un BMP de la pantalla actual en /media/amiga_data/screenshots/.
 * Devuelve 0 en éxito, -1 en error. */
static int take_screenshot(SDL_Renderer *ren, int screen_w, int screen_h)
{
    mkdir("/media/amiga_data/screenshots", 0755);

    /* Nombre de fichero con timestamp */
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    char path[128];
    if (lt)
        strftime(path, sizeof(path),
                 "/media/amiga_data/screenshots/armiga_%Y%m%d_%H%M%S.bmp", lt);
    else
        snprintf(path, sizeof(path),
                 "/media/amiga_data/screenshots/armiga_%lu.bmp", (unsigned long)t);

    SDL_Surface *surf = SDL_RenderReadPixels(ren, NULL);
    if (!surf) return -1;

    int ret = SDL_SaveBMP(surf, path) ? 0 : -1;
    SDL_DestroySurface(surf);
    return ret;
}


/* ── Actualización OTA ────────────────────────────────────────────────────── */
#define GITHUB_API_URL "https://api.github.com/repos/amigagamesplus/armiga/releases/latest"
#define UPDATE_DIR     "/media/amiga_data/update"
#define UPDATE_IMG     "/media/amiga_data/update/armiga.img.gz"
#define UPDATE_SHA256  "/media/amiga_data/update/armiga.img.gz.sha256"
#define UPDATE_FLAG    "/media/amiga_data/update/pending"

typedef enum {
    UPD_CHECKING,
    UPD_NO_UPDATE,
    UPD_CONFIRM,
    UPD_DOWNLOADING,
    UPD_VERIFYING,
    UPD_READY,
    UPD_ERROR
} UpdatePhase;

/* Compara versiones semánticas "1.0" vs "1.1" → -1/0/1 */
static int semver_cmp(const char *a, const char *b)
{
    int ma = 0, mi_a = 0, pa = 0;
    int mb = 0, mi_b = 0, pb = 0;
    sscanf(a, "%d.%d.%d", &ma, &mi_a, &pa);
    sscanf(b, "%d.%d.%d", &mb, &mi_b, &pb);
    if (ma != mb) return ma > mb ? 1 : -1;
    if (mi_a != mi_b) return mi_a > mi_b ? 1 : -1;
    if (pa != pb) return pa > pb ? 1 : -1;
    return 0;
}

/* Consulta GitHub API y devuelve versión+URL del asset.
 * Usa curl para peticiones HTTPS. Devuelve 0 si OK. */
static int check_update(const char *current_ver,
                        char *new_ver, size_t new_ver_sz,
                        char *dl_url, size_t dl_url_sz,
                        char *sha_url, size_t sha_url_sz)
{
    strncpy(new_ver, "", new_ver_sz);
    strncpy(dl_url,  "", dl_url_sz);
    strncpy(sha_url, "", sha_url_sz);

    /* Descargar JSON de la API a un fichero temporal */
    const char *tmp = "/tmp/armiga_release.json";
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --max-time 10 -L -o %s "
             "\"" GITHUB_API_URL "\" 2>/dev/null", tmp);
    if (system(cmd) != 0) return -1;

    FILE *f = fopen(tmp, "r");
    if (!f) return -1;

    char line[512];
    char tag[32] = "";
    char asset_url[512] = "";
    char sha_asset_url[512] = "";
    bool in_assets = false;
    char last_name[128] = "";

    while (fgets(line, sizeof(line), f)) {
        /* Extraer tag_name */
        char *p;
        if ((p = strstr(line, "\"tag_name\""))) {
            sscanf(p, "\"tag_name\" : \"%31[^\"]\"", tag);
            if (!tag[0]) sscanf(p, "\"tag_name\":\"%31[^\"]\"", tag);
        }
        /* Detectar sección assets */
        if (strstr(line, "\"assets\"")) in_assets = true;
        if (in_assets) {
            if ((p = strstr(line, "\"name\"")))
                sscanf(p, "\"name\" : \"%127[^\"]\"", last_name),
                sscanf(p, "\"name\":\"%127[^\"]\"", last_name);
            if ((p = strstr(line, "\"browser_download_url\""))) {
                char url[512] = "";
                sscanf(p, "\"browser_download_url\" : \"%511[^\"]\"", url);
                if (!url[0]) sscanf(p, "\"browser_download_url\":\"%511[^\"]\"", url);
                if (strstr(last_name, ".img.gz") && !strstr(last_name, ".sha256"))
                    strncpy(asset_url, url, sizeof(asset_url)-1);
                if (strstr(last_name, ".sha256"))
                    strncpy(sha_asset_url, url, sizeof(sha_asset_url)-1);
            }
        }
    }
    fclose(f);
    unlink(tmp);

    if (!tag[0] || !asset_url[0]) return -1;

    /* Normalizar tag: quitar 'v' inicial */
    const char *ver = tag;
    if (ver[0] == 'v') ver++;
    strncpy(new_ver, ver, new_ver_sz-1);
    strncpy(dl_url,  asset_url,     dl_url_sz-1);
    strncpy(sha_url, sha_asset_url, sha_url_sz-1);

    return semver_cmp(ver, current_ver) > 0 ? 1 : 0; /* 1=hay update, 0=al día */
}

/* Descarga el .img.gz con progreso. Ejecuta curl en background y
 * monitoriza el fichero destino para actualizar la barra. */
static int download_update(const char *url, float *progress_out)
{
    mkdir(UPDATE_DIR, 0755);
    /* Obtener tamaño esperado */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "curl -s --max-time 300 -L "
             "-o \"" UPDATE_IMG "\" \"%s\" 2>/tmp/curl_progress &"
             " echo $! > /tmp/curl_pid", url);
    if (system(cmd) != 0) return -1;
    *progress_out = 0.0f;
    return 0;
}

static float get_download_progress(const char *url)
{
    /* Leer el pid y comprobar si sigue vivo */
    FILE *f = fopen("/tmp/curl_pid", "r");
    if (!f) return -1.0f; /* error */
    int pid = 0;
    (void)fscanf(f, "%d", &pid);
    fclose(f);

    /* Comprobar si curl sigue vivo */
    char proc[32];
    snprintf(proc, sizeof(proc), "/proc/%d/status", pid);
    bool running = (access(proc, F_OK) == 0);

    if (!running) {
        /* Verificar que el fichero existe y tiene tamaño */
        struct stat st;
        if (stat(UPDATE_IMG, &st) == 0 && st.st_size > 1024*1024)
            return 1.0f; /* completado */
        return -1.0f; /* error */
    }

    /* Estimar progreso por tamaño del fichero descargado */
    struct stat st;
    if (stat(UPDATE_IMG, &st) != 0) return 0.0f;
    /* Tamaño esperado ~55MB comprimido */
    float expected = 55.0f * 1024 * 1024;
    float pct = (float)st.st_size / expected;
    if (pct > 0.99f) pct = 0.99f;
    return pct;
}

static int verify_sha256(void)
{
    if (access(UPDATE_SHA256, F_OK) != 0) return 0; /* sin sha, aceptar */
    /* sha256sum -c verifica el fichero */
    int ret = system("cd \"" UPDATE_DIR "\" && sha256sum -c armiga.img.gz.sha256 >/dev/null 2>&1");
    return (ret == 0) ? 0 : -1;
}

static void write_update_flag(void)
{
    FILE *f = fopen(UPDATE_FLAG, "w");
    if (f) { fprintf(f, "pending\n"); fclose(f); }
}

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

static void read_release(char *kernel, char *mesa, char *retroarch, char *sdl3,
                         char *build_date)
{
    strncpy(kernel,     "?", 32);
    strncpy(mesa,       "?", 32);
    strncpy(retroarch,  "?", 32);
    strncpy(sdl3,       "?", 32);
    if (build_date) strncpy(build_date, "?", 24);
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
            if (build_date && !strcmp(key, "BUILD_DATE")) strncpy(build_date, val, 24);
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

/* Dibuja la barra de estado superior derecha: HORA | WIFI | BATERIA
 * Posicion fija: esquina superior derecha, y=18 */
static void draw_statusbar(SDL_Renderer *ren, TTF_Font *f,
                            const char *time_str, bool wifi_up, int battery)
{
    SDL_Color c_white  = COL_WHITE;
    SDL_Color c_green  = COL_GREEN;
    SDL_Color c_gray   = COL_GRAY;
    SDL_Color c_red    = COL_RED;
    float right = SCREEN_W - 20.0f;
    float y     = 18.0f;
    float gap   = 10.0f;
    int w, h;

    /* BATERIA */
    char batt_buf[8];
    if (battery >= 0) snprintf(batt_buf, sizeof(batt_buf), "%d%%", battery);
    else              strncpy(batt_buf, "--", sizeof(batt_buf));
    TTF_GetStringSize(f, batt_buf, 0, &w, &h);
    draw_text(ren, f, batt_buf, c_white, right - (float)w, y);
    right -= (float)w + gap;

    /* Separador */
    TTF_GetStringSize(f, "|", 0, &w, &h);
    draw_text(ren, f, "|", c_gray, right - (float)w, y);
    right -= (float)w + gap;

    /* WIFI */
    const char *wifi_lbl = wifi_up ? "WIFI" : "WIFI";
    SDL_Color wifi_col   = wifi_up ? c_green : c_red;
    TTF_GetStringSize(f, wifi_lbl, 0, &w, &h);
    draw_text(ren, f, wifi_lbl, wifi_col, right - (float)w, y);
    right -= (float)w + gap;

    /* Separador */
    TTF_GetStringSize(f, "|", 0, &w, &h);
    draw_text(ren, f, "|", c_gray, right - (float)w, y);
    right -= (float)w + gap;

    /* HORA */
    TTF_GetStringSize(f, time_str, 0, &w, &h);
    draw_text(ren, f, time_str, c_white, right - (float)w, y);
}

/* Dibuja footer unificado: leyenda izquierda + version derecha */
static void draw_footer(SDL_Renderer *ren, TTF_Font *f,
                        const char *legend)
{
    SDL_Color c_gray    = COL_GRAY;
    SDL_Color c_dkgreen = COL_DKGREEN;
    draw_text(ren, f, legend, c_gray, 20.0f, 448.0f);
    draw_text_right(ren, f, ARMIGA_VERSION, c_dkgreen, SCREEN_W - 20.0f, 448.0f);
}



static long read_proc_stat_cpu(long *idle_out)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) { if (idle_out) *idle_out = 0; return 0; }
    long u = 0, n = 0, s = 0, id = 0, wa = 0, hi = 0, si = 0, st = 0;
    long total = 0;
    if (fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &u, &n, &s, &id, &wa, &hi, &si, &st) == 8) {
        if (idle_out) *idle_out = id + wa;
        total = u + n + s + id + wa + hi + si + st;
    }
    fclose(f);
    return total;
}

static void read_cpu_usage(char *buf, size_t bufsize, int *pct_out)
{
    /* Delta entre dos snapshots separados 80ms */
    long idle1 = 0, idle2 = 0;
    long total1 = read_proc_stat_cpu(&idle1);
    SDL_Delay(80);
    long total2 = read_proc_stat_cpu(&idle2);

    long dtotal = total2 - total1;
    long didle  = idle2  - idle1;
    int pct = 0;
    if (dtotal > 0)
        pct = (int)(100L * (dtotal - didle) / dtotal);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (pct_out) *pct_out = pct;
    snprintf(buf, bufsize, "%d%%", pct);
}

static void read_loadavg(char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return;
    float l1, l5, l15;
    if (fscanf(f, "%f %f %f", &l1, &l5, &l15) == 3)
        snprintf(buf, bufsize, "%.2f  %.2f  %.2f", l1, l5, l15);
    fclose(f);
}

static void read_wifi_signal(char *buf, size_t bufsize, int *pct_out)
{
    strncpy(buf, "--", bufsize);
    if (pct_out) *pct_out = -1;
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return;
    char line[128];
    /* Saltar las dos primeras líneas de cabecera */
    (void)fgets(line, sizeof(line), f);
    (void)fgets(line, sizeof(line), f);
    if (fgets(line, sizeof(line), f)) {
        char iface[16];
        int status;
        float quality, signal, noise;
        /* Formato: "wlan0: 0000   54.  -56.  -256. ..." */
        if (sscanf(line, " %15[^:]: %d %f %f %f",
                   iface, &status, &quality, &signal, &noise) >= 4) {
            int dbm = (int)signal;
            /* Convertir dBm a porcentaje (rango típico -100 a -50 dBm) */
            int pct = 0;
            if (dbm <= -100)      pct = 0;
            else if (dbm >= -50)  pct = 100;
            else                  pct = 2 * (dbm + 100);
            if (pct_out) *pct_out = pct;
            snprintf(buf, bufsize, "%d dBm (%d%%)", dbm, pct);
        }
    }
    fclose(f);
}

static void read_mac_address(char *buf, size_t bufsize)
{
    strncpy(buf, "--", bufsize);
    FILE *f = fopen("/sys/class/net/wlan0/address", "r");
    if (!f) return;
    if (fgets(buf, (int)bufsize, f)) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
    }
    fclose(f);
}

/* Dibuja barra ASCII [##########] con parte rellena en verde y vacía en verde oscuro.
 * ncols = número de caracteres de la barra (sin contar corchetes). */
static void draw_bar(SDL_Renderer *r, TTF_Font *f, int pct,
                     SDL_Color c_fill, SDL_Color c_empty,
                     float x, float y, int ncols)
{
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (ncols < 1 || ncols > 60) ncols = 10;

    int filled = (pct * ncols) / 100;
    int empty  = ncols - filled;

    /* Tres segmentos: "[" + "#"*filled  |  "-"*empty  |  "]" */
    char s_open[4]   = "[";
    char s_fill[64]  = {0};
    char s_emp[64]   = {0};
    char s_close[4]  = "]";

    for (int i = 0; i < filled && i < 63; i++) { s_fill[i] = '#'; s_fill[i+1] = '\0'; }
    for (int i = 0; i < empty  && i < 63; i++) { s_emp[i]  = '-'; s_emp[i+1]  = '\0'; }

    int wo = 0, wf = 0, we = 0, h = 0;
    TTF_GetStringSize(f, s_open,  0, &wo, &h);
    TTF_GetStringSize(f, s_fill,  0, &wf, &h);
    TTF_GetStringSize(f, s_emp,   0, &we, &h);

    draw_text(r, f, s_open,  c_fill,  x,                           y);
    if (filled > 0)
        draw_text(r, f, s_fill,  c_fill,  x + (float)wo,           y);
    if (empty > 0)
        draw_text(r, f, s_emp,   c_empty, x + (float)wo + (float)wf, y);
    draw_text(r, f, s_close, c_fill,  x + (float)wo + (float)wf + (float)we, y);
}

/* Fila de sysinfo: etiqueta izquierda, valor derecha, opcional barra debajo */
static void draw_si_row(SDL_Renderer *r, TTF_Font *f_lbl, TTF_Font *f_val,
                        const char *label, const char *value,
                        SDL_Color c_lbl, SDL_Color c_val,
                        float x, float y, float col_w)
{
    draw_text(r, f_lbl, label, c_lbl, x, y);
    /* Valor alineado a la derecha de la columna */
    int wv = 0, hv = 0;
    TTF_GetStringSize(f_val, value, 0, &wv, &hv);
    float vx = x + col_w - (float)wv;
    if (vx < x + 60.0f) vx = x + 60.0f; /* mínimo para etiquetas largas */
    draw_text(r, f_val, value, c_val, vx, y);
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
    for (;;) {
    bool relaunch_after_retroarch = false;

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
    char s_kernel[32], s_mesa[32], s_retroarch[32], s_sdl3[32], s_build_date[24];
    read_release(s_kernel, s_mesa, s_retroarch, s_sdl3, s_build_date);

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
    Uint64 screenshot_flash_until = 0; /* ms hasta cuando mostrar flash */

    char dev_ip[32]     = "sin red";
    char dev_uptime[16] = "--";
    char dev_ram[24]    = "--";

    char sysinfo_disk_data[32] = "--";
    char sysinfo_disk_root[32] = "--";
    char sysinfo_temp[16]      = "--";
    char sysinfo_cpu_usage[8]  = "--";
    int  sysinfo_cpu_pct       = 0;
    char sysinfo_loadavg[32]   = "--";
    char sysinfo_wifi_sig[32]  = "--";
    int  sysinfo_wifi_pct      = -1;
    char sysinfo_mac[24]       = "--";
    char sysinfo_build[24]     = "--";
    Uint64 last_sysinfo_update = 0;
    /* Variables de actualización OTA */
    UpdatePhase update_phase   = UPD_CHECKING;
    bool  update_checked       = false;
    char  upd_new_ver[32]      = "";
    char  upd_dl_url[512]      = "";
    char  upd_sha_url[512]     = "";
    char  upd_msg[128]         = "";
    float upd_progress         = 0.0f;
    Uint64 upd_check_start     = 0;


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
    float mx     = 20.0f;
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
                else if (state == STATE_SYSINFO) state = STATE_MENU;
                else if (state == STATE_UPDATE) { if (update_phase != UPD_DOWNLOADING) state = STATE_MENU; }
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
                        exec_req = EXEC_DEV_TERMINAL;
                    } else if (dev_selected == DEV_ACTION_BTOP) {
                        running = false;
                        exec_req = EXEC_DEV_BTOP;
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
            else if (state == STATE_SYSINFO) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
            }
            else if (state == STATE_UPDATE) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B &&
                    update_phase != UPD_DOWNLOADING)
                    state = STATE_MENU;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B &&
                    update_phase == UPD_CONFIRM)
                    update_phase = UPD_DOWNLOADING;
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
                read_ip_address(dev_ip, sizeof(dev_ip));
                read_uptime(dev_uptime, sizeof(dev_uptime));
                read_ram_usage(dev_ram, sizeof(dev_ram));
            }
        } else if (state != STATE_MENU) {
            devmode_combo_held = false;
            devmode_hold_start = 0;
        }

        /* Combo screenshot: SELECT + R1 pulsados simultaneamente (cualquier estado) */
        if (joy) {
            bool sel = SDL_GetJoystickButton(joy, BTN_SDL_SELECT);
            bool r1  = SDL_GetJoystickButton(joy, BTN_SDL_R1);
            if (sel && r1) {
                /* Renderizar flash blanco encima del frame actual y presentarlo */
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
                SDL_FRect flash_rect = {0, 0, (float)SCREEN_W, (float)SCREEN_H};
                SDL_RenderFillRect(ren, &flash_rect);
                SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
                SDL_RenderPresent(ren);
                SDL_Delay(80); /* visible al menos 2 frames */
                take_screenshot(ren, SCREEN_W, SCREEN_H);
                screenshot_flash_until = SDL_GetTicks() + 500;
                SDL_PumpEvents();
                /* Esperar a que suelten los botones para evitar disparos multiples */
                SDL_PumpEvents();
                while (SDL_GetJoystickButton(joy, BTN_SDL_SELECT) ||
                       SDL_GetJoystickButton(joy, BTN_SDL_R1)) {
                    SDL_PumpEvents();
                    SDL_Delay(20);
                }
                SDL_Delay(200); /* debounce tras soltar */
            }
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_SHELL) {
                /* "Apagar dispositivo" en menu principal */
                exec_req = EXEC_SHUTDOWN;
                running = false;
            } else if (action == ACTION_ROMS) {
                running = false;
                relaunch_after_retroarch = true;
            } else if (action == ACTION_UPDATE) {
                state = STATE_UPDATE;
                update_phase = UPD_CHECKING;
                update_checked = false;
            } else if (action == ACTION_INFO) {
                state = STATE_SYSINFO;
                last_sysinfo_update = 0; /* forzar refresco inmediato */
            }
            action = ACTION_NONE;
        }

        Uint64 now_ticks = SDL_GetTicks();
        if (last_status_update == 0 || now_ticks - last_status_update > 3000) {
            update_status(status_time, sizeof(status_time),
                         &status_wifi_up, &status_battery);
            last_status_update = now_ticks;
        }

        /* Lógica OTA */
        if (state == STATE_UPDATE) {
            if (update_phase == UPD_CHECKING && !update_checked) {
                update_checked = true;
                upd_check_start = now_ticks;
                char current_ver[32] = "1.0";
                /* Leer versión actual */
                FILE *rf = fopen("/etc/armiga-release", "r");
                if (rf) {
                    char ln[128];
                    while (fgets(ln, sizeof(ln), rf))
                        if (!strncmp(ln, "ARMIGA_VERSION=", 15))
                            sscanf(ln+15, "%31s", current_ver);
                    fclose(rf);
                }
                int res = check_update(current_ver,
                                       upd_new_ver, sizeof(upd_new_ver),
                                       upd_dl_url,  sizeof(upd_dl_url),
                                       upd_sha_url, sizeof(upd_sha_url));
                if (res == 1)       update_phase = UPD_CONFIRM;
                else if (res == 0)  update_phase = UPD_NO_UPDATE;
                else { update_phase = UPD_ERROR;
                       strncpy(upd_msg, "Error al conectar con el servidor.", sizeof(upd_msg)); }
            }
            if (update_phase == UPD_DOWNLOADING) {
                if (upd_progress == 0.0f)
                    download_update(upd_dl_url, &upd_progress);
                float p = get_download_progress(upd_dl_url);
                if (p >= 0.0f) upd_progress = p;
                if (p == 1.0f) update_phase = UPD_VERIFYING;
                if (p < 0.0f) { update_phase = UPD_ERROR;
                    strncpy(upd_msg, "Error en la descarga.", sizeof(upd_msg)); }
            }
            if (update_phase == UPD_VERIFYING) {
                if (verify_sha256() == 0) {
                    /* Descargar también el .sha256 */
                    if (upd_sha_url[0]) {
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd),
                                 "curl -s -L -o \"" UPDATE_SHA256 "\" \"%s\"", upd_sha_url);
                        (void)system(cmd);
                    }
                    write_update_flag();
                    update_phase = UPD_READY;
                } else {
                    update_phase = UPD_ERROR;
                    strncpy(upd_msg, "Error de verificacion SHA256.", sizeof(upd_msg));
                    unlink(UPDATE_IMG);
                }
            }
        }

        if (state == STATE_SYSINFO &&
            (last_sysinfo_update == 0 || now_ticks - last_sysinfo_update > 5000)) {
            read_ip_address(dev_ip, sizeof(dev_ip));
            read_uptime(dev_uptime, sizeof(dev_uptime));
            read_ram_usage(dev_ram, sizeof(dev_ram));
            read_disk_usage("/media/amiga_data", sysinfo_disk_data, sizeof(sysinfo_disk_data));
            read_disk_usage("/", sysinfo_disk_root, sizeof(sysinfo_disk_root));
            read_cpu_temp(sysinfo_temp, sizeof(sysinfo_temp));
            read_cpu_usage(sysinfo_cpu_usage, sizeof(sysinfo_cpu_usage), &sysinfo_cpu_pct);
            read_loadavg(sysinfo_loadavg, sizeof(sysinfo_loadavg));
            read_wifi_signal(sysinfo_wifi_sig, sizeof(sysinfo_wifi_sig), &sysinfo_wifi_pct);
            read_mac_address(sysinfo_mac, sizeof(sysinfo_mac));
            strncpy(sysinfo_build, s_build_date, sizeof(sysinfo_build));
            last_sysinfo_update = now_ticks;
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

        draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);

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

        /* Panel derecho: contexto de la opcion seleccionada */
        {
            draw_text(ren, f_sm, MENU_ITEMS[selected], c_green, rx, menu_y0);
            /* Descripcion en dos lineas */
            const char *desc = MENU_DESC[selected];
            char line1[64] = {0}, line2[64] = {0};
            const char *nl = strchr(desc, '\n');
            if (nl) {
                size_t l1 = (size_t)(nl - desc);
                if (l1 >= sizeof(line1)) l1 = sizeof(line1) - 1;
                strncpy(line1, desc, l1);
                strncpy(line2, nl + 1, sizeof(line2) - 1);
            } else {
                strncpy(line1, desc, sizeof(line1) - 1);
            }
            draw_text(ren, f_sm, line1, c_gray, rx, menu_y0 + 18.0f);
            if (line2[0])
                draw_text(ren, f_sm, line2, c_gray, rx, menu_y0 + 34.0f);
        }

        /* Barra inferior */
        draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
        draw_footer(ren, f_sm, "[B] Seleccionar  [DPAD] Navegar");

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
            /* Titulo pequeño arriba a la izquierda */
            draw_text(ren, f_sm, "MODO DESARROLLADOR", c_green, mx, 20.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);
            draw_line(ren, sep_x, 44.0f, sep_x, 438.0f, c_green);

            /* Menú (columna izquierda), mismo estilo compacto que el menu principal */
            float dev_y0 = 64.0f;
            float dev_item_h = 26.0f;

            for (int i = 0; i < DEV_MENU_COUNT; i++) {
                float iy = dev_y0 + i * dev_item_h;
                if (i == dev_selected) {
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     mw, dev_item_h - 2.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, dev_item_h - 2.0f, c_green);
                    draw_text(ren, f_sm, DEV_MENU_ITEMS[i], c_green, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_sm, DEV_MENU_ITEMS[i], c_gray, mx + 8.0f, iy);
                }
            }

            /* Panel derecho: info tecnica para el desarrollador */
            float ry = 64.0f;
            draw_text(ren, f_sm,  "IP",         c_green, rx, ry);
            draw_text(ren, f_med, dev_ip,       c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "UPTIME",     c_green, rx, ry);
            draw_text(ren, f_med, dev_uptime,   c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "RAM",        c_green, rx, ry);
            draw_text(ren, f_med, dev_ram,      c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "BATERIA",    c_green, rx, ry);
            {
                char batt_buf[8];
                if (status_battery >= 0)
                    snprintf(batt_buf, sizeof(batt_buf), "%d%%", status_battery);
                else
                    strncpy(batt_buf, "--", sizeof(batt_buf));
                draw_text(ren, f_med, batt_buf, c_white, rx, ry + 14.0f);
            }
            ry += 40.0f;
            draw_text(ren, f_sm,  "KERNEL",     c_green, rx, ry);
            draw_text(ren, f_med, s_kernel,     c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "MESA",       c_green, rx, ry);
            draw_text(ren, f_med, s_mesa,       c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "RETROARCH",  c_green, rx, ry);
            draw_text(ren, f_med, s_retroarch,  c_white, rx, ry + 14.0f);
            ry += 40.0f;
            draw_text(ren, f_sm,  "SDL3",       c_green, rx, ry);
            draw_text(ren, f_med, s_sdl3,       c_white, rx, ry + 14.0f);

            /* Barra inferior */
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, "[B] Seleccionar  [A] Volver");

        } else if (state == STATE_CONFIRM) {
            const char *label = (confirm_target == DEV_ACTION_REBOOT)
                                 ? "Reiniciar el dispositivo?"
                                 : "Apagar el dispositivo?";
            draw_text_centered(ren, f_med, label, c_white,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f - 30.0f);
            draw_text_centered(ren, f_med, "[B] Si        [A] No", c_green,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f + 10.0f);

        } else if (state == STATE_SYSINFO) {
            /* ── Layout: cuadrícula 2 columnas × 3 bloques ──────────────────
             *  Pantalla: 640×480, margen 20px, separador vertical en x=330
             *  Bloques apilados verticalmente: header(44) + 3×bloques(~128) + footer(42)
             *  Col izq: x=20..330  Col der: x=348..620
             *  Cada bloque: título(14) + guiones(10) + N filas×(label+valor, 18px)
             */
            SDL_Color c_red = COL_RED;

            /* Constantes de layout sysinfo (distintas del menú principal) */
            const float SI_MX    = 20.0f;   /* margen izquierdo */
            const float SI_SEP   = 330.0f;  /* separador vertical */
            const float SI_RX    = 348.0f;  /* columna derecha X */
            const float SI_CW_L  = 300.0f;  /* ancho columna izquierda */
            const float SI_CW_R  = 272.0f;  /* ancho columna derecha */
            const float SI_ROW_H = 17.0f;   /* altura de fila label+valor */
            const float SI_BLK_H = 128.0f;  /* altura de cada bloque (3 bloques × 128 = 384) */
            const float SI_Y0    = 50.0f;   /* Y inicio primer bloque */
            /* Separador horizontal en y=44 */
            const float SI_SEP_H1 = SI_Y0 + SI_BLK_H;      /* ~178 */
            const float SI_SEP_H2 = SI_Y0 + SI_BLK_H * 2;  /* ~306 */

            /* Título y separador superior */
            draw_text(ren, f_sm, "INFORMACIÓN DEL SISTEMA", c_green, SI_MX, 20.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, SI_MX, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            /* Separador vertical */
            draw_line(ren, SI_SEP, 44.0f, SI_SEP, 438.0f, c_green);

            /* Separadores horizontales entre bloques */
            draw_line(ren, SI_MX, SI_SEP_H1, SCREEN_W - 20.0f, SI_SEP_H1, c_dkgreen);
            draw_line(ren, SI_MX, SI_SEP_H2, SCREEN_W - 20.0f, SI_SEP_H2, c_dkgreen);

/* Macro auxiliar: título de bloque + línea de guiones */
#define SI_BLOCK_TITLE(xpos, ypos, title) do { \
    draw_text(ren, f_sm, title, c_green, (xpos), (ypos)); \
    /* guiones bajo el título */ \
    { char _dashes[32]; int _tl = (int)strlen(title); \
      if (_tl > 31) _tl = 31; \
      for (int _i=0;_i<_tl;_i++) _dashes[_i]='-'; _dashes[_tl]='\0'; \
      draw_text(ren, f_sm, _dashes, c_dkgreen, (xpos), (ypos)+14.0f); } \
} while(0)

/* Macro fila: etiqueta fija + valor alineado a col_right */
#define SI_ROW(xpos, ypos, col_right, lbl, val) do { \
    draw_text(ren, f_sm,  (lbl), c_gray,  (xpos),       (ypos)); \
    draw_text_right(ren, f_sm, (val), c_white, (col_right), (ypos)); \
} while(0)

/* Macro fila con barra: etiqueta, barra, valor */
#define SI_ROW_BAR(xpos, ypos, col_right, lbl, val, pct) do { \
    draw_text(ren, f_sm, (lbl), c_gray, (xpos), (ypos)); \
    { int _ncols = 10; \
      int _fw = 0, _fh = 0; \
      TTF_GetStringSize(f_sm, (val), 0, &_fw, &_fh); \
      float _bar_right = (col_right) - (float)_fw - 6.0f; \
      draw_text(ren, f_sm, (val), c_white, (col_right) - (float)_fw, (ypos)); \
      /* calcular ancho de una barra de _ncols chars */ \
      int _bw = 0; char _probe[16]; \
      snprintf(_probe, sizeof(_probe), "[##########]"); \
      TTF_GetStringSize(f_sm, _probe, 0, &_bw, &_fh); \
      draw_bar(ren, f_sm, (pct), c_green, c_dkgreen, _bar_right - (float)_bw, (ypos), _ncols); \
    } \
} while(0)

            /* ── BLOQUE 1 IZQ: SISTEMA ───────────────────────────────────── */
            float y = SI_Y0 + 2.0f;
            SI_BLOCK_TITLE(SI_MX, y, "SISTEMA");
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Version OS",       "v1.0");          y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Kernel",       s_kernel);        y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Arquitectura", "aarch64");       y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Compilación",        sysinfo_build);   y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Hostname",     "armiga");

            /* ── BLOQUE 1 DER: ESTADO ────────────────────────────────────── */
            y = SI_Y0 + 2.0f;
            SI_BLOCK_TITLE(SI_RX, y, "MÉTRICAS:");
            y += 28.0f;
            {
                /* RAM: calcular pct */
                int ram_pct = 0;
                {
                    FILE *fm = fopen("/proc/meminfo", "r");
                    if (fm) {
                        long mt = -1, ma = -1; char ln[128];
                        while (fgets(ln, sizeof(ln), fm)) {
                            if (!strncmp(ln, "MemTotal:",    9)) sscanf(ln, "MemTotal: %ld",    &mt);
                            if (!strncmp(ln, "MemAvailable:",13)) sscanf(ln, "MemAvailable: %ld",&ma);
                        }
                        fclose(fm);
                        if (mt > 0 && ma >= 0) ram_pct = (int)(100 * (mt - ma) / mt);
                    }
                }
                int temp_pct = 0;
                { int td = 0; if (read_sysfs_int("/sys/class/thermal/thermal_zone0/temp",&td)) temp_pct = td/1000; if(temp_pct>100)temp_pct=100; }

                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, "Carga CPU",  sysinfo_cpu_usage, sysinfo_cpu_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, "Ocupación RAM",  dev_ram,           ram_pct);          y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, "Temp CPU", sysinfo_temp,      temp_pct);         y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Uptime",   dev_uptime);                          y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Load Avg", sysinfo_loadavg);
            }

            /* ── BLOQUE 2 IZQ: ALMACENAMIENTO ───────────────────────────── */
            y = SI_SEP_H1 + 4.0f;
            SI_BLOCK_TITLE(SI_MX, y, "VOLÚMENES:");
            y += 28.0f;
            {
                /* Disco sistema: pct */
                int disk_root_pct = 0, disk_data_pct = 0;
                { struct statvfs st;
                  if (statvfs("/", &st) == 0 && st.f_blocks > 0)
                      disk_root_pct = (int)(100 - 100ULL * st.f_bfree / st.f_blocks); }
                { struct statvfs st;
                  if (statvfs("/media/amiga_data", &st) == 0 && st.f_blocks > 0)
                      disk_data_pct = (int)(100 - 100ULL * st.f_bfree / st.f_blocks); }

                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, "DH0: (Sistema)", sysinfo_disk_root, disk_root_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, "DH1: (Datos)",   sysinfo_disk_data, disk_data_pct); y += SI_ROW_H;
                /* Libre total en datos */
                {
                    char free_buf[32] = "--";
                    struct statvfs st;
                    if (statvfs("/media/amiga_data", &st) == 0) {
                        unsigned long long free_mb = (unsigned long long)st.f_bfree * st.f_frsize / (1024*1024);
                        if (free_mb >= 1024) snprintf(free_buf, sizeof(free_buf), "%.1f GB", free_mb / 1024.0);
                        else                 snprintf(free_buf, sizeof(free_buf), "%llu MB", free_mb);
                    }
                    SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Espacio disponible", free_buf);
                }
            }

            /* ── BLOQUE 2 DER: HARDWARE ──────────────────────────────────── */
            y = SI_SEP_H1 + 4.0f;
            SI_BLOCK_TITLE(SI_RX, y, "ESPECIFICACIONES:");
            y += 28.0f;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "CPU",           "Cortex-A53 @1.51GHz"); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "GPU",           "Mali-G31 (Panfrost)"); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "RAM",           "1 GB LPDDR4");         y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "Almacenamiento","microSD");              y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "Resolución",    "640x480 @ 60Hz");

            /* ── BLOQUE 3 IZQ: SOFTWARE ──────────────────────────────────── */
            y = SI_SEP_H2 + 4.0f;
            SI_BLOCK_TITLE(SI_MX, y, "MOTOR DE EMULACION:");
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "RetroArch", s_retroarch); y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Mesa",      s_mesa);      y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "SDL3",      s_sdl3);      y += SI_ROW_H;

            /* ── BLOQUE 3 DER: RED ───────────────────────────────────────── */
            y = SI_SEP_H2 + 4.0f;
            SI_BLOCK_TITLE(SI_RX, y, "CONECTIVIDAD");
            y += 28.0f;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "IP",          dev_ip);          y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "WiFi",        status_wifi_up ? "Conectado" : "Desconectado"); y += SI_ROW_H;
            SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, "Intensidad",  sysinfo_wifi_sig, sysinfo_wifi_pct >= 0 ? sysinfo_wifi_pct : 0); y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "MAC",         sysinfo_mac);

#undef SI_BLOCK_TITLE
#undef SI_ROW
#undef SI_ROW_BAR

            /* Barra inferior */
            draw_line(ren, SI_MX, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, "[A] Volver");
        } else if (state == STATE_UPDATE) {
            const float UX = 20.0f;
            draw_text(ren, f_sm, "ACTUALIZACI\xc3\x93N DE SISTEMA", c_green, UX, 20.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, UX, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            /* Versión actual */
            {
                char cur_ver[32] = "1.0";
                FILE *rf = fopen("/etc/armiga-release", "r");
                if (rf) {
                    char ln[128];
                    while (fgets(ln, sizeof(ln), rf))
                        if (!strncmp(ln, "ARMIGA_VERSION=", 15))
                            sscanf(ln+15, "%31s", cur_ver);
                    fclose(rf);
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "Versión instalada:   %s", cur_ver);
                draw_text(ren, f_sm, buf, c_gray, UX, 64.0f);
            }

            if (update_phase == UPD_CHECKING) {
                draw_text(ren, f_sm, "Comprobando actualizaciones...", c_white, UX, 100.0f);

            } else if (update_phase == UPD_NO_UPDATE) {
                draw_text(ren, f_sm, "El sistema est\xc3\xa1 actualizado.", c_green, UX, 100.0f);

            } else if (update_phase == UPD_CONFIRM) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Nueva versión disponible:   %s", upd_new_ver);
                draw_text(ren, f_sm, buf, c_green, UX, 100.0f);
                draw_text(ren, f_sm, "La descarga se realizará en segundo plano.", c_gray, UX, 122.0f);
                draw_text(ren, f_sm, "El dispositivo se reiniciará al completar.", c_gray, UX, 140.0f);
                draw_line(ren, UX, 170.0f, SCREEN_W - 20.0f, 170.0f, c_dkgreen);
                draw_text(ren, f_med, "[B] Descargar e instalar", c_green,  UX,          188.0f);
                draw_text(ren, f_med, "[A] Cancelar",             c_gray,   UX + 260.0f, 188.0f);

            } else if (update_phase == UPD_DOWNLOADING) {
                draw_text(ren, f_sm, "Descargando actualizaci\xc3\xb3n...", c_white, UX, 100.0f);
                /* Barra de progreso */
                int pct = (int)(upd_progress * 100.0f);
                char pct_buf[8]; snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
                draw_bar(ren, f_sm, pct, c_green, c_dkgreen, UX, 122.0f, 30);
                draw_text(ren, f_sm, pct_buf, c_white, UX + 280.0f, 122.0f);
                draw_text(ren, f_sm, "No apagues el dispositivo durante la descarga.", c_gray, UX, 144.0f);

            } else if (update_phase == UPD_VERIFYING) {
                draw_text(ren, f_sm, "Verificando integridad...", c_white, UX, 100.0f);

            } else if (update_phase == UPD_READY) {
                draw_text(ren, f_sm, "Actualización lista. Reiniciando...", c_green, UX, 100.0f);
                /* Reiniciar automáticamente */
                SDL_Delay(2000);
                exec_req = EXEC_REBOOT;
                running  = false;

            } else if (update_phase == UPD_ERROR) {
                { SDL_Color _c_red = COL_RED; draw_text(ren, f_sm, "Error:", _c_red, UX, 100.0f); }
                draw_text(ren, f_sm, upd_msg,  c_gray, UX, 118.0f);
            }

            draw_line(ren, UX, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            if (update_phase != UPD_DOWNLOADING)
                draw_footer(ren, f_sm, "[A] Volver");
            else
                draw_footer(ren, f_sm, "");

        } /* end STATE_UPDATE */

        /* Flash blanco al hacer screenshot */
        if (screenshot_flash_until > 0 && SDL_GetTicks() < screenshot_flash_until) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(ren, 255, 255, 255, 180);
            SDL_FRect flash_rect = {0, 0, (float)SCREEN_W, (float)SCREEN_H};
            SDL_RenderFillRect(ren, &flash_rect);
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
        } else {
            screenshot_flash_until = 0;
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
            if (!redirect_stdio_to_local_console()) return 1;
            const char *shell = getenv("SHELL");
            if (!shell || shell[0] == '\0') shell = "/bin/sh";
            execl(shell, shell, "-i", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar %s: %s\n",
                    shell, strerror(errno));
            return 1;
        }
        case EXEC_DEV_TERMINAL: {
            if (!redirect_stdio_to_local_console()) return 1;
            const char *shell = getenv("SHELL");
            if (!shell || shell[0] == '\0') shell = "/bin/sh";
            execl(shell, shell, "-i", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar %s: %s\n",
                    shell, strerror(errno));
            return 1;
        }
        case EXEC_DEV_BTOP:
            if (!redirect_stdio_to_local_console()) return 1;
            execl("/usr/bin/btop", "btop", "--force-utf", (char *)NULL);
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
            if (relaunch_after_retroarch) {
                pid_t pid = fork();
                if (pid == 0) {
                    /* hijo: RetroArch toma la pantalla/DRM.
                     * Forzamos HOME porque el launcher arranca desde
                     * inittab con un entorno minimo (sin HOME), y
                     * RetroArch falla en silencio sin esa variable. */
                    setenv("HOME", "/root", 1);
                    execl("/usr/bin/retroarch", "retroarch", (char *)NULL);
                    fprintf(stderr, "armiga-launcher: no se pudo ejecutar retroarch: %s\n",
                            strerror(errno));
                    _exit(1);
                } else if (pid > 0) {
                    int status;
                    waitpid(pid, &status, 0);
                    /* al terminar RetroArch, volvemos al inicio del for(;;)
                     * para reinicializar SDL/DRM desde cero */
                    continue;
                } else {
                    fprintf(stderr, "armiga-launcher: fork() fallo: %s\n",
                            strerror(errno));
                }
            }
            return 0;
    }
    } /* for(;;) */
}
