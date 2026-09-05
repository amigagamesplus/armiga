/* =============================================================================
 * armiga-launcher — Launcher del sistema armiga para Anbernic RG40XX H
 *
 * Autor:    Vince
 * Proyecto: armiga (https://github.com/amigagamesplus/armiga)
 * Licencia: GPLv3
 * =============================================================================
 */

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <linux/kd.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include "logo.h"

/* Limpia /dev/fb0 a negro. Necesario porque S05splash pinta el splash
 * "armiga" en el framebuffer legacy (fbcon) UNA sola vez en el boot y
 * nunca se vuelve a tocar. Mientras el launcher tiene el DRM master
 * (SDL) no se ve, pero cada vez que se suelta el master (SDL_Quit()
 * antes de un execl, y de nuevo cuando el proceso siguiente -p.ej.
 * RetroArch- hace su propio setup antes de tomar el DRM) el kernel cae
 * momentaneamente al contenido de fbcon, que sigue siendo el splash
 * "armiga" del arranque -> parpadeo doble del logo antes de que se
 * abra RetroArch. Se limpia una vez aqui, justo tras SDL_Quit(), y
 * queda en negro (persistente en memoria) hasta el proximo boot, que
 * S05splash lo repinta de nuevo. */
static void clear_fb0(void)
{
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) return;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(fd);
        return;
    }
    size_t fbsize = finfo.smem_len;
    unsigned char *fbmem = mmap(NULL, fbsize, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);
    if (fbmem != MAP_FAILED) {
        memset(fbmem, 0, fbsize);
        munmap(fbmem, fbsize);
    }
    close(fd);
}

/* strncpy no garantiza null-terminacion si src >= sz; este helper si */
static void safe_copy(char *dst, const char *src, size_t sz) {
    if (sz == 0) return;
    strncpy(dst, src, sz - 1);
    dst[sz - 1] = '\0';
}

#define SCREEN_W  640
#define SCREEN_H  480

#define FONT_PATH    "/usr/share/armiga/fonts/JetBrainsMonoNL-ExtraBold.ttf"
#define FONT_MED     13
#define FONT_SM      12
#define FONT_XS      9
#define FONT_XSM     10
#define FONT_LG      28

#define COL_BG       {15, 31, 24, 255}
#define COL_CREAM    {231, 239, 231, 255} /* usado antes bajo 3 nombres distintos (GREEN/DKGREEN/GRAY) con el mismo valor */
#define COL_WHITE    {220, 220, 220, 255}
#define COL_SEL_BG   {183, 221, 91, 255}
#define COL_RED      {200,  40,  40, 255}
#define COL_KEY_BG   { 22,  22,  22, 255}


typedef enum {
    STATE_MENU,
    STATE_DEVMODE,
    STATE_CONFIRM,
    STATE_SYSINFO,
    STATE_UPDATE,
    STATE_SETTINGS,
    STATE_WIFI_CONFIG,
    STATE_KEYBOARD,
    STATE_BACKUP_MENU,
    STATE_BACKUP_LIST,
    STATE_LED_CONFIG,
    STATE_TIMEZONE_CONFIG,
    STATE_SCREENDIM_CONFIG,
    STATE_BRIGHTNESS_CONFIG,
    STATE_PERF_CONFIG,
    STATE_BLUETOOTH_CONFIG,
    STATE_AREXX_LIST,
    STATE_AREXX_RUN,
    STATE_CONTROLLER_TEST
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
#define ACTION_AREXX   4
#define ACTION_SETTINGS 5
#define ACTION_SHELL   6
#define ACTION_REBOOT  7

#define KB_ROWS 4
#define KB_MAX_COLS 10
/* Filas de letras, min y mayus intercambiables por modo. Los indices de
   fila/columna coinciden entre minusculas, mayusculas y numeros/simbolos
   para que la navegacion no salte al cambiar de modo. */
static const char *KB_LOWER[KB_ROWS][KB_MAX_COLS] = {
    {"q","w","e","r","t","y","u","i","o","p"},
    {"a","s","d","f","g","h","j","k","l","ñ"},
    {"z","x","c","v","b","n","m","-","_", NULL},
    {" ", NULL},
};
static const char *KB_UPPER[KB_ROWS][KB_MAX_COLS] = {
    {"Q","W","E","R","T","Y","U","I","O","P"},
    {"A","S","D","F","G","H","J","K","L","Ñ"},
    {"Z","X","C","V","B","N","M","-","_", NULL},
    {" ", NULL},
};
static const char *KB_SYMBOLS[KB_ROWS][KB_MAX_COLS] = {
    {"1","2","3","4","5","6","7","8","9","0"},
    {"!","@","#","$","%","^","&","*","(", ")"},
    {".",",","/","\\",":",";","+","=", NULL, NULL},
    {" ", NULL},
};
#define KB_MODE_LOWER   0
#define KB_MODE_UPPER   1
#define KB_MODE_SYMBOLS 2

static const char *kb_key_at(int mode, int row, int col)
{
    if (row < 0 || row >= KB_ROWS || col < 0 || col >= KB_MAX_COLS) return NULL;
    if (mode == KB_MODE_UPPER)   return KB_UPPER[row][col];
    if (mode == KB_MODE_SYMBOLS) return KB_SYMBOLS[row][col];
    return KB_LOWER[row][col];
}
static int kb_row_len(int mode, int row)
{
    int c = 0;
    while (c < KB_MAX_COLS && kb_key_at(mode, row, c) != NULL) c++;
    return c;
}

static const char *MENU_ICONS[] = {
    "[>]",
    "[~]",
    "[i]",
    "[#]",
    "[O]",
};

static const char *MENU_ITEMS[][2] = {
    {"Catálogo Amiga",              "Amiga Catalog"},
    {"Actualización de sistema",    "System Update"},
    {"Diagnóstico del sistema",     "System Diagnostics"},
    {"ARexx Scripts",               "ARexx Scripts"},
    {"Configuración",               "Settings"},
    {"Apagar dispositivo",          "Power Off"},
    {"Reiniciar dispositivo",       "Reboot"},
};
static const char *MENU_DESC[][2] = {
    {"Explora y lanza juegos\n" "Amiga desde tu biblioteca.",
     "Browse and launch Amiga\n" "games from your library."},
    {"Descarga e instala la\n" "ultima version de armiga.",
     "Download and install the\n" "latest version of armiga."},
    {"Revisa el estado del\n" "hardware y el sistema.",
     "Check the status of the\n" "hardware and system."},
    {"Scripts del sistema para\n" "tareas y comodidades extra.",
     "System scripts for extra\n" "tasks and conveniences."},
    {"Ajustes del sistema:\n" "red inalambrica y mas.",
     "System settings:\n" "wireless network and more."},
    {"Apaga el dispositivo\n" "de forma segura.",
     "Shut down the device\n" "safely."},
    {"Reinicia el dispositivo\n" "de forma segura.",
     "Restart the device\n" "safely."},
};
#define MENU_COUNT 7

static const char *SETTINGS_MENU_ITEMS[][2] = {
    {"Red inalámbrica",             "Wireless Network"},
    {"Copia de seguridad",          "Backup"},
    {"LED RGB analógicos",          "Analog Stick LEDs"},
    {"Zona horaria",                "Time Zone"},
    {"Ahorro de pantalla",          "Screen Dimming"},
    {"Brillo de pantalla",          "Screen Brightness"},
    {"SSH",                         "SSH"},
    {"Samba (\\\\armiga)",           "Samba (\\\\armiga)"},
    {"Rendimiento",                 "Performance"},
    {"Bluetooth",                   "Bluetooth"},
    {"Frecuencia de refresco",       "Refresh Rate"},
    {"Restablecer valores de fábrica", "Factory reset"},
    {"Test de mando",                "Controller Test"},
};
#define SETTINGS_MENU_COUNT 13
#define SETTINGS_ACTION_FACTORY_RESET 11
#define SETTINGS_ITEM_CONTROLLER_TEST 12

/* Tiempos de inactividad seleccionables, en segundos. 0 = Nunca. */
static const int DIM_TIMEOUT_OPTIONS[] = {0, 60, 300, 600, 900, 1800, 3600};
static const char *DIM_TIMEOUT_LABELS[][2] = {
    {"Nunca",     "Never"},
    {"1 minuto",  "1 minute"},
    {"5 minutos", "5 minutes"},
    {"10 minutos","10 minutes"},
    {"15 minutos","15 minutes"},
    {"30 minutos","30 minutes"},
    {"60 minutos","60 minutes"},
};
#define DIM_TIMEOUT_COUNT (int)(sizeof(DIM_TIMEOUT_OPTIONS) / sizeof(DIM_TIMEOUT_OPTIONS[0]))
#define DIM_BACKLIGHT_PATH "/sys/class/backlight/backlight/brightness"
#define DIM_BACKLIGHT_MAX_PATH "/sys/class/backlight/backlight/max_brightness"

typedef struct {
    const char *tz_name;    /* nombre IANA, usado como valor TZ real */
    const char *label[2];   /* etiqueta mostrada, es/en */
} TimezoneEntry;

static const TimezoneEntry TIMEZONE_LIST[] = {
    {"Europe/Madrid",              {"Madrid",        "Madrid"}},
    {"Europe/London",               {"Londres",       "London"}},
    {"Europe/Berlin",               {"Berlín",        "Berlin"}},
    {"Europe/Moscow",               {"Moscú",         "Moscow"}},
    {"America/New_York",            {"Nueva York",    "New York"}},
    {"America/Chicago",             {"Chicago",       "Chicago"}},
    {"America/Denver",              {"Denver",        "Denver"}},
    {"America/Los_Angeles",         {"Los Ángeles",   "Los Angeles"}},
    {"America/Mexico_City",         {"Ciudad de México", "Mexico City"}},
    {"America/Sao_Paulo",           {"São Paulo",     "Sao Paulo"}},
    {"America/Argentina/Buenos_Aires", {"Buenos Aires", "Buenos Aires"}},
    {"Asia/Dubai",                  {"Dubái",         "Dubai"}},
    {"Asia/Kolkata",                {"Bombay",        "Mumbai"}},
    {"Asia/Shanghai",               {"Shanghái",      "Shanghai"}},
    {"Asia/Tokyo",                  {"Tokio",         "Tokyo"}},
    {"Asia/Seoul",                  {"Seúl",          "Seoul"}},
    {"Australia/Sydney",            {"Sídney",        "Sydney"}},
    {"Pacific/Auckland",            {"Auckland",      "Auckland"}},
    {"Africa/Cairo",                {"El Cairo",      "Cairo"}},
    {"UTC",                         {"UTC (sin ajuste)", "UTC (no offset)"}},
};
#define TIMEZONE_LIST_COUNT (int)(sizeof(TIMEZONE_LIST) / sizeof(TIMEZONE_LIST[0]))

static const char *BACKUP_MENU_ITEMS[][2] = {
    {"Crear nueva copia",           "Create new backup"},
    {"Restaurar copia",             "Restore backup"},
};
#define BACKUP_MENU_COUNT 2

#define DEV_ACTION_TERMINAL 0
#define DEV_ACTION_BTOP     1
#define DEV_ACTION_REBOOT   2
#define DEV_ACTION_SHUTDOWN 3
#define DEV_ACTION_FPS_TOGGLE 4

static const char *DEV_MENU_ITEMS[] = {
    "Terminal",
    "btop",
    "Reboot",
    "Shutdown",
    "FPS Counter",
};
#define DEV_MENU_COUNT 5

/* SDL button indices del H700 (confirmados en hardware, no kernel/evdev) */
#define BTN_SDL_B      1
#define BTN_SDL_A      0
#define BTN_SDL_L1     4
#define BTN_SDL_R1     5
#define BTN_SDL_SELECT 8
#define BTN_SDL_START  9
#define BTN_SDL_X      3
#define BTN_SDL_MODE   10

#define DEVMODE_HOLD_MS 3000

#define ARMIGA_CONFIG_PATH "/media/amiga_data/armiga.cfg"
typedef enum { LANG_ES = 0, LANG_EN = 1 } Lang;
static Lang current_lang = LANG_ES;
static const char *tr(const char *es, const char *en)
{
    return (current_lang == LANG_EN) ? en : es;
}

/* Cache en RAM de armiga.cfg para valores leidos en el bucle de render
 * (PERF_PROFILE, SSH_ENABLED). Poblada una vez al arrancar via config_load();
 * los save_* existentes deben actualizar los campos correspondientes tras
 * escribir a disco, para no depender de releer. */
typedef struct {
    int perf_profile;
    int ssh_enabled;
} AppConfigCache;
static AppConfigCache g_cfg = { .perf_profile = 1, .ssh_enabled = 1 };

static void config_load(void)
{
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "PERF_PROFILE")) g_cfg.perf_profile = atoi(val);
            else if (!strcmp(key, "SSH_ENABLED")) g_cfg.ssh_enabled = atoi(val);
        }
    }
    fclose(f);
    if (g_cfg.perf_profile < 0 || g_cfg.perf_profile > 2) g_cfg.perf_profile = 1;
    g_cfg.ssh_enabled = g_cfg.ssh_enabled ? 1 : 0;
}

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
    /* Forzar tty0 como terminal de control de esta sesion. Sin esto,
     * la shell interactiva (/bin/sh -i) no tiene control de trabajos
     * ("can't access tty; job control turned off") tras el setsid -w
     * del wrapper, que desvincula al launcher de cualquier ctty previo. */
    if (ioctl(fd, TIOCSCTTY, 1) < 0) {
        fprintf(stderr, "armiga-launcher: TIOCSCTTY fallo: %s\n", strerror(errno));
    }
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    /* SDL/DRM deja la consola en KD_GRAPHICS; sin esto el shell corre
     * pero no se ve nada en pantalla (cursor parpadeando, sin texto). */
    ioctl(fd, KDSETMODE, KD_TEXT);
    /* Limpiar residuos graficos previos (p.ej. armiga-splash-text) que
     * pudieran seguir en el framebuffer bajo el modo texto. */
    write(fd, "\033[2J\033[H", 7);
    /* El kernel arranca con vt.global_cursor_default=0 (cursor oculto
     * globalmente, para que el launcher grafico no muestre cursor de
     * texto de fondo). Lo reactivamos aqui solo para esta sesion de
     * terminal/btop; al volver al launcher (modo grafico via SDL/DRM)
     * el cursor de texto deja de ser relevante, no hace falta desactivarlo. */
    write(fd, "\033[?25h", 6);
    if (fd > STDERR_FILENO) close(fd);
    return true;
}

static void read_ip_address(char *buf, size_t bufsize)
{
    safe_copy(buf, "sin red", bufsize);
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
    safe_copy(buf, "--", bufsize);
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
    safe_copy(buf, "--", bufsize);
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
    safe_copy(buf, "--", bufsize);
    struct statvfs st;
    if (statvfs(path, &st) != 0) return;
    unsigned long long total_mb = (unsigned long long)st.f_blocks * st.f_frsize / (1024 * 1024);
    unsigned long long free_mb  = (unsigned long long)st.f_bfree  * st.f_frsize / (1024 * 1024);
    unsigned long long used_mb  = total_mb - free_mb;
    snprintf(buf, bufsize, "%llu/%llu MB", used_mb, total_mb);
}

static void read_disk_free_short(const char *path, char *buf, size_t bufsize)
{
    safe_copy(buf, "--", bufsize);
    struct statvfs st;
    if (statvfs(path, &st) != 0) return;
    unsigned long long free_mb = (unsigned long long)st.f_bfree * st.f_frsize / (1024 * 1024);
    if (free_mb >= 1024) {
        snprintf(buf, bufsize, "%.1f GB", free_mb / 1024.0);
    } else {
        snprintf(buf, bufsize, "%llu MB", free_mb);
    }
}
static void read_cpu_temp(char *buf, size_t bufsize)
{
    safe_copy(buf, "--", bufsize);
    FILE *f = fopen("/sys/class/thermal/thermal_zone2/temp", "r"); /* cpu-thermal; zone0 es gpu-thermal */
    if (!f) return;
    int millideg = 0;
    if (fscanf(f, "%d", &millideg) == 1) {
        snprintf(buf, bufsize, "%.1f C", millideg / 1000.0);
    }
    fclose(f);
}

/* Guarda un BMP de la pantalla actual en /media/amiga_data/screenshots/.
 * Devuelve 0 en éxito, -1 en error. */
static int take_screenshot(SDL_Renderer *ren, int screen_w, int screen_h,
                            SDL_Surface *clean_frame)
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

    /* Usa el frame limpio (sin esquinas redondeadas) guardado justo antes
     * de dibujar draw_screen_corners en la ultima iteracion del bucle,
     * en vez de leer el backbuffer actual (que ya incluye las esquinas). */
    SDL_Surface *surf = clean_frame;
    bool owned = false;
    if (!surf) {
        surf = SDL_RenderReadPixels(ren, NULL);
        owned = true;
        if (!surf) return -1;
    }

    int ret = SDL_SaveBMP(surf, path) ? 0 : -1;
    if (owned) SDL_DestroySurface(surf);
    return ret;
}


/* ── Actualización OTA ────────────────────────────────────────────────────── */
#define GITHUB_API_URL "https://api.github.com/repos/amigagamesplus/armiga/releases"
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
    if (a[0] == 'v' || a[0] == 'V') a++;
    if (b[0] == 'v' || b[0] == 'V') b++;
    sscanf(a, "%d.%d.%d", &ma, &mi_a, &pa);
    sscanf(b, "%d.%d.%d", &mb, &mi_b, &pb);
    if (ma != mb) return ma > mb ? 1 : -1;
    if (mi_a != mi_b) return mi_a > mi_b ? 1 : -1;
    if (pa != pb) return pa > pb ? 1 : -1;
    return 0;
}

/* Consulta GitHub API y devuelve versión+URL del asset.
 * Usa curl para peticiones HTTPS. Devuelve 0 si OK. */
/* PID del hijo curl para la consulta a la API de GitHub (async). */
static pid_t s_checkjson_pid = -1;
/* PID del hijo curl para la descarga del .sha256 (async, fase VERIFYING). */
static pid_t s_sha_pid = -1;
#define CHECK_JSON_TMP "/tmp/armiga_release.json"
#define BG_CHECK_JSON_TMP "/tmp/armiga_bgcheck.json"
/* Lanza curl en background hacia out_path, sin pasar por shell (B01). */
static pid_t spawn_curl_to_file(const char *url, const char *out_path, const char *max_time_secs)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execlp("curl", "curl", "-s", "--max-time", max_time_secs, "-L",
               "-o", out_path, url, (char *)NULL);
        _exit(127);
    }
    return pid;
}
/* Sondea un curl lanzado con spawn_curl_to_file. Devuelve: 0=en curso,
 * 1=completado con exito (fichero existe y pesa >= min_size_ok),
 * -1=error (proceso fallo o fichero vacio/ausente). */
static int poll_curl_pid(pid_t *pid_var, const char *out_path, long min_size_ok)
{
    if (*pid_var <= 0) return -1;
    int status = 0;
    pid_t r = waitpid(*pid_var, &status, WNOHANG);
    if (r == 0) return 0;
    *pid_var = -1;
    if (r > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        struct stat st;
        if (stat(out_path, &st) == 0 && st.st_size >= min_size_ok) return 1;
    }
    return -1;
}
/* Lanza en background la consulta a la API de GitHub (R01: permite animar
 * "Comprobando..." mientras se espera la respuesta de red). */
static void start_check_update_async(void)
{
    unlink(CHECK_JSON_TMP);
    s_checkjson_pid = spawn_curl_to_file(GITHUB_API_URL, CHECK_JSON_TMP, "10");
}
/* Parsea el JSON ya descargado por start_check_update_async(). Llamar solo
 * cuando poll_curl_pid() haya devuelto != 0 para el fetch. */
static int finish_check_update(const char *json_path, const char *current_ver,
                        char *new_ver, size_t new_ver_sz,
                        char *dl_url, size_t dl_url_sz,
                        char *sha_url, size_t sha_url_sz)
{
    safe_copy(new_ver, "", new_ver_sz);
    safe_copy(dl_url,  "", dl_url_sz);
    safe_copy(sha_url, "", sha_url_sz);
    FILE *f = fopen(json_path, "r");
    if (!f) return -1;
    char line[512];
    char tag[32] = "";
    char asset_url[512] = "";
    char sha_asset_url[512] = "";
    bool in_assets = false;
    char last_name[128] = "";
    while (fgets(line, sizeof(line), f)) {
        char *p;
        if ((p = strstr(line, "\"tag_name\""))) {
            sscanf(p, "\"tag_name\" : \"%31[^\"]\"", tag);
            if (!tag[0]) sscanf(p, "\"tag_name\":\"%31[^\"]\"", tag);
            if (!tag[0]) sscanf(p, "\"tag_name\": \"%31[^\"]\"", tag);
        }
        if (strstr(line, "\"assets\"")) in_assets = true;
        if (in_assets) {
            if ((p = strstr(line, "\"name\""))) {
                last_name[0] = '\0';
                sscanf(p, "\"name\" : \"%127[^\"]\"", last_name);
                if (!last_name[0]) sscanf(p, "\"name\":\"%127[^\"]\"", last_name);
                if (!last_name[0]) sscanf(p, "\"name\": \"%127[^\"]\"", last_name);
            }
            if ((p = strstr(line, "\"browser_download_url\""))) {
                char url[512] = "";
                sscanf(p, "\"browser_download_url\" : \"%511[^\"]\"", url);
                if (!url[0]) sscanf(p, "\"browser_download_url\":\"%511[^\"]\"", url);
                if (!url[0]) sscanf(p, "\"browser_download_url\": \"%511[^\"]\"", url);
                if (strstr(last_name, ".img.gz") && !strstr(last_name, ".sha256"))
                    safe_copy(asset_url, url, sizeof(asset_url));
                if (strstr(last_name, ".sha256"))
                    safe_copy(sha_asset_url, url, sizeof(sha_asset_url));
                if (asset_url[0] && sha_asset_url[0]) goto parse_done;
            }
        }
    }
    parse_done:
    fclose(f);
    unlink(json_path);
    if (!tag[0] || !asset_url[0]) return 0;
    const char *ver = tag;
    if (ver[0] == 'v') ver++;
    safe_copy(new_ver, ver, new_ver_sz);
    safe_copy(dl_url,  asset_url,     dl_url_sz);
    safe_copy(sha_url, sha_asset_url, sha_url_sz);
    return semver_cmp(ver, current_ver) > 0 ? 1 : 0;
}

/* Descarga el .img.gz con progreso. Ejecuta curl en background y
 * monitoriza el fichero destino para actualizar la barra.
 *
 * Tolerancia a red inestable (WiFi de mano, cortes frecuentes):
 * - Si la URL coincide con el ultimo intento, NO se borra el .img.gz
 *   parcial: curl -C - retoma desde el byte ya descargado en vez de
 *   volver a empezar de 0. Solo se borra al cambiar de version/URL
 *   (nueva actualizacion) o tras verificar SHA256 con exito.
 * - --retry/--retry-delay/--connect-timeout dan margen a curl para
 *   recuperarse de cortes breves sin abortar el proceso entero. */
/* PID real del hijo curl (no via fichero, evita reciclado de PID). */
static pid_t s_curl_pid = -1;
static char s_last_download_url[512] = "";
static int download_update(const char *url, float *progress_out)
{
    mkdir(UPDATE_DIR, 0755);
    bool same_target = (strcmp(url, s_last_download_url) == 0);
    if (!same_target) {
        /* Nueva version/URL distinta a la del ultimo intento: descartar
         * cualquier descarga parcial previa, no es reanudable. */
        unlink(UPDATE_IMG);
        unlink(UPDATE_SHA256);
        safe_copy(s_last_download_url, url, sizeof(s_last_download_url));
    }
    s_curl_pid = -1;

    pid_t pid = fork();
    if (pid < 0) return -1; /* fork fallo */
    if (pid == 0) {
        /* Hijo: redirige salida y ejecuta curl directamente, sin shell.
         * url llega como argv literal: sin interpolacion, sin riesgo de
         * inyeccion de comandos (B01). */
        int fd = open("/tmp/curl_progress", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execlp("curl", "curl", "-s", "-C", "-",
               "--retry", "3", "--retry-delay", "2",
               "--connect-timeout", "15", "--max-time", "300", "-L",
               "-o", UPDATE_IMG, url, (char *)NULL);
        _exit(127); /* solo si execlp falla */
    }
    s_curl_pid = pid;
    *progress_out = 0.0f;
    return 0;
}

static float get_download_progress(const char *url)
{
    (void)url;
    if (s_curl_pid <= 0) return -1.0f; /* error */
    int status = 0;
    pid_t r = waitpid(s_curl_pid, &status, WNOHANG);
    bool running = (r == 0);
    if (!running) {
        s_curl_pid = -1;
        /* Verificar que el fichero existe y tiene tamaño, y que curl salio OK */
        struct stat st;
        bool curl_ok = (r > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        if (curl_ok && stat(UPDATE_IMG, &st) == 0 && st.st_size > 1024*1024)
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
    if (access(UPDATE_SHA256, F_OK) != 0) return -1; /* sin sha, rechazar (fail-safe) */
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
            } else if (!strcmp(key, "LANG")) {
                current_lang = (!strcmp(val, "EN")) ? LANG_EN : LANG_ES;
            }
        }
    }
    fclose(f);
}
/* Escritura atomica generica de N claves en ARMIGA_CONFIG_PATH,
 * preservando el resto de lineas. Reemplaza el patron duplicado de las
 * 9 funciones save_*_config. Fichero temporal + fsync + rename evita
 * corrupcion de armiga.cfg si la consola se apaga a mitad de guardado. */
static void config_set_kv_multi(const char *keys[], const char *vals[], int count)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char line_key[32], line_val[96];
            bool skip = false;
            if (sscanf(lines[n], "%31[^=]=%95s", line_key, line_val) == 2) {
                for (int i = 0; i < count; i++) {
                    if (!strcmp(line_key, keys[i])) { skip = true; break; }
                }
            }
            if (skip) continue;
            n++;
        }
        fclose(f);
    }
    char tmp_path[160];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", ARMIGA_CONFIG_PATH);
    f = fopen(tmp_path, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    for (int i = 0; i < count; i++) fprintf(f, "%s=%s\n", keys[i], vals[i]);
    fflush(f);
    fsync(fileno(f));
    fclose(f);
    rename(tmp_path, ARMIGA_CONFIG_PATH);
}
static void config_set_kv(const char *key, const char *val)
{
    const char *keys[1] = { key };
    const char *vals[1] = { val };
    config_set_kv_multi(keys, vals, 1);
}
/* Guarda el idioma actual en armiga.cfg. */
static void save_lang_config(void)
{
    config_set_kv("LANG", (current_lang == LANG_EN) ? "EN" : "ES");
}
/* Lee el valor TZ actual de armiga.cfg (sin aplicarlo, solo para saber
 * cual esta activo, ej. al abrir el selector de zona horaria). */
static void read_current_tz(char *tz_name, size_t tz_sz)
{
    strncpy(tz_name, "UTC", tz_sz - 1);
    tz_name[tz_sz - 1] = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2 && !strcmp(key, "TZ")) {
            strncpy(tz_name, val, tz_sz - 1);
            tz_name[tz_sz - 1] = 0;
        }
    }
    fclose(f);
}
/* Guarda TZ en armiga.cfg. */
static void save_timezone_config(const char *tz_name)
{
    config_set_kv("TZ", tz_name);
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

static bool s_status_charging = false;
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

    char chg_status[16] = {0};
    s_status_charging = read_sysfs_str("/sys/class/power_supply/battery/status", chg_status, sizeof(chg_status))
                         && (strcmp(chg_status, "Charging") == 0 || strcmp(chg_status, "Full") == 0);
}

#define WIFI_CONF_PATH "/media/amiga_data/wifi.conf"
static void read_wifi_conf(char *ssid, size_t ssid_sz, char *password, size_t password_sz)
{
    safe_copy(ssid, "", ssid_sz);
    safe_copy(password, "", password_sz);
    FILE *f = fopen(WIFI_CONF_PATH, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "SSID=", 5))
            safe_copy(ssid, line + 5, ssid_sz);
        else if (!strncmp(line, "PASSWORD=", 9))
            safe_copy(password, line + 9, password_sz);
    }
    fclose(f);
}
static bool save_wifi_conf(const char *ssid, const char *password)
{
    FILE *f = fopen(WIFI_CONF_PATH, "w");
    if (!f) return false;
    fprintf(f, "SSID=%s\nPASSWORD=%s\n", ssid, password);
    fclose(f);
    return true;
}
static int read_max_brightness(void)
{
    FILE *f = fopen(DIM_BACKLIGHT_MAX_PATH, "r");
    if (!f) return 2499; /* fallback razonable si el sysfs no responde */
    int v = 2499;
    if (fscanf(f, "%d", &v) != 1) v = 2499;
    fclose(f);
    return v;
}
static int read_current_brightness(void)
{
    FILE *f = fopen(DIM_BACKLIGHT_PATH, "r");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v;
}
/* Cambia el gobernador de CPU en todos los cores. Usado al atenuar/
 * restaurar pantalla (ahorro de bateria durante inactividad). */
static void set_cpu_governor(const char *gov)
{
    for (int i = 0; i < 8; i++) {
        char path[80];
        snprintf(path, sizeof(path),
                 "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", i);
        FILE *f = fopen(path, "w");
        if (!f) continue; /* ese core puede no existir, no es error */
        fprintf(f, "%s", gov);
        fclose(f);
    }
}
/* Escribe un valor en un fichero sysfs directamente (open/write/close),
 * evitando el fork+exec de /bin/sh que supone system("echo ... > ..."). */
static void write_sysfs_str(const char *path, const char *val)
{
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    ssize_t unused_result = write(fd, val, strlen(val));
    (void)unused_result;
    close(fd);
}
static void write_brightness(int value)
{
    FILE *f = fopen(DIM_BACKLIGHT_PATH, "w");
    if (!f) return;
    fprintf(f, "%d", value);
    fclose(f);
}
/* Lee DIM_TIMEOUT y DIM_PERCENT de armiga.cfg. Defaults: Nunca (0), 20%. */
static void read_dim_config(int *timeout_sec, int *dim_percent)
{
    *timeout_sec = 0;
    *dim_percent = 20;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "DIM_TIMEOUT")) *timeout_sec = atoi(val);
            else if (!strcmp(key, "DIM_PERCENT")) *dim_percent = atoi(val);
        }
    }
    fclose(f);
}
/* Guarda DIM_TIMEOUT y DIM_PERCENT en armiga.cfg, una unica escritura
 * atomica (ambas claves juntas). */
static void save_dim_config(int timeout_sec, int dim_percent)
{
    char v1[16], v2[16];
    snprintf(v1, sizeof(v1), "%d", timeout_sec);
    snprintf(v2, sizeof(v2), "%d", dim_percent);
    const char *keys[2] = { "DIM_TIMEOUT", "DIM_PERCENT" };
    const char *vals[2] = { v1, v2 };
    config_set_kv_multi(keys, vals, 2);
}
/* Lee BRIGHTNESS_PCT de armiga.cfg. Default: 80%. */
static int read_brightness_config(void)
{
    int pct = 80;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return pct;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "BRIGHTNESS_PCT")) pct = atoi(val);
        }
    }
    fclose(f);
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    return pct;
}
/* Guarda BRIGHTNESS_PCT en armiga.cfg. */
static void save_brightness_config(int pct)
{
    char v[16];
    snprintf(v, sizeof(v), "%d", pct);
    config_set_kv("BRIGHTNESS_PCT", v);
}
/* Lee REFRESH_120HZ de armiga.cfg. Default: desactivado (0, = 60Hz). */
static int read_refresh_120hz(void)
{
    int enabled = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "REFRESH_120HZ")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}
/* Guarda REFRESH_120HZ en armiga.cfg, preservando otras claves,
 * mismo patron que save_ssh_enabled. */
static void save_refresh_120hz(int enabled)
{
    config_set_kv("REFRESH_120HZ", enabled ? "1" : "0");
}
/* Guarda SSH_ENABLED en armiga.cfg. */
static void save_ssh_enabled(int enabled)
{
    config_set_kv("SSH_ENABLED", enabled ? "1" : "0");
    g_cfg.ssh_enabled = enabled ? 1 : 0;
}
/* Aplica el estado SSH en caliente, sin reiniciar. */
static void apply_refresh_120hz(SDL_Window *win, int enabled)
{
    SDL_DisplayID disp = SDL_GetDisplayForWindow(win);
    int num_modes = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(disp, &num_modes);
    for (int i = 0; i < num_modes; i++) {
        bool is_120 = modes[i]->refresh_rate > 100.0f;
        if ((enabled && is_120) || (!enabled && !is_120)) {
            SDL_SetWindowFullscreenMode(win, modes[i]);
            break;
        }
    }
    SDL_free(modes);
}
static void apply_ssh_enabled(int enabled)
{
    if (enabled)
        system("/etc/init.d/S50dropbear start >/dev/null 2>&1");
    else
        system("/etc/init.d/S50dropbear stop >/dev/null 2>&1");
}
/* Lee BT_ENABLED de armiga.cfg. Default: activado (1). */
static int read_bt_enabled(void)
{
    int enabled = 1;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "BT_ENABLED")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}
/* Guarda BT_ENABLED en armiga.cfg. */
static void save_bt_enabled(int enabled)
{
    config_set_kv("BT_ENABLED", enabled ? "1" : "0");
}
/* Fija (o revierte a altavoz) el audio_device de RetroArch para que el
 * audio del emulador salga por el Bluetooth conectado. mac==NULL o vacio
 * revierte a salida por defecto (altavoz interno). */
/* Reemplaza (o inserta al final si no existe) la linea que empieza por
 * "key = " en retroarch.cfg, preservando el resto del fichero linea a
 * linea. Evita el fork+exec de /bin/sh + sed que suponia system("sed -i").
 * new_line debe incluir el salto de linea final. */
static void patch_retroarch_cfg_line(const char *key, const char *new_line)
{
    const char *path = "/media/amiga_data/retroarch/retroarch.cfg";
    char **lines = NULL;
    int n = 0, cap = 0;
    bool replaced = false;
    size_t key_len = strlen(key);

    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        if (n == cap) {
            cap = cap ? cap * 2 : 512;
            lines = realloc(lines, (size_t)cap * sizeof(char *));
        }
        if (!replaced && strncmp(buf, key, key_len) == 0 &&
            (buf[key_len] == ' ' || buf[key_len] == '=')) {
            lines[n++] = strdup(new_line);
            replaced = true;
        } else {
            lines[n++] = strdup(buf);
        }
    }
    fclose(f);
    if (!replaced) {
        if (n == cap) {
            cap = cap ? cap * 2 : 512;
            lines = realloc(lines, (size_t)cap * sizeof(char *));
        }
        lines[n++] = strdup(new_line);
    }

    f = fopen(path, "w");
    if (f) {
        for (int i = 0; i < n; i++) fputs(lines[i], f);
        fclose(f);
    }
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
}
static void set_retroarch_audio_device(const char *mac)
{
    char line[300];
    if (mac && mac[0]) {
        snprintf(line, sizeof(line),
            "audio_device = \"bluealsa:DEV=%s,PROFILE=a2dp\"\n", mac);
    } else {
        snprintf(line, sizeof(line), "audio_device = \"\"\n");
    }
    patch_retroarch_cfg_line("audio_device", line);
}
/* Fija video_refresh_rate en retroarch.cfg para que RetroArch (proceso
 * aparte, con su propio SDL/DRM) pida el mismo modo de pantalla que el
 * launcher; si no, RetroArch siempre arranca a 60Hz independientemente
 * de lo elegido en Configuracion. Mismo patron que set_retroarch_audio_device. */
static void set_retroarch_refresh_rate(int hz)
{
    char line[300];
    snprintf(line, sizeof(line), "video_refresh_rate = \"%d.000000\"\n", hz);
    patch_retroarch_cfg_line("video_refresh_rate", line);
}
static void apply_bt_enabled(int enabled)
{
    /* Nunca bloquear el hilo principal: S21bluetooth puede tardar (sleeps
     * internos, arranque de bluetoothd/bluealsa) y un system() sincrono aqui
     * congela el mando. Se lanza en background, igual que scan/connect. */
    if (enabled)
        system("/etc/init.d/S21bluetooth start >/dev/null 2>&1 &");
    else
        system("/etc/init.d/S21bluetooth stop >/dev/null 2>&1 &");
}
static int read_wifi_enabled(void)
{
    int enabled = 1;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "WIFI_ENABLED")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}
/* Guarda WIFI_ENABLED en armiga.cfg. */
static void save_wifi_enabled(int enabled)
{
    config_set_kv("WIFI_ENABLED", enabled ? "1" : "0");
}
static void apply_wifi_enabled(int enabled)
{
    /* Igual que apply_bt_enabled: S41wifi puede tardar (espera de interface,
     * wpa_supplicant, DHCP) y un system() sincrono aqui congela el mando. */
    if (enabled)
        system("/etc/init.d/S41wifi start >/dev/null 2>&1 &");
    else
        system("/etc/init.d/S41wifi stop >/dev/null 2>&1 &");
}
/* Lee el MAC/nombre conectado desde el fichero que arma armiga-bt-scan en
 * background (formato "MAC|Nombre"). NUNCA lanza un subproceso propio aqui:
 * un popen() sincrono a bluetoothctl desde el hilo principal se ha
 * confirmado que puede colgarse (contencion con la sesion de escaneo en
 * paralelo) y congela el mando por completo. Solo lectura de fichero. */
static void read_bt_connected(char *mac_out, size_t mac_sz, char *name_out, size_t name_sz)
{
    if (mac_sz > 0) mac_out[0] = 0;
    if (name_out && name_sz > 0) name_out[0] = 0;
    FILE *f = fopen("/tmp/armiga-bt-connected.txt", "r");
    if (!f) return;
    char line[160];
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *sep = strchr(line, '|');
        if (sep) {
            *sep = 0;
            snprintf(mac_out, mac_sz, "%s", line);
            if (name_out && name_sz > 0) snprintf(name_out, name_sz, "%s", sep + 1);
        }
    }
    fclose(f);
}
static void save_perf_profile(int profile)
{
    char v[16];
    snprintf(v, sizeof(v), "%d", profile);
    config_set_kv("PERF_PROFILE", v);
    g_cfg.perf_profile = profile;
}
/* Aplica el perfil en caliente: CPU governor + GPU devfreq governor. */
static void apply_perf_profile(int profile)
{
    const char *gpu_gov_path = "/sys/class/devfreq/1800000.gpu/governor";
    const char *boost_path = "/sys/devices/system/cpu/cpufreq/boost";
    if (profile == 0) {
        set_cpu_governor("performance");
        write_sysfs_str(gpu_gov_path, "performance");
        /* CPU Boost: desbloquea el OPP de 1512MHz (por encima del maximo
         * normal de 1416MHz) solo en Maximum Performance. Volatil (se
         * pierde al reiniciar), por eso se reaplica aqui en caliente cada
         * vez que se selecciona el perfil, igual que el governor. */
        write_sysfs_str(boost_path, "1");
    } else if (profile == 2) {
        set_cpu_governor("powersave");
        write_sysfs_str(gpu_gov_path, "powersave");
        write_sysfs_str(boost_path, "0");
    } else {
        set_cpu_governor("schedutil");
        write_sysfs_str(gpu_gov_path, "performance");
        write_sysfs_str(boost_path, "0");
    }
}

/* Lee SAMBA_ENABLED de armiga.cfg. Default: activado (1). */
static int read_samba_enabled(void)
{
    int enabled = 1;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "SAMBA_ENABLED")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}

/* Guarda SAMBA_ENABLED en armiga.cfg. */
static void save_samba_enabled(int enabled)
{
    config_set_kv("SAMBA_ENABLED", enabled ? "1" : "0");
}

/* Aplica el estado Samba en caliente, sin reiniciar. */
static void apply_samba_enabled(int enabled)
{
    if (enabled)
        system("/etc/init.d/S52samba start >/dev/null 2>&1");
    else
        system("/etc/init.d/S52samba stop >/dev/null 2>&1");
}
#define LED_CONF_PATH "/media/amiga_data/led.conf"
static void read_led_conf(int *r_right, int *g_right, int *b_right,
                           int *r_left, int *g_left, int *b_left,
                           int *brightness)
{
    *r_right = 0; *g_right = 0; *b_right = 0;
    *r_left = 0; *g_left = 0; *b_left = 0;
    *brightness = 128;
    FILE *f = fopen(LED_CONF_PATH, "r");
    if (!f) return;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "R_RIGHT=", 8)) *r_right = atoi(line + 8);
        else if (!strncmp(line, "G_RIGHT=", 8)) *g_right = atoi(line + 8);
        else if (!strncmp(line, "B_RIGHT=", 8)) *b_right = atoi(line + 8);
        else if (!strncmp(line, "R_LEFT=", 7)) *r_left = atoi(line + 7);
        else if (!strncmp(line, "G_LEFT=", 7)) *g_left = atoi(line + 7);
        else if (!strncmp(line, "B_LEFT=", 7)) *b_left = atoi(line + 7);
        else if (!strncmp(line, "BRIGHTNESS=", 11)) *brightness = atoi(line + 11);
    }
    fclose(f);
}
static bool save_led_conf(int r_right, int g_right, int b_right,
                           int r_left, int g_left, int b_left,
                           int brightness)
{
    FILE *f = fopen(LED_CONF_PATH, "w");
    if (!f) return false;
    fprintf(f, "R_RIGHT=%d\nG_RIGHT=%d\nB_RIGHT=%d\n"
               "R_LEFT=%d\nG_LEFT=%d\nB_LEFT=%d\n"
               "BRIGHTNESS=%d\n",
               r_right, g_right, b_right, r_left, g_left, b_left, brightness);
    fclose(f);
    return true;
}
#define LED_SERIAL_DEV "/dev/ttyS2"
#define LED_LEDS_PER_STICK 8
/* Pauta 3: fd de LED_SERIAL_DEV cacheado en vez de abrir/cerrar en cada
 * llamada (hasta 16/s durante repeat-hold en LED_CONFIG). O_CLOEXEC evita
 * que el fd se filtre a execl(retroarch/shell/btop). */
static int s_led_fd = -1;
static int get_led_fd(void)
{
    if (s_led_fd >= 0) return s_led_fd;
    int fd = open(LED_SERIAL_DEV, O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) return -1;
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tcsetattr(fd, TCSANOW, &tio);
    }
    s_led_fd = fd;
    return fd;
}
static void send_led_payload(int brightness,
                              int r_right, int g_right, int b_right,
                              int r_left, int g_left, int b_left)
{
    write_sysfs_str("/sys/class/leds/rgb:kbd_backlight/brightness", "1");

    int fd = get_led_fd();
    if (fd < 0) return;

    unsigned char payload[2 + LED_LEDS_PER_STICK * 3 * 2 + 1];
    int idx = 0;
    unsigned int sum = 0;

    payload[idx] = 1; sum += payload[idx]; idx++;
    payload[idx] = (unsigned char)brightness; sum += payload[idx]; idx++;

    for (int i = 0; i < LED_LEDS_PER_STICK; i++) {
        payload[idx] = (unsigned char)r_right; sum += payload[idx]; idx++;
        payload[idx] = (unsigned char)g_right; sum += payload[idx]; idx++;
        payload[idx] = (unsigned char)b_right; sum += payload[idx]; idx++;
    }
    for (int i = 0; i < LED_LEDS_PER_STICK; i++) {
        payload[idx] = (unsigned char)r_left; sum += payload[idx]; idx++;
        payload[idx] = (unsigned char)g_left; sum += payload[idx]; idx++;
        payload[idx] = (unsigned char)b_left; sum += payload[idx]; idx++;
    }
    payload[idx] = (unsigned char)(sum & 0xFF); idx++;

    write(fd, payload, idx);
}
static void factory_reset(void)
{
    system("rm -f /media/amiga_data/armiga.cfg "
           "/media/amiga_data/wifi.conf "
           "/media/amiga_data/test_wifi.conf "
           "/media/amiga_data/wifi_debug.log "
           "/media/amiga_data/retroarch/retroarch.cfg");
    system("rm -rf /media/amiga_data/retroarch/config "
           "/media/amiga_data/.cache "
           "/media/amiga_data/.config "
           "/media/amiga_data/.local");
}
#define BACKUP_DIR "/media/amiga_data/backups"
#define BACKUP_MAX 3
/* PID del hijo tar en curso (backup async), -1 si no hay ninguno. */
static pid_t s_backup_pid = -1;
/* Lanza la creacion del backup en background via fork+execvp (sin shell,
 * coherente con B01). Escribe el nombre de fichero generado en out_name. */
static void start_backup_async(char *out_name, size_t out_name_sz)
{
    system("mkdir -p " BACKUP_DIR);
    char ts[32];
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    strftime(ts, sizeof(ts), "%d%m%Y_%H%M%S", tm_now);
    char backup_path[256];
    snprintf(backup_path, sizeof(backup_path), BACKUP_DIR "/backup_%s.tar.gz", ts);
    if (out_name && out_name_sz > 0)
        snprintf(out_name, out_name_sz, "backup_%s.tar.gz", ts);

    s_backup_pid = -1;
    pid_t pid = fork();
    if (pid < 0) return; /* fork fallo, poll_backup_progress lo detectara como error */
    if (pid == 0) {
        chdir("/media/amiga_data");
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
        {
            char cmd[512];
            char extra[256] = "";
            struct stat st;
            if (stat("/media/amiga_data/led.conf", &st) == 0)
                strcat(extra, " led.conf");
            if (stat("/media/amiga_data/shaders", &st) == 0)
                strcat(extra, " shaders");
            if (stat("/media/amiga_data/kickstarts", &st) == 0)
                strcat(extra, " kickstarts");
            snprintf(cmd, sizeof(cmd),
                "tar -caf '%s' armiga.cfg wifi.conf "
                "retroarch/retroarch.cfg retroarch/config "
                "retroarch/saves retroarch/states "
                "retroarch/playlists retroarch/thumbnails%s",
                backup_path, extra);
            execlp("sh", "sh", "-c", cmd, (char *)NULL);
        }
        _exit(127); /* solo si execlp falla */
    }
    s_backup_pid = pid;
}
/* Rota backups antiguos, mantiene solo BACKUP_MAX. Llamar tras confirmar
 * que el tar en background termino con exito. */
static void rotate_backups(void)
{
    char rot_cmd[160];
    snprintf(rot_cmd, sizeof(rot_cmd),
        "cd " BACKUP_DIR " && ls -t backup_*.tar.gz 2>/dev/null | "
        "tail -n +%d | xargs -r rm -f", BACKUP_MAX + 1);
    system(rot_cmd);
}
/* Sondea el estado del backup en curso. Devuelve: 0=en curso, 1=completado
 * con exito (ya rotado), -1=error. */
static int poll_backup_progress(void)
{
    if (s_backup_pid <= 0) return -1;
    int status = 0;
    pid_t r = waitpid(s_backup_pid, &status, WNOHANG);
    if (r == 0) return 0; /* sigue en curso */
    s_backup_pid = -1;
    if (r > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        rotate_backups();
        return 1;
    }
    return -1;
}
typedef struct { char name[64]; time_t mtime; } BackupEntry;
static int backup_entry_cmp_desc(const void *a, const void *b)
{
    const BackupEntry *ea = (const BackupEntry *)a, *eb = (const BackupEntry *)b;
    if (eb->mtime > ea->mtime) return 1;
    if (eb->mtime < ea->mtime) return -1;
    return 0;
}
/* Reemplaza popen("ls -t ...") por opendir/readdir + stat, evitando el
 * fork()+shell+ls por cada listado. Preserva el mismo orden (mas reciente
 * primero) via qsort por mtime real, ya que readdir() no garantiza orden. */
static int list_backups(char names[][64], int max_names)
{
    DIR *dir = opendir(BACKUP_DIR);
    if (!dir) return 0;
    BackupEntry entries[64];
    int n = 0;
    struct dirent *ent;
    while (n < 64 && (ent = readdir(dir)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 12) continue; /* "backup_X.tar.gz" minimo */
        if (strncmp(ent->d_name, "backup_", 7) != 0) continue;
        if (strcmp(ent->d_name + len - 7, ".tar.gz") != 0) continue;
        char path[288];
        snprintf(path, sizeof(path), BACKUP_DIR "/%s", ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        safe_copy(entries[n].name, ent->d_name, sizeof(entries[n].name));
        entries[n].mtime = st.st_mtime;
        n++;
    }
    closedir(dir);
    qsort(entries, n, sizeof(BackupEntry), backup_entry_cmp_desc);
    int out_n = (n < max_names) ? n : max_names;
    for (int i = 0; i < out_n; i++) {
        safe_copy(names[i], entries[i].name, 64);
    }
    return out_n;
}
/* Borra un backup por nombre (sin system(), sin riesgo de inyeccion). */
static void delete_backup(const char *filename)
{
    char path[288];
    snprintf(path, sizeof(path), BACKUP_DIR "/%s", filename);
    unlink(path);
}
#define AREXX_SCRIPTS_DIR "/usr/share/armiga/arexx_scripts"
#define AREXX_MAX_SCRIPTS 16
typedef struct {
    char filename[64];
    char desc[2][96]; /* [0]=ES, [1]=EN */
} ArexxScript;
/* Lista los .sh de AREXX_SCRIPTS_DIR (orden alfabetico via ls) y lee la
 * cabecera de cada uno buscando lineas "# DESC_ES:"/"# DESC_EN:" entre las
 * primeras 8 lineas del fichero. Si no encuentra cabecera, usa el nombre
 * de fichero como descripcion en ambos idiomas (fallback). */
static int arexx_filename_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}
/* Reemplaza popen("ls *.sh") por opendir/readdir + qsort alfabetico,
 * evitando el fork()+shell+ls por cada listado. Mismo orden que antes
 * (ls sin flags = alfabetico ascendente). */
static int list_arexx_scripts(ArexxScript *scripts, int max_scripts)
{
    DIR *dir = opendir(AREXX_SCRIPTS_DIR);
    if (!dir) return 0;
    char names[AREXX_MAX_SCRIPTS][64];
    int n = 0;
    struct dirent *ent;
    while (n < max_scripts && (ent = readdir(dir)) != NULL) {
        size_t len = strlen(ent->d_name);
        if (len < 4 || strcmp(ent->d_name + len - 3, ".sh") != 0) continue;
        safe_copy(names[n], ent->d_name, sizeof(names[n]));
        n++;
    }
    closedir(dir);
    qsort(names, n, sizeof(names[0]), arexx_filename_cmp);
    for (int i = 0; i < n; i++) {
        safe_copy(scripts[i].filename, names[i], sizeof(scripts[i].filename));
        safe_copy(scripts[i].desc[0], names[i], sizeof(scripts[i].desc[0]));
        safe_copy(scripts[i].desc[1], names[i], sizeof(scripts[i].desc[1]));
        char full_path[288];
        snprintf(full_path, sizeof(full_path), AREXX_SCRIPTS_DIR "/%s", names[i]);
        FILE *sf = fopen(full_path, "r");
        if (sf) {
            char hline[192];
            for (int j = 0; j < 8 && fgets(hline, sizeof(hline), sf); j++) {
                hline[strcspn(hline, "\r\n")] = 0;
                if (!strncmp(hline, "# DESC_ES:", 10)) {
                    const char *v = hline + 10;
                    while (*v == ' ') v++;
                    safe_copy(scripts[i].desc[0], v, sizeof(scripts[i].desc[0]));
                } else if (!strncmp(hline, "# DESC_EN:", 10)) {
                    const char *v = hline + 10;
                    while (*v == ' ') v++;
                    safe_copy(scripts[i].desc[1], v, sizeof(scripts[i].desc[1]));
                }
            }
            fclose(sf);
        }
    }
    return n;
}
/* Calcula el MD5 de un script ARexx (via popen a md5sum, mismo patron que
 * list_arexx_scripts). Escribe solo el hash hexadecimal en out_buf. */
static void compute_script_md5(const char *filename, char *out_buf, size_t out_sz)
{
    safe_copy(out_buf, "--", out_sz);
    char path[288];
    snprintf(path, sizeof(path), AREXX_SCRIPTS_DIR "/%s", filename);
    char cmd[320];
    snprintf(cmd, sizeof(cmd), "md5sum %s 2>/dev/null", path);
    FILE *p = popen(cmd, "r");
    if (!p) return;
    char line[128];
    if (fgets(line, sizeof(line), p)) {
        char *sp = strchr(line, ' ');
        if (sp) *sp = '\0';
        safe_copy(out_buf, line, out_sz);
    }
    pclose(p);
}
/* Ejecuta un script ARexx forzando +x antes, capturando su salida linea a
 * linea en un buffer de texto para mostrarla en STATE_AREXX_RUN. */
static pid_t s_arexx_pid = -1;
static int s_arexx_out_fd = -1;
/* Lanza un script ARexx en background (fork + pipe no bloqueante), forzando
 * +x antes. No bloquea el hilo principal: la salida se lee progresivamente
 * via poll_arexx_script() desde el bucle principal. */
static void start_arexx_script_async(const char *filename)
{
    char path[288];
    snprintf(path, sizeof(path), AREXX_SCRIPTS_DIR "/%s", filename);
    chmod(path, 0755);
    int pipefd[2];
    if (pipe(pipefd) != 0) { s_arexx_pid = -1; s_arexx_out_fd = -1; return; }
    s_arexx_pid = fork();
    if (s_arexx_pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        s_arexx_pid = -1; s_arexx_out_fd = -1;
        return;
    }
    if (s_arexx_pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl(path, path, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    s_arexx_out_fd = pipefd[0];
}
/* Sondea el script en curso: lee lo disponible del pipe sin bloquear y lo
 * anexa a out_buf. Devuelve: 0=en curso, 1=terminado. */
/* Crece *buf/*cap segun haga falta (sin techo artificial) y anexa chunk. */
static void arexx_output_append(char **buf, size_t *len, size_t *cap,
                                 const char *chunk, size_t n)
{
    size_t need = *len + n + 1;
    if (need > *cap) {
        size_t newcap = (*cap == 0) ? 4096 : *cap;
        while (newcap < need) newcap *= 2;
        char *nb = realloc(*buf, newcap);
        if (!nb) return; /* sin memoria: se descarta el resto de este chunk */
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *len, chunk, n);
    *len += n;
    (*buf)[*len] = '\0';
}
static int poll_arexx_script(char **buf, size_t *len, size_t *cap)
{
    if (s_arexx_out_fd >= 0) {
        char chunk[4096];
        ssize_t n;
        while ((n = read(s_arexx_out_fd, chunk, sizeof(chunk))) > 0)
            arexx_output_append(buf, len, cap, chunk, (size_t)n);
    }
    if (s_arexx_pid <= 0) return 1;
    int status = 0;
    pid_t r = waitpid(s_arexx_pid, &status, WNOHANG);
    if (r == 0) return 0; /* sigue en curso */
    /* Termino: drenar cualquier resto que quedara en el pipe */
    if (s_arexx_out_fd >= 0) {
        char chunk[4096];
        ssize_t n;
        while ((n = read(s_arexx_out_fd, chunk, sizeof(chunk))) > 0)
            arexx_output_append(buf, len, cap, chunk, (size_t)n);
        close(s_arexx_out_fd);
        s_arexx_out_fd = -1;
    }
    s_arexx_pid = -1;
    if (*len == 0)
        arexx_output_append(buf, len, cap, "(sin salida)\n", 13);
    return 1;
}
static void restore_backup(const char *filename)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "cd /media/amiga_data && tar xaf " BACKUP_DIR "/%s 2>/dev/null", filename);
    system(cmd);
}
static void read_release(char *kernel, char *mesa, char *retroarch, char *sdl3, char *puae_core,
                         char *build_date, char *version, char *build_number)
{
    safe_copy(kernel,     "?", 32);
    safe_copy(mesa,       "?", 32);
    safe_copy(retroarch,  "?", 32);
    safe_copy(sdl3,       "?", 32);
    if (puae_core) safe_copy(puae_core, "?", 32);
    if (build_date) safe_copy(build_date, "?", 24);
    if (version) safe_copy(version, "1.0", 32);
    if (build_number) safe_copy(build_number, "?", 16);
    FILE *f = fopen("/etc/armiga-release", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63s", key, val) == 2) {
            if (!strcmp(key, "KERNEL_VERSION"))    safe_copy(kernel,    val, 32);
            if (!strcmp(key, "MESA_VERSION"))      safe_copy(mesa,      val, 32);
            if (!strcmp(key, "RETROARCH_VERSION")) {
                /* %s de sscanf corta en el primer espacio; RETROARCH_VERSION
                 * puede llevar uno (ej. "1.22.2-nightly (34c069f)"), asi que
                 * se relee la linea entera tras el '=' en vez de usar val. */
                char *eq = strchr(line, '=');
                if (eq) {
                    char *full = eq + 1;
                    full[strcspn(full, "\r\n")] = '\0';
                    safe_copy(retroarch, full, 32);
                } else {
                    safe_copy(retroarch, val, 32);
                }
            }
            if (!strcmp(key, "SDL3_VERSION"))      safe_copy(sdl3,      val, 32);
            if (puae_core && !strcmp(key, "PUAE2021_CORE_VERSION")) safe_copy(puae_core, val, 32);
            if (build_date && !strcmp(key, "BUILD_DATE")) {
                /* %s de sscanf corta en el primer espacio; BUILD_DATE
                 * lleva uno entre fecha y hora ("28/08/2026 16:13"), asi
                 * que se relee la linea entera tras el '=' en vez de val. */
                char *eq = strchr(line, '=');
                if (eq) {
                    char *full = eq + 1;
                    full[strcspn(full, "\r\n")] = '\0';
                    safe_copy(build_date, full, 24);
                } else {
                    safe_copy(build_date, val, 24);
                }
            }
            if (version && !strcmp(key, "ARMIGA_VERSION")) safe_copy(version, val, 32);
            if (build_number && !strcmp(key, "BUILD_NUMBER")) safe_copy(build_number, val, 16);
        }
    }
    fclose(f);
}

/* Se incrementa cada vez que se crea un SDL_Renderer nuevo (tras
 * volver de RetroArch, el launcher reinicializa SDL/DRM entero).
 * Las cachés de texturas lo usan para invalidarse: comparar por
 * puntero de renderer no es fiable, la nueva instancia puede
 * reutilizar la misma direccion de memoria que la destruida. */
static int g_render_generation = 0;
/* Cache LRU de texturas de texto, indexada por (fuente, texto, color).
 * La inmensa mayoria de los textos dibujados por frame son constantes
 * (etiquetas de menu, botones, etc.) y antes se creaban/destruian como
 * textura GPU nueva en CADA frame via TTF_RenderText_Blended +
 * SDL_CreateTextureFromSurface -> SDL_DestroyTexture, decenas de veces
 * por frame. Con cache, se generan una sola vez y se reutilizan. */
#define TEXT_CACHE_SIZE 128
typedef struct {
    TTF_Font *font;
    char text[128];
    SDL_Color color;
    SDL_Texture *texture;
    int w, h;
    Uint64 last_used;
    int generation;
} CachedText;
static CachedText s_text_cache[TEXT_CACHE_SIZE];
static void draw_text(SDL_Renderer *r, TTF_Font *f, const char *t,
                      SDL_Color c, float x, float y)
{
    if (!t || !t[0]) return;
    if (strlen(t) >= sizeof(s_text_cache[0].text)) {
        SDL_Surface *s = TTF_RenderText_Blended(f, t, 0, c);
        if (!s) return;
        SDL_Texture *tx = SDL_CreateTextureFromSurface(r, s);
        SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(r, tx, NULL, &dst);
        SDL_DestroyTexture(tx);
        SDL_DestroySurface(s);
        return;
    }

    Uint64 now = SDL_GetTicks();
    int free_slot = -1;
    Uint64 oldest = SDL_MAX_UINT64;
    int oldest_slot = 0;

    for (int i = 0; i < TEXT_CACHE_SIZE; i++) {
        if (s_text_cache[i].texture && s_text_cache[i].generation == g_render_generation) {
            if (s_text_cache[i].font == f &&
                memcmp(&s_text_cache[i].color, &c, sizeof(SDL_Color)) == 0 &&
                strcmp(s_text_cache[i].text, t) == 0) {
                s_text_cache[i].last_used = now;
                SDL_FRect dst = {x, y, (float)s_text_cache[i].w, (float)s_text_cache[i].h};
                SDL_RenderTexture(r, s_text_cache[i].texture, NULL, &dst);
                return;
            }
            if (s_text_cache[i].last_used < oldest) {
                oldest = s_text_cache[i].last_used;
                oldest_slot = i;
            }
        } else {
            if (free_slot == -1) free_slot = i;
        }
    }

    int slot = (free_slot != -1) ? free_slot : oldest_slot;
    if (s_text_cache[slot].texture && s_text_cache[slot].generation == g_render_generation) {
        SDL_DestroyTexture(s_text_cache[slot].texture);
    }
    s_text_cache[slot].texture = NULL;

    SDL_Surface *s = TTF_RenderText_Blended(f, t, 0, c);
    if (!s) return;
    SDL_Texture *tx = SDL_CreateTextureFromSurface(r, s);
    if (tx) {
        s_text_cache[slot].font = f;
        strncpy(s_text_cache[slot].text, t, sizeof(s_text_cache[slot].text) - 1);
        s_text_cache[slot].text[sizeof(s_text_cache[slot].text) - 1] = '\0';
        s_text_cache[slot].color = c;
        s_text_cache[slot].texture = tx;
        s_text_cache[slot].w = s->w;
        s_text_cache[slot].h = s->h;
        s_text_cache[slot].last_used = now;
        s_text_cache[slot].generation = g_render_generation;

        SDL_FRect dst = {x, y, (float)s->w, (float)s->h};
        SDL_RenderTexture(r, tx, NULL, &dst);
    }
    SDL_DestroySurface(s);
}

/* Envuelve texto en varias lineas segun max_w, sin truncar nunca.
 * Devuelve el numero de lineas dibujadas (para calcular el Y siguiente). */
/* Mide el wrap de draw_text_wrapped sin dibujar: devuelve numero de lineas
 * y el ancho maximo real entre todas ellas (para ajustar pildoras al
 * contenido real, en vez de al ancho de la cadena completa sin envolver). */
static int measure_text_wrapped(TTF_Font *f, const char *text, float max_w, float *out_max_line_w)
{
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int line_count = 0;
    float max_line_w = 0.0f;
    char *saveptr = NULL;
    char *word = strtok_r(buf, " ", &saveptr);
    char line[256] = {0};
    while (word) {
        char candidate[256];
        if (line[0])
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);
        else
            snprintf(candidate, sizeof(candidate), "%s", word);
        int w = 0, h = 0;
        TTF_GetStringSize(f, candidate, 0, &w, &h);
        if ((float)w > max_w && line[0]) {
            int lw = 0, lh = 0;
            TTF_GetStringSize(f, line, 0, &lw, &lh);
            if ((float)lw > max_line_w) max_line_w = (float)lw;
            line_count++;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        word = strtok_r(NULL, " ", &saveptr);
    }
    if (line[0]) {
        int lw = 0, lh = 0;
        TTF_GetStringSize(f, line, 0, &lw, &lh);
        if ((float)lw > max_line_w) max_line_w = (float)lw;
        line_count++;
    }
    if (out_max_line_w) *out_max_line_w = max_line_w;
    return line_count;
}
static int draw_text_wrapped(SDL_Renderer *r, TTF_Font *f, const char *text,
                              SDL_Color c, float x, float y, float max_w, float line_h)
{
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int line_count = 0;
    char *saveptr = NULL;
    char *word = strtok_r(buf, " ", &saveptr);
    char line[256] = {0};
    while (word) {
        char candidate[256];
        if (line[0])
            snprintf(candidate, sizeof(candidate), "%s %s", line, word);
        else
            snprintf(candidate, sizeof(candidate), "%s", word);
        int w = 0, h = 0;
        TTF_GetStringSize(f, candidate, 0, &w, &h);
        if ((float)w > max_w && line[0]) {
            draw_text(r, f, line, c, x, y + (float)line_count * line_h);
            line_count++;
            snprintf(line, sizeof(line), "%s", word);
        } else {
            snprintf(line, sizeof(line), "%s", candidate);
        }
        word = strtok_r(NULL, " ", &saveptr);
    }
    if (line[0]) {
        draw_text(r, f, line, c, x, y + (float)line_count * line_h);
        line_count++;
    }
    return line_count;
}
/* Dibuja label + puntos animados ciclicos (.  ..  ...) segun ticks. */
static void draw_text_animdots(SDL_Renderer *r, TTF_Font *f, const char *label,
                                SDL_Color c, float x, float y, Uint64 ticks)
{
    char dots[4];
    int ndots = (int)((ticks / 400) % 4);
    memset(dots, '.', ndots);
    dots[ndots] = '\0';
    char buf[96];
    snprintf(buf, sizeof(buf), "%s%s", label, dots);
    draw_text(r, f, buf, c, x, y);
}
/* Dibuja texto truncando con "..." si excede max_w (breadcrumbs largos). */
static void draw_text_truncated(SDL_Renderer *r, TTF_Font *f, const char *t,
                                 SDL_Color c, float x, float y, float max_w)
{
    int w = 0, h = 0;
    TTF_GetStringSize(f, t, 0, &w, &h);
    if ((float)w <= max_w) {
        draw_text(r, f, t, c, x, y);
        return;
    }
    char buf[256];
    size_t len = strlen(t);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, t, len);
    buf[len] = '\0';
    while (len > 0) {
        len--;
        buf[len] = '\0';
        char tmp[260];
        snprintf(tmp, sizeof(tmp), "%s...", buf);
        TTF_GetStringSize(f, tmp, 0, &w, &h);
        if ((float)w <= max_w) {
            draw_text(r, f, tmp, c, x, y);
            return;
        }
    }
    draw_text(r, f, "...", c, x, y);
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

/* Dibuja footer unificado: leyenda izquierda + version derecha */
static SDL_Texture *g_perf_icons[3] = {NULL, NULL, NULL};

#define BT_MAX_DEVICES 16
typedef struct { char mac[18]; char name[64]; int rssi; bool has_rssi; } BTDevice;

/* Parsea /tmp/armiga-bt-devices.txt (formato "MAC|Nombre|RSSI" por linea,
 * generado por armiga-bt-scan; RSSI puede venir vacio) hacia el array. */
static int bt_parse_devices(BTDevice *devices, int max_count)
{
    FILE *f = fopen("/tmp/armiga-bt-devices.txt", "r");
    if (!f) return 0;
    char line[128];
    int count = 0;
    while (count < max_count && fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        char *sep1 = strchr(line, '|');
        if (!sep1) continue;
        *sep1 = 0;
        char *name = sep1 + 1;
        char *sep2 = strchr(name, '|');
        char *rssi_str = NULL;
        if (sep2) {
            *sep2 = 0;
            rssi_str = sep2 + 1;
        }
        safe_copy(devices[count].mac, line, sizeof(devices[count].mac));
        safe_copy(devices[count].name, name, sizeof(devices[count].name));
        if (rssi_str && rssi_str[0]) {
            devices[count].rssi = atoi(rssi_str);
            devices[count].has_rssi = true;
        } else {
            devices[count].rssi = 0;
            devices[count].has_rssi = false;
        }
        count++;
    }
    fclose(f);
    return count;
}

#define DEVMODE_TEMP_HISTORY_LEN 60
static int g_devmode_temp_history[DEVMODE_TEMP_HISTORY_LEN];
static int g_devmode_temp_history_count = 0;

static void devmode_push_temp(int temp_c)
{
    if (g_devmode_temp_history_count < DEVMODE_TEMP_HISTORY_LEN) {
        g_devmode_temp_history[g_devmode_temp_history_count++] = temp_c;
    } else {
        memmove(g_devmode_temp_history, g_devmode_temp_history + 1,
                (DEVMODE_TEMP_HISTORY_LEN - 1) * sizeof(int));
        g_devmode_temp_history[DEVMODE_TEMP_HISTORY_LEN - 1] = temp_c;
    }
}

static void draw_footer(SDL_Renderer *ren, TTF_Font *f,
                        const char *legend, const char *version)
{
    SDL_Color c_gray    = COL_CREAM;
    SDL_Color c_dkgreen = COL_CREAM;
    SDL_Color c_gold    = {27, 39, 8, 255};
    SDL_Color c_lime    = {183, 221, 91, 255};
    draw_text(ren, f, legend, c_gray, 20.0f, 448.0f);
    draw_text_right(ren, f, version, c_lime, SCREEN_W - 20.0f, 448.0f);

    int active_profile = g_cfg.perf_profile;
    SDL_Texture *active_icon = (active_profile >= 0 && active_profile < 3) ? g_perf_icons[active_profile] : NULL;
    if (active_icon) {
        int ver_w = 0, ver_h = 0;
        TTF_GetStringSize(f, version, 0, &ver_w, &ver_h);
        float icon_size = 24.0f;
        float icon_x = SCREEN_W - 20.0f - (float)ver_w - 10.0f - icon_size;
        float icon_y = 448.0f + ((float)ver_h - icon_size) / 2.0f + 2.0f;
        SDL_SetTextureColorMod(active_icon, c_dkgreen.r, c_dkgreen.g, c_dkgreen.b);
        SDL_FRect icon_dst = {icon_x, icon_y, icon_size, icon_size};
        SDL_RenderTexture(ren, active_icon, NULL, &icon_dst);
    }
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

/* Delta contra el snapshot de la llamada anterior (esta funcion se llama
 * cada 5s desde STATE_SYSINFO, asi que ya hay margen de sobra entre
 * muestras). Antes bloqueaba el hilo principal 80ms con SDL_Delay para
 * tomar dos snapshots separados en la misma llamada -> 5-10 frames
 * perdidos de golpe cada vez. Sin delay: primera llamada tras arrancar
 * devuelve 0% (no hay snapshot previo), se corrige solo en la siguiente. */
static void read_cpu_usage(char *buf, size_t bufsize, int *pct_out)
{
    static long prev_total = 0;
    static long prev_idle = 0;
    long idle = 0;
    long total = read_proc_stat_cpu(&idle);
    int pct = 0;
    if (prev_total > 0 && total > prev_total) {
        long dtotal = total - prev_total;
        long didle  = idle  - prev_idle;
        if (dtotal > 0)
            pct = (int)(100L * (dtotal - didle) / dtotal);
    }
    prev_total = total;
    prev_idle = idle;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    if (pct_out) *pct_out = pct;
    snprintf(buf, bufsize, "%d%%", pct);
}

static void read_loadavg(char *buf, size_t bufsize)
{
    safe_copy(buf, "--", bufsize);
    FILE *f = fopen("/proc/loadavg", "r");
    if (!f) return;
    float l1, l5, l15;
    if (fscanf(f, "%f %f %f", &l1, &l5, &l15) == 3)
        snprintf(buf, bufsize, "%.2f  %.2f  %.2f", l1, l5, l15);
    fclose(f);
}

static void read_wifi_signal(char *buf, size_t bufsize, int *pct_out)
{
    safe_copy(buf, "--", bufsize);
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
    safe_copy(buf, "--", bufsize);
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

/* Rectangulo relleno con esquinas redondeadas, via barrido por filas:
 * en las franjas superior/inferior de altura 'radius', cada fila recorta
 * su ancho segun la ecuacion del circulo (esquina), el resto de filas se
 * dibujan a ancho completo. SDL3 no tiene primitiva nativa para esto. */
/* Signed distance de un punto (relativo al centro del rect w x h) a una
 * capsula: rectangulo interior (w-2r) x (h-2r) centrado en el origen,
 * expandido por radius en todas direcciones. Negativo = dentro, positivo =
 * fuera, 0 = justo en el borde. Esto define la capsula como una unica
 * forma continua (sin costura entre rectangulo y semicirculos). */
static float capsule_sdf(float px, float py, float half_w, float half_h, float radius)
{
    float qx = SDL_fabsf(px) - (half_w - radius);
    float qy = SDL_fabsf(py) - (half_h - radius);
    float ax = qx > 0.0f ? qx : 0.0f;
    float ay = qy > 0.0f ? qy : 0.0f;
    float outside = SDL_sqrtf(ax * ax + ay * ay);
    float inside = (qx > qy ? qx : qy);
    if (inside > 0.0f) inside = 0.0f;
    return outside + inside - radius;
}
/* Cobertura de un pixel de 1x1 centrado en (px,py) respecto a la capsula,
 * via supersampling 4x4 (16 submuestras) sobre el SDF. */
static float capsule_coverage(float px, float py, float half_w, float half_h, float radius)
{
    const int SS = 4;
    int inside = 0;
    for (int sy = 0; sy < SS; sy++) {
        float oy = py - 0.5f + ((float)sy + 0.5f) / (float)SS;
        for (int sx = 0; sx < SS; sx++) {
            float ox = px - 0.5f + ((float)sx + 0.5f) / (float)SS;
            if (capsule_sdf(ox, oy, half_w, half_h, radius) <= 0.0f) inside++;
        }
    }
    return (float)inside / (float)(SS * SS);
}
/* Cobertura de un pixel de 1x1 centrado en (px,py) respecto al exterior de
 * un cuarto de circulo de radio 'radius' centrado en (cx,cy), via
 * supersampling 4x4. Devuelve 1.0 si el pixel esta totalmente fuera del
 * circulo (debe pintarse), 0.0 si esta totalmente dentro (no pintar). */
static float corner_mask_coverage(float px, float py, float cx, float cy, float radius)
{
    /* SS alto (antes 4): ahora esta funcion solo se llama al generar la
     * mascara cacheada (unas pocas veces en toda la ejecucion, una por
     * radio distinto), nunca por frame, asi que el coste extra es
     * insignificante y mejora mucho el antialiasing del borde. */
    const int SS = 16;
    int outside = 0;
    for (int sy = 0; sy < SS; sy++) {
        float oy = py - 0.5f + ((float)sy + 0.5f) / (float)SS;
        for (int sx = 0; sx < SS; sx++) {
            float ox = px - 0.5f + ((float)sx + 0.5f) / (float)SS;
            float dx = ox - cx;
            float dy = oy - cy;
            if ((dx * dx + dy * dy) > (radius * radius)) outside++;
        }
    }
    return (float)outside / (float)(SS * SS);
}
/* Dibuja mascaras negras en las 4 esquinas de la pantalla, simulando
 * esquinas redondeadas fisicas del panel. Se llama al final de cada frame,
 * justo antes de SDL_RenderPresent, para que quede por encima de todo. */
/* Mascara de una esquina redondeada (orientacion top-left) cacheada en
 * textura la primera vez que se llama; las otras 3 esquinas se pintan
 * volteando (flip) esa misma textura. Antes se recalculaba pixel a pixel
 * con 4x4 supersampling en CADA frame, incondicionalmente, para las 4
 * esquinas: era el mayor cuello de botella real de framerate del launcher. */
static void draw_screen_corners(SDL_Renderer *r, int screen_w, int screen_h, float radius)
{
    static SDL_Texture *corner_tex = NULL;
    static int cached_rad_i = -1;
    static int cache_generation = -1;
    int rad_i = (int)SDL_ceilf(radius) + 1;

    if (cache_generation != g_render_generation) {
        corner_tex = NULL;
        cached_rad_i = -1;
        cache_generation = g_render_generation;
    }

    if (!corner_tex || cached_rad_i != rad_i) {
        if (corner_tex) SDL_DestroyTexture(corner_tex);
        corner_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                        SDL_TEXTUREACCESS_TARGET, rad_i, rad_i);
        SDL_SetTextureBlendMode(corner_tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(corner_tex, SDL_SCALEMODE_LINEAR);
        SDL_Texture *prev_target = SDL_GetRenderTarget(r);
        SDL_SetRenderTarget(r, corner_tex);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        for (int row = 0; row < rad_i; row++) {
            float py = (float)row + 0.5f;
            for (int col = 0; col < rad_i; col++) {
                float px = (float)col + 0.5f;
                float cov = corner_mask_coverage(px, py, radius, radius, radius);
                Uint8 a = (Uint8)(cov * 255.0f);
                SDL_SetRenderDrawColor(r, 0, 0, 0, a);
                SDL_FRect px_rect = {(float)col, (float)row, 1.0f, 1.0f};
                SDL_RenderFillRect(r, &px_rect);
            }
        }
        SDL_SetRenderTarget(r, prev_target);
        cached_rad_i = rad_i;
    }

    SDL_FRect dst;
    dst = (SDL_FRect){0.0f, 0.0f, (float)rad_i, (float)rad_i};
    SDL_RenderTextureRotated(r, corner_tex, NULL, &dst, 0.0, NULL, SDL_FLIP_NONE);
    dst = (SDL_FRect){(float)screen_w - (float)rad_i, 0.0f, (float)rad_i, (float)rad_i};
    SDL_RenderTextureRotated(r, corner_tex, NULL, &dst, 0.0, NULL, SDL_FLIP_HORIZONTAL);
    dst = (SDL_FRect){0.0f, (float)screen_h - (float)rad_i, (float)rad_i, (float)rad_i};
    SDL_RenderTextureRotated(r, corner_tex, NULL, &dst, 0.0, NULL, SDL_FLIP_VERTICAL);
    dst = (SDL_FRect){(float)screen_w - (float)rad_i, (float)screen_h - (float)rad_i, (float)rad_i, (float)rad_i};
    SDL_RenderTextureRotated(r, corner_tex, NULL, &dst, 0.0, NULL, (SDL_FlipMode)(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
}
/* Mascara de un cuarto de circulo (radio dado) cacheada en textura alfa
 * (blanco puro, alpha = cobertura interior), reutilizable para CUALQUIER
 * rectangulo redondeado de ese radio via SDL_SetTextureColorMod/AlphaMod +
 * flip en las 4 esquinas. Antes cada pildora recalculaba sus esquinas
 * pixel a pixel (SDF + supersampling 4x4) en CADA frame: con la barra de
 * estado + seleccion de menu dibujando varias pildoras por frame, era el
 * mayor cuello de botella real de framerate del launcher tras el fix de
 * draw_screen_corners. */
static SDL_Texture *get_pill_corner_mask(SDL_Renderer *r, float radius)
{
    static struct { int rad_i; SDL_Texture *tex; } cache[16];
    static int cache_count = 0;
    static int cache_generation = -1;
    int rad_i = (int)SDL_ceilf(radius) + 1;

    if (cache_generation != g_render_generation) {
        cache_count = 0;
        cache_generation = g_render_generation;
    }

    for (int i = 0; i < cache_count; i++)
        if (cache[i].rad_i == rad_i) return cache[i].tex;

    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET, rad_i, rad_i);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *prev_target = SDL_GetRenderTarget(r);
    SDL_SetRenderTarget(r, tex);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
    SDL_RenderClear(r);
    for (int row = 0; row < rad_i; row++) {
        float py = (float)row + 0.5f;
        for (int col = 0; col < rad_i; col++) {
            float px = (float)col + 0.5f;
            float cov = 1.0f - corner_mask_coverage(px, py, radius, radius, radius);
            Uint8 a = (Uint8)(cov * 255.0f);
            SDL_SetRenderDrawColor(r, 255, 255, 255, a);
            SDL_FRect px_rect = {(float)col, (float)row, 1.0f, 1.0f};
            SDL_RenderFillRect(r, &px_rect);
        }
    }
    SDL_SetRenderTarget(r, prev_target);

    if (cache_count < 16) {
        cache[cache_count].rad_i = rad_i;
        cache[cache_count].tex = tex;
        cache_count++;
    }
    return tex;
}
/* Anade un quad (2 triangulos, 4 vertices, 6 indices) al buffer de geometria.
 * uv0=(u0,v0) esquina superior-izquierda del quad, uv1=(u1,v1) esquina inferior-derecha,
 * en el espacio de textura de la mascara de esquina (0,0)-(1,1) sin flip. */
static void geom_add_quad(SDL_Vertex *verts, int *nv, int *inds, int *ni,
                           float x0, float y0, float x1, float y1,
                           float u0, float v0, float u1, float v1,
                           SDL_FColor col)
{
    int base = *nv;
    verts[base + 0] = (SDL_Vertex){ {x0, y0}, col, {u0, v0} };
    verts[base + 1] = (SDL_Vertex){ {x1, y0}, col, {u1, v0} };
    verts[base + 2] = (SDL_Vertex){ {x1, y1}, col, {u1, v1} };
    verts[base + 3] = (SDL_Vertex){ {x0, y1}, col, {u0, v1} };
    inds[*ni + 0] = base + 0; inds[*ni + 1] = base + 1; inds[*ni + 2] = base + 2;
    inds[*ni + 3] = base + 0; inds[*ni + 4] = base + 2; inds[*ni + 5] = base + 3;
    *nv += 4;
    *ni += 6;
}
/* Version de draw_rounded_rect_filled con 1 sola llamada a SDL_RenderGeometry
 * en vez de 9 draw calls (5x SDL_RenderFillRect + 4x SDL_RenderTextureRotated).
 * Reutiliza la misma textura de mascara de esquina cacheada por radio
 * (get_pill_corner_mask): las 4 esquinas muestrean su UV real (con flip
 * horizontal/vertical segun corresponda, igual que antes con SDL_FLIP_*)
 * para conservar el antialiasing por supersampling; el centro y las 4 tiras
 * rectas muestrean un unico texel garantizado 100% opaco de esa misma
 * mascara (la esquina interior, mas alejada del arco), asi todo el poligono
 * usa la misma textura sin perder el relleno solido. */
static void draw_rounded_rect_filled(SDL_Renderer *r, float x, float y,
                                      float w, float h, float radius,
                                      SDL_Color c)
{
    if (radius <= 0.0f || radius * 2.0f > h || radius * 2.0f > w) {
        draw_rect_filled(r, x, y, w, h, c);
        return;
    }
    int rad_i = (int)SDL_ceilf(radius) + 1;
    float rf = (float)rad_i;
    SDL_Texture *mask = get_pill_corner_mask(r, radius);

    SDL_FColor col = { c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f };
    const float UV_OPAQUE_U = 1.0f, UV_OPAQUE_V = 1.0f; /* texel interior, 100% opaco */

    SDL_Vertex verts[36];
    int inds[54];
    int nv = 0, ni = 0;

    /* Esquina TL: uv normal (0,0)-(1,1) */
    geom_add_quad(verts, &nv, inds, &ni, x, y, x + rf, y + rf, 0.0f, 0.0f, 1.0f, 1.0f, col);
    /* Esquina TR: flip horizontal (u invertido) */
    geom_add_quad(verts, &nv, inds, &ni, x + w - rf, y, x + w, y + rf, 1.0f, 0.0f, 0.0f, 1.0f, col);
    /* Esquina BL: flip vertical (v invertido) */
    geom_add_quad(verts, &nv, inds, &ni, x, y + h - rf, x + rf, y + h, 0.0f, 1.0f, 1.0f, 0.0f, col);
    /* Esquina BR: flip ambos */
    geom_add_quad(verts, &nv, inds, &ni, x + w - rf, y + h - rf, x + w, y + h, 1.0f, 1.0f, 0.0f, 0.0f, col);

    /* Centro + 4 tiras rectas, todas con UV constante (texel opaco) */
    if (w - 2.0f * rf > 0.0f && h - 2.0f * rf > 0.0f)
        geom_add_quad(verts, &nv, inds, &ni, x + rf, y + rf, x + w - rf, y + h - rf,
                      UV_OPAQUE_U, UV_OPAQUE_V, UV_OPAQUE_U, UV_OPAQUE_V, col);
    if (w - 2.0f * rf > 0.0f)
        geom_add_quad(verts, &nv, inds, &ni, x + rf, y, x + w - rf, y + rf,
                      UV_OPAQUE_U, UV_OPAQUE_V, UV_OPAQUE_U, UV_OPAQUE_V, col);
    if (w - 2.0f * rf > 0.0f)
        geom_add_quad(verts, &nv, inds, &ni, x + rf, y + h - rf, x + w - rf, y + h,
                      UV_OPAQUE_U, UV_OPAQUE_V, UV_OPAQUE_U, UV_OPAQUE_V, col);
    if (h - 2.0f * rf > 0.0f)
        geom_add_quad(verts, &nv, inds, &ni, x, y + rf, x + rf, y + h - rf,
                      UV_OPAQUE_U, UV_OPAQUE_V, UV_OPAQUE_U, UV_OPAQUE_V, col);
    if (h - 2.0f * rf > 0.0f)
        geom_add_quad(verts, &nv, inds, &ni, x + w - rf, y + rf, x + w, y + h - rf,
                      UV_OPAQUE_U, UV_OPAQUE_V, UV_OPAQUE_U, UV_OPAQUE_V, col);

    SDL_BlendMode prev_blend;
    SDL_GetTextureBlendMode(mask, &prev_blend);
    SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r, mask, verts, nv, inds, ni);
    SDL_SetTextureBlendMode(mask, prev_blend);
}
/* Barra de progreso con esquinas redondeadas: fondo + relleno proporcional,
 * ambos con radio = altura/2. Sustituye al patron anterior de dos
 * draw_rect_filled (fondo+relleno planos) usado en Brillo/LEDs/Dimming. */
static void draw_bar_rounded(SDL_Renderer *r, float x, float y, float w, float h,
                              float frac, SDL_Color c_bg, SDL_Color c_fill)
{
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float radius = h / 2.0f;
    draw_rounded_rect_filled(r, x, y, w, h, radius, c_bg);
    float fill_w = w * frac;
    if (fill_w > 0.0f && fill_w < h) fill_w = h; /* asegura capsula completa, nunca un fragmento ambiguo */
    if (fill_w > w) fill_w = w;
    if (fill_w > 0.0f)
        draw_rounded_rect_filled(r, x, y, fill_w, h, radius, c_fill);
}
static float draw_status_pill(SDL_Renderer *ren, TTF_Font *f, float right_edge, float y_center,
                               SDL_Texture *icon, const char *label, SDL_Color fg, SDL_Color bg,
                               float icon_size_override)
{
    int w = 0, h = 0;
    TTF_GetStringSize(f, label, 0, &w, &h);
    bool icon_only = (icon && w <= 10);
    float icon_w = icon ? (icon_size_override > 0.0f ? icon_size_override : 20.0f) : 0.0f;
    float icon_gap = (icon && !icon_only) ? 6.0f : 0.0f;
    float pad_x = icon_only ? 10.0f : 14.0f;
    float pill_h = (float)h + 14.0f;
    float pill_w = icon_only ? (pad_x * 2.0f + icon_w) : (pad_x * 2.0f + icon_w + icon_gap + (float)w);
    float pill_x = right_edge - pill_w;
    float pill_y = y_center - pill_h / 2.0f;
    draw_rounded_rect_filled(ren, pill_x, pill_y, pill_w, pill_h, pill_h / 2.0f, bg);
    float cursor_x = pill_x + pad_x;
    if (icon) {
        SDL_SetTextureColorMod(icon, fg.r, fg.g, fg.b);
        SDL_FRect icon_dst = {cursor_x, y_center - icon_w / 2.0f, icon_w, icon_w};
        SDL_RenderTexture(ren, icon, NULL, &icon_dst);
        cursor_x += icon_w + icon_gap;
    }
    if (!icon_only)
        draw_text(ren, f, label, fg, cursor_x, y_center - (float)h / 2.0f);
    return pill_w;
}
static float draw_statusbar(SDL_Renderer *ren, TTF_Font *f, TTF_Font *f_ampm,
                            const char *time_str, bool wifi_up, int battery, bool bt_up,
                            SDL_Texture *wifi_icon_tex, SDL_Texture *battery_icon_tex,
                            SDL_Texture *bt_icon_tex)
{
    SDL_Color c_cream    = {231, 239, 231, 255};
    SDL_Color c_gold     = {27, 39, 8, 255};
    SDL_Color c_red      = COL_RED;
    SDL_Color c_pill_on  = {183, 221, 91, 255};
    SDL_Color c_pill_off = {28, 52, 40, 255};
    SDL_Color c_dim_fg   = {70, 90, 80, 255};
    float right = SCREEN_W - 20.0f;
    float y     = 25.0f;
    float gap   = 9.0f;

    char batt_buf[12];
    SDL_Color batt_fg = c_gold;
    if (battery >= 0) {
        if (s_status_charging) {
            snprintf(batt_buf, sizeof(batt_buf), "%d%% +", battery);
            batt_fg = c_gold;
        } else {
            snprintf(batt_buf, sizeof(batt_buf), "%d%%", battery);
            if (battery <= 20) batt_fg = c_red;
            else                batt_fg = c_gold;
        }
    } else {
        strncpy(batt_buf, "--", sizeof(batt_buf));
    }
    right -= draw_status_pill(ren, f, right, y, battery_icon_tex, batt_buf, batt_fg, c_pill_on, 24.0f);
    right -= gap;

    SDL_Color bt_fg = bt_up ? c_gold : c_dim_fg;
    right -= draw_status_pill(ren, f, right, y, bt_icon_tex, " ", bt_fg, bt_up ? c_pill_on : c_pill_off, 0.0f);
    right -= gap;

    SDL_Color wifi_fg = wifi_up ? c_gold : c_dim_fg;
    right -= draw_status_pill(ren, f, right, y, wifi_icon_tex, " ", wifi_fg, wifi_up ? c_pill_on : c_pill_off, 0.0f);
    right -= gap;

    {
        int hh = 0, mm = 0;
        sscanf(time_str, "%d:%d", &hh, &mm);
        const char *ampm = (hh < 12) ? "AM" : "PM";
        bool colon_visible = (SDL_GetTicks() / 500) % 2 == 0;
        char time_display[16];
        snprintf(time_display, sizeof(time_display), "%02d%s%02d", hh, colon_visible ? ":" : " ", mm);
        int tw = 0, th = 0, aw = 0, ah = 0;
        TTF_GetStringSize(f, time_str, 0, &tw, &th);
        TTF_GetStringSize(f_ampm, ampm, 0, &aw, &ah);
        float pad_x = 14.0f;
        float ampm_gap = 4.0f;
        float pill_h = (float)th + 14.0f;
        float pill_w = pad_x * 2.0f + (float)tw + ampm_gap + (float)aw;
        float pill_x = right - pill_w;
        float pill_y = y - pill_h / 2.0f;
        draw_rounded_rect_filled(ren, pill_x, pill_y, pill_w, pill_h, pill_h / 2.0f, c_pill_off);
        draw_text(ren, f, time_display, c_cream, pill_x + pad_x, y - (float)th / 2.0f);
        draw_text(ren, f_ampm, ampm, c_cream, pill_x + pad_x + (float)tw + ampm_gap, y - (float)ah / 2.0f);
        right -= pill_w;
    }
    right -= gap;

    int ssh_on = g_cfg.ssh_enabled;
    right -= draw_status_pill(ren, f, right, y, NULL, "SSH", ssh_on ? c_gold : c_dim_fg, ssh_on ? c_pill_on : c_pill_off, 0.0f);
    return right;
}
/* Circulo relleno via barrido por filas (mismo principio que
 * draw_rounded_rect_filled, con radius aplicado a las 4 "esquinas"). */
static void draw_circle_filled(SDL_Renderer *r, float cx, float cy,
                                float radius, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int rows = (int)(radius * 2.0f);
    for (int row = 0; row < rows; row++) {
        float dy = (float)row - radius + 0.5f;
        float dx2 = radius * radius - dy * dy;
        if (dx2 <= 0.0f) continue;
        float half_w = SDL_sqrtf(dx2);
        SDL_FRect line_rect = {cx - half_w, cy - radius + (float)row, half_w * 2.0f, 1.0f};
        SDL_RenderFillRect(r, &line_rect);
    }
}

/* Simula un rectangulo redondeado "solo borde": dibuja el rect exterior
 * del color de borde, luego un rect interior mas pequeno del color de
 * fondo, dejando visible solo un marco de "thickness" px. */
static void draw_rounded_rect_outline(SDL_Renderer *r, float x, float y,
                                       float w, float h, float radius,
                                       float thickness, SDL_Color c, SDL_Color bg)
{
    draw_rounded_rect_filled(r, x, y, w, h, radius, c);
    float inner_r = radius - thickness;
    if (inner_r < 0.0f) inner_r = 0.0f;
    draw_rounded_rect_filled(r, x + thickness, y + thickness,
                              w - thickness * 2.0f, h - thickness * 2.0f,
                              inner_r, bg);
}
/* Sistema de navegacion "Active Dash Pill": dots atenuados para niveles
 * previos + capsula alargada verde-lima para el nivel activo + titulo de
 * la seccion actual. Sustituye al breadcrumb textual "Menu > X > Y". */
static void draw_active_dash_breadcrumbs(SDL_Renderer *ren, TTF_Font *font,
                                         float x, float y_center,
                                         int depth, const char *title)
{
    if (depth <= 0) return;

    const SDL_Color c_prev_dot    = { 28,  52,  40, 255};
    const SDL_Color c_active_dash = {183, 221,  91, 255};
    const SDL_Color c_text        = {231, 239, 231, 255};

    float cur_x = x;

    const float r_dot = 2.5f;
    const float dot_gap = 6.0f;
    for (int i = 1; i < depth; i++) {
        draw_circle_filled(ren, cur_x + r_dot, y_center, r_dot, c_prev_dot);
        cur_x += (r_dot * 2.0f) + dot_gap;
    }

    const float dash_w = 16.0f;
    const float dash_h = 7.0f;
    const float dash_r = dash_h / 2.0f;
    float dash_y = y_center - (dash_h / 2.0f);
    draw_rounded_rect_filled(ren, cur_x, dash_y, dash_w, dash_h, dash_r, c_active_dash);
    cur_x += dash_w + 10.0f;

    if (title && title[0] && font) {
        int tw = 0, th = 0;
        TTF_GetStringSize(font, title, 0, &tw, &th);
        draw_text(ren, font, title, c_text, cur_x, y_center - ((float)th / 2.0f));
    }
}
static void draw_line(SDL_Renderer *r, float x1, float y1,
                      float x2, float y2, SDL_Color c)
{
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderLine(r, x1, y1, x2, y2);
}
/* Panel de contexto derecho: lineas "label: valor" extensibles.
 * Anadir mas entradas simplemente incrementando n_lines en la llamada. */
static void draw_context_panel(SDL_Renderer *ren, TTF_Font *f, float x, float y,
                                const char lines[][64], int n_lines, SDL_Color c)
{
    for (int i = 0; i < n_lines; i++) {
        draw_text(ren, f, lines[i], c, x, y + (float)i * 16.0f);
    }
}
/* Calcula HH:MM actual en la zona horaria IANA indicada, sin tocar el TZ
 * persistente del sistema (solo afecta a este proceso, restaurado despues). */
static void get_time_in_tz(const char *tz_name, char *buf, size_t bufsize)
{
    char *old_tz = getenv("TZ");
    char old_tz_copy[128] = {0};
    if (old_tz) safe_copy(old_tz_copy, old_tz, sizeof(old_tz_copy));
    setenv("TZ", tz_name, 1);
    tzset();
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) strftime(buf, bufsize, "%H:%M", lt);
    else    safe_copy(buf, "--:--", bufsize);
    if (old_tz_copy[0]) setenv("TZ", old_tz_copy, 1);
    else                unsetenv("TZ");
    tzset();
}

/* =========================================================================
 * Audio tactil acustico retro (sintetizado en RAM, sin ficheros externos)
 * ========================================================================= */
#define CLICK_SAMPLE_RATE 44100
#define CLICK_DURATION_MS 18
#define CLICK_SAMPLES ((CLICK_SAMPLE_RATE * CLICK_DURATION_MS) / 1000)

static Sint16 s_click_buffer[CLICK_SAMPLES];
static SDL_AudioStream *s_audio_stream = NULL;

static void audio_click_init(void)
{
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        float t = (float)i / (float)CLICK_SAMPLE_RATE;
        float progress = (float)i / (float)CLICK_SAMPLES;
        float freq = 420.0f - (300.0f * progress);
        float phase = t * freq;
        float tri = 2.0f * SDL_fabsf(2.0f * (phase - SDL_floorf(phase + 0.5f))) - 1.0f;
        float env = 1.0f - progress;
        env = env * env;
        s_click_buffer[i] = (Sint16)(tri * env * 3000.0f);
    }

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16LE;
    spec.channels = 1;
    spec.freq = CLICK_SAMPLE_RATE;

    s_audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (s_audio_stream) {
        SDL_ResumeAudioStreamDevice(s_audio_stream);
    }
}

static bool s_click_sound_enabled = true;
static void play_ui_click(void)
{
    if (s_audio_stream && s_click_sound_enabled) {
        SDL_PutAudioStreamData(s_audio_stream, s_click_buffer, sizeof(s_click_buffer));
    }
}
/* Lee CLICK_SOUND_ENABLED de armiga.cfg. Default: activado (1). */
static int read_click_sound_enabled(void)
{
    int enabled = 1;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "CLICK_SOUND_ENABLED")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}
static void save_click_sound_enabled(int enabled)
{
    config_set_kv("CLICK_SOUND_ENABLED", enabled ? "1" : "0");
}

int main(void)
{
    for (;;) {
    bool relaunch_after_retroarch = false;
    config_load();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    audio_click_init();
    if (!TTF_Init()) {
        fprintf(stderr, "TTF_Init: %s\n", SDL_GetError());
        SDL_Quit(); return 1;
    }

    apply_timezone();

    SDL_Window *win = SDL_CreateWindow("armiga",
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!win) { TTF_Quit(); SDL_Quit(); return 1; }
    apply_refresh_120hz(win, read_refresh_120hz());

    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return 1; }
    SDL_SetRenderVSync(ren, 1);
    g_render_generation++;

    TTF_Font *f_med   = TTF_OpenFont(FONT_PATH, FONT_MED);
    TTF_Font *f_sm    = TTF_OpenFont(FONT_PATH, FONT_SM);
    TTF_Font *f_lg    = TTF_OpenFont(FONT_PATH, FONT_LG);
    TTF_Font *f_xs    = TTF_OpenFont(FONT_PATH, FONT_XS);
    TTF_Font *f_xsm   = TTF_OpenFont(FONT_PATH, FONT_XSM);
    if (!f_med || !f_sm || !f_lg || !f_xs || !f_xsm) {
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
    /* Iconos monocromos del menu principal (PNG blanco+alfa, recoloreados
     * en tiempo real via SDL_SetTextureColorMod segun seleccion). */
    #define MENU_ICON_COUNT 7
    static const char *MENU_ICON_PATHS[MENU_ICON_COUNT] = {
        "/usr/share/armiga/icons/device-gamepad-2.png",
        "/usr/share/armiga/icons/cloud-download.png",
        "/usr/share/armiga/icons/activity.png",
        "/usr/share/armiga/icons/terminal.png",
        "/usr/share/armiga/icons/settings.png",
        "/usr/share/armiga/icons/power.png",
        "/usr/share/armiga/icons/reboot.png",
    };
    SDL_Texture *menu_icon_tex[MENU_ICON_COUNT] = {0};
    for (int mi = 0; mi < MENU_ICON_COUNT; mi++) {
        menu_icon_tex[mi] = IMG_LoadTexture(ren, MENU_ICON_PATHS[mi]);
        if (menu_icon_tex[mi]) SDL_SetTextureScaleMode(menu_icon_tex[mi], SDL_SCALEMODE_LINEAR);
    }
    /* Iconos de la barra de estado (wifi, bateria) */
    SDL_Texture *wifi_icon_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/wifi.png");
    if (wifi_icon_tex) SDL_SetTextureScaleMode(wifi_icon_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *bt_icon_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/bluetooth.png");
    if (bt_icon_tex) SDL_SetTextureScaleMode(bt_icon_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *battery_icon_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/battery-3.png");
    if (battery_icon_tex) SDL_SetTextureScaleMode(battery_icon_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *perf_bolt_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/perf-bolt.png");
    if (perf_bolt_tex) SDL_SetTextureScaleMode(perf_bolt_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *perf_scale_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/perf-scale.png");
    if (perf_scale_tex) SDL_SetTextureScaleMode(perf_scale_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *perf_battery_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/perf-battery.png");
    g_perf_icons[0] = perf_bolt_tex;
    g_perf_icons[1] = perf_scale_tex;
    g_perf_icons[2] = perf_battery_tex;
    if (perf_battery_tex) SDL_SetTextureScaleMode(perf_battery_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *arexx_icon_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/script.png");
    if (arexx_icon_tex) SDL_SetTextureScaleMode(arexx_icon_tex, SDL_SCALEMODE_LINEAR);
    SDL_Texture *update_icon_tex = IMG_LoadTexture(ren, "/usr/share/armiga/icons/arrow-big-up-lines.png");
    if (update_icon_tex) SDL_SetTextureScaleMode(update_icon_tex, SDL_SCALEMODE_LINEAR);

    /* Leer versiones */
    char s_kernel[32], s_mesa[32], s_retroarch[32], s_sdl3[32], s_puae_core[32], s_build_date[24], s_version[32], s_build_number[16];
    read_release(s_kernel, s_mesa, s_retroarch, s_sdl3, s_puae_core, s_build_date, s_version, s_build_number);

    /* Joystick */
    SDL_Joystick *joy = NULL;
    int nj = 0;
    SDL_JoystickID *jids = SDL_GetJoysticks(&nj);
    if (jids && nj > 0) joy = SDL_OpenJoystick(jids[0]);
    SDL_free(jids);

    int selected = 0;
    float menu_cursor_y = -1.0f; /* -1 = sin inicializar, se fija en el primer frame de STATE_MENU */
    Uint64 menu_lerp_last_time = SDL_GetTicksNS();
    float settings_cursor_y = -1.0f;
    Uint64 settings_lerp_last_time = SDL_GetTicksNS();
    float wifi_cursor_y = -1.0f;
    Uint64 wifi_lerp_last_time = SDL_GetTicksNS();
    float bkm_cursor_y = -1.0f;
    Uint64 bkm_lerp_last_time = SDL_GetTicksNS();
    float bkl_cursor_y = -1.0f;
    Uint64 bkl_lerp_last_time = SDL_GetTicksNS();
    float led_cursor_y = -1.0f;
    Uint64 led_lerp_last_time = SDL_GetTicksNS();
    float tz_cursor_y = -1.0f;
    char tz_preview_buf[8] = "--:--";
    int tz_preview_last_sel = -1;
    Uint64 tz_preview_last_time = 0;
    Uint64 tz_lerp_last_time = SDL_GetTicksNS();
    float dim_cursor_y = -1.0f;
    Uint64 dim_lerp_last_time = SDL_GetTicksNS();
    float perf_cursor_y = -1.0f;
    Uint64 perf_lerp_last_time = SDL_GetTicksNS();
    float bt_cursor_y = -1.0f;
    Uint64 bt_lerp_last_time = SDL_GetTicksNS();
    int dev_selected = 0;
    ArexxScript arexx_scripts[AREXX_MAX_SCRIPTS];
    int arexx_count = 0;
    int arexx_selected = 0;
    int arexx_md5_cached_for = -1;
    char arexx_md5[40] = "";
    /* Pauta 4: cache de wrap de descripcion ARexx, evita recalcular
     * TTF_GetStringSize palabra a palabra en cada frame si la seleccion
     * y el idioma no han cambiado. */
    int arexx_desc_cached_for = -1;
    int arexx_desc_cached_lang = -1;
    int arexx_desc_lines_cached = 0;
    float arexx_desc_max_line_w_cached = 0.0f;
    /* Cache de wrap de descripciones de perfil (Maximum/Balanced/Battery):
     * contenido fijo, solo depende del idioma, no de la seleccion. */
    int perf_desc_cached_lang = -1;
    int perf_desc_lines_cached[3] = {0, 0, 0};
    float perf_desc_max_line_w_cached[3] = {0.0f, 0.0f, 0.0f};
    char *arexx_output = NULL;
    size_t arexx_output_len = 0;
    size_t arexx_output_cap = 0;
    int arexx_scroll = 0;
    int arexx_user_scrolled = 0;
    bool show_fps_counter = false;
    int fps_frame_count = 0;
    float fps_display = 0.0f;
    Uint64 fps_last_update = SDL_GetTicksNS();
    int confirm_target = DEV_ACTION_REBOOT; /* cual de los dos confirm. */
    AppState confirm_return_state = STATE_DEVMODE;
    int settings_selected = 0;
    int backup_selected = 0;
    Uint64 backup_msg_until = 0;
    char backup_created_name[64] = "";
    bool backup_creating = false;      /* R01: en curso, mostrar "Generando..." */
    bool backup_create_frame_shown = false; /* deja repintar antes de bloquear */
    #define BACKUP_LIST_MAX BACKUP_MAX
    char backup_list[BACKUP_LIST_MAX][64];
    int backup_count = 0;
    int backup_list_selected = 0;
    char wifi_ssid[64] = "";
    char wifi_password[64] = "";
    int wifi_field_selected = 0;
    bool wifi_show_password = false;
    int led_selected = 0; /* 0,1,2 = R,G,B derecho; 3,4,5 = R,G,B izquierdo; 6 = Brillo */
    #define LED_SLIDER_COUNT 7
    int led_r_right = 0, led_g_right = 0, led_b_right = 0;
    int led_r_left = 0, led_g_left = 0, led_b_left = 0;
    int led_brightness = 128;
    read_led_conf(&led_r_right, &led_g_right, &led_b_right,
                  &led_r_left, &led_g_left, &led_b_left, &led_brightness);
    send_led_payload(led_brightness, led_r_right, led_g_right, led_b_right,
                      led_r_left, led_g_left, led_b_left);
    Uint64 led_repeat_next = 0;
    int led_repeat_dir = 0; /* -1 izq, +1 der, 0 ninguno */
    char timezone_current[64] = "UTC";
    int timezone_selected = 0;
    int perf_selected = g_cfg.perf_profile;
    read_current_tz(timezone_current, sizeof(timezone_current));
    for (int i = 0; i < TIMEZONE_LIST_COUNT; i++) {
        if (!strcmp(TIMEZONE_LIST[i].tz_name, timezone_current)) {
            timezone_selected = i;
            break;
        }
    }
    int dim_timeout_sec = 0;
    int dim_percent = 20;
    read_dim_config(&dim_timeout_sec, &dim_percent);
    int dim_timeout_selected = 0;
    for (int i = 0; i < DIM_TIMEOUT_COUNT; i++) {
        if (DIM_TIMEOUT_OPTIONS[i] == dim_timeout_sec) { dim_timeout_selected = i; break; }
    }
    int dim_field_selected = 0; /* 0 = tiempo, 1 = porcentaje */
    int brightness_pct = read_brightness_config();
    write_brightness((int)((int64_t)2499 * brightness_pct / 100));
    apply_perf_profile(perf_selected);
    int dim_max_brightness = read_max_brightness();
    int ssh_enabled = g_cfg.ssh_enabled; /* aplicado ya por S51ssh-toggle en boot, solo reflejar estado en UI */
    int refresh_120hz = read_refresh_120hz(); /* aplicado ya al crear la ventana, solo reflejar estado en UI */
    int samba_enabled = read_samba_enabled(); /* aplicado ya por S53samba-toggle en boot, solo reflejar estado en UI */
    int bt_enabled = read_bt_enabled(); /* aplicado ya por S22bluetooth-toggle en boot, solo reflejar estado en UI */
    int wifi_enabled = read_wifi_enabled(); /* aplicado ya por S41wifi-toggle en boot, solo reflejar estado en UI */
    s_click_sound_enabled = read_click_sound_enabled() ? true : false;
    int bt_selected = 0; /* cursor en lista de dispositivos escaneados */
    BTDevice bt_devices[BT_MAX_DEVICES];
    int bt_device_count = 0;
    bool bt_scanning = false;
    Uint64 bt_scan_start = 0;
    Uint64 bt_light_check_trigger = 0;
    Uint64 bt_last_poll = 0;
    bool bt_connecting = false;
    Uint64 bt_connect_start = 0;
    char bt_connect_status[64] = "";
    char bt_connected_mac[18] = "";
    char bt_connected_name[64] = "";
    char bt_connect_target_mac[18] = "";
    int dim_saved_brightness = -1; /* brillo del usuario antes de atenuar, -1 = no atenuado */
    bool dim_active = false;
    bool led_dimmed = false;
    Uint64 led_lowbat_timer = 0;
    Uint64 last_input_ticks = SDL_GetTicks();
    char kb_buffer[64] = "";
    int  kb_row = 0;
    int  kb_col = 0;
    int  kb_mode = KB_MODE_LOWER;
    AppState kb_return_state = STATE_WIFI_CONFIG;
    AppState state = STATE_MENU;
    AppState prev_state = STATE_MENU;
    int menu_axis_prev = 0; /* reset al re-entrar a STATE_MENU, evita movimiento fantasma (B05) */
    int stick_axis_prev = 0; /* debounce del eje Y del stick izq. traducido a HAT en pantallas fuera de STATE_MENU */
    ExecRequest exec_req = EXEC_NONE;
    int action   = ACTION_NONE;
    bool running = true;
    SDL_Event ev;

    Uint64 devmode_hold_start = 0; /* 0 = combo no presionado */
    bool devmode_combo_held = false;
    Uint64 screenshot_flash_until = 0; /* ms hasta cuando mostrar flash */
    bool screenshot_capture_pending = false; /* diferir captura al final del frame */

    char dev_ip[32]     = "sin red";
    char dev_uptime[16] = "--";
    char dev_ram[24]    = "--";
    Uint64 dev_last_temp_sample = 0;
    int dev_cpu_cur_freq = 0;
    int dev_cpu_max_freq = 0;
    read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &dev_cpu_max_freq);

    char sysinfo_disk_data[32] = "--";
    char sysinfo_disk_root[32] = "--";
    char menu_disk_free[32]    = "--";
    char dash_cpu_temp[16]     = "--";
    char dash_cpu_load[16]     = "--";
    char dash_ram[32]          = "--";
    Uint64 last_menu_refresh   = 0;
    Uint64 last_cpu_temp_sample = 0;
    int sysinfo_page = 0; /* 0 = Sistema/Metricas/Volumenes, 1 = Specs/Software/Red */
    char sysinfo_temp[16]      = "--";
    char sysinfo_cpu_usage[8]  = "--";
    int  sysinfo_cpu_pct       = 0;
    int  sysinfo_temp_pct      = 0; /* thermal_zone2 (cpu-thermal), para la barra de temperatura en STATE_SYSINFO */
    int  sysinfo_ram_pct       = 0;
    int  sysinfo_disk_root_pct = 0;
    int  sysinfo_disk_data_pct = 0;
    char sysinfo_data_free_str[32] = "--";
    char sysinfo_loadavg[32]   = "--";
    char sysinfo_wifi_sig[32]  = "--";
    int  sysinfo_wifi_pct      = -1;
    char sysinfo_mac[24]       = "--";
    char sysinfo_build[32]     = "--";
    Uint64 last_sysinfo_update = 0;
    /* Variables de actualización OTA */
    UpdatePhase update_phase   = UPD_CHECKING;
    bool  update_checked       = false;
    bool  upd_check_frame_shown = false; /* R01: deja repintar "Comprobando..." antes de bloquear */
    bool  upd_verify_dl_started = false; /* fase VERIFYING: descarga async del .sha256 ya lanzada */
    char  upd_new_ver[32]      = "";
    char  upd_dl_url[512]      = "";
    char  upd_sha_url[512]     = "";
    char  upd_msg[128]         = "";
    float upd_progress         = 0.0f;
    /* Check de actualizacion en background (independiente de STATE_UPDATE),
     * para mostrar un indicador visual en el menu principal si hay
     * una version nueva disponible, sin que el usuario tenga que entrar
     * al submenu de Actualizacion de sistema. */
    bool   bg_update_checked    = false;
    bool   bg_update_available  = false;
    pid_t  s_bgcheck_pid        = -1;
    Uint64 bg_check_start_delay = 0; /* se fija al primer frame */
    char   bg_new_ver[32]       = "";
    char   bg_dl_url[512]       = "";
    char   bg_sha_url[512]      = "";
    Uint64 upd_check_start     = 0;


    char status_time[8] = "--:--";
    bool status_wifi_up = false;
    bool status_bt_up = false;
    int  status_battery = -1;
    Uint64 last_status_update = 0;
    Uint64 last_bt_check_trigger = 0;
    char rt_bt_applied_mac[18] = "";
    bool rt_bt_applied_init = false;

    SDL_Color c_bg      = COL_BG;
    SDL_Color c_green   = COL_CREAM;
    SDL_Color c_dkgreen = COL_CREAM;
    SDL_Color c_white   = COL_WHITE;
    SDL_Color c_gray    = COL_CREAM;
    SDL_Color c_selbg   = COL_SEL_BG;

    /* Layout */
    float mx     = 20.0f;
    float mw     = 390.0f;
    float sep_x  = 440.0f;
    float rx      = 400.0f;
    float rx_max_w = SCREEN_W - rx - 15.0f;
    float menu_y0 = 130.0f;
    float item_h  = 34.0f;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ||
                ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION || ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
                last_input_ticks = SDL_GetTicks();
                if (dim_active) {
                    write_brightness(dim_saved_brightness);
                    dim_active = false;
                    /* Restaura el perfil de rendimiento real del usuario
                     * (governor + GPU + boost), no un governor fijo — antes
                     * dejaba siempre schedutil aunque el usuario tuviera
                     * Maximum Performance activo antes de atenuar. */
                    apply_perf_profile(perf_selected);
                }
            }
            /* Traduce el eje Y del stick izquierdo a eventos HAT sinteticos,
             * para que cualquier pantalla que ya escucha
             * SDL_EVENT_JOYSTICK_HAT_MOTION (D-pad) responda tambien al
             * stick. STATE_MENU ya tiene su propio manejo de eje
             * (menu_axis_prev, mas arriba) y queda excluido para no
             * duplicar el movimiento. */
            if (state != STATE_MENU && ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
                ev.jaxis.axis == 1) {
                int v = ev.jaxis.value;
                int zone = (v < -16000) ? -1 : (v > 16000) ? 1 : 0;
                if (zone != stick_axis_prev && zone != 0) {
                    ev.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
                    ev.jhat.value = (zone == -1) ? SDL_HAT_UP : SDL_HAT_DOWN;
                }
                stick_axis_prev = zone;
            }

            /* Atajo global: MODE + DPAD UP/DOWN ajusta brillo desde
             * cualquier pantalla, salvo dentro de STATE_BRIGHTNESS_CONFIG
             * (donde el D-pad ya tiene su propio uso LEFT/RIGHT). */
            if (state != STATE_BRIGHTNESS_CONFIG &&
                ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION &&
                joy && SDL_GetJoystickButton(joy, BTN_SDL_MODE)) {
                int brightness_delta = 0;
                if (ev.jhat.value == SDL_HAT_UP)   brightness_delta = +5;
                if (ev.jhat.value == SDL_HAT_DOWN) brightness_delta = -5;
                if (brightness_delta != 0) {
                    brightness_pct += brightness_delta;
                    if (brightness_pct < 5)   brightness_pct = 5;
                    if (brightness_pct > 100) brightness_pct = 100;
                    write_brightness((int)((int64_t)2499 * brightness_pct / 100));
                    save_brightness_config(brightness_pct);
                    continue;
                }
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                if (state == STATE_MENU) running = false;
                else if (state == STATE_CONFIRM) state = STATE_DEVMODE;
                else if (state == STATE_DEVMODE) state = STATE_MENU;
                else if (state == STATE_SYSINFO) state = STATE_MENU;
                else if (state == STATE_CONTROLLER_TEST) state = STATE_SETTINGS;
                else if (state == STATE_UPDATE) { if (update_phase != UPD_DOWNLOADING && update_phase != UPD_CONFIRM) state = STATE_MENU; }
                else if (state == STATE_SETTINGS) state = STATE_MENU;
            }

            if (state == STATE_MENU) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_L1 &&
                    !(joy && SDL_GetJoystickButton(joy, BTN_SDL_SELECT) &&
                             SDL_GetJoystickButton(joy, BTN_SDL_START))) {
                    current_lang = (current_lang == LANG_ES) ? LANG_EN : LANG_ES;
                    save_lang_config();
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B &&
                    joy && SDL_GetJoystickButton(joy, BTN_SDL_MODE)) {
                    s_click_sound_enabled = !s_click_sound_enabled;
                    save_click_sound_enabled(s_click_sound_enabled ? 1 : 0);
                }
                /* MODE+START: restart launcher only (clean exit, inittab
                 * respawn relaunches armiga-launcher automatically). */
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_START &&
                    joy && SDL_GetJoystickButton(joy, BTN_SDL_MODE)) {
                    running = false;
                }
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        selected = (selected + 1) % MENU_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_RETURN)
                        action = selected + 1;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        selected = (selected + 1) % MENU_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION &&
                    ev.jaxis.axis == 1) {
                    int v = ev.jaxis.value;
                    int zone = (v < -16000) ? -1 : (v > 16000) ? 1 : 0;
                    if (zone != menu_axis_prev) {
                        if (zone == -1)
                            selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        else if (zone == 1)
                            selected = (selected + 1) % MENU_COUNT;
                        if (zone != 0) play_ui_click();
                        menu_axis_prev = zone;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A)
                    action = selected + 1;
            }
            else if (state == STATE_DEVMODE) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        dev_selected = (dev_selected - 1 + DEV_MENU_COUNT) % DEV_MENU_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        dev_selected = (dev_selected + 1) % DEV_MENU_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        dev_selected = (dev_selected - 1 + DEV_MENU_COUNT) % DEV_MENU_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        dev_selected = (dev_selected + 1) % DEV_MENU_COUNT;
                        play_ui_click();
                    }
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
                    } else if (dev_selected == DEV_ACTION_FPS_TOGGLE) {
                        show_fps_counter = !show_fps_counter;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
            }
            else if (state == STATE_SETTINGS) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        settings_selected = (settings_selected - 1 + SETTINGS_MENU_COUNT) % SETTINGS_MENU_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        settings_selected = (settings_selected + 1) % SETTINGS_MENU_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        settings_selected = (settings_selected - 1 + SETTINGS_MENU_COUNT) % SETTINGS_MENU_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        settings_selected = (settings_selected + 1) % SETTINGS_MENU_COUNT;
                        play_ui_click();
                    }
                }
                bool settings_trigger_action =
                    (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && ev.jbutton.button == BTN_SDL_A);
                if (settings_trigger_action) {
                    switch (settings_selected) {
                        case 0:
                            read_wifi_conf(wifi_ssid, sizeof(wifi_ssid), wifi_password, sizeof(wifi_password));
                            wifi_field_selected = 0;
                            wifi_show_password = false;
                            state = STATE_WIFI_CONFIG;
                            break;
                        case 1:
                            backup_selected = 0;
                            state = STATE_BACKUP_MENU;
                            break;
                        case 2:
                            led_selected = 0;
                            state = STATE_LED_CONFIG;
                            break;
                        case 3:
                            state = STATE_TIMEZONE_CONFIG;
                            break;
                        case 4:
                            dim_field_selected = 0;
                            state = STATE_SCREENDIM_CONFIG;
                            break;
                        case 5:
                            state = STATE_BRIGHTNESS_CONFIG;
                            break;
                        case 6:
                            ssh_enabled = !ssh_enabled;
                            save_ssh_enabled(ssh_enabled);
                            apply_ssh_enabled(ssh_enabled);
                            break;
                        case 7:
                            samba_enabled = !samba_enabled;
                            save_samba_enabled(samba_enabled);
                            apply_samba_enabled(samba_enabled);
                            break;
                        case 8:
                            state = STATE_PERF_CONFIG;
                            break;
                        case 9:
                            bt_selected = 0;
                            bt_device_count = 0;
                            bt_connect_status[0] = 0;
                            read_bt_connected(bt_connected_mac, sizeof(bt_connected_mac), bt_connected_name, sizeof(bt_connected_name));
                            if (bt_connected_mac[0]) set_retroarch_audio_device(bt_connected_mac);
                            if (bt_enabled) {
                                bt_scanning = true;
                                bt_scan_start = SDL_GetTicks();
                                system("armiga-bt-scan >/dev/null 2>&1 &");
                            }
                            state = STATE_BLUETOOTH_CONFIG;
                            break;
                        case 10:
                            refresh_120hz = !refresh_120hz;
                            save_refresh_120hz(refresh_120hz);
                            apply_refresh_120hz(win, refresh_120hz);
                            set_retroarch_refresh_rate(refresh_120hz ? 120 : 60);
                            break;
                        case 11:
                            confirm_target = SETTINGS_ACTION_FACTORY_RESET;
                            confirm_return_state = STATE_SETTINGS;
                            state = STATE_CONFIRM;
                            break;
                        case SETTINGS_ITEM_CONTROLLER_TEST:
                            state = STATE_CONTROLLER_TEST;
                            break;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
            }
            else if (state == STATE_BRIGHTNESS_CONFIG) {
                int brightness_pct_prev = brightness_pct;
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_LEFT) {
                        brightness_pct -= 5;
                        if (brightness_pct < 5) brightness_pct = 5;
                    }
                    if (ev.key.key == SDLK_RIGHT) {
                        brightness_pct += 5;
                        if (brightness_pct > 100) brightness_pct = 100;
                    }
                    if (ev.key.key == SDLK_RETURN) {
                        save_brightness_config(brightness_pct);
                        state = STATE_SETTINGS;
                    }
                    if (ev.key.key == SDLK_ESCAPE) {
                        brightness_pct = read_brightness_config();
                        write_brightness((int)((int64_t)2499 * brightness_pct / 100));
                        state = STATE_SETTINGS;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_LEFT) {
                        brightness_pct -= 5;
                        if (brightness_pct < 5) brightness_pct = 5;
                    } else if (ev.jhat.value == SDL_HAT_RIGHT) {
                        brightness_pct += 5;
                        if (brightness_pct > 100) brightness_pct = 100;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
                    save_brightness_config(brightness_pct);
                    state = STATE_SETTINGS;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B) {
                    brightness_pct = read_brightness_config();
                    write_brightness((int)((int64_t)2499 * brightness_pct / 100));
                    state = STATE_SETTINGS;
                }
                if (brightness_pct != brightness_pct_prev)
                    write_brightness((int)((int64_t)2499 * brightness_pct / 100));
            }
            else if (state == STATE_PERF_CONFIG) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        perf_selected = (perf_selected - 1 + 3) % 3;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        perf_selected = (perf_selected + 1) % 3;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_RETURN) {
                        save_perf_profile(perf_selected);
                        apply_perf_profile(perf_selected);
                        state = STATE_SETTINGS;
                    }
                    if (ev.key.key == SDLK_ESCAPE) {
                        perf_selected = g_cfg.perf_profile;
                        state = STATE_SETTINGS;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        perf_selected = (perf_selected - 1 + 3) % 3;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        perf_selected = (perf_selected + 1) % 3;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
                    save_perf_profile(perf_selected);
                    apply_perf_profile(perf_selected);
                    state = STATE_SETTINGS;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B) {
                    perf_selected = g_cfg.perf_profile;
                    state = STATE_SETTINGS;
                }
            }
            else if (state == STATE_BLUETOOTH_CONFIG) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_SELECT) {
                    bt_enabled = !bt_enabled;
                    save_bt_enabled(bt_enabled);
                    apply_bt_enabled(bt_enabled);
                    if (!bt_enabled) {
                        bt_scanning = false;
                        bt_device_count = 0;
                        bt_connected_mac[0] = 0;
                        bt_connected_name[0] = 0;
                        set_retroarch_audio_device(NULL);
                    }
                }
                if (!bt_connecting && bt_device_count > 0) {
                    if (ev.type == SDL_EVENT_KEY_DOWN) {
                        if (ev.key.key == SDLK_UP) {
                            bt_selected = (bt_selected - 1 + bt_device_count) % bt_device_count;
                            play_ui_click();
                        }
                        if (ev.key.key == SDLK_DOWN) {
                            bt_selected = (bt_selected + 1) % bt_device_count;
                            play_ui_click();
                        }
                    }
                    if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                        if (ev.jhat.value == SDL_HAT_UP) {
                            bt_selected = (bt_selected - 1 + bt_device_count) % bt_device_count;
                            play_ui_click();
                        } else if (ev.jhat.value == SDL_HAT_DOWN) {
                            bt_selected = (bt_selected + 1) % bt_device_count;
                            play_ui_click();
                        }
                    }
                    if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                        ev.jbutton.button == BTN_SDL_A) {
                        char cmd[128];
                        safe_copy(bt_connect_target_mac, bt_devices[bt_selected].mac, sizeof(bt_connect_target_mac));
                        snprintf(cmd, sizeof(cmd), "armiga-bt-connect %s >/dev/null 2>&1 &", bt_devices[bt_selected].mac);
                        system(cmd);
                        bt_connecting = true;
                        bt_connect_start = SDL_GetTicks();
                        bt_connect_status[0] = 0;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
                    state = STATE_SETTINGS;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_SETTINGS;
            }
            else if (state == STATE_AREXX_LIST) {
                if (arexx_count > 0) {
                    if (ev.type == SDL_EVENT_KEY_DOWN) {
                        if (ev.key.key == SDLK_UP) {
                            arexx_selected = (arexx_selected - 1 + arexx_count) % arexx_count;
                            play_ui_click();
                        }
                        if (ev.key.key == SDLK_DOWN) {
                            arexx_selected = (arexx_selected + 1) % arexx_count;
                            play_ui_click();
                        }
                    }
                    if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                        if (ev.jhat.value == SDL_HAT_UP) {
                            arexx_selected = (arexx_selected - 1 + arexx_count) % arexx_count;
                            play_ui_click();
                        } else if (ev.jhat.value == SDL_HAT_DOWN) {
                            arexx_selected = (arexx_selected + 1) % arexx_count;
                            play_ui_click();
                        }
                    }
                    if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                        ev.jbutton.button == BTN_SDL_A) {
                        free(arexx_output);
                        arexx_output = NULL;
                        arexx_output_len = 0;
                        arexx_output_cap = 0;
                        arexx_scroll = 0;
                        arexx_user_scrolled = 0;
                        start_arexx_script_async(arexx_scripts[arexx_selected].filename);
                        state = STATE_AREXX_RUN;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
                    state = STATE_MENU;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
            }
            else if (state == STATE_AREXX_RUN) {
                if (s_arexx_pid <= 0 &&
                    ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                     (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && ev.jbutton.button == BTN_SDL_B)))
                    state = STATE_AREXX_LIST;
                if (s_arexx_pid <= 0) {
                    int arexx_scroll_before = arexx_scroll;
                    if (ev.type == SDL_EVENT_KEY_DOWN) {
                        if (ev.key.key == SDLK_UP) arexx_scroll--;
                        if (ev.key.key == SDLK_DOWN) arexx_scroll++;
                    }
                    if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                        if (ev.jhat.value == SDL_HAT_UP) arexx_scroll--;
                        else if (ev.jhat.value == SDL_HAT_DOWN) arexx_scroll++;
                    }
                    if (arexx_scroll < 0) arexx_scroll = 0;
                    if (arexx_scroll != arexx_scroll_before) arexx_user_scrolled = 1;
                }
            }
            else if (state == STATE_TIMEZONE_CONFIG) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        timezone_selected = (timezone_selected - 1 + TIMEZONE_LIST_COUNT) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        timezone_selected = (timezone_selected + 1) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        timezone_selected = (timezone_selected - 1 + TIMEZONE_LIST_COUNT) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        timezone_selected = (timezone_selected + 1) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
                    if (ev.jbutton.button == BTN_SDL_L1) {
                        timezone_selected = (timezone_selected - 5 + TIMEZONE_LIST_COUNT) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    } else if (ev.jbutton.button == BTN_SDL_R1) {
                        timezone_selected = (timezone_selected + 5) % TIMEZONE_LIST_COUNT;
                        play_ui_click();
                    }
                }
                if ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && ev.jbutton.button == BTN_SDL_A)) {
                    strncpy(timezone_current, TIMEZONE_LIST[timezone_selected].tz_name,
                            sizeof(timezone_current) - 1);
                    timezone_current[sizeof(timezone_current) - 1] = 0;
                    setenv("TZ", timezone_current, 1);
                    tzset();
                    save_timezone_config(timezone_current);
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_SETTINGS;
            }
            else if (state == STATE_SCREENDIM_CONFIG) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        dim_field_selected = (dim_field_selected - 1 + 2) % 2;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        dim_field_selected = (dim_field_selected + 1) % 2;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_LEFT) {
                        if (dim_field_selected == 0) {
                            dim_timeout_selected = (dim_timeout_selected - 1 + DIM_TIMEOUT_COUNT) % DIM_TIMEOUT_COUNT;
                            play_ui_click();
                        } else {
                            dim_percent -= 5;
                            if (dim_percent < 5) dim_percent = 5;
                        }
                    }
                    if (ev.key.key == SDLK_RIGHT) {
                        if (dim_field_selected == 0) {
                            dim_timeout_selected = (dim_timeout_selected + 1) % DIM_TIMEOUT_COUNT;
                            play_ui_click();
                        } else {
                            dim_percent += 5;
                            if (dim_percent > 95) dim_percent = 95;
                        }
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        dim_field_selected = (dim_field_selected - 1 + 2) % 2;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        dim_field_selected = (dim_field_selected + 1) % 2;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_LEFT) {
                        if (dim_field_selected == 0) {
                            dim_timeout_selected = (dim_timeout_selected - 1 + DIM_TIMEOUT_COUNT) % DIM_TIMEOUT_COUNT;
                            play_ui_click();
                        } else {
                            dim_percent -= 5;
                            if (dim_percent < 5) dim_percent = 5;
                        }
                    }
                    else if (ev.jhat.value == SDL_HAT_RIGHT) {
                        if (dim_field_selected == 0) {
                            dim_timeout_selected = (dim_timeout_selected + 1) % DIM_TIMEOUT_COUNT;
                            play_ui_click();
                        } else {
                            dim_percent += 5;
                            if (dim_percent > 95) dim_percent = 95;
                        }
                    }
                }
                if ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && ev.jbutton.button == BTN_SDL_A)) {
                    dim_timeout_sec = DIM_TIMEOUT_OPTIONS[dim_timeout_selected];
                    save_dim_config(dim_timeout_sec, dim_percent);
                    if (dim_active) {
                        write_brightness(dim_saved_brightness);
                        dim_active = false;
                        apply_perf_profile(perf_selected);
                    }
                    last_input_ticks = SDL_GetTicks();
                    state = STATE_SETTINGS;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_SETTINGS;
            }
            else if (state == STATE_BACKUP_MENU) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        backup_selected = (backup_selected - 1 + BACKUP_MENU_COUNT) % BACKUP_MENU_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        backup_selected = (backup_selected + 1) % BACKUP_MENU_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        backup_selected = (backup_selected - 1 + BACKUP_MENU_COUNT) % BACKUP_MENU_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        backup_selected = (backup_selected + 1) % BACKUP_MENU_COUNT;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && backup_selected == 0 && !backup_creating) {
                    backup_creating = true;
                    backup_create_frame_shown = false;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && backup_selected == 1) {
                    backup_count = list_backups(backup_list, BACKUP_LIST_MAX);
                    backup_list_selected = 0;
                    state = STATE_BACKUP_LIST;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_SETTINGS;
            }
            else if (state == STATE_LED_CONFIG) {
                int *led_vals[LED_SLIDER_COUNT] = {
                    &led_r_right, &led_g_right, &led_b_right,
                    &led_r_left,  &led_g_left,  &led_b_left,
                    &led_brightness
                };
                bool led_dirty = false;
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        led_selected = (led_selected - 1 + LED_SLIDER_COUNT) % LED_SLIDER_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        led_selected = (led_selected + 1) % LED_SLIDER_COUNT;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_LEFT) {
                        *led_vals[led_selected] -= 5;
                        if (*led_vals[led_selected] < 0) *led_vals[led_selected] = 0;
                        led_dirty = true;
                        led_repeat_dir = -1;
                        led_repeat_next = SDL_GetTicks() + 350;
                    }
                    if (ev.key.key == SDLK_RIGHT) {
                        *led_vals[led_selected] += 5;
                        if (*led_vals[led_selected] > 255) *led_vals[led_selected] = 255;
                        led_dirty = true;
                        led_repeat_dir = 1;
                        led_repeat_next = SDL_GetTicks() + 350;
                    }
                }
                if (ev.type == SDL_EVENT_KEY_UP &&
                    (ev.key.key == SDLK_LEFT || ev.key.key == SDLK_RIGHT)) {
                    led_repeat_dir = 0;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        led_selected = (led_selected - 1 + LED_SLIDER_COUNT) % LED_SLIDER_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        led_selected = (led_selected + 1) % LED_SLIDER_COUNT;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_LEFT) {
                        *led_vals[led_selected] -= 5;
                        if (*led_vals[led_selected] < 0) *led_vals[led_selected] = 0;
                        led_dirty = true;
                        led_repeat_dir = -1;
                        led_repeat_next = SDL_GetTicks() + 350;
                    }
                    else if (ev.jhat.value == SDL_HAT_RIGHT) {
                        *led_vals[led_selected] += 5;
                        if (*led_vals[led_selected] > 255) *led_vals[led_selected] = 255;
                        led_dirty = true;
                        led_repeat_dir = 1;
                        led_repeat_next = SDL_GetTicks() + 350;
                    }
                    else {
                        led_repeat_dir = 0;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_L1) {
                    *led_vals[led_selected] -= 20;
                    if (*led_vals[led_selected] < 0) *led_vals[led_selected] = 0;
                    led_dirty = true;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_R1) {
                    *led_vals[led_selected] += 20;
                    if (*led_vals[led_selected] > 255) *led_vals[led_selected] = 255;
                    led_dirty = true;
                }
                if (led_dirty) {
                    send_led_payload(led_brightness,
                                      led_r_right, led_g_right, led_b_right,
                                      led_r_left, led_g_left, led_b_left);
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B) {
                    led_repeat_dir = 0;
                    save_led_conf(led_r_right, led_g_right, led_b_right,
                                  led_r_left, led_g_left, led_b_left, led_brightness);
                    state = STATE_SETTINGS;
                }
            }
            else if (state == STATE_BACKUP_LIST) {
                if (ev.type == SDL_EVENT_KEY_DOWN && backup_count > 0) {
                    if (ev.key.key == SDLK_UP) {
                        backup_list_selected = (backup_list_selected - 1 + backup_count) % backup_count;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        backup_list_selected = (backup_list_selected + 1) % backup_count;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION && backup_count > 0) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        backup_list_selected = (backup_list_selected - 1 + backup_count) % backup_count;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        backup_list_selected = (backup_list_selected + 1) % backup_count;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && backup_count > 0) {
                    restore_backup(backup_list[backup_list_selected]);
                    running = false;
                    exec_req = EXEC_REBOOT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_X && backup_count > 0) {
                    delete_backup(backup_list[backup_list_selected]);
                    backup_count = list_backups(backup_list, BACKUP_LIST_MAX);
                    if (backup_list_selected >= backup_count)
                        backup_list_selected = backup_count > 0 ? backup_count - 1 : 0;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_BACKUP_MENU;
            }
            else if (state == STATE_WIFI_CONFIG) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP) {
                        wifi_field_selected = (wifi_field_selected - 1 + 3) % 3;
                        play_ui_click();
                    }
                    if (ev.key.key == SDLK_DOWN) {
                        wifi_field_selected = (wifi_field_selected + 1) % 3;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP) {
                        wifi_field_selected = (wifi_field_selected - 1 + 3) % 3;
                        play_ui_click();
                    } else if (ev.jhat.value == SDL_HAT_DOWN) {
                        wifi_field_selected = (wifi_field_selected + 1) % 3;
                        play_ui_click();
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_SELECT)
                    wifi_show_password = !wifi_show_password;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
                    if (wifi_field_selected == 2) {
                        wifi_enabled = !wifi_enabled;
                        save_wifi_enabled(wifi_enabled);
                        apply_wifi_enabled(wifi_enabled);
                    } else {
                        if (wifi_field_selected == 0)
                            strncpy(kb_buffer, wifi_ssid, sizeof(kb_buffer) - 1);
                        else
                            strncpy(kb_buffer, wifi_password, sizeof(kb_buffer) - 1);
                        kb_buffer[sizeof(kb_buffer) - 1] = 0;
                        kb_row = 0;
                        kb_col = 0;
                        kb_mode = KB_MODE_LOWER;
                        kb_return_state = STATE_WIFI_CONFIG;
                        state = STATE_KEYBOARD;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B) {
                    save_wifi_conf(wifi_ssid, wifi_password);
                    state = STATE_SETTINGS;
                }
            }
            else if (state == STATE_KEYBOARD) {
                int row_len = kb_row_len(kb_mode, kb_row);
                if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    int dir_up = 0, dir_down = 0, dir_left = 0, dir_right = 0;
                    if (ev.type == SDL_EVENT_KEY_DOWN) {
                        dir_up    = (ev.key.key == SDLK_UP);
                        dir_down  = (ev.key.key == SDLK_DOWN);
                        dir_left  = (ev.key.key == SDLK_LEFT);
                        dir_right = (ev.key.key == SDLK_RIGHT);
                    } else {
                        dir_up    = (ev.jhat.value == SDL_HAT_UP);
                        dir_down  = (ev.jhat.value == SDL_HAT_DOWN);
                        dir_left  = (ev.jhat.value == SDL_HAT_LEFT);
                        dir_right = (ev.jhat.value == SDL_HAT_RIGHT);
                    }
                    if (dir_up)    kb_row = (kb_row - 1 + KB_ROWS) % KB_ROWS;
                    if (dir_down)  kb_row = (kb_row + 1) % KB_ROWS;
                    if (dir_left)  kb_col = (kb_col - 1 + row_len) % row_len;
                    if (dir_right) kb_col = (kb_col + 1) % row_len;
                    /* al cambiar de fila, si la columna actual no existe en la nueva fila, recolocar */
                    int new_row_len = kb_row_len(kb_mode, kb_row);
                    if (new_row_len > 0 && kb_col >= new_row_len) kb_col = new_row_len - 1;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
                    const char *k = kb_key_at(kb_mode, kb_row, kb_col);
                    if (k) {
                        size_t len = strlen(kb_buffer);
                        if (len + strlen(k) < sizeof(kb_buffer) - 1) {
                            strcat(kb_buffer, k);
                        }
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_L1) {
                    size_t len = strlen(kb_buffer);
                    if (len > 0) kb_buffer[len - 1] = 0;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_R1) {
                    if (wifi_field_selected == 0)
                        strncpy(wifi_ssid, kb_buffer, sizeof(wifi_ssid) - 1);
                    else
                        strncpy(wifi_password, kb_buffer, sizeof(wifi_password) - 1);
                    state = kb_return_state;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B) {
                    state = kb_return_state;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_SELECT) {
                    kb_mode = (kb_mode == KB_MODE_LOWER) ? KB_MODE_UPPER :
                              (kb_mode == KB_MODE_UPPER) ? KB_MODE_SYMBOLS : KB_MODE_LOWER;
                    int nl = kb_row_len(kb_mode, kb_row);
                    if (nl > 0 && kb_col >= nl) kb_col = nl - 1;
                }
            }
            else if (state == STATE_CONFIRM) {
                if ((ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_RETURN) ||
                    (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                     ev.jbutton.button == BTN_SDL_A)) {
                    if (confirm_target == SETTINGS_ACTION_FACTORY_RESET) {
                        factory_reset();
                        running = false;
                        exec_req = EXEC_REBOOT;
                    } else {
                        running = false;
                        exec_req = (confirm_target == DEV_ACTION_REBOOT)
                                   ? EXEC_REBOOT : EXEC_SHUTDOWN;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = confirm_return_state;
            }
            else if (state == STATE_SYSINFO) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B)
                    state = STATE_MENU;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_L1)
                    sysinfo_page = (sysinfo_page + 1) % 2;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_R1)
                    sysinfo_page = (sysinfo_page + 1) % 2;
            }
            else if (state == STATE_UPDATE) {
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
                    if (update_phase == UPD_CONFIRM)
                        update_phase = UPD_DOWNLOADING;
                    else if (update_phase != UPD_DOWNLOADING)
                        state = STATE_MENU;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_B &&
                    update_phase != UPD_DOWNLOADING)
                    state = STATE_MENU;
            }
            else if (state == STATE_CONTROLLER_TEST) {
                /* Salida via combo SELECT+START (no via un boton individual,
                 * ya que esta pantalla existe precisamente para testear
                 * TODOS los botones, incluido B). */
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    (ev.jbutton.button == BTN_SDL_SELECT || ev.jbutton.button == BTN_SDL_START) &&
                    SDL_GetJoystickButton(joy, BTN_SDL_SELECT) &&
                    SDL_GetJoystickButton(joy, BTN_SDL_START))
                    state = STATE_SETTINGS;
                if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE)
                    state = STATE_SETTINGS;
            }
        }

        if (state == STATE_MENU && prev_state != STATE_MENU)
            menu_axis_prev = 0; /* evita movimiento fantasma al volver de un submenu (B05) */
        prev_state = state;

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
                g_devmode_temp_history_count = 0;
                dev_last_temp_sample = 0;
            }
        } else if (state != STATE_MENU) {
            devmode_combo_held = false;
            devmode_hold_start = 0;
        }

        /* Combo screenshot: SELECT + R1 pulsados simultaneamente (cualquier
         * estado excepto Controller Test, donde R1 debe poder probarse sin
         * disparar una captura de pantalla). */
        if (joy && state != STATE_CONTROLLER_TEST) {
            bool sel = SDL_GetJoystickButton(joy, BTN_SDL_SELECT);
            bool r1  = SDL_GetJoystickButton(joy, BTN_SDL_R1);
            if (sel && r1) {
                /* No capturar aqui: en este punto del bucle (antes de que
                 * este frame dibuje nada) el backbuffer puede contener
                 * estado indefinido tras el ultimo Present (double
                 * buffering). Solo se marca la peticion; la captura real
                 * ocurre al final del frame, justo antes de dibujar el
                 * flash y draw_screen_corners, cuando el contenido de ESTE
                 * frame ya esta completamente dibujado y es valido. */
                screenshot_capture_pending = true;
                SDL_PumpEvents();
                /* Esperar a que suelten los botones para evitar disparos multiples */
                SDL_PumpEvents();
                while (SDL_GetJoystickButton(joy, BTN_SDL_SELECT) ||
                       SDL_GetJoystickButton(joy, BTN_SDL_R1)) {
                    SDL_PumpEvents();
                    SDL_Delay(20);
                }
                SDL_Delay(200); /* debounce tras soltar */
                /* La ventana del flash se fija AQUI, tras soltar los
                 * botones: si se fijara antes del bucle de espera, un
                 * usuario que mantenga la combinacion pulsada mas de
                 * 500ms nunca veria el flash (ventana ya expirada al
                 * reanudar el dibujado del bucle principal). */
                screenshot_flash_until = SDL_GetTicks() + 500;
            }
        }

        if (action != ACTION_NONE) {
            if (action == ACTION_SHELL) {
                /* "Apagar dispositivo" en menu principal */
                exec_req = EXEC_SHUTDOWN;
                running = false;
            } else if (action == ACTION_REBOOT) {
                /* "Reiniciar dispositivo" en menu principal */
                exec_req = EXEC_REBOOT;
                running = false;
            } else if (action == ACTION_ROMS) {
                running = false;
                relaunch_after_retroarch = true;
            } else if (action == ACTION_UPDATE) {
                state = STATE_UPDATE;
                update_phase = UPD_CHECKING;
                update_checked = false;
                upd_check_frame_shown = false;
                upd_verify_dl_started = false;
            } else if (action == ACTION_INFO) {
                state = STATE_SYSINFO;
                last_sysinfo_update = 0; /* forzar refresco inmediato */
            } else if (action == ACTION_AREXX) {
                state = STATE_AREXX_LIST;
                arexx_selected = 0;
                arexx_count = list_arexx_scripts(arexx_scripts, AREXX_MAX_SCRIPTS);
            } else if (action == ACTION_SETTINGS) {
                state = STATE_SETTINGS;
                settings_selected = 0;
            }
            action = ACTION_NONE;
        }

        Uint64 now_ticks = SDL_GetTicks();
        if (last_status_update == 0 || now_ticks - last_status_update > 3000) {
            update_status(status_time, sizeof(status_time),
                         &status_wifi_up, &status_battery);
            status_bt_up = (bool)bt_enabled;
            last_status_update = now_ticks;
        }
        /* Enrutado transparente de audio BT -> RetroArch: comprobacion
         * ligera en background cada ~6s (sea cual sea la pantalla actual),
         * sin bloquear nunca el hilo principal. Si el conectado cambia
         * respecto a lo ya aplicado, actualiza retroarch.cfg. */
        /* Nunca competir con la pantalla Bluetooth: ella ya gestiona su
         * propio ciclo de escaneo/emparejamiento/conexion con sesiones
         * bluetoothctl propias. Lanzar este check ahi tambien causaba
         * fallos de conexion por contencion. Solo activo fuera de esa
         * pantalla y sin una conexion en curso. */
        if (bt_enabled && !bt_connecting && state != STATE_BLUETOOTH_CONFIG &&
            (last_bt_check_trigger == 0 || now_ticks - last_bt_check_trigger > 6000)) {
            system("armiga-bt-connected-check >/dev/null 2>&1 &");
            last_bt_check_trigger = now_ticks;
        }
        /* Deteccion de transicion conectado->desconectado: SIEMPRE activa,
         * en cualquier pantalla (incluida la de Bluetooth), para que el
         * vinculo se elimine sin importar donde estuviera el usuario en el
         * momento exacto de la desconexion. Solo el lanzamiento del check
         * en background se restringe fuera de la pantalla BT (para no
         * competir con su propio ciclo de escaneo). */
        {
            char live_mac[18] = "";
            read_bt_connected(live_mac, sizeof(live_mac), NULL, 0);
            if (!bt_enabled) live_mac[0] = 0;
            if (!rt_bt_applied_init || strcmp(live_mac, rt_bt_applied_mac) != 0) {
                if (rt_bt_applied_init && rt_bt_applied_mac[0] && !live_mac[0]) {
                    /* Transicion conectado -> desconectado: eliminar el
                     * vinculo (bonding) para que el auricular no se
                     * reconecte solo la proxima vez que se encienda.
                     * Vince quiere conexion siempre elegida manualmente. */
                    char rm_cmd[80];
                    snprintf(rm_cmd, sizeof(rm_cmd), "bluetoothctl remove %s >/dev/null 2>&1 &", rt_bt_applied_mac);
                    system(rm_cmd);
                }
                set_retroarch_audio_device(live_mac[0] ? live_mac : NULL);
                safe_copy(rt_bt_applied_mac, live_mac, sizeof(rt_bt_applied_mac));
                rt_bt_applied_init = true;
            }
        }
        /* Check de actualizacion en background: se lanza una sola vez,
         * 2s despues de arrancar (da tiempo a que el WiFi conecte),
         * sin bloquear la UI ni interferir con STATE_UPDATE. */
        if (!bg_update_checked) {
            if (bg_check_start_delay == 0) bg_check_start_delay = now_ticks;
            if (s_bgcheck_pid == -1 && status_wifi_up &&
                now_ticks - bg_check_start_delay >= 2000) {
                unlink(BG_CHECK_JSON_TMP);
                s_bgcheck_pid = spawn_curl_to_file(GITHUB_API_URL, BG_CHECK_JSON_TMP, "10");
            } else if (s_bgcheck_pid != -1) {
                int r = poll_curl_pid(&s_bgcheck_pid, BG_CHECK_JSON_TMP, 1);
                if (r != 0) {
                    if (r > 0) {
                        bg_update_checked = true;
                        int res = finish_check_update(BG_CHECK_JSON_TMP, s_version,
                                               bg_new_ver, sizeof(bg_new_ver),
                                               bg_dl_url,  sizeof(bg_dl_url),
                                               bg_sha_url, sizeof(bg_sha_url));
                        bg_update_available = (res == 1);
                    } else {
                        /* Fallo (sin red aun, timeout, etc): reintentar
                         * en 15s en vez de dar el check por definitivo. */
                        bg_check_start_delay = now_ticks + 15000;
                    }
                }
            }
        }
        if (dim_timeout_sec > 0 && !dim_active &&
            (now_ticks - last_input_ticks) >= (Uint64)(dim_timeout_sec * 1000)) {
            dim_saved_brightness = read_current_brightness();
            if (dim_saved_brightness >= 0) {
                int target = (int)((int64_t)dim_saved_brightness * dim_percent / 100);
                if (target < 1) target = 1; /* nunca apagar del todo, sigue siendo legible */
                write_brightness(target);
                dim_active = true;
                set_cpu_governor("powersave");
            }
        }
        if (state == STATE_LED_CONFIG && led_repeat_dir != 0 &&
            now_ticks >= led_repeat_next) {
            int *led_vals[LED_SLIDER_COUNT] = {
                &led_r_right, &led_g_right, &led_b_right,
                &led_r_left,  &led_g_left,  &led_b_left,
                &led_brightness
            };
            *led_vals[led_selected] += led_repeat_dir * 5;
            if (*led_vals[led_selected] < 0) *led_vals[led_selected] = 0;
            if (*led_vals[led_selected] > 255) *led_vals[led_selected] = 255;
            send_led_payload(led_brightness,
                              led_r_right, led_g_right, led_b_right,
                              led_r_left, led_g_left, led_b_left);
            led_repeat_next = now_ticks + 60;
        }
        /* LEDs reactivos: atenuar al entrar en dim, restaurar al salir. */
        if (dim_active && !led_dimmed) {
            int dim_b = led_brightness / 8;
            if (dim_b < 5) dim_b = 5;
            send_led_payload(dim_b, led_r_right, led_g_right, led_b_right,
                              led_r_left, led_g_left, led_b_left);
            led_dimmed = true;
        } else if (!dim_active && led_dimmed) {
            send_led_payload(led_brightness, led_r_right, led_g_right, led_b_right,
                              led_r_left, led_g_left, led_b_left);
            led_dimmed = false;
        }
        /* LEDs reactivos: alerta bateria critica (<=15%, no cargando, sin dim activo) */
        if (!dim_active && status_battery > 0 && status_battery <= 15 && !s_status_charging) {
            if (led_lowbat_timer == 0 || now_ticks - led_lowbat_timer > 1000) {
                bool pulse = (now_ticks / 1000) % 2 == 0;
                if (pulse) {
                    send_led_payload(led_brightness, 255, 0, 0, 255, 0, 0);
                } else {
                    send_led_payload(0, 0, 0, 0, 0, 0, 0);
                }
                led_lowbat_timer = now_ticks;
            }
        }

        /* Creacion de backup async (B01/B02 pattern): tar corre en
         * background, se sondea cada frame sin bloquear la UI. */
        if (backup_creating) {
            if (!backup_create_frame_shown) {
                backup_create_frame_shown = true;
                start_backup_async(backup_created_name, sizeof(backup_created_name));
            } else {
                int r = poll_backup_progress();
                if (r != 0) {
                    backup_creating = false;
                    backup_msg_until = SDL_GetTicks() + 3000;
                    if (r < 0) backup_created_name[0] = '\0'; /* error: sin nombre que mostrar */
                }
            }
        }
        /* Lógica OTA (async: fork+exec en background, sondeo por frame,
         * la UI nunca bloquea y la animacion de puntos avanza de verdad) */
        if (state == STATE_UPDATE) {
            if (update_phase == UPD_CHECKING && !update_checked && !upd_check_frame_shown) {
                upd_check_frame_shown = true;
                upd_check_start = now_ticks;
                start_check_update_async();
            } else if (update_phase == UPD_CHECKING && !update_checked) {
                int r = poll_curl_pid(&s_checkjson_pid, CHECK_JSON_TMP, 1);
                if (r != 0) {
                    update_checked = true;
                    if (r > 0) {
                        int res = finish_check_update(CHECK_JSON_TMP, s_version,
                                               upd_new_ver, sizeof(upd_new_ver),
                                               upd_dl_url,  sizeof(upd_dl_url),
                                               upd_sha_url, sizeof(upd_sha_url));
                        if (res == 1)       update_phase = UPD_CONFIRM;
                        else if (res == 0)  update_phase = UPD_NO_UPDATE;
                        else { update_phase = UPD_ERROR;
                               safe_copy(upd_msg, tr("Error al conectar con el servidor.", "Error connecting to server."), sizeof(upd_msg)); }
                    } else {
                        update_phase = UPD_ERROR;
                        safe_copy(upd_msg, tr("Error al conectar con el servidor.", "Error connecting to server."), sizeof(upd_msg));
                    }
                }
            }
            if (update_phase == UPD_DOWNLOADING) {
                if (upd_progress == 0.0f) {
                    download_update(upd_dl_url, &upd_progress);
                    upd_progress = 0.001f; /* centinela: descarga iniciada */
                    /* Dar tiempo a curl para arrancar antes de comprobar progreso */
                    SDL_Delay(500);
                } else {
                    float p = get_download_progress(upd_dl_url);
                    if (p >= 0.0f) upd_progress = p;
                    if (p == 1.0f) update_phase = UPD_VERIFYING;
                    if (p < 0.0f && upd_progress > 0.0f) {
                        update_phase = UPD_ERROR;
                        safe_copy(upd_msg, tr("Error en la descarga.", "Download error."), sizeof(upd_msg));
                    }
                }
            }
            if (update_phase == UPD_VERIFYING) {
                if (upd_sha_url[0]) {
                    if (!upd_verify_dl_started) {
                        upd_verify_dl_started = true;
                        s_sha_pid = spawn_curl_to_file(upd_sha_url, UPDATE_SHA256, "10");
                    } else {
                        int r = poll_curl_pid(&s_sha_pid, UPDATE_SHA256, 1);
                        if (r != 0) {
                            if (verify_sha256() == 0) {
                                write_update_flag();
                                update_phase = UPD_READY;
                            } else {
                                update_phase = UPD_ERROR;
                                safe_copy(upd_msg, tr("Error de verificación SHA256.", "SHA256 verification error."), sizeof(upd_msg));
                                unlink(UPDATE_IMG);
                                unlink(UPDATE_SHA256);
                                s_last_download_url[0] = '\0';
                            }
                        }
                    }
                } else {
                    /* sin sha_url: verify_sha256() rechazara igualmente (B06) */
                    if (verify_sha256() == 0) {
                        write_update_flag();
                        update_phase = UPD_READY;
                    } else {
                        update_phase = UPD_ERROR;
                        safe_copy(upd_msg, tr("Error de verificación SHA256.", "SHA256 verification error."), sizeof(upd_msg));
                        unlink(UPDATE_IMG);
                        unlink(UPDATE_SHA256);
                        s_last_download_url[0] = '\0';
                    }
                }
            }
        }

        if ((state == STATE_MENU || state == STATE_SYSINFO) &&
            (last_cpu_temp_sample == 0 || now_ticks - last_cpu_temp_sample > 1000)) {
            read_cpu_temp(dash_cpu_temp, sizeof(dash_cpu_temp));
            safe_copy(sysinfo_temp, dash_cpu_temp, sizeof(sysinfo_temp));
            { int td = 0; if (read_sysfs_int("/sys/class/thermal/thermal_zone2/temp", &td)) sysinfo_temp_pct = td / 1000; if (sysinfo_temp_pct > 100) sysinfo_temp_pct = 100; }
            last_cpu_temp_sample = now_ticks;
        }
        if (state == STATE_MENU &&
            (last_menu_refresh == 0 || now_ticks - last_menu_refresh > 1000)) {
            read_disk_free_short("/media/amiga_data", menu_disk_free, sizeof(menu_disk_free));
            read_cpu_usage(dash_cpu_load, sizeof(dash_cpu_load), NULL);
            read_ram_usage(dash_ram, sizeof(dash_ram));
            last_menu_refresh = now_ticks;
        }
        if (state == STATE_SYSINFO &&
            (last_sysinfo_update == 0 || now_ticks - last_sysinfo_update > 1000)) {
            read_ip_address(dev_ip, sizeof(dev_ip));
            read_uptime(dev_uptime, sizeof(dev_uptime));
            read_ram_usage(dev_ram, sizeof(dev_ram));
            read_disk_usage("/media/amiga_data", sysinfo_disk_data, sizeof(sysinfo_disk_data));
            read_disk_usage("/", sysinfo_disk_root, sizeof(sysinfo_disk_root));
            read_cpu_usage(sysinfo_cpu_usage, sizeof(sysinfo_cpu_usage), &sysinfo_cpu_pct);
            read_loadavg(sysinfo_loadavg, sizeof(sysinfo_loadavg));
            read_wifi_signal(sysinfo_wifi_sig, sizeof(sysinfo_wifi_sig), &sysinfo_wifi_pct);
            read_mac_address(sysinfo_mac, sizeof(sysinfo_mac));
            snprintf(sysinfo_build, sizeof(sysinfo_build), "%s (%s)", s_build_date, s_build_number);
            {
                FILE *fm = fopen("/proc/meminfo", "r");
                if (fm) {
                    long mt = -1, ma = -1; char ln[128];
                    while (fgets(ln, sizeof(ln), fm)) {
                        if (!strncmp(ln, "MemTotal:",    9)) sscanf(ln, "MemTotal: %ld",    &mt);
                        if (!strncmp(ln, "MemAvailable:",13)) sscanf(ln, "MemAvailable: %ld",&ma);
                    }
                    fclose(fm);
                    if (mt > 0 && ma >= 0) sysinfo_ram_pct = (int)(100 * (mt - ma) / mt);
                }
            }
            {
                struct statvfs st;
                if (statvfs("/", &st) == 0 && st.f_blocks > 0)
                    sysinfo_disk_root_pct = (int)(100 - 100ULL * st.f_bfree / st.f_blocks);
            }
            {
                struct statvfs st;
                if (statvfs("/media/amiga_data", &st) == 0 && st.f_blocks > 0) {
                    sysinfo_disk_data_pct = (int)(100 - 100ULL * st.f_bfree / st.f_blocks);
                    unsigned long long free_mb = (unsigned long long)st.f_bfree * st.f_frsize / (1024*1024);
                    if (free_mb >= 1024) snprintf(sysinfo_data_free_str, sizeof(sysinfo_data_free_str), "%.1f GB", free_mb / 1024.0);
                    else                 snprintf(sysinfo_data_free_str, sizeof(sysinfo_data_free_str), "%llu MB", free_mb);
                }
            }
            last_sysinfo_update = now_ticks;
        }
        if (state == STATE_DEVMODE &&
            (dev_last_temp_sample == 0 || now_ticks - dev_last_temp_sample > 1000)) {
            int t = 0;
            if (read_sysfs_int("/sys/class/thermal/thermal_zone2/temp", &t))
                devmode_push_temp(t / 1000);
            read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &dev_cpu_cur_freq);
            dev_last_temp_sample = now_ticks;
        }
        if (state == STATE_BLUETOOTH_CONFIG &&
            (bt_last_poll == 0 || now_ticks - bt_last_poll > 1000)) {
            bt_device_count = bt_parse_devices(bt_devices, BT_MAX_DEVICES);
            read_bt_connected(bt_connected_mac, sizeof(bt_connected_mac), bt_connected_name, sizeof(bt_connected_name));
            /* El dispositivo conectado/emparejado ya se muestra en la
             * pildora de arriba: se excluye por completo de "disponibles"
             * (ni se dibuja ni es navegable ahi), evita informacion
             * duplicada y una fila fantasma. */
            if (bt_connected_mac[0]) {
                for (int bi = 0; bi < bt_device_count; bi++) {
                    if (!strcmp(bt_devices[bi].mac, bt_connected_mac)) {
                        memmove(&bt_devices[bi], &bt_devices[bi + 1],
                                sizeof(BTDevice) * (bt_device_count - bi - 1));
                        bt_device_count--;
                        break;
                    }
                }
            }
            if (bt_scanning && now_ticks - bt_scan_start > 10000)
                bt_scanning = false;
            if (!bt_scanning && !bt_connecting && now_ticks - bt_scan_start > 15000) {
                bt_scanning = true;
                bt_scan_start = now_ticks;
                system("armiga-bt-scan >/dev/null 2>&1 &");
            }
            /* Chequeo ligero (sin escaneo activo) del conectado, mas
             * frecuente que el ciclo completo de arriba, para que una
             * desconexion se refleje antes en la pildora/fila. Nunca se
             * solapa con escaneo o conexion en curso. */
            if (!bt_scanning && !bt_connecting && bt_enabled &&
                (bt_light_check_trigger == 0 || now_ticks - bt_light_check_trigger > 4000)) {
                system("armiga-bt-connected-check >/dev/null 2>&1 &");
                bt_light_check_trigger = now_ticks;
            }
            if (bt_connecting) {
                FILE *fc = fopen("/tmp/armiga-bt-connect.log", "r");
                if (fc) {
                    char buf[4096];
                    size_t rd = fread(buf, 1, sizeof(buf) - 1, fc);
                    buf[rd] = 0;
                    fclose(fc);
                    /* "Failed to pair: ...AlreadyExists" es benigno: el
                     * dispositivo ya estaba emparejado de una sesion previa,
                     * el flujo de trust/connect continua y suele acabar en
                     * exito unos segundos despues. No cortar el polling por
                     * este mensaje concreto (confirmado en log real). */
                    bool bt_pair_failed_real = strstr(buf, "Failed to pair") && !strstr(buf, "AlreadyExists");
                    if (strstr(buf, "Connection successful")) {
                        safe_copy(bt_connect_status, tr("Conectado", "Connected"), sizeof(bt_connect_status));
                        safe_copy(bt_connected_mac, bt_connect_target_mac, sizeof(bt_connected_mac));
                        set_retroarch_audio_device(bt_connect_target_mac);
                        bt_connecting = false;
                    } else if (strstr(buf, "Failed to connect") || bt_pair_failed_real) {
                        safe_copy(bt_connect_status, tr("Fallo de conexion", "Connection failed"), sizeof(bt_connect_status));
                        bt_connecting = false;
                    }
                }
                if (bt_connecting && now_ticks - bt_connect_start > 20000) {
                    safe_copy(bt_connect_status, tr("Tiempo agotado", "Timed out"), sizeof(bt_connect_status));
                    bt_connecting = false;
                }
            }
            if (bt_selected >= bt_device_count) bt_selected = bt_device_count > 0 ? bt_device_count - 1 : 0;
            bt_last_poll = now_ticks;
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

        draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);

        /* Menú */
        SDL_Color c_menu_gold  = {27, 39, 8, 255};
        SDL_Color c_menu_beige = {231, 239, 231, 255};
        SDL_Color c_menu_selbg = {183, 221, 91, 255};
        {
            float target_y = menu_y0 + selected * item_h;
            menu_cursor_y = target_y;
        }
        for (int i = 0; i < MENU_COUNT; i++) {
            float iy = menu_y0 + i * item_h;
            int label_w = 0, label_h = 0;
            TTF_GetStringSize(f_med, MENU_ITEMS[i][current_lang], 0, &label_w, &label_h);
            if (i == selected) {
                float sel_w = 46.0f + (float)label_w + 32.0f; /* icono+texto, aire lateral moderado */
                float pill_h = item_h - 4.0f;
                float pill_radius = pill_h / 2.0f;
                draw_rounded_rect_filled(ren, mx - 10.0f, menu_cursor_y - 5.0f,
                                 sel_w, pill_h, pill_radius, c_menu_selbg);
                if (menu_icon_tex[i]) {
                    /* Respiracion sutil: pulso senoidal 20px-23px, ~1.8s de ciclo */
                    float breath = (SDL_sinf((float)now_ticks * 0.0035f) + 1.0f) * 0.5f;
                    float icon_sz = 20.0f + (breath * 3.0f);
                    float icon_offset = (icon_sz - 20.0f) / 2.0f;
                    float icon_x = (mx + 8.0f) - icon_offset;
                    float icon_y = iy - icon_offset;
                    SDL_SetTextureColorMod(menu_icon_tex[i], c_menu_gold.r, c_menu_gold.g, c_menu_gold.b);
                    SDL_FRect icon_dst = {icon_x, icon_y, icon_sz, icon_sz};
                    SDL_RenderTexture(ren, menu_icon_tex[i], NULL, &icon_dst);
                }
                draw_text(ren, f_med, MENU_ITEMS[i][current_lang], c_menu_gold, mx + 46.0f, iy);
            } else {
                if (menu_icon_tex[i]) {
                    SDL_SetTextureColorMod(menu_icon_tex[i], c_menu_beige.r, c_menu_beige.g, c_menu_beige.b);
                    SDL_FRect icon_dst = {mx + 8.0f, iy, 20.0f, 20.0f};
                    SDL_RenderTexture(ren, menu_icon_tex[i], NULL, &icon_dst);
                }
                draw_text(ren, f_med, MENU_ITEMS[i][current_lang], c_menu_beige, mx + 46.0f, iy);
            }
            if (i == 1 && bg_update_available) {
                SDL_Color c_red = COL_RED;
                float sel_w = 46.0f + (float)label_w + 32.0f;
                float txt_x = (mx - 10.0f) + sel_w + 10.0f;
                const char *upd_txt = tr("[!] Nueva Actualización", "[!] New Update");
                draw_text(ren, f_sm, upd_txt, c_red, txt_x, iy);
            }
        }

        /* Panel derecho: contexto de la opcion seleccionada */
        {
            draw_text_truncated(ren, f_sm, MENU_ITEMS[selected][current_lang], c_green, rx, menu_y0, rx_max_w);
            /* Descripcion: reemplaza el separador de linea original por espacio,
             * y envuelve el texto completo sin truncar nunca. */
            char desc_flat[128];
            snprintf(desc_flat, sizeof(desc_flat), "%s", MENU_DESC[selected][current_lang]);
            for (char *p = desc_flat; *p; p++) if (*p == '\n') *p = ' ';
            int n_lines = draw_text_wrapped(ren, f_sm, desc_flat, c_gray,
                                             rx, menu_y0 + 18.0f, rx_max_w, 16.0f);
            /* Info adicional del sistema, extensible: anadir mas lineas aqui */
            char ctx_lines[4][64];
            int ctx_n = 0;
            snprintf(ctx_lines[ctx_n], sizeof(ctx_lines[ctx_n]), "%s: %s",
                     tr("Espacio libre", "Free space"), menu_disk_free);
            ctx_n++;
            snprintf(ctx_lines[ctx_n], sizeof(ctx_lines[ctx_n]), "%s: %s",
                     tr("Temp. CPU", "CPU temp"), dash_cpu_temp);
            ctx_n++;
            snprintf(ctx_lines[ctx_n], sizeof(ctx_lines[ctx_n]), "%s: %s",
                     tr("Carga CPU", "CPU load"), dash_cpu_load);
            ctx_n++;
            snprintf(ctx_lines[ctx_n], sizeof(ctx_lines[ctx_n]), "%s: %s",
                     tr("RAM", "RAM"), dash_ram);
            ctx_n++;
            float ctx_y = menu_y0 + 18.0f + (float)n_lines * 16.0f + 20.0f;
            float ctx_box_pad = 10.0f;
            float ctx_max_line_w = 0.0f;
            for (int ci = 0; ci < ctx_n; ci++) {
                int clw = 0, clh = 0;
                TTF_GetStringSize(f_sm, ctx_lines[ci], 0, &clw, &clh);
                if ((float)clw > ctx_max_line_w) ctx_max_line_w = (float)clw;
            }
            float ctx_box_x = rx - ctx_box_pad;
            float ctx_box_y = ctx_y - ctx_box_pad;
            float ctx_box_w = ctx_max_line_w + ctx_box_pad * 2.0f;
            float ctx_box_h = (float)ctx_n * 16.0f + ctx_box_pad * 2.0f;
            SDL_Color c_ctx_box_bg = COL_BG;
            SDL_Color c_ctx_box_border = {183, 221, 91, 255};
            draw_rounded_rect_outline(ren, ctx_box_x, ctx_box_y, ctx_box_w, ctx_box_h,
                                       10.0f, 2.0f, c_ctx_box_border, c_ctx_box_bg);
            draw_context_panel(ren, f_sm, rx, ctx_y, ctx_lines, ctx_n, c_dkgreen);
        }

        /* Barra inferior */
        draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
        draw_footer(ren, f_sm, tr("[B] Seleccionar  [DPAD] Navegar  [L1] Idioma  [MODE+A] Sonido", "[B] Select  [DPAD] Navigate  [L1] Language  [MODE+A] Sound"), s_version);

        /* Barra de progreso del hold de modo dev (si se está manteniendo) */
        if (devmode_combo_held && devmode_hold_start != 0) {
            Uint64 elapsed = SDL_GetTicks() - devmode_hold_start;
            float frac = (float)elapsed / (float)DEVMODE_HOLD_MS;
            if (frac > 1.0f) frac = 1.0f;
            float bar_w = 200.0f;
            float bar_x = (SCREEN_W - bar_w) / 2.0f;
            float bar_y = SCREEN_H - 64.0f;
            SDL_Color c_devbar_lime = {183, 221, 91, 255};
            draw_bar_rounded(ren, bar_x, bar_y, bar_w, 4.0f, frac, c_devbar_lime, c_white);
        }

        } else if (state == STATE_SETTINGS) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 2, tr("Configuración", "Settings"));

            float settings_y0 = 64.0f;
            float settings_item_h = 32.0f;
            int settings_visible = 11;
            int settings_scroll = 0;
            if (settings_selected >= settings_visible)
                settings_scroll = settings_selected - settings_visible + 1;
            if (settings_scroll > SETTINGS_MENU_COUNT - settings_visible)
                settings_scroll = SETTINGS_MENU_COUNT - settings_visible;
            if (settings_scroll < 0) settings_scroll = 0;
            {
                float target_y = settings_y0 + (settings_selected - settings_scroll) * settings_item_h;
                settings_cursor_y = target_y;
            }
            for (int row = 0; row < settings_visible && (row + settings_scroll) < SETTINGS_MENU_COUNT; row++) {
                int i = row + settings_scroll;
                float iy = settings_y0 + row * settings_item_h;
                char item_label[64];
                if (i == 6) {
                    snprintf(item_label, sizeof(item_label), "%s: %s",
                             SETTINGS_MENU_ITEMS[i][current_lang],
                             ssh_enabled ? tr("Activado", "Enabled") : tr("Desactivado", "Disabled"));
                } else if (i == 7) {
                    snprintf(item_label, sizeof(item_label), "%s: %s",
                             SETTINGS_MENU_ITEMS[i][current_lang],
                             samba_enabled ? tr("Activado", "Enabled") : tr("Desactivado", "Disabled"));
                } else if (i == 10) {
                    snprintf(item_label, sizeof(item_label), "%s: %s",
                             SETTINGS_MENU_ITEMS[i][current_lang],
                             refresh_120hz ? "120Hz" : "60Hz");
                } else {
                    safe_copy(item_label, SETTINGS_MENU_ITEMS[i][current_lang], sizeof(item_label));
                }
                if (i == settings_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_med, item_label, 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 32.0f;
                    float pill_h = settings_item_h - 4.0f;
                    draw_rounded_rect_filled(ren, mx - 10.0f, settings_cursor_y - 5.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                    draw_text(ren, f_med, item_label, c_menu_gold, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_med, item_label, c_menu_beige, mx + 8.0f, iy);
                }
            }
            if (settings_scroll + settings_visible < SETTINGS_MENU_COUNT) {
                draw_text(ren, f_xs, tr("más abajo ↓", "more below ↓"), c_menu_selbg,
                          mx + 8.0f, settings_y0 + settings_visible * settings_item_h + 2.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[B] Seleccionar  [A] Volver", "[B] Select  [A] Back"), s_version);

        } else if (state == STATE_BRIGHTNESS_CONFIG) {
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Brillo de pantalla", "Screen Brightness"));

            {
                float iy = 90.0f;
                float bar_w = 220.0f, bar_h = 10.0f;
                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d%%", brightness_pct);
                draw_text(ren, f_sm, tr("Brillo", "Brightness"), c_green, mx + 8.0f, iy);
                draw_text(ren, f_med, valbuf, c_white, mx + 8.0f, iy + 16.0f);
                float frac = brightness_pct / 100.0f;
                SDL_Color c_bar_lime = {183, 221, 91, 255};
                draw_bar_rounded(ren, mx + 8.0f, iy + 44.0f, bar_w, bar_h, frac, c_bar_lime, c_white);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[<>] Ajustar  [B] Aplicar  [A] Volver", "[<>] Adjust  [B] Apply  [A] Back"), s_version);

        } else if (state == STATE_PERF_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Rendimiento", "Performance"));
            struct { const char *title[2]; const char *desc[2]; SDL_Texture *icon; } perf_opts[3] = {
                {{"Rendimiento máximo", "Maximum performance"},
                 {"CPU y GPU siempre a máxima\nfrecuencia. Mayor consumo.",
                  "CPU and GPU always at max\nfrequency. Higher consumption."},
                 perf_bolt_tex},
                {{"Equilibrado", "Balanced"},
                 {"CPU adaptativa, GPU a máxima\nfrecuencia. Buen balance.",
                  "Adaptive CPU, max GPU\nfrequency. Good balance."},
                 perf_scale_tex},
                {{"Ahorro de batería", "Battery saver"},
                 {"CPU y GPU a frecuencia\nminima. Mayor autonomia.",
                  "CPU and GPU at minimum\nfrequency. Longer battery life."},
                 perf_battery_tex},
            };
            float perf_item_h = 58.0f;
            float perf_y0 = 66.0f + (372.0f - 3.0f * perf_item_h) / 2.0f;
            float perf_w = 480.0f;
            float perf_x = (SCREEN_W - perf_w) / 2.0f;
            {
                float target_y = perf_y0 + perf_selected * perf_item_h;
                perf_cursor_y = target_y;
            }
            for (int i = 0; i < 3; i++) {
                float iy = perf_y0 + i * perf_item_h;
                bool sel = (i == perf_selected);
                SDL_Color titlec = sel ? c_menu_gold : c_menu_beige;
                float pill_h2 = perf_item_h - 8.0f;
                char desc_flat[128];
                snprintf(desc_flat, sizeof(desc_flat), "%s", perf_opts[i].desc[current_lang]);
                for (char *p = desc_flat; *p; p++) if (*p == '\n') *p = ' ';
                int tw = 0, th = 0;
                TTF_GetStringSize(f_med, perf_opts[i].title[current_lang], 0, &tw, &th);
                if (perf_desc_cached_lang != current_lang) {
                    for (int pj = 0; pj < 3; pj++) {
                        char desc_flat_pj[128];
                        snprintf(desc_flat_pj, sizeof(desc_flat_pj), "%s", perf_opts[pj].desc[current_lang]);
                        for (char *p = desc_flat_pj; *p; p++) if (*p == '\n') *p = ' ';
                        perf_desc_lines_cached[pj] = measure_text_wrapped(f_sm, desc_flat_pj, perf_w - 30.0f, &perf_desc_max_line_w_cached[pj]);
                    }
                    perf_desc_cached_lang = current_lang;
                }
                float desc_max_line_w = perf_desc_max_line_w_cached[i];
                int desc_line_count = perf_desc_lines_cached[i];
                float content_w = (tw > desc_max_line_w) ? (float)tw : desc_max_line_w;
                /* Geometria real: texto arranca en perf_x+38, pill arranca en perf_x-10 */
                float text_left_pad = 38.0f - (-10.0f); /* = 48: desde el borde del pill hasta el texto */
                float pill_w2 = text_left_pad + content_w + 14.0f; /* margen derecho */
                if (pill_w2 > perf_w + 10.0f) pill_w2 = perf_w + 10.0f;
                /* Alto real: icono ocupa iy..iy+24, texto ocupa iy..iy+20+desc_line_count*15 */
                float icon_bottom = 24.0f;
                float text_bottom = 20.0f + (float)desc_line_count * 15.0f;
                float content_bottom = (icon_bottom > text_bottom) ? icon_bottom : text_bottom;
                float pill_h3 = content_bottom + 16.0f;
                if (sel) {
                    draw_rounded_rect_filled(ren, perf_x - 10.0f, perf_cursor_y - 8.0f,
                                     pill_w2, pill_h3, pill_h3 / 2.0f, c_menu_selbg);
                }
                if (perf_opts[i].icon) {
                    SDL_SetTextureColorMod(perf_opts[i].icon, titlec.r, titlec.g, titlec.b);
                    SDL_FRect icon_dst = {perf_x + 4.0f, iy, 24.0f, 24.0f};
                    SDL_RenderTexture(ren, perf_opts[i].icon, NULL, &icon_dst);
                }
                draw_text(ren, f_med, perf_opts[i].title[current_lang], titlec, perf_x + 38.0f, iy);
                draw_text_wrapped(ren, f_sm, desc_flat, titlec, perf_x + 38.0f, iy + 20.0f, perf_w - 30.0f, 15.0f);
            }
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[DPAD] Elegir  [B] Aplicar  [A] Volver", "[DPAD] Choose  [B] Apply  [A] Back"), s_version);

        } else if (state == STATE_BLUETOOTH_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            SDL_Color c_bt_card    = {183, 221, 91, 255};
            SDL_Color c_bt_dim     = {90, 84, 66, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Bluetooth", "Bluetooth"));
            {
                float toggle_y = 64.0f;
                float toggle_h = 34.0f;
                int lw = 0, lh = 0;
                TTF_GetStringSize(f_med, "Bluetooth", 0, &lw, &lh);
                const char *bt_status = bt_enabled ? tr("ACTIVADO", "ENABLED") : tr("DESACTIVADO", "DISABLED");
                int sw = 0, sh = 0;
                TTF_GetStringSize(f_sm, bt_status, 0, &sw, &sh);
                float badge_pad = 10.0f;
                float badge_w = (float)sw + badge_pad * 2.0f;
                float badge_h = (float)sh + 6.0f;
                float label_pad = 16.0f;
                float badge_gap = 16.0f;
                float toggle_w = label_pad + (float)lw + badge_gap + badge_w + 12.0f;
                draw_rounded_rect_filled(ren, mx, toggle_y, toggle_w, toggle_h, toggle_h / 2.0f, c_bt_card);
                draw_text(ren, f_med, "Bluetooth", c_menu_gold, mx + label_pad, toggle_y + 8.0f);
                float badge_x = mx + toggle_w - 12.0f - badge_w;
                float badge_y = toggle_y + (toggle_h - badge_h) / 2.0f;
                SDL_Color badge_bg = bt_enabled ? (SDL_Color){15, 31, 24, 255} : (SDL_Color){28, 52, 40, 255};
                draw_rounded_rect_filled(ren, badge_x, badge_y, badge_w, badge_h, badge_h / 2.0f, badge_bg);
                SDL_Color bt_status_c = bt_enabled ? c_green : c_white;
                draw_text(ren, f_sm, bt_status, bt_status_c, badge_x + badge_pad, badge_y + 3.0f);
                if (bt_connected_mac[0] && bt_connected_name[0]) {
                    char paired_buf[80];
                    snprintf(paired_buf, sizeof(paired_buf), "%s %s", tr("Conectado a", "Connected to"), bt_connected_name);
                    int pw = 0, ph = 0;
                    TTF_GetStringSize(f_sm, paired_buf, 0, &pw, &ph);
                    float pbadge_pad = 12.0f;
                    float pbadge_max_w = SCREEN_W - mx - (mx + toggle_w + 16.0f);
                    float pbadge_w = (float)pw + pbadge_pad * 2.0f;
                    if (pbadge_w > pbadge_max_w) pbadge_w = pbadge_max_w;
                    float pbadge_h = toggle_h;
                    draw_rounded_rect_filled(ren, mx + toggle_w + 16.0f, toggle_y, pbadge_w, pbadge_h, pbadge_h / 2.0f, c_bt_card);
                    draw_text_truncated(ren, f_sm, paired_buf, c_menu_gold, mx + toggle_w + 16.0f + pbadge_pad, toggle_y + (pbadge_h - (float)ph) / 2.0f,
                                         pbadge_w - pbadge_pad * 2.0f);
                }
            }
            if (!bt_enabled) {
                draw_text(ren, f_sm, tr("Bluetooth desactivado", "Bluetooth disabled"), c_menu_beige, mx, 116.0f);
            } else {
                draw_text(ren, f_sm, tr("DISPOSITIVOS DISPONIBLES", "AVAILABLE DEVICES"), c_menu_selbg, mx, 112.0f);
                float bt_y0 = 134.0f;
                float bt_item_h = 30.0f;
                int bt_visible = 9;
                bool bt_has_more = bt_device_count > bt_visible;
                /* Si hay mas de los que caben, se reserva la ultima fila
                 * solo para el indicador "+N mas" (nunca comparte fila con
                 * un dispositivo real, evita solapar con su dBm). */
                int bt_list_rows = bt_has_more ? bt_visible - 1 : bt_visible;
                int bt_scroll = 0;
                if (bt_selected >= bt_list_rows)
                    bt_scroll = bt_selected - bt_list_rows + 1;
                if (bt_scroll > bt_device_count - bt_list_rows)
                    bt_scroll = bt_device_count - bt_list_rows;
                if (bt_scroll < 0) bt_scroll = 0;
                {
                    float target_y = bt_y0 + (bt_selected - bt_scroll) * bt_item_h;
                    bt_cursor_y = target_y;
                }
                for (int row = 0; row < bt_list_rows && (row + bt_scroll) < bt_device_count; row++) {
                    int i = row + bt_scroll;
                    float iy = bt_y0 + row * bt_item_h;
                    float row_h = bt_item_h - 4.0f;
                    const char *label = bt_devices[i].name[0] ? bt_devices[i].name : bt_devices[i].mac;
                    bool sel = (i == bt_selected);
                    bool connected = (bt_connected_mac[0] && !strcmp(bt_connected_mac, bt_devices[i].mac));
                    if (connected) {
                        /* Sin capsula de fondo aqui: el estado de
                         * emparejado/conectado ya se muestra en la pildora
                         * de arriba; el texto en verde/dorado de la fila
                         * basta como indicador, evita informacion duplicada. */
                    } else if (sel) {
                        int lbl_w = 0, lbl_h = 0;
                        TTF_GetStringSize(f_sm, label, 0, &lbl_w, &lbl_h);
                        float pill_pad = 10.0f;
                        float pill_x = mx + 4.0f - pill_pad;
                        float pill_w = (float)lbl_w + pill_pad * 2.0f;
                        draw_rounded_rect_filled(ren, pill_x, bt_cursor_y - 4.0f, pill_w, row_h, row_h / 2.0f, c_menu_selbg);
                    }
                    SDL_Color labelc = connected ? c_menu_gold : (sel ? c_menu_gold : c_menu_beige);
                    draw_text(ren, f_sm, label, labelc, mx + 4.0f, iy);
                    if (connected) {
                        /* Sin badge aqui: ya se muestra "Emparejado: <nombre>"
                         * en la pildora de arriba, este texto era redundante. */
                    } else if (bt_devices[i].has_rssi) {
                        char rbuf[16];
                        snprintf(rbuf, sizeof(rbuf), "%d dBm", bt_devices[i].rssi);
                        draw_text_right(ren, f_sm, rbuf, c_menu_selbg, SCREEN_W - mx - 6.0f, iy);
                    }
                }
                if (bt_scroll + bt_list_rows < bt_device_count) {
                    char more_buf[32];
                    snprintf(more_buf, sizeof(more_buf), "+ %d %s", bt_device_count - (bt_scroll + bt_list_rows), tr("dispositivos mas", "more devices"));
                    draw_text(ren, f_xs, more_buf, c_menu_selbg, mx + 4.0f, bt_y0 + bt_list_rows * bt_item_h + 4.0f);
                }
                if (bt_device_count == 0 && !bt_scanning) {
                    draw_text(ren, f_sm, tr("Ningun dispositivo encontrado", "No devices found"), c_menu_beige, mx, bt_y0);
                }
                if (bt_connecting) {
                    char cbuf[96];
                    snprintf(cbuf, sizeof(cbuf), "%s %s...", tr("Conectando a", "Connecting to"), bt_devices[bt_selected].name[0] ? bt_devices[bt_selected].name : bt_devices[bt_selected].mac);
                    draw_text(ren, f_sm, cbuf, c_menu_selbg, mx, 392.0f);
                } else if (bt_connect_status[0]) {
                    draw_text(ren, f_sm, bt_connect_status, c_menu_selbg, mx, 392.0f);
                }
                if (bt_scanning) {
                    float sp_cx = mx + 6.0f;
                    float sp_cy = 423.0f;
                    float sp_r = 6.0f;
                    int active = (int)((now_ticks / 100) % 8);
                    for (int d = 0; d < 8; d++) {
                        float ang = (float)d * (2.0f * 3.14159265f / 8.0f);
                        float dx = sp_cx + sp_r * SDL_cosf(ang);
                        float dy = sp_cy + sp_r * SDL_sinf(ang);
                        int dist = (d - active + 8) % 8;
                        float sp_t = (float)(7 - dist) / 7.0f; /* 0=mas tenue, 1=punto activo */
                        SDL_Color dotc = {
                            (Uint8)(60 + sp_t * (183 - 60)),
                            (Uint8)(80 + sp_t * (221 - 80)),
                            (Uint8)(50 + sp_t * (91 - 50)),
                            255
                        };
                        draw_rect_filled(ren, dx - 1.5f, dy - 1.5f, 3.0f, 3.0f, dotc);
                    }
                    draw_text(ren, f_sm, tr("Buscando dispositivos...", "Searching for devices..."), c_menu_beige, mx + 20.0f, 416.0f);
                }
            }
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[DPAD] Elegir  [B] Conectar  [SELECT] Activar  [A] Volver", "[DPAD] Choose  [B] Connect  [SELECT] Toggle  [A] Back"), s_version);
        } else if (state == STATE_TIMEZONE_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Zona horaria", "Time Zone"));

            float tz_y0 = 60.0f;
            float tz_item_h = 20.0f;
            int tz_visible = 18;
            int tz_scroll = 0;
            if (timezone_selected >= tz_visible)
                tz_scroll = timezone_selected - tz_visible + 1;
            if (tz_scroll > TIMEZONE_LIST_COUNT - tz_visible)
                tz_scroll = TIMEZONE_LIST_COUNT - tz_visible;
            if (tz_scroll < 0) tz_scroll = 0;

            {
                float target_y = tz_y0 + (timezone_selected - tz_scroll) * tz_item_h;
                tz_cursor_y = target_y;
            }

            for (int row = 0; row < tz_visible && (row + tz_scroll) < TIMEZONE_LIST_COUNT; row++) {
                int i = row + tz_scroll;
                float iy = tz_y0 + row * tz_item_h;
                bool sel = (i == timezone_selected);
                bool active = !strcmp(TIMEZONE_LIST[i].tz_name, timezone_current);
                int text_w = 0, text_h = 0;
                TTF_GetStringSize(f_sm, TIMEZONE_LIST[i].label[current_lang], 0, &text_w, &text_h);
                if (sel) {
                    float sel_w = (float)text_w + 42.0f;
                    float pill_h = tz_item_h + 2.0f;
                    draw_rounded_rect_filled(ren, mx - 10.0f, tz_cursor_y - 3.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                draw_text(ren, f_sm, TIMEZONE_LIST[i].label[current_lang], labelc, mx + 8.0f, iy);
                if (active)
                    draw_text(ren, f_sm, "✓", sel ? c_menu_gold : c_menu_selbg, mx + 8.0f + (float)text_w + 8.0f, iy);
            }

            /* Panel derecho: hora en vivo de la zona resaltada por el cursor */
            {
                float tzp_x = mx + mw + 30.0f;
                if (timezone_selected != tz_preview_last_sel || now_ticks - tz_preview_last_time > 1000) {
                    get_time_in_tz(TIMEZONE_LIST[timezone_selected].tz_name, tz_preview_buf, sizeof(tz_preview_buf));
                    tz_preview_last_sel = timezone_selected;
                    tz_preview_last_time = now_ticks;
                }
                draw_text(ren, f_sm, tr("Hora actual", "Current time"), c_menu_beige, tzp_x, tz_y0);
                draw_text(ren, f_lg, tz_preview_buf, c_menu_selbg, tzp_x, tz_y0 + 22.0f);
                draw_text(ren, f_sm, TIMEZONE_LIST[timezone_selected].label[current_lang], c_menu_beige, tzp_x, tz_y0 + 66.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[B] Aplicar  [A] Volver  [L1/R1] Salto x5", "[B] Apply  [A] Back  [L1/R1] Jump x5"), s_version);

        } else if (state == STATE_SCREENDIM_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Ahorro de pantalla", "Screen Dimming"));

            float dim_y0 = 70.0f;
            float dim_item_h = 46.0f;
            float dim_bar_w = 220.0f;
            float dim_bar_h = 10.0f;

            {
                float target_y = dim_y0 + dim_field_selected * dim_item_h;
                dim_cursor_y = target_y;
            }

            {
                float iy = dim_y0;
                bool sel = (dim_field_selected == 0);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                const char *dim_val_disp = DIM_TIMEOUT_LABELS[dim_timeout_selected][current_lang];
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, tr("Atenuar tras", "Dim after"), 0, &lw, &lh);
                    TTF_GetStringSize(f_med, dim_val_disp, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 40.0f;
                    float pill_h = dim_item_h - 2.0f;
                    draw_rounded_rect_filled(ren, mx - 14.0f, dim_cursor_y - 4.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, tr("Atenuar tras", "Dim after"), labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, dim_val_disp, labelc, mx + 8.0f, iy + 16.0f);
            }

            {
                float iy = dim_y0 + dim_item_h;
                bool sel = (dim_field_selected == 1);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                if (sel) {
                    int lw = 0, lh = 0;
                    TTF_GetStringSize(f_sm, tr("Brillo al atenuar", "Brightness when dimmed"), 0, &lw, &lh);
                    float bar_total_w = dim_bar_w + 10.0f + 40.0f; /* barra + gap + "100%" aprox */
                    float sel_w = ((float)lw > bar_total_w ? (float)lw : bar_total_w) + 40.0f;
                    float pill_h = dim_item_h + 2.0f;
                    draw_rounded_rect_filled(ren, mx - 14.0f, dim_cursor_y - 8.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, tr("Brillo al atenuar", "Brightness when dimmed"),
                          labelc, mx + 8.0f, iy);
                float bar_x = mx + 8.0f;
                float bar_y = iy + 20.0f;
                float frac = dim_percent / 100.0f;
                draw_bar_rounded(ren, bar_x, bar_y, dim_bar_w, dim_bar_h, frac, c_selbg, labelc);
                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d%%", dim_percent);
                draw_text(ren, f_sm, valbuf, labelc, bar_x + dim_bar_w + 10.0f, iy + 16.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[B] Guardar  [A] Volver", "[B] Save  [A] Back"), s_version);

        } else if (state == STATE_BACKUP_MENU) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Copia de seguridad", "Backup"));
            float bkm_y0 = 64.0f;
            float bkm_item_h = 34.0f;
            {
                float target_y = bkm_y0 + backup_selected * bkm_item_h;
                bkm_cursor_y = target_y;
            }
            for (int i = 0; i < BACKUP_MENU_COUNT; i++) {
                float iy = bkm_y0 + i * bkm_item_h;
                if (i == backup_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_med, BACKUP_MENU_ITEMS[i][current_lang], 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 32.0f;
                    float pill_h = bkm_item_h - 4.0f;
                    draw_rounded_rect_filled(ren, mx - 10.0f, bkm_cursor_y - 5.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                    draw_text(ren, f_med, BACKUP_MENU_ITEMS[i][current_lang], c_menu_gold, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_med, BACKUP_MENU_ITEMS[i][current_lang], c_menu_beige, mx + 8.0f, iy);
                }
            }
            if (backup_creating) {
                draw_text_animdots(ren, f_sm, tr("Generando copia de seguridad", "Creating backup"),
                          c_white, mx, bkm_y0 + BACKUP_MENU_COUNT * bkm_item_h + 20.0f, now_ticks);
            } else if (backup_msg_until > 0 && SDL_GetTicks() < backup_msg_until) {
                char msgbuf[128];
                SDL_Color msgc;
                SDL_Color c_red = COL_RED;
                if (backup_created_name[0]) {
                    snprintf(msgbuf, sizeof(msgbuf), "%s: %s%s",
                             tr("Copia creada", "Backup created"),
                             BACKUP_DIR "/", backup_created_name);
                    msgc = c_green;
                } else {
                    safe_copy(msgbuf, tr("Error al crear la copia de seguridad", "Error creating backup"), sizeof(msgbuf));
                    msgc = c_red;
                }
                draw_text_truncated(ren, f_sm, msgbuf,
                          msgc, mx, bkm_y0 + BACKUP_MENU_COUNT * bkm_item_h + 20.0f, SCREEN_W - 40.0f);
            }
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[B] Seleccionar  [A] Volver", "[B] Select  [A] Back"), s_version);

        } else if (state == STATE_BACKUP_LIST) {
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 4, tr("Restaurar copia", "Restore Backup"));
            float bkl_y0 = 64.0f;
            float bkl_item_h = 26.0f;
            if (backup_count == 0) {
                draw_text(ren, f_sm, tr("No hay copias disponibles", "No backups available"), c_gray, mx, bkl_y0);
            } else {
                {
                    float target_y = bkl_y0 + backup_list_selected * bkl_item_h;
                    bkl_cursor_y = target_y;
                }
                SDL_Color c_menu_gold = {27, 39, 8, 255};
                SDL_Color c_menu_selbg = {183, 221, 91, 255};
                for (int i = 0; i < backup_count; i++) {
                    float iy = bkl_y0 + i * bkl_item_h;
                    if (i == backup_list_selected) {
                        int text_w = 0, text_h = 0;
                        TTF_GetStringSize(f_sm, backup_list[i], 0, &text_w, &text_h);
                        float sel_w = (float)text_w + 32.0f;
                        float pill_h = 32.0f;
                        float pill_y = bkl_cursor_y - (pill_h - bkl_item_h) / 2.0f - 5.0f;
                        float text_y = pill_y + (pill_h - (float)text_h) / 2.0f;
                        draw_rounded_rect_filled(ren, mx - 10.0f, pill_y,
                                         sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                        draw_text(ren, f_sm, backup_list[i], c_menu_gold, mx + 8.0f, text_y);
                    } else {
                        draw_text(ren, f_sm, backup_list[i], c_gray, mx + 8.0f, iy);
                    }
                }
            }
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[B] Restaurar  [X] Eliminar  [A] Volver", "[B] Restore  [X] Delete  [A] Back"), s_version);

        } else if (state == STATE_AREXX_LIST) {
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 2, tr("ARexx Scripts", "ARexx Scripts"));
            float arx_y0 = 64.0f;
            float arx_item_h = 34.0f;
            if (arexx_count == 0) {
                draw_text(ren, f_sm, tr("No hay scripts disponibles", "No scripts available"), c_gray, mx, arx_y0);
            } else {
                SDL_Color c_menu_gold = {27, 39, 8, 255};
                SDL_Color c_menu_selbg = {183, 221, 91, 255};
                SDL_Color c_white_icon = {255, 255, 255, 255};
                float icon_size = 20.0f;
                float icon_gap = 26.0f;
                float pill_h_ref = 26.0f;
                float text_x = mx + icon_size + icon_gap;
                for (int i = 0; i < arexx_count; i++) {
                    float iy = arx_y0 + i * arx_item_h;
                    if (arexx_icon_tex) {
                        SDL_SetTextureColorMod(arexx_icon_tex, c_white_icon.r, c_white_icon.g, c_white_icon.b);
                        SDL_FRect icon_dst = {mx, iy + (pill_h_ref - icon_size) / 2.0f, icon_size, icon_size};
                        SDL_RenderTexture(ren, arexx_icon_tex, NULL, &icon_dst);
                    }
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_sm, arexx_scripts[i].filename, 0, &text_w, &text_h);
                    float row_h = 26.0f;
                    float text_y = iy + (row_h - (float)text_h) / 2.0f;
                    if (i == arexx_selected) {
                        float sel_w = (float)text_w + 32.0f;
                        float pill_x0 = text_x - 16.0f;
                        float text_draw_x = pill_x0 + (sel_w - (float)text_w) / 2.0f;
                        draw_rounded_rect_filled(ren, pill_x0, iy, sel_w, row_h, row_h / 2.0f, c_menu_selbg);
                        draw_text(ren, f_sm, arexx_scripts[i].filename, c_menu_gold, text_draw_x, text_y);
                    } else {
                        draw_text(ren, f_sm, arexx_scripts[i].filename, c_gray, text_x, text_y);
                    }
                }
                if (arexx_md5_cached_for != arexx_selected) {
                    compute_script_md5(arexx_scripts[arexx_selected].filename, arexx_md5, sizeof(arexx_md5));
                    arexx_md5_cached_for = arexx_selected;
                }
                char md5_line[64];
                snprintf(md5_line, sizeof(md5_line), "MD5: %s", arexx_md5);
                const char *desc_line = arexx_scripts[arexx_selected].desc[current_lang];
                float box_pad = 16.0f;
                int md5_w = 0, md5_h = 0;
                TTF_GetStringSize(f_sm, md5_line, 0, &md5_w, &md5_h);
                float box_w = (float)md5_w + box_pad * 2.0f;
                if (box_w < 240.0f) box_w = 240.0f;
                if (box_w > SCREEN_W - mx * 2.0f) box_w = SCREEN_W - mx * 2.0f;
                float desc_max_w = box_w - box_pad * 2.0f;
                float desc_line_h = 16.0f;
                float desc_max_line_w = 0.0f;
                if (arexx_desc_cached_for != arexx_selected || arexx_desc_cached_lang != current_lang) {
                    arexx_desc_lines_cached = measure_text_wrapped(f_sm, desc_line, desc_max_w, &arexx_desc_max_line_w_cached);
                    arexx_desc_cached_for = arexx_selected;
                    arexx_desc_cached_lang = current_lang;
                }
                int desc_lines = arexx_desc_lines_cached;
                desc_max_line_w = arexx_desc_max_line_w_cached;
                float box_h = box_pad + (float)desc_lines * desc_line_h + 6.0f + (float)md5_h + box_pad;
                float box_x = SCREEN_W - mx - box_w;
                float box_y = 438.0f - 12.0f - box_h;
                SDL_Color c_bg_box = COL_BG;
                draw_rounded_rect_outline(ren, box_x, box_y, box_w, box_h, 10.0f, 2.0f, c_menu_selbg, c_bg_box);
                draw_text_wrapped(ren, f_sm, desc_line, c_gray, box_x + box_pad, box_y + box_pad, desc_max_w, desc_line_h);
                draw_text(ren, f_sm, md5_line, c_gray, box_x + box_pad, box_y + box_pad + (float)desc_lines * desc_line_h + 6.0f);
            }
            char arx_counter[24];
            snprintf(arx_counter, sizeof(arx_counter), "%d %s %d", arexx_count, tr("de", "of"), AREXX_MAX_SCRIPTS);
            draw_text(ren, f_sm, arx_counter, c_gray, mx, 418.0f);
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[B] Ejecutar  [A] Volver", "[B] Run  [A] Back"), s_version);

        } else if (state == STATE_AREXX_RUN) {
            int arexx_still_running = (poll_arexx_script(&arexx_output, &arexx_output_len, &arexx_output_cap) == 0);
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Ejecutando Script", "Running Script"));
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_text(ren, f_sm, arexx_scripts[arexx_selected].filename, c_menu_selbg, mx, 60.0f);
            if (arexx_still_running) {
                int fname_w = 0, fname_h = 0;
                TTF_GetStringSize(f_sm, arexx_scripts[arexx_selected].filename, 0, &fname_w, &fname_h);
                draw_text_animdots(ren, f_xs, tr("Ejecutando", "Running"), c_gray, mx + (float)fname_w + 16.0f, 62.0f, SDL_GetTicks());
            }
            float arxr_y0 = 90.0f;
            float arxr_line_h = 16.0f;
            int arxr_nlines = 0;
            char **arxr_lines = NULL;
            char *arxr_tmp = NULL;
            if (arexx_output && arexx_output_len > 0) {
                arxr_tmp = malloc(arexx_output_len + 1);
                if (arxr_tmp) {
                    memcpy(arxr_tmp, arexx_output, arexx_output_len + 1);
                    int est_lines = 1;
                    for (size_t k = 0; k < arexx_output_len; k++)
                        if (arxr_tmp[k] == '\n') est_lines++;
                    arxr_lines = malloc(sizeof(char *) * (size_t)est_lines);
                    if (arxr_lines) {
                        char *tok = strtok(arxr_tmp, "\n");
                        while (tok && arxr_nlines < est_lines) {
                            arxr_lines[arxr_nlines++] = tok;
                            tok = strtok(NULL, "\n");
                        }
                    }
                }
            }
            int arxr_max_visible = (int)((438.0f - arxr_y0) / arxr_line_h);
            if (arxr_max_visible < 1) arxr_max_visible = 1;
            int arxr_max_scroll = (arxr_nlines <= arxr_max_visible) ? 0 : (arxr_nlines - arxr_max_visible);
            if (!arexx_user_scrolled) {
                arexx_scroll = arxr_max_scroll;
            } else if (arexx_scroll > arxr_max_scroll) {
                arexx_scroll = arxr_max_scroll;
            }
            int arxr_start = arexx_scroll;
            SDL_Color c_arxr_lime = {183, 221, 91, 255};
            SDL_Color c_arxr_red  = COL_RED;
            if (arxr_lines)
                for (int i = arxr_start; i < arxr_nlines && (i - arxr_start) < arxr_max_visible; i++) {
                    SDL_Color line_c = c_gray;
                    if (strstr(arxr_lines[i], "[CORRECTO]")) line_c = c_arxr_lime;
                    else if (strstr(arxr_lines[i], "[INCORRECTO]")) line_c = c_arxr_red;
                    draw_text(ren, f_xs, arxr_lines[i], line_c, mx, arxr_y0 + (i - arxr_start) * arxr_line_h);
                }
            if (!arexx_still_running && arxr_nlines > arxr_max_visible) {
                char scroll_info[32];
                snprintf(scroll_info, sizeof(scroll_info), "%d-%d/%d",
                         arxr_start + 1,
                         (arxr_start + arxr_max_visible < arxr_nlines) ? arxr_start + arxr_max_visible : arxr_nlines,
                         arxr_nlines);
                draw_text_right(ren, f_xs, scroll_info, c_gray, SCREEN_W - 20.0f, 70.0f);
            }
            free(arxr_lines);
            free(arxr_tmp);
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            if (!arexx_still_running && arxr_nlines > arxr_max_visible)
                draw_footer(ren, f_sm, tr("[DPAD] Navegar  [A] Volver", "[DPAD] Scroll  [A] Back"), s_version);
            else
                draw_footer(ren, f_sm, tr("[A] Volver", "[A] Back"), s_version);

        } else if (state == STATE_WIFI_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("Red inalámbrica", "Wireless Network"));

            float wifi_y0 = 64.0f;
            float wifi_item_h = 44.0f;

            {
                float target_y = wifi_y0 + wifi_field_selected * wifi_item_h;
                wifi_cursor_y = target_y;
            }

            {
                float iy = wifi_y0;
                bool sel = (wifi_field_selected == 0);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                const char *ssid_disp = wifi_ssid[0] ? wifi_ssid : "--";
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, "SSID", 0, &lw, &lh);
                    TTF_GetStringSize(f_med, ssid_disp, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 40.0f;
                    float pill_h = wifi_item_h + 4.0f;
                    draw_rounded_rect_filled(ren, mx - 14.0f, wifi_cursor_y - 8.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, "SSID", labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, ssid_disp, labelc, mx + 8.0f, iy + 16.0f);
            }

            {
                float iy = wifi_y0 + wifi_item_h;
                bool sel = (wifi_field_selected == 1);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                char masked[64];
                if (wifi_show_password || !wifi_password[0]) {
                    strncpy(masked, wifi_password[0] ? wifi_password : "--", sizeof(masked) - 1);
                    masked[sizeof(masked)-1] = 0;
                } else {
                    size_t len = strlen(wifi_password);
                    if (len >= sizeof(masked)) len = sizeof(masked) - 1;
                    for (size_t k = 0; k < len; k++) masked[k] = '*';
                    masked[len] = 0;
                }
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, tr("CONTRASEÑA", "PASSWORD"), 0, &lw, &lh);
                    TTF_GetStringSize(f_med, masked, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 40.0f;
                    float pill_h = wifi_item_h + 4.0f;
                    draw_rounded_rect_filled(ren, mx - 14.0f, wifi_cursor_y - 8.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, tr("CONTRASEÑA", "PASSWORD"), labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, masked, labelc, mx + 8.0f, iy + 16.0f);
            }

            {
                float iy = wifi_y0 + 2 * wifi_item_h;
                bool sel = (wifi_field_selected == 2);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                const char *wifi_status_disp = wifi_enabled ? tr("ACTIVADO", "ENABLED") : tr("DESACTIVADO", "DISABLED");
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, "WIFI", 0, &lw, &lh);
                    TTF_GetStringSize(f_med, wifi_status_disp, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 40.0f;
                    float pill_h = wifi_item_h + 4.0f;
                    draw_rounded_rect_filled(ren, mx - 14.0f, wifi_cursor_y - 8.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, "WIFI", labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, wifi_status_disp, labelc, mx + 8.0f, iy + 16.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[B] Editar/Alternar  [SELECT] Ver/Ocultar  [A] Guardar", "[B] Edit/Toggle  [SELECT] Show/Hide  [A] Save"),
                s_version);

        } else if (state == STATE_LED_CONFIG) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 3, tr("LEDs RGB analógicos", "Analog Stick LEDs"));

            static const char *LED_SLIDER_LABELS[][2] = {
                {"R (derecho)", "R (right)"},
                {"G (derecho)", "G (right)"},
                {"B (derecho)", "B (right)"},
                {"R (izquierdo)", "R (left)"},
                {"G (izquierdo)", "G (left)"},
                {"B (izquierdo)", "B (left)"},
                {"Brillo", "Brightness"},
            };
            int led_vals_r[LED_SLIDER_COUNT] = {
                led_r_right, led_g_right, led_b_right,
                led_r_left,  led_g_left,  led_b_left,
                led_brightness
            };
            SDL_Color led_bar_colors[LED_SLIDER_COUNT] = {
                {220, 60, 60, 255}, {60, 220, 60, 255}, {60, 60, 220, 255},
                {220, 60, 60, 255}, {60, 220, 60, 255}, {60, 60, 220, 255},
                c_gray
            };

            float led_y0 = 62.0f;
            float led_item_h = 34.0f;
            float led_bar_w = 220.0f;
            float led_bar_h = 10.0f;

            {
                float target_y = led_y0 + led_selected * led_item_h;
                led_cursor_y = target_y;
            }

            for (int i = 0; i < LED_SLIDER_COUNT; i++) {
                float iy = led_y0 + i * led_item_h;
                bool sel = (led_selected == i);
                SDL_Color labelc = sel ? c_menu_gold : c_menu_beige;
                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d", led_vals_r[i]);
                int valw = 0, valh = 0;
                TTF_GetStringSize(f_sm, valbuf, 0, &valw, &valh);
                float bar_x = mx + 180.0f;
                if (sel) {
                    float pill_h = led_item_h - 6.0f;
                    float pill_w = (bar_x - (mx - 10.0f)) + led_bar_w + 10.0f + (float)valw + 20.0f;
                    draw_rounded_rect_filled(ren, mx - 10.0f, led_cursor_y - 5.0f,
                                     pill_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                }
                draw_text(ren, f_sm, LED_SLIDER_LABELS[i][current_lang], labelc, mx + 8.0f, iy);

                float bar_y = iy + 3.0f;
                SDL_Color c_bar_empty = {20, 18, 14, 255};
                float frac = led_vals_r[i] / 255.0f;
                draw_bar_rounded(ren, bar_x, bar_y, led_bar_w, led_bar_h, frac, c_bar_empty, led_bar_colors[i]);

                draw_text(ren, f_sm, valbuf, labelc, bar_x + led_bar_w + 10.0f, iy);
            }

            SDL_Color preview_right = {(Uint8)led_r_right, (Uint8)led_g_right, (Uint8)led_b_right, 255};
            SDL_Color preview_left  = {(Uint8)led_r_left,  (Uint8)led_g_left,  (Uint8)led_b_left,  255};
            float preview_y = led_y0 + LED_SLIDER_COUNT * led_item_h + 16.0f;
            draw_text(ren, f_sm, tr("Vista previa", "Preview"), c_menu_beige, mx, preview_y);
            float sw_size = 60.0f;
            float sw_gap = 24.0f;
            float sw_y = preview_y + 22.0f;
            float sw_total_w = sw_size * 2.0f + sw_gap;
            float sw_left_x = (SCREEN_W - sw_total_w) / 2.0f;
            float sw_right_x = sw_left_x + sw_size + sw_gap;
            draw_rounded_rect_filled(ren, sw_left_x, sw_y, sw_size, 40.0f, 6.0f, preview_left);
            draw_rounded_rect_filled(ren, sw_right_x, sw_y, sw_size, 40.0f, 6.0f, preview_right);
            {
                int lw = 0, lh = 0;
                TTF_GetStringSize(f_sm, tr("Izquierdo", "Left"), 0, &lw, &lh);
                draw_text(ren, f_sm, tr("Izquierdo", "Left"), c_menu_beige, sw_left_x + (sw_size - (float)lw) / 2.0f, sw_y + 46.0f);
                TTF_GetStringSize(f_sm, tr("Derecho", "Right"), 0, &lw, &lh);
                draw_text(ren, f_sm, tr("Derecho", "Right"), c_menu_beige, sw_right_x + (sw_size - (float)lw) / 2.0f, sw_y + 46.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[<>] Ajustar  [L1/R1] +/-20  [A] Volver", "[<>] Adjust  [L1/R1] +/-20  [A] Back"),
                s_version);

        } else if (state == STATE_KEYBOARD) {
            draw_text(ren, f_sm,
                wifi_field_selected == 0 ? "SSID" : tr("CONTRASEÑA", "PASSWORD"),
                c_green, mx, 20.0f);
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);

            draw_rect_filled(ren, mx, 56.0f, SCREEN_W - 40.0f, 30.0f, c_selbg);
            SDL_Color c_kb_val = {27, 39, 8, 255};
            draw_text(ren, f_med, kb_buffer[0] ? kb_buffer : "", c_kb_val, mx + 8.0f, 62.0f);

            SDL_Color c_keybg = COL_KEY_BG;
            float kb_y0 = 130.0f;
            float key_w = 48.0f;
            float key_h = 42.0f;
            float key_gap = 6.0f;
            float kb_grid_w = 10.0f * key_w + 9.0f * key_gap;
            float kb_area_w = SCREEN_W - 40.0f;
            float kb_x0 = mx + (kb_area_w - kb_grid_w) / 2.0f;
            float space_w = 6.0f * key_w + 5.0f * key_gap;
            float space_x0 = kb_x0 + 2.0f * (key_w + key_gap);
            for (int r = 0; r < KB_ROWS; r++) {
                int rlen = kb_row_len(kb_mode, r);
                for (int c = 0; c < rlen; c++) {
                    const char *k = kb_key_at(kb_mode, r, c);
                    if (!k) continue;
                    bool is_space_row = (r == 3);
                    float kx = is_space_row ? space_x0 : kb_x0 + c * (key_w + key_gap);
                    float kw = is_space_row ? space_w : key_w;
                    float ky = kb_y0 + r * (key_h + key_gap);
                    bool sel = (r == kb_row && c == kb_col);
                    const char *label = is_space_row ? tr("ESPACIO", "SPACE") : k;
                    if (sel) {
                        SDL_Color c_kb_sel_text = {27, 39, 8, 255};
                        draw_rounded_rect_filled(ren, kx, ky, kw, key_h, 4.0f, c_selbg);
                        draw_text_centered(ren, f_sm, label, c_kb_sel_text, kx + kw/2.0f, ky + key_h/2.0f - 6.0f);
                    } else {
                        draw_rounded_rect_filled(ren, kx, ky, kw, key_h, 4.0f, c_keybg);
                        draw_text_centered(ren, f_sm, label, c_gray, kx + kw/2.0f, ky + key_h/2.0f - 6.0f);
                    }
                }
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm,
                tr("[B] Insertar [L1] Borrar [R1] Aceptar [A] Cancelar [SELECT] Mayus/Num", "[B] Insert [L1] Delete [R1] Accept [A] Cancel [SELECT] Caps/Num"),
                s_version);

        } else if (state == STATE_DEVMODE) {
            SDL_Color c_menu_gold  = {27, 39, 8, 255};
            SDL_Color c_menu_beige = {231, 239, 231, 255};
            SDL_Color c_menu_selbg = {183, 221, 91, 255};
            /* Titulo pequeño arriba a la izquierda */
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, mx, 25.0f, 2, tr("Modo Desarrollador", "Dev Mode"));

            /* Menú (columna izquierda), mismo estilo que el menu principal */
            float dev_y0 = 64.0f;
            float dev_item_h = 34.0f;

            for (int i = 0; i < DEV_MENU_COUNT; i++) {
                float iy = dev_y0 + i * dev_item_h;
                char dev_label_buf[40];
                const char *dev_label = DEV_MENU_ITEMS[i];
                if (i == DEV_ACTION_FPS_TOGGLE) {
                    snprintf(dev_label_buf, sizeof(dev_label_buf), "FPS Counter: %s",
                             show_fps_counter ? "ON" : "OFF");
                    dev_label = dev_label_buf;
                }
                if (i == dev_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_sm, dev_label, 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 32.0f;
                    float pill_h = dev_item_h - 4.0f;
                    draw_rounded_rect_filled(ren, mx - 10.0f, iy - 5.0f,
                                     sel_w, pill_h, pill_h / 2.0f, c_menu_selbg);
                    draw_text(ren, f_sm, dev_label, c_menu_gold, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_sm, dev_label, c_menu_beige, mx + 8.0f, iy);
                }
            }

            /* Panel derecho: info tecnica, agrupada en 2 columnas */
            {
                float ry0 = 64.0f;
                float row_h = 40.0f;
                char batt_buf[8];
                if (status_battery >= 0)
                    snprintf(batt_buf, sizeof(batt_buf), "%d%%", status_battery);
                else
                    strncpy(batt_buf, "--", sizeof(batt_buf));
                struct { const char *label; const char *val; } left_col[4] = {
                    {"IP", dev_ip},
                    {"UPTIME", dev_uptime},
                    {"RAM", dev_ram},
                    {tr("BATERÍA", "BATTERY"), batt_buf},
                };
                struct { const char *label; const char *val; } right_col[4] = {
                    {"KERNEL", s_kernel},
                    {"MESA", s_mesa},
                    {"RETROARCH", s_retroarch},
                    {"SDL3", s_sdl3},
                };
                float dm_rx = rx - 140.0f;
                float col2_x = dm_rx + 150.0f;
                for (int i = 0; i < 4; i++) {
                    float ry = ry0 + i * row_h;
                    draw_text(ren, f_sm, left_col[i].label, c_menu_beige, dm_rx, ry);
                    draw_text(ren, f_med, left_col[i].val, c_menu_selbg, dm_rx, ry + 16.0f);
                    draw_text(ren, f_sm, right_col[i].label, c_menu_beige, col2_x, ry);
                    draw_text(ren, f_med, right_col[i].val, c_menu_selbg, col2_x, ry + 16.0f);
                }
                float dm_rx2 = rx - 140.0f;
                float g_x0 = dm_rx2;
                float g_y0 = 250.0f;
                float g_w  = (SCREEN_W - 20.0f) - dm_rx2;
                float g_h  = 150.0f;
                SDL_Color c_red = COL_RED;
                int perf_now = g_cfg.perf_profile;
                bool throttle_applicable = (perf_now == 0) && dev_cpu_max_freq > 0 && dev_cpu_cur_freq > 0;
                bool throttling = throttle_applicable &&
                    dev_cpu_cur_freq < (int)(dev_cpu_max_freq * 0.95f);
                const char *throttle_label = !throttle_applicable
                    ? tr("N/D", "N/A")
                    : throttling
                    ? tr("THROTTLING ACTIVO", "THROTTLING ACTIVE")
                    : tr("Normal", "Normal");
                SDL_Color throttle_c = !throttle_applicable ? c_menu_beige : (throttling ? c_red : c_menu_selbg);
                draw_text(ren, f_sm, tr("TEMPERATURA CPU", "CPU TEMPERATURE"), c_menu_beige, g_x0, g_y0 - 18.0f);
                draw_text_right(ren, f_sm, throttle_label, throttle_c, g_x0 + g_w, g_y0 - 18.0f);
                draw_rect_filled(ren, g_x0, g_y0, g_w, g_h, (SDL_Color){26, 24, 18, 255});
                {
                    int tmin = 30, tmax = 90;
                    SDL_SetRenderDrawColor(ren, c_menu_selbg.r, c_menu_selbg.g, c_menu_selbg.b, 255);
                    for (int i = 0; i < g_devmode_temp_history_count - 1; i++) {
                        int va = g_devmode_temp_history[i];
                        int vb = g_devmode_temp_history[i + 1];
                        float fa = (float)(va - tmin) / (float)(tmax - tmin);
                        float fb = (float)(vb - tmin) / (float)(tmax - tmin);
                        if (fa < 0.0f) fa = 0.0f; if (fa > 1.0f) fa = 1.0f;
                        if (fb < 0.0f) fb = 0.0f; if (fb > 1.0f) fb = 1.0f;
                        float xa = g_x0 + g_w * ((float)i / (float)(DEVMODE_TEMP_HISTORY_LEN - 1));
                        float xb = g_x0 + g_w * ((float)(i + 1) / (float)(DEVMODE_TEMP_HISTORY_LEN - 1));
                        float ya = g_y0 + g_h - fa * g_h;
                        float yb = g_y0 + g_h - fb * g_h;
                        SDL_RenderLine(ren, xa, ya, xb, yb);
                    }
                    if (g_devmode_temp_history_count > 0) {
                        char tbuf[16];
                        snprintf(tbuf, sizeof(tbuf), "%d C", g_devmode_temp_history[g_devmode_temp_history_count - 1]);
                        draw_text(ren, f_med, tbuf, c_menu_selbg, g_x0 + 4.0f, g_y0 + 4.0f);
                    }
                }
            }

            /* Barra inferior */
            draw_footer(ren, f_sm, tr("[B] Seleccionar  [A] Volver", "[B] Select  [A] Back"), s_version);

        } else if (state == STATE_CONFIRM) {
            const char *label = (confirm_target == SETTINGS_ACTION_FACTORY_RESET)
                                 ? tr("¿Restablecer valores de fábrica?", "Factory reset?")
                                 : (confirm_target == DEV_ACTION_REBOOT)
                                 ? tr("¿Reiniciar el dispositivo?", "Reboot the device?")
                                 : tr("¿Apagar el dispositivo?", "Shut down the device?");
            draw_text_centered(ren, f_med, label, c_white,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f - 30.0f);
            draw_text_centered(ren, f_med, tr("[B] Si        [A] No", "[B] Yes       [A] No"), c_green,
                               SCREEN_W / 2.0f, SCREEN_H / 2.0f + 10.0f);

        } else if (state == STATE_SYSINFO) {
            /* ── Layout: cuadrícula 2 columnas × 3 bloques ──────────────────
             *  Pantalla: 640×480, margen 20px, separador vertical en x=330
             *  Bloques apilados verticalmente: header(44) + 3×bloques(~128) + footer(42)
             *  Col izq: x=20..330  Col der: x=348..620
             *  Cada bloque: título(14) + guiones(10) + N filas×(label+valor, 18px)
             */
            SDL_Color c_red = COL_RED;
            SDL_Color c_row_bg = {28, 52, 40, 255};
            SDL_Color c_si_title = {183, 221, 91, 255};
            int si_row_idx = 0;

            /* Constantes de layout sysinfo: 1 columna ancha, 3 bloques por pagina */
            const float SI_CW_L  = 420.0f;  /* ancho de fila (label...valor) */
            const float SI_CW_R  = 420.0f;
            const float SI_MX    = (SCREEN_W - SI_CW_L) / 2.0f;   /* centrado horizontal */
            const float SI_RX    = SI_MX;
            const float SI_ROW_H = 17.0f;   /* altura de fila label+valor */
            const float SI_BLK_H = 120.0f;  /* altura de cada bloque, 3 bloques por pagina */
            const float SI_Y0    = 54.0f;   /* Y inicio primer bloque */
            const float SI_SEP_H1 = SI_Y0 + SI_BLK_H;
            const float SI_SEP_H2 = SI_Y0 + SI_BLK_H * 2;

            /* Título y separador superior: siempre en el margen fijo, no en SI_MX centrado */
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, 20.0f, 25.0f, 2, tr("Diagnóstico del sistema", "System Diagnostics"));

            /* Indicador de pagina: encima del footer, alineado a la derecha */
            {
                char page_ind[16];
                snprintf(page_ind, sizeof(page_ind), "%d/2", sysinfo_page + 1);
                draw_text_right(ren, f_sm, page_ind, c_dkgreen, SCREEN_W - 20.0f, 418.0f);
            }

/* Macro auxiliar: título de bloque */
#define SI_BLOCK_TITLE(xpos, ypos, title) do { \
    draw_text(ren, f_sm, title, c_si_title, (xpos), (ypos)); \
} while(0)

/* Macro fila: fondo alterno (zebra) + etiqueta + valor alineado a col_right */
#define SI_ROW(xpos, ypos, col_right, lbl, val) do { \
    float _row_top = (ypos) - 2.0f; \
    if (si_row_idx % 2 == 0) \
        draw_rounded_rect_filled(ren, (xpos) - 10.0f, _row_top, (col_right) - (xpos) + 20.0f, SI_ROW_H, SI_ROW_H / 2.0f, c_row_bg); \
    si_row_idx++; \
    { int _th = 0, _tw0 = 0; TTF_GetStringSize(f_sm, (lbl), 0, &_tw0, &_th); \
      float _text_y = _row_top + (SI_ROW_H - (float)_th) / 2.0f; \
      draw_text(ren, f_sm,  (lbl), c_gray,  (xpos),       _text_y); \
      draw_text_right(ren, f_sm, (val), c_white, (col_right), _text_y); } \
} while(0)

/* Macro fila con barra: etiqueta, barra, valor */
#define SI_ROW_BAR(xpos, ypos, col_right, lbl, val, pct) do { \
    float _row_top = (ypos) - 2.0f; \
    if (si_row_idx % 2 == 0) \
        draw_rounded_rect_filled(ren, (xpos) - 10.0f, _row_top, (col_right) - (xpos) + 20.0f, SI_ROW_H, SI_ROW_H / 2.0f, c_row_bg); \
    si_row_idx++; \
    { int _fw = 0, _fh = 0; \
      TTF_GetStringSize(f_sm, (val), 0, &_fw, &_fh); \
      float _text_y = _row_top + (SI_ROW_H - (float)_fh) / 2.0f; \
      draw_text(ren, f_sm, (lbl), c_gray, (xpos), _text_y); \
      draw_text(ren, f_sm, (val), c_white, (col_right) - (float)_fw, _text_y); \
      float _bw = 60.0f; \
      float _bh = 6.0f; \
      float _bar_right = (col_right) - (float)_fw - 10.0f; \
      float _bar_y = _text_y + ((float)_fh - _bh) / 2.0f; \
      SDL_Color _c_bar_bg = {28, 52, 40, 255}; \
      SDL_Color _c_bar_fill = {183, 221, 91, 255}; \
      draw_bar_rounded(ren, _bar_right - _bw, _bar_y, _bw, _bh, (pct) / 100.0f, _c_bar_bg, _c_bar_fill); \
    } \
} while(0)

            float y = SI_Y0 + 2.0f;
            si_row_idx = 0;
            if (sysinfo_page == 0) {
            /* ── BLOQUE 1: SISTEMA ───────────────────────────────────── */
            SI_BLOCK_TITLE(SI_MX, y, tr("SISTEMA", "SYSTEM"));
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Version OS", "OS Version"),       s_version);       y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Kernel",       s_kernel);        y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Arquitectura", "Architecture"), "aarch64");       y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Compilación", "Build"),        sysinfo_build);   y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Hostname",     "armiga");
            }

            /* ── BLOQUE 2: METRICAS ────────────────────────────────────── */
            if (sysinfo_page == 0) {
            y = SI_SEP_H1 + 4.0f;
            si_row_idx = 0;
            SI_BLOCK_TITLE(SI_RX, y, tr("MÉTRICAS", "METRICS"));
            y += 28.0f;
            {
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Carga CPU", "CPU Load"),  sysinfo_cpu_usage, sysinfo_cpu_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Uso de RAM", "RAM Usage"),  dev_ram,           sysinfo_ram_pct);  y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Temp CPU", "CPU Temp"), sysinfo_temp,      sysinfo_temp_pct); y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Uptime",   dev_uptime);                          y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Load Avg", sysinfo_loadavg);
            }
            }

            /* ── BLOQUE 3: VOLUMENES ───────────────────────────── */
            if (sysinfo_page == 0) {
            y = SI_SEP_H2 + 4.0f;
            si_row_idx = 0;
            SI_BLOCK_TITLE(SI_MX, y, tr("VOLÚMENES", "VOLUMES"));
            y += 28.0f;
            {
                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, tr("DH0: (Sistema)", "DH0: (System)"), sysinfo_disk_root, sysinfo_disk_root_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, tr("DH1: (Datos)", "DH1: (Data)"),   sysinfo_disk_data, sysinfo_disk_data_pct); y += SI_ROW_H;
                SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Espacio disponible", "Free space"), sysinfo_data_free_str);
            }
            }

            /* ── BLOQUE 1: ESPECIFICACIONES ──────────────────────────────── */
            if (sysinfo_page == 1) {
            y = SI_Y0 + 2.0f;
            si_row_idx = 0;
            SI_BLOCK_TITLE(SI_RX, y, tr("ESPECIFICACIONES", "SPECIFICATIONS"));
            y += 28.0f;
            char sysinfo_cpu[32];
            int sysinfo_cpu_freq = 0;
            read_sysfs_int("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &sysinfo_cpu_freq);
            snprintf(sysinfo_cpu, sizeof(sysinfo_cpu), "Cortex-A53 @%.2fGHz",
                     sysinfo_cpu_freq > 0 ? sysinfo_cpu_freq / 1000000.0f : 1.42f);
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "CPU",           sysinfo_cpu); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "GPU",           "Mali-G31 (Panfrost)"); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "RAM",           "1 GB LPDDR4");         y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, tr("Almacenamiento", "Storage"),"microSD");              y += SI_ROW_H;
            char sysinfo_resolution[24];
            snprintf(sysinfo_resolution, sizeof(sysinfo_resolution), "640x480 @ %dHz", refresh_120hz ? 120 : 60);
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, tr("Resolución", "Resolution"),    sysinfo_resolution);
            }

            /* ── BLOQUE 3 IZQ: SOFTWARE ──────────────────────────────────── */
            if (sysinfo_page == 1) {
            y = SI_SEP_H1 + 4.0f;
            si_row_idx = 0;
            SI_BLOCK_TITLE(SI_MX, y, tr("MOTOR DE EMULACIÓN", "EMULATION ENGINE"));
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "RetroArch", s_retroarch); y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Mesa",      s_mesa);      y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "SDL3",      s_sdl3);      y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "puae2021_libretro.so", s_puae_core); y += SI_ROW_H;
            }

            /* ── BLOQUE 3 DER: RED ───────────────────────────────────────── */
            if (sysinfo_page == 1) {
            y = SI_SEP_H2 + 4.0f - SI_ROW_H;
            si_row_idx = 0;
            SI_BLOCK_TITLE(SI_RX, y, tr("CONECTIVIDAD", "CONNECTIVITY"));
            y += 28.0f;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "IP",          dev_ip);          y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "WiFi",        status_wifi_up ? tr("Conectado", "Connected") : tr("Desconectado", "Disconnected")); y += SI_ROW_H;
            SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Intensidad", "Signal"),  sysinfo_wifi_sig, sysinfo_wifi_pct >= 0 ? sysinfo_wifi_pct : 0); y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "MAC",         sysinfo_mac);
            }

#undef SI_BLOCK_TITLE
#undef SI_ROW
#undef SI_ROW_BAR

            /* Barra inferior */
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[A] Volver  [L1/R1] Pagina", "[A] Back  [L1/R1] Page"), s_version);
        } else if (state == STATE_UPDATE) {
            const float UX = 20.0f;
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, UX, 25.0f, 2, tr("Actualización de sistema", "System Update"));

            /* Versión actual */
            {
                char buf[64];
                snprintf(buf, sizeof(buf), tr("Versión instalada:   %s", "Installed version:   %s"), s_version);
                draw_text(ren, f_sm, buf, c_gray, UX, 64.0f);
            }

            if (update_phase == UPD_CHECKING) {
                draw_text_animdots(ren, f_sm, tr("Comprobando actualizaciones", "Checking for updates"), c_white, UX, 100.0f, now_ticks);

            } else if (update_phase == UPD_NO_UPDATE) {
                draw_text(ren, f_sm, tr("El sistema está actualizado.", "System is up to date."), c_green, UX, 100.0f);

            } else if (update_phase == UPD_CONFIRM) {
                char buf[64];
                snprintf(buf, sizeof(buf), tr("Nueva versión disponible:   %s", "New version available:   %s"), upd_new_ver);
                draw_text(ren, f_sm, buf, c_green, UX, 100.0f);
                draw_text(ren, f_sm, tr("La descarga se realizará en segundo plano.", "The download will run in the background."), c_gray, UX, 122.0f);
                draw_text(ren, f_sm, tr("El dispositivo se reiniciará al completar.", "The device will restart when finished."), c_gray, UX, 140.0f);
                draw_text(ren, f_med, tr("[B] Descargar e instalar", "[B] Download and install"), c_green,  UX,          188.0f);
                draw_text(ren, f_med, tr("[A] Cancelar", "[A] Cancel"),             c_gray,   UX + 260.0f, 188.0f);

            } else if (update_phase == UPD_DOWNLOADING) {
                draw_text_animdots(ren, f_sm, tr("Descargando actualización", "Downloading update"), c_white, UX, 100.0f, now_ticks);
                /* Barra de progreso */
                int pct = (int)(upd_progress * 100.0f);
                char pct_buf[8]; snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
                { SDL_Color _c_upd_bg = {28, 52, 40, 255};
                  SDL_Color _c_upd_fill = {183, 221, 91, 255};
                  draw_bar_rounded(ren, UX, 122.0f, 260.0f, 12.0f, upd_progress, _c_upd_bg, _c_upd_fill); }
                draw_text(ren, f_sm, pct_buf, c_white, UX + 270.0f, 118.0f);
                draw_text(ren, f_sm, tr("No apagues el dispositivo durante la descarga.", "Do not turn off the device during download."), c_gray, UX, 144.0f);

            } else if (update_phase == UPD_VERIFYING) {
                draw_text_animdots(ren, f_sm, tr("Verificando integridad", "Verifying integrity"), c_white, UX, 100.0f, now_ticks);

            } else if (update_phase == UPD_READY) {
                draw_text(ren, f_sm, tr("Actualización lista. Reiniciando...", "Update ready. Restarting..."), c_green, UX, 100.0f);
                /* Reiniciar automáticamente */
                SDL_Delay(2000);
                exec_req = EXEC_REBOOT;
                running  = false;

            } else if (update_phase == UPD_ERROR) {
                { SDL_Color _c_red = COL_RED; draw_text(ren, f_sm, "Error:", _c_red, UX, 100.0f); }
                draw_text(ren, f_sm, upd_msg,  c_gray, UX, 118.0f);
            }

            draw_line(ren, UX, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            if (update_phase != UPD_DOWNLOADING)
                draw_footer(ren, f_sm, tr("[A] Volver", "[A] Back"), s_version);
            else
                draw_footer(ren, f_sm, "", s_version);

        } /* end STATE_UPDATE */
        else if (state == STATE_CONTROLLER_TEST) {
            const float CX = 20.0f;
            draw_statusbar(ren, f_sm, f_xs, status_time, status_wifi_up, status_battery, status_bt_up, wifi_icon_tex, battery_icon_tex, bt_icon_tex);
            draw_active_dash_breadcrumbs(ren, f_sm, CX, 25.0f, 3, tr("Test de mando", "Controller Test"));

            if (!joy) {
                draw_text(ren, f_sm, tr("No se detecta ningún mando.", "No controller detected."), c_gray, CX, 100.0f);
            } else {
                /* ── D-PAD (hat) ────────────────────────────────────────── */
                float dpad_cx = 140.0f, dpad_cy = 160.0f, dpad_sz = 26.0f, dpad_gap = 4.0f;
                Uint8 hat = SDL_GetJoystickHat(joy, 0);
                draw_text_centered(ren, f_sm, "D-Pad", c_gray, dpad_cx, dpad_cy - 70.0f);
                bool dpad_up_p    = hat & SDL_HAT_UP;
                bool dpad_down_p  = hat & SDL_HAT_DOWN;
                bool dpad_left_p  = hat & SDL_HAT_LEFT;
                bool dpad_right_p = hat & SDL_HAT_RIGHT;
                float dp_up_x = dpad_cx - dpad_sz/2, dp_up_y = dpad_cy - dpad_sz - dpad_gap;
                float dp_down_x = dpad_cx - dpad_sz/2, dp_down_y = dpad_cy + dpad_gap;
                float dp_left_x = dpad_cx - dpad_sz - dpad_gap - dpad_sz/2, dp_left_y = dpad_cy - dpad_sz/2;
                float dp_right_x = dpad_cx + dpad_gap + dpad_sz/2, dp_right_y = dpad_cy - dpad_sz/2;
                { SDL_Color border_c = COL_SEL_BG, bg_c = COL_KEY_BG;
                  if (dpad_up_p)    draw_rounded_rect_filled(ren, dp_up_x, dp_up_y, dpad_sz, dpad_sz, 4.0f, border_c);
                  else              draw_rounded_rect_outline(ren, dp_up_x, dp_up_y, dpad_sz, dpad_sz, 4.0f, 2.0f, border_c, bg_c);
                  if (dpad_down_p)  draw_rounded_rect_filled(ren, dp_down_x, dp_down_y, dpad_sz, dpad_sz, 4.0f, border_c);
                  else              draw_rounded_rect_outline(ren, dp_down_x, dp_down_y, dpad_sz, dpad_sz, 4.0f, 2.0f, border_c, bg_c);
                  if (dpad_left_p)  draw_rounded_rect_filled(ren, dp_left_x, dp_left_y, dpad_sz, dpad_sz, 4.0f, border_c);
                  else              draw_rounded_rect_outline(ren, dp_left_x, dp_left_y, dpad_sz, dpad_sz, 4.0f, 2.0f, border_c, bg_c);
                  if (dpad_right_p) draw_rounded_rect_filled(ren, dp_right_x, dp_right_y, dpad_sz, dpad_sz, 4.0f, border_c);
                  else              draw_rounded_rect_outline(ren, dp_right_x, dp_right_y, dpad_sz, dpad_sz, 4.0f, 2.0f, border_c, bg_c);
                }

                /* ── STICK IZQUIERDO (ejes 0,1) ─────────────────────────── */
                float stickL_cx = 320.0f, stickL_cy = 160.0f, stick_r = 45.0f, dot_r = 8.0f;
                draw_text_centered(ren, f_sm, tr("Stick Izq.", "Left Stick"), c_gray, stickL_cx, stickL_cy - 70.0f);
                { SDL_Color ring_c = COL_SEL_BG; SDL_Color bg_c = COL_BG;
                  draw_rounded_rect_outline(ren, stickL_cx - stick_r, stickL_cy - stick_r, stick_r*2, stick_r*2, stick_r, 2.0f, ring_c, bg_c); }
                { SDL_Color deadzone_c = {40, 65, 50, 255};
                  draw_circle_filled(ren, stickL_cx, stickL_cy, stick_r * 0.10f, deadzone_c); }
                Sint16 axL_x = SDL_GetJoystickAxis(joy, 0);
                Sint16 axL_y = SDL_GetJoystickAxis(joy, 1);
                float normL_x = (float)axL_x / 32767.0f;
                float normL_y = (float)axL_y / 32767.0f;
                float magL = SDL_sqrtf(normL_x * normL_x + normL_y * normL_y);
                if (magL > 1.0f) { normL_x /= magL; normL_y /= magL; }
                float dotL_x = stickL_cx + normL_x * (stick_r - dot_r);
                float dotL_y = stickL_cy + normL_y * (stick_r - dot_r);
                { bool l3_pressed = (SDL_GetNumJoystickButtons(joy) > 11) && SDL_GetJoystickButton(joy, 11);
                  SDL_Color dot_c = l3_pressed ? (SDL_Color)COL_RED : (SDL_Color)COL_SEL_BG;
                  draw_circle_filled(ren, dotL_x, dotL_y, dot_r, dot_c); }
                { char buf[24]; snprintf(buf, sizeof(buf), "X:%d Y:%d", axL_x, axL_y);
                  draw_text_centered(ren, f_xsm, buf, c_gray, stickL_cx, stickL_cy + stick_r + 10.0f); }

                /* ── STICK DERECHO (ejes 2,3) ───────────────────────────── */
                float stickR_cx = 500.0f, stickR_cy = 160.0f;
                draw_text_centered(ren, f_sm, tr("Stick Dcho.", "Right Stick"), c_gray, stickR_cx, stickR_cy - 70.0f);
                { SDL_Color ring_c = COL_SEL_BG; SDL_Color bg_c = COL_BG;
                  draw_rounded_rect_outline(ren, stickR_cx - stick_r, stickR_cy - stick_r, stick_r*2, stick_r*2, stick_r, 2.0f, ring_c, bg_c); }
                { SDL_Color deadzone_c = {40, 65, 50, 255};
                  draw_circle_filled(ren, stickR_cx, stickR_cy, stick_r * 0.10f, deadzone_c); }
                Sint16 axR_x = SDL_GetJoystickAxis(joy, 2);
                Sint16 axR_y = SDL_GetJoystickAxis(joy, 3);
                float normR_x = (float)axR_x / 32767.0f;
                float normR_y = (float)axR_y / 32767.0f;
                float magR = SDL_sqrtf(normR_x * normR_x + normR_y * normR_y);
                if (magR > 1.0f) { normR_x /= magR; normR_y /= magR; }
                float dotR_x = stickR_cx + normR_x * (stick_r - dot_r);
                float dotR_y = stickR_cy + normR_y * (stick_r - dot_r);
                { bool r3_pressed = (SDL_GetNumJoystickButtons(joy) > 12) && SDL_GetJoystickButton(joy, 12);
                  SDL_Color dot_c = r3_pressed ? (SDL_Color)COL_RED : (SDL_Color)COL_SEL_BG;
                  draw_circle_filled(ren, dotR_x, dotR_y, dot_r, dot_c); }
                { char buf[24]; snprintf(buf, sizeof(buf), "X:%d Y:%d", axR_x, axR_y);
                  draw_text_centered(ren, f_xsm, buf, c_gray, stickR_cx, stickR_cy + stick_r + 10.0f); }

                /* ── BOTONES (indice SDL crudo, sin asumir nombres no verificados) ── */
                int n_btn = SDL_GetNumJoystickButtons(joy);
                draw_text_centered(ren, f_sm, tr("Botones", "Buttons"), c_gray, SCREEN_W / 2.0f, 260.0f);
                float btn_y0 = 285.0f, btn_w = 36.0f, btn_h = 36.0f, btn_gap = 8.0f;
                int btn_per_row = 10;
                int btn_rows = (n_btn + btn_per_row - 1) / btn_per_row;
                for (int b = 0; b < n_btn; b++) {
                    int row = b / btn_per_row, col = b % btn_per_row;
                    int items_this_row = (row == btn_rows - 1) ? (n_btn - row * btn_per_row) : btn_per_row;
                    float row_w = items_this_row * btn_w + (items_this_row - 1) * btn_gap;
                    float btn_x0 = (SCREEN_W - row_w) / 2.0f;
                    float bx = btn_x0 + col * (btn_w + btn_gap);
                    float by = btn_y0 + row * (btn_h + btn_gap);
                    bool pressed = SDL_GetJoystickButton(joy, b);
                    if (pressed) {
                        SDL_Color bc = COL_SEL_BG;
                        draw_rounded_rect_filled(ren, bx, by, btn_w, btn_h, 6.0f, bc);
                    } else {
                        SDL_Color border_c = COL_SEL_BG, bg_c = COL_KEY_BG;
                        draw_rounded_rect_outline(ren, bx, by, btn_w, btn_h, 6.0f, 2.0f, border_c, bg_c);
                    }
                    char bl[4]; snprintf(bl, sizeof(bl), "%d", b);
                    int tw = 0, th = 0;
                    TTF_GetStringSize(f_med, bl, 0, &tw, &th);
                    SDL_Color txt_c = pressed ? (SDL_Color){0,0,0,255} : (SDL_Color)COL_SEL_BG;
                    draw_text(ren, f_med, bl, txt_c, bx + btn_w/2.0f - (float)tw/2.0f, by + btn_h/2.0f - (float)th/2.0f);
                }

                /* Nombre del boton pulsado, segun orden estandar evdev/SDL
                 * (indices 0,1,3,4,5,8,9 confirmados via constantes
                 * BTN_SDL_* ya existentes; el resto inferido por orden de
                 * aparicion en evtest, autoverificable en esta pantalla). */
                {
                    static const char *CTRL_BTN_NAMES[] = {
                        "B (SOUTH)", "A (EAST)", "Y (WEST)", "X (NORTH)",
                        "L1", "R1", "L2", "R2",
                        "SELECT", "START", "MODE",
                        "L3", "R3",
                        "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT"
                    };
                    int n_names = (int)(sizeof(CTRL_BTN_NAMES) / sizeof(CTRL_BTN_NAMES[0]));
                    char active_btns_str[160] = "";
                    bool any_pressed = false;
                    for (int b = 0; b < n_btn && b < n_names; b++) {
                        if (SDL_GetJoystickButton(joy, b)) {
                            if (any_pressed && strlen(active_btns_str) < sizeof(active_btns_str) - 24)
                                strcat(active_btns_str, " + ");
                            if (strlen(active_btns_str) < sizeof(active_btns_str) - strlen(CTRL_BTN_NAMES[b]) - 1)
                                strcat(active_btns_str, CTRL_BTN_NAMES[b]);
                            any_pressed = true;
                        }
                    }
                    if (any_pressed) {
                        SDL_Color name_c = COL_SEL_BG;
                        draw_text_centered(ren, f_sm, active_btns_str, name_c, SCREEN_W / 2.0f, 385.0f);
                    }
                }
            }

            { SDL_Color joytest_c = COL_SEL_BG;
              draw_text_right(ren, f_sm, "armiga-joytest v1.1", joytest_c, SCREEN_W - 20.0f, 414.0f); }
            /* Test de vibracion: mantener L2 (indice 6, confirmado en
             * hardware) dispara un pulso corto de rumble, repetido
             * mientras se mantenga pulsado (50ms por pulso, sin overlap
             * agresivo gracias al propio SDL que reemplaza el efecto
             * activo en cada llamada). */
            if (joy && SDL_GetNumJoystickButtons(joy) > 6 && SDL_GetJoystickButton(joy, 6)) {
                SDL_RumbleJoystick(joy, 0x4000, 0x8000, 50);
            }
            draw_line(ren, CX, 438.0f, SCREEN_W - 20.0f, 438.0f, (SDL_Color){183, 221, 91, 255});
            draw_footer(ren, f_sm, tr("[SELECT+START] Volver  [L2] Test vibración", "[SELECT+START] Back  [L2] Vibration Test"), s_version);
        } /* end STATE_CONTROLLER_TEST */

        if (screenshot_capture_pending) {
            SDL_Surface *clean_frame_for_screenshot = SDL_RenderReadPixels(ren, NULL);
            take_screenshot(ren, SCREEN_W, SCREEN_H, clean_frame_for_screenshot);
            if (clean_frame_for_screenshot) SDL_DestroySurface(clean_frame_for_screenshot);
            screenshot_capture_pending = false;
        }
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
        draw_screen_corners(ren, SCREEN_W, SCREEN_H, 22.0f);
        if (show_fps_counter) {
            fps_frame_count++;
            Uint64 fps_now_ns = SDL_GetTicksNS();
            float fps_elapsed = (float)(fps_now_ns - fps_last_update) / 1000000000.0f;
            if (fps_elapsed >= 0.5f) {
                fps_display = (float)fps_frame_count / fps_elapsed;
                fps_frame_count = 0;
                fps_last_update = fps_now_ns;
            }
            char fps_buf[16];
            snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS", fps_display);
            SDL_Color c_fps_white = COL_WHITE;
            draw_text_right(ren, f_sm, fps_buf, c_fps_white, SCREEN_W - 20.0f, 70.0f);
        }
        SDL_RenderPresent(ren);
        /* VSync activo (SDL_SetRenderVSync) sustituye al SDL_Delay(16):
         * RenderPresent bloquea hasta el siguiente VBlank, 0% CPU en espera. */
        /* Confirmar arranque exitoso ante el mecanismo de rollback A/B,
         * una sola vez, tras el primer frame realmente dibujado en
         * pantalla (prueba de que SDL/DRM y el launcher arrancaron bien,
         * no solo que los scripts init.d terminaron sin error). */
        static bool boot_confirmed = false;
        if (!boot_confirmed) {
            system("/etc/init.d/S02bootcheck confirm >/dev/null 2>&1 &");
            boot_confirmed = true;
        }
    }

    if (logo_tex) SDL_DestroyTexture(logo_tex);
    for (int mi = 0; mi < MENU_ICON_COUNT; mi++)
        if (menu_icon_tex[mi]) SDL_DestroyTexture(menu_icon_tex[mi]);
    if (wifi_icon_tex) SDL_DestroyTexture(wifi_icon_tex);
    if (bt_icon_tex) SDL_DestroyTexture(bt_icon_tex);
    if (battery_icon_tex) SDL_DestroyTexture(battery_icon_tex);
    if (perf_bolt_tex) SDL_DestroyTexture(perf_bolt_tex);
    if (perf_scale_tex) SDL_DestroyTexture(perf_scale_tex);
    if (perf_battery_tex) SDL_DestroyTexture(perf_battery_tex);
    if (arexx_icon_tex) SDL_DestroyTexture(arexx_icon_tex);
    free(arexx_output);
    if (update_icon_tex) SDL_DestroyTexture(update_icon_tex);
    if (joy) SDL_CloseJoystick(joy);
    TTF_CloseFont(f_sm);
    TTF_CloseFont(f_med);
    TTF_CloseFont(f_lg);
    TTF_CloseFont(f_xs);
    TTF_CloseFont(f_xsm);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    TTF_Quit();
    if (s_audio_stream) {
        SDL_PauseAudioStreamDevice(s_audio_stream);
        SDL_DestroyAudioStream(s_audio_stream);
        s_audio_stream = NULL;
    }
    SDL_Quit();
    clear_fb0();

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
            redirect_stdio_to_local_console();
            execl("/sbin/reboot", "reboot", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar reboot: %s\n",
                    strerror(errno));
            return 1;
        case EXEC_SHUTDOWN:
            redirect_stdio_to_local_console();
            system("amixer sset 'Speaker' off >/dev/null 2>&1");
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
