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
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <linux/kd.h>
#include "logo.h"

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

#define COL_BG       { 16,  16,  16, 255}
#define COL_GREEN    {  0, 255, 136, 255}
#define COL_DKGREEN  {  0, 119,  68, 255}
#define COL_WHITE    {220, 220, 220, 255}
#define COL_GRAY     {136, 136, 136, 255}
#define COL_SEL_BG   { 42,  42,  42, 255}
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
    STATE_BRIGHTNESS_CONFIG
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
#define ACTION_SETTINGS 4
#define ACTION_SHELL   5

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
    {"Configuración",               "Settings"},
    {"Apagar dispositivo",          "Power Off"},
};
static const char *MENU_DESC[][2] = {
    {"Explora y lanza juegos\n" "Amiga desde tu biblioteca.",
     "Browse and launch Amiga\n" "games from your library."},
    {"Descarga e instala la\n" "ultima version de armiga.",
     "Download and install the\n" "latest version of armiga."},
    {"Revisa el estado del\n" "hardware y el sistema.",
     "Check the status of the\n" "hardware and system."},
    {"Ajustes del sistema:\n" "red inalambrica y mas.",
     "System settings:\n" "wireless network and more."},
    {"Apaga el dispositivo\n" "de forma segura.",
     "Shut down the device\n" "safely."},
};
#define MENU_COUNT 5

static const char *SETTINGS_MENU_ITEMS[][2] = {
    {"Red inalámbrica",             "Wireless Network"},
    {"Copia de seguridad",          "Backup"},
    {"LED RGB analógicos",          "Analog Stick LEDs"},
    {"Zona horaria",                "Time Zone"},
    {"Ahorro de pantalla",          "Screen Dimming"},
    {"Brillo de pantalla",          "Screen Brightness"},
    {"SSH",                         "SSH"},
    {"Samba (\\\\armiga)",           "Samba (\\\\armiga)"},
    {"Restablecer valores de fábrica", "Factory reset"},
};
#define SETTINGS_MENU_COUNT 9
#define SETTINGS_ACTION_FACTORY_RESET 10

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
#define BTN_SDL_X      3

#define DEVMODE_HOLD_MS 3000

#define ARMIGA_CONFIG_PATH "/media/amiga_data/armiga.cfg"
typedef enum { LANG_ES = 0, LANG_EN = 1 } Lang;
static Lang current_lang = LANG_ES;
static const char *tr(const char *es, const char *en)
{
    return (current_lang == LANG_EN) ? en : es;
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

static void read_cpu_temp(char *buf, size_t bufsize)
{
    safe_copy(buf, "--", bufsize);
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
static int finish_check_update(const char *current_ver,
                        char *new_ver, size_t new_ver_sz,
                        char *dl_url, size_t dl_url_sz,
                        char *sha_url, size_t sha_url_sz)
{
    safe_copy(new_ver, "", new_ver_sz);
    safe_copy(dl_url,  "", dl_url_sz);
    safe_copy(sha_url, "", sha_url_sz);
    FILE *f = fopen(CHECK_JSON_TMP, "r");
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
        }
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
                    safe_copy(asset_url, url, sizeof(asset_url));
                if (strstr(last_name, ".sha256"))
                    safe_copy(sha_asset_url, url, sizeof(sha_asset_url));
                if (asset_url[0] && sha_asset_url[0]) goto parse_done;
            }
        }
    }
    parse_done:
    fclose(f);
    unlink(CHECK_JSON_TMP);
    if (!tag[0] || !asset_url[0]) return 0;
    const char *ver = tag;
    if (ver[0] == 'v') ver++;
    safe_copy(new_ver, ver, new_ver_sz);
    safe_copy(dl_url,  asset_url,     dl_url_sz);
    safe_copy(sha_url, sha_asset_url, sha_url_sz);
    return semver_cmp(ver, current_ver) > 0 ? 1 : 0;
}

/* Descarga el .img.gz con progreso. Ejecuta curl en background y
 * monitoriza el fichero destino para actualizar la barra. */
/* PID real del hijo curl (no via fichero, evita reciclado de PID). */
static pid_t s_curl_pid = -1;
static int download_update(const char *url, float *progress_out)
{
    mkdir(UPDATE_DIR, 0755);
    /* Limpiar ficheros de descarga anterior */
    unlink(UPDATE_IMG);
    unlink(UPDATE_SHA256);
    s_curl_pid = -1;

    pid_t pid = fork();
    if (pid < 0) return -1; /* fork fallo */
    if (pid == 0) {
        /* Hijo: redirige salida y ejecuta curl directamente, sin shell.
         * url llega como argv literal: sin interpolacion, sin riesgo de
         * inyeccion de comandos (B01). */
        int fd = open("/tmp/curl_progress", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); dup2(fd, STDERR_FILENO); close(fd); }
        execlp("curl", "curl", "-s", "--max-time", "300", "-L",
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
/* Guarda el idioma actual en armiga.cfg, preservando el resto de claves
 * (ej. TZ) linea a linea. */
static void save_lang_config(void)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                !strcmp(key, "LANG")) {
                continue; /* se reescribe al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "LANG=%s\n", (current_lang == LANG_EN) ? "EN" : "ES");
    fclose(f);
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
/* Guarda TZ en armiga.cfg, preservando el resto de claves (ej. LANG),
 * mismo patron que save_lang_config. */
static void save_timezone_config(const char *tz_name)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                !strcmp(key, "TZ")) {
                continue; /* se reescribe al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "TZ=%s\n", tz_name);
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
/* Guarda DIM_TIMEOUT y DIM_PERCENT en armiga.cfg, preservando otras claves
 * (TZ, LANG), mismo patron que save_timezone_config/save_lang_config. */
static void save_dim_config(int timeout_sec, int dim_percent)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                (!strcmp(key, "DIM_TIMEOUT") || !strcmp(key, "DIM_PERCENT"))) {
                continue; /* se reescriben al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "DIM_TIMEOUT=%d\nDIM_PERCENT=%d\n", timeout_sec, dim_percent);
    fclose(f);
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
/* Guarda BRIGHTNESS_PCT en armiga.cfg, preservando otras claves,
 * mismo patron que save_dim_config/save_timezone_config. */
static void save_brightness_config(int pct)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                !strcmp(key, "BRIGHTNESS_PCT")) {
                continue; /* se reescribe al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "BRIGHTNESS_PCT=%d\n", pct);
    fclose(f);
}
/* Lee SSH_ENABLED de armiga.cfg. Default: activado (1). */
static int read_ssh_enabled(void)
{
    int enabled = 1;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (!f) return enabled;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[32], val[96];
        if (sscanf(line, "%31[^=]=%95s", key, val) == 2) {
            if (!strcmp(key, "SSH_ENABLED")) enabled = atoi(val);
        }
    }
    fclose(f);
    return enabled ? 1 : 0;
}
/* Guarda SSH_ENABLED en armiga.cfg, preservando otras claves,
 * mismo patron que save_brightness_config. */
static void save_ssh_enabled(int enabled)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                !strcmp(key, "SSH_ENABLED")) {
                continue; /* se reescribe al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "SSH_ENABLED=%d\n", enabled ? 1 : 0);
    fclose(f);
}
/* Aplica el estado SSH en caliente, sin reiniciar. */
static void apply_ssh_enabled(int enabled)
{
    if (enabled)
        system("/etc/init.d/S50dropbear start >/dev/null 2>&1");
    else
        system("/etc/init.d/S50dropbear stop >/dev/null 2>&1");
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

/* Guarda SAMBA_ENABLED en armiga.cfg, preservando otras claves,
 * mismo patron que save_ssh_enabled. */
static void save_samba_enabled(int enabled)
{
    char lines[32][128];
    int n = 0;
    FILE *f = fopen(ARMIGA_CONFIG_PATH, "r");
    if (f) {
        while (n < 32 && fgets(lines[n], sizeof(lines[n]), f)) {
            char key[32], val[96];
            if (sscanf(lines[n], "%31[^=]=%95s", key, val) == 2 &&
                !strcmp(key, "SAMBA_ENABLED")) {
                continue; /* se reescribe al final, no duplicar */
            }
            n++;
        }
        fclose(f);
    }
    f = fopen(ARMIGA_CONFIG_PATH, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fputs(lines[i], f);
    fprintf(f, "SAMBA_ENABLED=%d\n", enabled ? 1 : 0);
    fclose(f);
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
static void send_led_payload(int brightness,
                              int r_right, int g_right, int b_right,
                              int r_left, int g_left, int b_left)
{
    system("echo 1 > /sys/class/leds/rgb:kbd_backlight/brightness 2>/dev/null");

    int fd = open(LED_SERIAL_DEV, O_WRONLY | O_NOCTTY);
    if (fd < 0) return;

    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetispeed(&tio, B115200);
        cfsetospeed(&tio, B115200);
        tcsetattr(fd, TCSANOW, &tio);
    }

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
    close(fd);
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
    strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", tm_now);
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
        execlp("tar", "tar", "caf", backup_path,
               "armiga.cfg", "wifi.conf",
               "retroarch/retroarch.cfg", "retroarch/config",
               "retroarch/saves", "retroarch/states",
               "retroarch/playlists", "retroarch/thumbnails",
               (char *)NULL);
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
static int list_backups(char names[][64], int max_names)
{
    FILE *p = popen("ls -t " BACKUP_DIR "/backup_*.tar.gz 2>/dev/null", "r");
    if (!p) return 0;
    int n = 0;
    char line[256];
    while (n < max_names && fgets(line, sizeof(line), p)) {
        line[strcspn(line, "\r\n")] = 0;
        const char *base = strrchr(line, '/');
        base = base ? base + 1 : line;
        strncpy(names[n], base, 63);
        names[n][63] = 0;
        n++;
    }
    pclose(p);
    return n;
}
/* Borra un backup por nombre (sin system(), sin riesgo de inyeccion). */
static void delete_backup(const char *filename)
{
    char path[288];
    snprintf(path, sizeof(path), BACKUP_DIR "/%s", filename);
    unlink(path);
}
static void restore_backup(const char *filename)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "cd /media/amiga_data && tar xaf " BACKUP_DIR "/%s 2>/dev/null", filename);
    system(cmd);
}
static void read_release(char *kernel, char *mesa, char *retroarch, char *sdl3,
                         char *build_date, char *version, char *build_number)
{
    safe_copy(kernel,     "?", 32);
    safe_copy(mesa,       "?", 32);
    safe_copy(retroarch,  "?", 32);
    safe_copy(sdl3,       "?", 32);
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
            if (!strcmp(key, "RETROARCH_VERSION")) safe_copy(retroarch, val, 32);
            if (!strcmp(key, "SDL3_VERSION"))      safe_copy(sdl3,      val, 32);
            if (build_date && !strcmp(key, "BUILD_DATE")) safe_copy(build_date, val, 24);
            if (version && !strcmp(key, "ARMIGA_VERSION")) safe_copy(version, val, 32);
            if (build_number && !strcmp(key, "BUILD_NUMBER")) safe_copy(build_number, val, 16);
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
static void draw_statusbar(SDL_Renderer *ren, TTF_Font *f,
                            const char *time_str, bool wifi_up, int battery)
{
    SDL_Color c_white  = COL_WHITE;
    SDL_Color c_green  = COL_GREEN;
    SDL_Color c_gray   = COL_GRAY;
    SDL_Color c_red    = COL_RED;
    (void)c_red;
    float right = SCREEN_W - 20.0f;
    float y     = 18.0f;
    float gap   = 10.0f;
    int w, h;

    /* BATERIA */
    char batt_buf[8];
    SDL_Color batt_col = c_white;
    if (battery >= 0) {
        snprintf(batt_buf, sizeof(batt_buf), "%d%%", battery);
        if (battery <= 20)      batt_col = c_red;
        else if (battery <= 70) batt_col = (SDL_Color){255, 165, 0, 255}; /* naranja */
        else                    batt_col = c_green;
    } else {
        strncpy(batt_buf, "--", sizeof(batt_buf));
    }
    TTF_GetStringSize(f, batt_buf, 0, &w, &h);
    draw_text(ren, f, batt_buf, batt_col, right - (float)w, y);
    right -= (float)w + gap;

    /* Separador */
    TTF_GetStringSize(f, "|", 0, &w, &h);
    draw_text(ren, f, "|", c_gray, right - (float)w, y);
    right -= (float)w + gap;

    /* WIFI */
    const char *wifi_lbl = "WIFI";
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
    right -= (float)w + gap;

    /* Separador */
    TTF_GetStringSize(f, "|", 0, &w, &h);
    draw_text(ren, f, "|", c_gray, right - (float)w, y);
    right -= (float)w + gap;

    /* SSH */
    SDL_Color c_dim = {38, 38, 38, 255};
    int ssh_on = read_ssh_enabled();
    SDL_Color ssh_col = ssh_on ? c_green : c_dim;
    TTF_GetStringSize(f, "SSH", 0, &w, &h);
    draw_text(ren, f, "SSH", ssh_col, right - (float)w, y);
    right -= (float)w + gap;

    /* SAMBA */
    int samba_on = read_samba_enabled();
    SDL_Color samba_col = samba_on ? c_green : c_dim;
    TTF_GetStringSize(f, "SAMBA", 0, &w, &h);
    draw_text(ren, f, "SAMBA", samba_col, right - (float)w, y);
}

/* Dibuja footer unificado: leyenda izquierda + version derecha */
static void draw_footer(SDL_Renderer *ren, TTF_Font *f,
                        const char *legend, const char *version)
{
    SDL_Color c_gray    = COL_GRAY;
    SDL_Color c_dkgreen = COL_DKGREEN;
    draw_text(ren, f, legend, c_gray, 20.0f, 448.0f);
    draw_text_right(ren, f, version, c_dkgreen, SCREEN_W - 20.0f, 448.0f);
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
static void draw_rounded_rect_filled(SDL_Renderer *r, float x, float y,
                                      float w, float h, float radius,
                                      SDL_Color c)
{
    if (radius <= 0.0f || radius * 2.0f > h || radius * 2.0f > w) {
        draw_rect_filled(r, x, y, w, h, c);
        return;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    int rows = (int)h;
    for (int row = 0; row < rows; row++) {
        float dy = (float)row;
        float inset = 0.0f;
        /* Distancia vertical al centro de la esquina mas cercana
         * (arriba o abajo), solo relevante dentro de la franja radius. */
        float corner_dy = -1.0f;
        if (dy < radius) {
            corner_dy = radius - dy - 0.5f;
        } else if (dy > h - radius - 1.0f) {
            corner_dy = dy - (h - radius) + 0.5f;
        }
        if (corner_dy >= 0.0f && corner_dy <= radius) {
            float dx2 = radius * radius - corner_dy * corner_dy;
            inset = radius - (dx2 > 0.0f ? SDL_sqrtf(dx2) : 0.0f);
        }
        SDL_FRect line_rect = {x + inset, y + dy, w - inset * 2.0f, 1.0f};
        if (line_rect.w > 0.0f) SDL_RenderFillRect(r, &line_rect);
    }
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

    SDL_Window *win = SDL_CreateWindow("armiga",
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN);
    if (!win) { TTF_Quit(); SDL_Quit(); return 1; }

    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    if (!ren) { SDL_DestroyWindow(win); TTF_Quit(); SDL_Quit(); return 1; }

    TTF_Font *f_med   = TTF_OpenFont(FONT_PATH, FONT_MED);
    TTF_Font *f_sm    = TTF_OpenFont(FONT_PATH, FONT_SM);
    if (!f_med || !f_sm) {
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
    char s_kernel[32], s_mesa[32], s_retroarch[32], s_sdl3[32], s_build_date[24], s_version[32], s_build_number[16];
    read_release(s_kernel, s_mesa, s_retroarch, s_sdl3, s_build_date, s_version, s_build_number);

    /* Joystick */
    SDL_Joystick *joy = NULL;
    int nj = 0;
    SDL_JoystickID *jids = SDL_GetJoysticks(&nj);
    if (jids && nj > 0) joy = SDL_OpenJoystick(jids[0]);
    SDL_free(jids);

    int selected = 0;
    int dev_selected = 0;
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
    int dim_max_brightness = read_max_brightness();
    int ssh_enabled = read_ssh_enabled(); /* aplicado ya por S51ssh-toggle en boot, solo reflejar estado en UI */
    int samba_enabled = read_samba_enabled(); /* aplicado ya por S53samba-toggle en boot, solo reflejar estado en UI */
    int dim_saved_brightness = -1; /* brillo del usuario antes de atenuar, -1 = no atenuado */
    bool dim_active = false;
    Uint64 last_input_ticks = SDL_GetTicks();
    char kb_buffer[64] = "";
    int  kb_row = 0;
    int  kb_col = 0;
    int  kb_mode = KB_MODE_LOWER;
    AppState kb_return_state = STATE_WIFI_CONFIG;
    AppState state = STATE_MENU;
    AppState prev_state = STATE_MENU;
    int menu_axis_prev = 0; /* reset al re-entrar a STATE_MENU, evita movimiento fantasma (B05) */
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
            if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN ||
                ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION || ev.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
                last_input_ticks = SDL_GetTicks();
                if (dim_active) {
                    write_brightness(dim_saved_brightness);
                    dim_active = false;
                    set_cpu_governor("performance");
                }
            }
            if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                if (state == STATE_MENU) running = false;
                else if (state == STATE_CONFIRM) state = STATE_DEVMODE;
                else if (state == STATE_DEVMODE) state = STATE_MENU;
                else if (state == STATE_SYSINFO) state = STATE_MENU;
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
                    int v = ev.jaxis.value;
                    int zone = (v < -16000) ? -1 : (v > 16000) ? 1 : 0;
                    if (zone != menu_axis_prev) {
                        if (zone == -1)
                            selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
                        else if (zone == 1)
                            selected = (selected + 1) % MENU_COUNT;
                        menu_axis_prev = zone;
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
            else if (state == STATE_SETTINGS) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP)
                        settings_selected = (settings_selected - 1 + SETTINGS_MENU_COUNT) % SETTINGS_MENU_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        settings_selected = (settings_selected + 1) % SETTINGS_MENU_COUNT;
                    if (ev.key.key == SDLK_RETURN && settings_selected == 0) {
                        read_wifi_conf(wifi_ssid, sizeof(wifi_ssid), wifi_password, sizeof(wifi_password));
                        wifi_field_selected = 0;
                        wifi_show_password = false;
                        state = STATE_WIFI_CONFIG;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 1) {
                        backup_selected = 0;
                        state = STATE_BACKUP_MENU;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 2) {
                        led_selected = 0;
                        state = STATE_LED_CONFIG;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 3) {
                        state = STATE_TIMEZONE_CONFIG;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 4) {
                        dim_field_selected = 0;
                        state = STATE_SCREENDIM_CONFIG;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 5) {
                        state = STATE_BRIGHTNESS_CONFIG;
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 6) {
                        ssh_enabled = !ssh_enabled;
                        save_ssh_enabled(ssh_enabled);
                        apply_ssh_enabled(ssh_enabled);
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 7) {
                        samba_enabled = !samba_enabled;
                        save_samba_enabled(samba_enabled);
                        apply_samba_enabled(samba_enabled);
                    }
                    if (ev.key.key == SDLK_RETURN && settings_selected == 8) {
                        confirm_target = SETTINGS_ACTION_FACTORY_RESET;
                        confirm_return_state = STATE_SETTINGS;
                        state = STATE_CONFIRM;
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        settings_selected = (settings_selected - 1 + SETTINGS_MENU_COUNT) % SETTINGS_MENU_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        settings_selected = (settings_selected + 1) % SETTINGS_MENU_COUNT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 0) {
                    read_wifi_conf(wifi_ssid, sizeof(wifi_ssid), wifi_password, sizeof(wifi_password));
                    wifi_field_selected = 0;
                    wifi_show_password = false;
                    state = STATE_WIFI_CONFIG;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 1) {
                    backup_selected = 0;
                    state = STATE_BACKUP_MENU;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 2) {
                    led_selected = 0;
                    state = STATE_LED_CONFIG;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 3) {
                    state = STATE_TIMEZONE_CONFIG;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 4) {
                    dim_field_selected = 0;
                    state = STATE_SCREENDIM_CONFIG;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 5) {
                    state = STATE_BRIGHTNESS_CONFIG;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 6) {
                    ssh_enabled = !ssh_enabled;
                    save_ssh_enabled(ssh_enabled);
                    apply_ssh_enabled(ssh_enabled);
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 7) {
                    samba_enabled = !samba_enabled;
                    save_samba_enabled(samba_enabled);
                    apply_samba_enabled(samba_enabled);
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A && settings_selected == 8) {
                    confirm_target = SETTINGS_ACTION_FACTORY_RESET;
                    confirm_return_state = STATE_SETTINGS;
                    state = STATE_CONFIRM;
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
            else if (state == STATE_TIMEZONE_CONFIG) {
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_UP)
                        timezone_selected = (timezone_selected - 1 + TIMEZONE_LIST_COUNT) % TIMEZONE_LIST_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        timezone_selected = (timezone_selected + 1) % TIMEZONE_LIST_COUNT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        timezone_selected = (timezone_selected - 1 + TIMEZONE_LIST_COUNT) % TIMEZONE_LIST_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        timezone_selected = (timezone_selected + 1) % TIMEZONE_LIST_COUNT;
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
                    if (ev.key.key == SDLK_UP)
                        dim_field_selected = (dim_field_selected - 1 + 2) % 2;
                    if (ev.key.key == SDLK_DOWN)
                        dim_field_selected = (dim_field_selected + 1) % 2;
                    if (ev.key.key == SDLK_LEFT) {
                        if (dim_field_selected == 0)
                            dim_timeout_selected = (dim_timeout_selected - 1 + DIM_TIMEOUT_COUNT) % DIM_TIMEOUT_COUNT;
                        else {
                            dim_percent -= 5;
                            if (dim_percent < 5) dim_percent = 5;
                        }
                    }
                    if (ev.key.key == SDLK_RIGHT) {
                        if (dim_field_selected == 0)
                            dim_timeout_selected = (dim_timeout_selected + 1) % DIM_TIMEOUT_COUNT;
                        else {
                            dim_percent += 5;
                            if (dim_percent > 95) dim_percent = 95;
                        }
                    }
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        dim_field_selected = (dim_field_selected - 1 + 2) % 2;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        dim_field_selected = (dim_field_selected + 1) % 2;
                    else if (ev.jhat.value == SDL_HAT_LEFT) {
                        if (dim_field_selected == 0)
                            dim_timeout_selected = (dim_timeout_selected - 1 + DIM_TIMEOUT_COUNT) % DIM_TIMEOUT_COUNT;
                        else {
                            dim_percent -= 5;
                            if (dim_percent < 5) dim_percent = 5;
                        }
                    }
                    else if (ev.jhat.value == SDL_HAT_RIGHT) {
                        if (dim_field_selected == 0)
                            dim_timeout_selected = (dim_timeout_selected + 1) % DIM_TIMEOUT_COUNT;
                        else {
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
                        set_cpu_governor("performance");
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
                    if (ev.key.key == SDLK_UP)
                        backup_selected = (backup_selected - 1 + BACKUP_MENU_COUNT) % BACKUP_MENU_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        backup_selected = (backup_selected + 1) % BACKUP_MENU_COUNT;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        backup_selected = (backup_selected - 1 + BACKUP_MENU_COUNT) % BACKUP_MENU_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        backup_selected = (backup_selected + 1) % BACKUP_MENU_COUNT;
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
                    if (ev.key.key == SDLK_UP)
                        led_selected = (led_selected - 1 + LED_SLIDER_COUNT) % LED_SLIDER_COUNT;
                    if (ev.key.key == SDLK_DOWN)
                        led_selected = (led_selected + 1) % LED_SLIDER_COUNT;
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
                    if (ev.jhat.value == SDL_HAT_UP)
                        led_selected = (led_selected - 1 + LED_SLIDER_COUNT) % LED_SLIDER_COUNT;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        led_selected = (led_selected + 1) % LED_SLIDER_COUNT;
                    else if (ev.jhat.value == SDL_HAT_LEFT) {
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
                    if (ev.key.key == SDLK_UP)
                        backup_list_selected = (backup_list_selected - 1 + backup_count) % backup_count;
                    if (ev.key.key == SDLK_DOWN)
                        backup_list_selected = (backup_list_selected + 1) % backup_count;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION && backup_count > 0) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        backup_list_selected = (backup_list_selected - 1 + backup_count) % backup_count;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        backup_list_selected = (backup_list_selected + 1) % backup_count;
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
                    if (ev.key.key == SDLK_UP)
                        wifi_field_selected = (wifi_field_selected - 1 + 2) % 2;
                    if (ev.key.key == SDLK_DOWN)
                        wifi_field_selected = (wifi_field_selected + 1) % 2;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
                    if (ev.jhat.value == SDL_HAT_UP)
                        wifi_field_selected = (wifi_field_selected - 1 + 2) % 2;
                    else if (ev.jhat.value == SDL_HAT_DOWN)
                        wifi_field_selected = (wifi_field_selected + 1) % 2;
                }
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_SELECT)
                    wifi_show_password = !wifi_show_password;
                if (ev.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN &&
                    ev.jbutton.button == BTN_SDL_A) {
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
                upd_check_frame_shown = false;
                upd_verify_dl_started = false;
            } else if (action == ACTION_INFO) {
                state = STATE_SYSINFO;
                last_sysinfo_update = 0; /* forzar refresco inmediato */
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
            last_status_update = now_ticks;
        }
        /* Check de actualizacion en background: se lanza una sola vez,
         * 2s despues de arrancar (da tiempo a que el WiFi conecte),
         * sin bloquear la UI ni interferir con STATE_UPDATE. */
        if (!bg_update_checked) {
            if (bg_check_start_delay == 0) bg_check_start_delay = now_ticks;
            if (s_bgcheck_pid == -1 && now_ticks - bg_check_start_delay >= 2000) {
                unlink(BG_CHECK_JSON_TMP);
                s_bgcheck_pid = spawn_curl_to_file(GITHUB_API_URL, BG_CHECK_JSON_TMP, "10");
            } else if (s_bgcheck_pid != -1) {
                int r = poll_curl_pid(&s_bgcheck_pid, BG_CHECK_JSON_TMP, 1);
                if (r != 0) {
                    bg_update_checked = true;
                    if (r > 0) {
                        int res = finish_check_update(s_version,
                                               bg_new_ver, sizeof(bg_new_ver),
                                               bg_dl_url,  sizeof(bg_dl_url),
                                               bg_sha_url, sizeof(bg_sha_url));
                        bg_update_available = (res == 1);
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
                        int res = finish_check_update(s_version,
                                               upd_new_ver, sizeof(upd_new_ver),
                                               upd_dl_url,  sizeof(upd_dl_url),
                                               upd_sha_url, sizeof(upd_sha_url));
                        if (res == 1)       update_phase = UPD_CONFIRM;
                        else if (res == 0)  update_phase = UPD_NO_UPDATE;
                        else { update_phase = UPD_ERROR;
                               strncpy(upd_msg, "Error al conectar con el servidor.", sizeof(upd_msg)); }
                    } else {
                        update_phase = UPD_ERROR;
                        strncpy(upd_msg, "Error al conectar con el servidor.", sizeof(upd_msg));
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
                        strncpy(upd_msg, "Error en la descarga.", sizeof(upd_msg));
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
                                strncpy(upd_msg, "Error de verificacion SHA256.", sizeof(upd_msg));
                                unlink(UPDATE_IMG);
                                unlink(UPDATE_SHA256);
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
                        strncpy(upd_msg, "Error de verificacion SHA256.", sizeof(upd_msg));
                        unlink(UPDATE_IMG);
                        unlink(UPDATE_SHA256);
                    }
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
            snprintf(sysinfo_build, sizeof(sysinfo_build), "%s (%s)", s_build_date, s_build_number);
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
                int text_w = 0, text_h = 0;
                TTF_GetStringSize(f_med, MENU_ITEMS[i][current_lang], 0, &text_w, &text_h);
                float sel_w = 46.0f + (float)text_w + 40.0f; /* icono+texto + espacio para "> " */
                draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                 sel_w, item_h - 2.0f, 8.0f, c_selbg);
                draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                 4.0f, item_h - 2.0f, c_green);
                draw_text(ren, f_med, MENU_ICONS[i], c_green, mx + 8.0f, iy);
                draw_text(ren, f_med, MENU_ITEMS[i][current_lang], c_green, mx + 46.0f, iy);
                draw_text(ren, f_med, ">", c_green, mx + mw - 20.0f, iy);
                if (i == 1 && bg_update_available) {
                    int tw2 = 0, th2 = 0;
                    TTF_GetStringSize(f_med, MENU_ITEMS[i][current_lang], 0, &tw2, &th2);
                    float bx = mx + 46.0f + (float)tw2 + 12.0f;
                    float by = iy + 2.0f;
                    SDL_Color c_red_badge = {220, 40, 40, 255};
                    draw_rect_filled(ren, bx, by, 16.0f, 16.0f, c_red_badge);
                    /* Flecha hacia arriba: triangulo + tallo, en blanco sobre el badge rojo */
                    SDL_Color c_white_arrow = {255, 255, 255, 255};
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 4.0f, by + 9.0f, c_white_arrow);
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 12.0f, by + 9.0f, c_white_arrow);
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 8.0f, by + 13.0f, c_white_arrow);
                }
            } else {
                draw_text(ren, f_med, MENU_ICONS[i], c_gray, mx + 8.0f, iy);
                draw_text(ren, f_med, MENU_ITEMS[i][current_lang], c_gray, mx + 46.0f, iy);
                draw_text(ren, f_med, ">", c_gray, mx + mw - 20.0f, iy);
                if (i == 1 && bg_update_available) {
                    int tw2 = 0, th2 = 0;
                    TTF_GetStringSize(f_med, MENU_ITEMS[i][current_lang], 0, &tw2, &th2);
                    float bx = mx + 46.0f + (float)tw2 + 12.0f;
                    float by = iy + 2.0f;
                    SDL_Color c_red_badge = {220, 40, 40, 255};
                    draw_rect_filled(ren, bx, by, 16.0f, 16.0f, c_red_badge);
                    SDL_Color c_white_arrow = {255, 255, 255, 255};
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 4.0f, by + 9.0f, c_white_arrow);
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 12.0f, by + 9.0f, c_white_arrow);
                    draw_line(ren, bx + 8.0f, by + 3.0f, bx + 8.0f, by + 13.0f, c_white_arrow);
                }
            }
        }

        /* Panel derecho: contexto de la opcion seleccionada */
        {
            draw_text(ren, f_sm, MENU_ITEMS[selected][current_lang], c_green, rx, menu_y0);
            /* Descripcion en dos lineas */
            const char *desc = MENU_DESC[selected][current_lang];
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
        draw_footer(ren, f_sm, tr("[B] Seleccionar  [DPAD] Navegar  [L1] Idioma", "[B] Select  [DPAD] Navigate  [L1] Language"), s_version);

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

        } else if (state == STATE_SETTINGS) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración", "Menu > Settings"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            float settings_y0 = 64.0f;
            float settings_item_h = 30.0f;
            for (int i = 0; i < SETTINGS_MENU_COUNT; i++) {
                float iy = settings_y0 + i * settings_item_h;
                char item_label[64];
                if (i == 6) {
                    snprintf(item_label, sizeof(item_label), "%s: %s",
                             SETTINGS_MENU_ITEMS[i][current_lang],
                             ssh_enabled ? tr("Activado", "Enabled") : tr("Desactivado", "Disabled"));
                } else if (i == 7) {
                    snprintf(item_label, sizeof(item_label), "%s: %s",
                             SETTINGS_MENU_ITEMS[i][current_lang],
                             samba_enabled ? tr("Activado", "Enabled") : tr("Desactivado", "Disabled"));
                } else {
                    safe_copy(item_label, SETTINGS_MENU_ITEMS[i][current_lang], sizeof(item_label));
                }
                if (i == settings_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_med, item_label, 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 24.0f; /* padding izquierdo (8) + derecho (16) */
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, settings_item_h - 2.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, settings_item_h - 2.0f, c_green);
                    draw_text(ren, f_med, item_label, c_green, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_med, item_label, c_gray, mx + 8.0f, iy);
                }
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, tr("[B] Seleccionar  [A] Volver", "[B] Select  [A] Back"), s_version);

        } else if (state == STATE_BRIGHTNESS_CONFIG) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Brillo de pantalla", "Menu > Settings > Screen Brightness"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            {
                float iy = 90.0f;
                float bar_w = 220.0f, bar_h = 10.0f;
                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d%%", brightness_pct);
                draw_text(ren, f_sm, tr("Brillo", "Brightness"), c_green, mx + 8.0f, iy);
                draw_text(ren, f_med, valbuf, c_white, mx + 8.0f, iy + 16.0f);
                float frac = brightness_pct / 100.0f;
                draw_rect_filled(ren, mx + 8.0f, iy + 44.0f, bar_w, bar_h, c_gray);
                draw_rect_filled(ren, mx + 8.0f, iy + 44.0f, bar_w * frac, bar_h, c_green);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[<>] Ajustar  [B] Aplicar  [A] Volver", "[<>] Adjust  [B] Apply  [A] Back"), s_version);

        } else if (state == STATE_TIMEZONE_CONFIG) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Zona horaria", "Menu > Settings > Time Zone"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            float tz_y0 = 60.0f;
            float tz_item_h = 20.0f;
            int tz_visible = 18;
            int tz_scroll = 0;
            if (timezone_selected >= tz_visible)
                tz_scroll = timezone_selected - tz_visible + 1;
            if (tz_scroll > TIMEZONE_LIST_COUNT - tz_visible)
                tz_scroll = TIMEZONE_LIST_COUNT - tz_visible;
            if (tz_scroll < 0) tz_scroll = 0;

            for (int row = 0; row < tz_visible && (row + tz_scroll) < TIMEZONE_LIST_COUNT; row++) {
                int i = row + tz_scroll;
                float iy = tz_y0 + row * tz_item_h;
                bool sel = (i == timezone_selected);
                bool active = !strcmp(TIMEZONE_LIST[i].tz_name, timezone_current);
                if (sel) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_sm, TIMEZONE_LIST[i].label[current_lang], 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 3.0f,
                                     sel_w, tz_item_h - 2.0f, 6.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 3.0f,
                                     4.0f, tz_item_h - 2.0f, c_green);
                }
                SDL_Color labelc = sel ? c_green : c_gray;
                draw_text(ren, f_sm, TIMEZONE_LIST[i].label[current_lang], labelc, mx + 8.0f, iy);
                if (active)
                    draw_text(ren, f_sm, "*", c_green, mx + mw - 16.0f, iy);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[B] Aplicar  [A] Volver", "[B] Apply  [A] Back"), s_version);

        } else if (state == STATE_SCREENDIM_CONFIG) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Ahorro de pantalla", "Menu > Settings > Screen Dimming"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            float dim_y0 = 70.0f;
            float dim_item_h = 46.0f;
            float dim_bar_w = 220.0f;
            float dim_bar_h = 10.0f;

            {
                float iy = dim_y0;
                bool sel = (dim_field_selected == 0);
                SDL_Color labelc = sel ? c_green : c_gray;
                const char *dim_val_disp = DIM_TIMEOUT_LABELS[dim_timeout_selected][current_lang];
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, tr("Atenuar tras", "Dim after"), 0, &lw, &lh);
                    TTF_GetStringSize(f_med, dim_val_disp, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, dim_item_h - 8.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, dim_item_h - 8.0f, c_green);
                }
                draw_text(ren, f_sm, tr("Atenuar tras", "Dim after"), labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, dim_val_disp, c_white, mx + 8.0f, iy + 16.0f);
            }

            {
                float iy = dim_y0 + dim_item_h;
                bool sel = (dim_field_selected == 1);
                SDL_Color labelc = sel ? c_green : c_gray;
                if (sel) {
                    int lw = 0, lh = 0;
                    TTF_GetStringSize(f_sm, tr("Brillo al atenuar", "Brightness when dimmed"), 0, &lw, &lh);
                    float bar_total_w = dim_bar_w + 10.0f + 40.0f; /* barra + gap + "100%" aprox */
                    float sel_w = ((float)lw > bar_total_w ? (float)lw : bar_total_w) + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, dim_item_h - 8.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, dim_item_h - 8.0f, c_green);
                }
                draw_text(ren, f_sm, tr("Brillo al atenuar", "Brightness when dimmed"),
                          labelc, mx + 8.0f, iy);
                float bar_x = mx + 8.0f;
                float bar_y = iy + 20.0f;
                draw_rect_filled(ren, bar_x, bar_y, dim_bar_w, dim_bar_h, c_selbg);
                float frac = dim_percent / 100.0f;
                draw_rect_filled(ren, bar_x, bar_y, dim_bar_w * frac, dim_bar_h, c_green);
                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d%%", dim_percent);
                draw_text(ren, f_sm, valbuf, c_white, bar_x + dim_bar_w + 10.0f, iy + 16.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[B] Guardar  [A] Volver", "[B] Save  [A] Back"), s_version);

        } else if (state == STATE_BACKUP_MENU) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Copia de seguridad", "Menu > Settings > Backup"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);
            float bkm_y0 = 64.0f;
            float bkm_item_h = 30.0f;
            for (int i = 0; i < BACKUP_MENU_COUNT; i++) {
                float iy = bkm_y0 + i * bkm_item_h;
                if (i == backup_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_med, BACKUP_MENU_ITEMS[i][current_lang], 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, bkm_item_h - 2.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, bkm_item_h - 2.0f, c_green);
                    draw_text(ren, f_med, BACKUP_MENU_ITEMS[i][current_lang], c_green, mx + 8.0f, iy);
                } else {
                    draw_text(ren, f_med, BACKUP_MENU_ITEMS[i][current_lang], c_gray, mx + 8.0f, iy);
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
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, tr("[B] Seleccionar  [A] Volver", "[B] Select  [A] Back"), s_version);

        } else if (state == STATE_BACKUP_LIST) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Copia de seguridad > Restaurar copia", "Menu > Settings > Backup > Restore Backup"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);
            float bkl_y0 = 64.0f;
            float bkl_item_h = 26.0f;
            if (backup_count == 0) {
                draw_text(ren, f_sm, tr("No hay copias disponibles", "No backups available"), c_gray, mx, bkl_y0);
            } else {
                for (int i = 0; i < backup_count; i++) {
                    float iy = bkl_y0 + i * bkl_item_h;
                    if (i == backup_list_selected) {
                        int text_w = 0, text_h = 0;
                        TTF_GetStringSize(f_sm, backup_list[i], 0, &text_w, &text_h);
                        float sel_w = (float)text_w + 24.0f;
                        draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                         sel_w, bkl_item_h - 2.0f, 8.0f, c_selbg);
                        draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                         4.0f, bkl_item_h - 2.0f, c_green);
                        draw_text(ren, f_sm, backup_list[i], c_green, mx + 8.0f, iy);
                    } else {
                        draw_text(ren, f_sm, backup_list[i], c_gray, mx + 8.0f, iy);
                    }
                }
            }
            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, tr("[B] Restaurar  [X] Eliminar  [A] Volver", "[B] Restore  [X] Delete  [A] Back"), s_version);

        } else if (state == STATE_WIFI_CONFIG) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > Red inalámbrica", "Menu > Settings > Wireless Network"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            float wifi_y0 = 64.0f;
            float wifi_item_h = 44.0f;

            {
                float iy = wifi_y0;
                bool sel = (wifi_field_selected == 0);
                SDL_Color labelc = sel ? c_green : c_gray;
                const char *ssid_disp = wifi_ssid[0] ? wifi_ssid : "--";
                if (sel) {
                    int lw = 0, lh = 0, vw = 0, vh = 0;
                    TTF_GetStringSize(f_sm, "SSID", 0, &lw, &lh);
                    TTF_GetStringSize(f_med, ssid_disp, 0, &vw, &vh);
                    float sel_w = (float)(lw > vw ? lw : vw) + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, wifi_item_h - 6.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, wifi_item_h - 6.0f, c_green);
                }
                draw_text(ren, f_sm, "SSID", labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, ssid_disp, c_white, mx + 8.0f, iy + 16.0f);
            }

            {
                float iy = wifi_y0 + wifi_item_h;
                bool sel = (wifi_field_selected == 1);
                SDL_Color labelc = sel ? c_green : c_gray;
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
                    float sel_w = (float)(lw > vw ? lw : vw) + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, wifi_item_h - 6.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, wifi_item_h - 6.0f, c_green);
                }
                draw_text(ren, f_sm, tr("CONTRASEÑA", "PASSWORD"), labelc, mx + 8.0f, iy);
                draw_text(ren, f_med, masked, c_white, mx + 8.0f, iy + 16.0f);
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[B] Editar  [SELECT] Ver/Ocultar  [A] Guardar", "[B] Edit  [SELECT] Show/Hide  [A] Save"),
                s_version);

        } else if (state == STATE_LED_CONFIG) {
            draw_text_truncated(ren, f_sm, tr("Menú > Configuración > LED RGB analógicos", "Menu > Settings > Analog Stick LEDs"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

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

            for (int i = 0; i < LED_SLIDER_COUNT; i++) {
                float iy = led_y0 + i * led_item_h;
                bool sel = (led_selected == i);
                SDL_Color labelc = sel ? c_green : c_gray;
                if (sel) {
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     mw, led_item_h - 8.0f, 8.0f, c_selbg);
                    draw_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     4.0f, led_item_h - 8.0f, c_green);
                }
                draw_text(ren, f_sm, LED_SLIDER_LABELS[i][current_lang], labelc, mx + 8.0f, iy);

                float bar_x = mx + 180.0f;
                float bar_y = iy + 3.0f;
                draw_rect_filled(ren, bar_x, bar_y, led_bar_w, led_bar_h, c_selbg);
                float frac = led_vals_r[i] / 255.0f;
                draw_rect_filled(ren, bar_x, bar_y, led_bar_w * frac, led_bar_h, led_bar_colors[i]);

                char valbuf[8];
                snprintf(valbuf, sizeof(valbuf), "%d", led_vals_r[i]);
                draw_text(ren, f_sm, valbuf, c_white, bar_x + led_bar_w + 10.0f, iy);
            }

            SDL_Color preview_right = {(Uint8)led_r_right, (Uint8)led_g_right, (Uint8)led_b_right, 255};
            SDL_Color preview_left  = {(Uint8)led_r_left,  (Uint8)led_g_left,  (Uint8)led_b_left,  255};
            float preview_y = led_y0 + LED_SLIDER_COUNT * led_item_h + 10.0f;
            draw_text(ren, f_sm, tr("Vista previa:", "Preview:"), c_gray, mx, preview_y);
            draw_rect_filled(ren, mx + 100.0f, preview_y - 2.0f, 30.0f, 16.0f, preview_left);
            draw_rect_filled(ren, mx + 140.0f, preview_y - 2.0f, 30.0f, 16.0f, preview_right);

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[<>] Ajustar  [L1/R1] +/-20  [A] Volver", "[<>] Adjust  [L1/R1] +/-20  [A] Back"),
                s_version);

        } else if (state == STATE_KEYBOARD) {
            draw_text(ren, f_sm,
                wifi_field_selected == 0 ? "SSID" : tr("CONTRASEÑA", "PASSWORD"),
                c_green, mx, 20.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            draw_rect_filled(ren, mx, 56.0f, SCREEN_W - 40.0f, 30.0f, c_selbg);
            draw_text(ren, f_med, kb_buffer[0] ? kb_buffer : "", c_white, mx + 8.0f, 62.0f);

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
                        draw_rounded_rect_filled(ren, kx, ky, kw, key_h, 4.0f, c_selbg);
                        draw_text_centered(ren, f_sm, label, c_green, kx + kw/2.0f, ky + key_h/2.0f - 6.0f);
                    } else {
                        draw_rounded_rect_filled(ren, kx, ky, kw, key_h, 4.0f, c_keybg);
                        draw_text_centered(ren, f_sm, label, c_gray, kx + kw/2.0f, ky + key_h/2.0f - 6.0f);
                    }
                }
            }

            draw_line(ren, mx, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm,
                tr("[B] Insertar [L1] Borrar [R1] Aceptar [A] Cancelar [SELECT] Mayus/Num", "[B] Insert [L1] Delete [R1] Accept [A] Cancel [SELECT] Caps/Num"),
                s_version);

        } else if (state == STATE_DEVMODE) {
            /* Titulo pequeño arriba a la izquierda */
            draw_text_truncated(ren, f_sm, tr("Menú > Modo desarrollador", "Menu > Developer Mode"), c_green, mx, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, mx, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);
            draw_line(ren, sep_x, 44.0f, sep_x, 438.0f, c_green);

            /* Menú (columna izquierda), mismo estilo compacto que el menu principal */
            float dev_y0 = 64.0f;
            float dev_item_h = 26.0f;

            for (int i = 0; i < DEV_MENU_COUNT; i++) {
                float iy = dev_y0 + i * dev_item_h;
                if (i == dev_selected) {
                    int text_w = 0, text_h = 0;
                    TTF_GetStringSize(f_sm, DEV_MENU_ITEMS[i], 0, &text_w, &text_h);
                    float sel_w = (float)text_w + 24.0f;
                    draw_rounded_rect_filled(ren, mx - 4.0f, iy - 4.0f,
                                     sel_w, dev_item_h - 2.0f, 8.0f, c_selbg);
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
            draw_text(ren, f_sm,  tr("BATERÍA", "BATTERY"),    c_green, rx, ry);
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
            draw_text_truncated(ren, f_sm, tr("Menú > Diagnóstico del sistema", "Menu > System Diagnostics"), c_green, SI_MX, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, SI_MX, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

            /* Separador vertical */
            draw_line(ren, SI_SEP, 44.0f, SI_SEP, 438.0f, c_green);

            /* Separadores horizontales entre bloques */
            draw_line(ren, SI_MX, SI_SEP_H1, SCREEN_W - 20.0f, SI_SEP_H1, c_dkgreen);
            draw_line(ren, SI_MX, SI_SEP_H2, SCREEN_W - 20.0f, SI_SEP_H2, c_dkgreen);

/* Macro auxiliar: título de bloque */
#define SI_BLOCK_TITLE(xpos, ypos, title) do { \
    draw_text(ren, f_sm, title, c_green, (xpos), (ypos)); \
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
            SI_BLOCK_TITLE(SI_MX, y, tr("SISTEMA", "SYSTEM"));
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Version OS", "OS Version"),       s_version);       y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Kernel",       s_kernel);        y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Arquitectura", "Architecture"), "aarch64");       y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Compilación", "Build"),        sysinfo_build);   y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Hostname",     "armiga");

            /* ── BLOQUE 1 DER: ESTADO ────────────────────────────────────── */
            y = SI_Y0 + 2.0f;
            SI_BLOCK_TITLE(SI_RX, y, tr("MÉTRICAS", "METRICS"));
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

                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Carga CPU", "CPU Load"),  sysinfo_cpu_usage, sysinfo_cpu_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Uso de RAM", "RAM Usage"),  dev_ram,           ram_pct);          y += SI_ROW_H;
                SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Temp CPU", "CPU Temp"), sysinfo_temp,      temp_pct);         y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Uptime",   dev_uptime);                          y += SI_ROW_H;
                SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "Load Avg", sysinfo_loadavg);
            }

            /* ── BLOQUE 2 IZQ: ALMACENAMIENTO ───────────────────────────── */
            y = SI_SEP_H1 + 4.0f;
            SI_BLOCK_TITLE(SI_MX, y, tr("VOLÚMENES", "VOLUMES"));
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

                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, tr("DH0: (Sistema)", "DH0: (System)"), sysinfo_disk_root, disk_root_pct); y += SI_ROW_H;
                SI_ROW_BAR(SI_MX, y, SI_MX + SI_CW_L, tr("DH1: (Datos)", "DH1: (Data)"),   sysinfo_disk_data, disk_data_pct); y += SI_ROW_H;
                /* Libre total en datos */
                {
                    char free_buf[32] = "--";
                    struct statvfs st;
                    if (statvfs("/media/amiga_data", &st) == 0) {
                        unsigned long long free_mb = (unsigned long long)st.f_bfree * st.f_frsize / (1024*1024);
                        if (free_mb >= 1024) snprintf(free_buf, sizeof(free_buf), "%.1f GB", free_mb / 1024.0);
                        else                 snprintf(free_buf, sizeof(free_buf), "%llu MB", free_mb);
                    }
                    SI_ROW(SI_MX, y, SI_MX + SI_CW_L, tr("Espacio disponible", "Free space"), free_buf);
                }
            }

            /* ── BLOQUE 2 DER: HARDWARE ──────────────────────────────────── */
            y = SI_SEP_H1 + 4.0f;
            SI_BLOCK_TITLE(SI_RX, y, tr("ESPECIFICACIONES", "SPECIFICATIONS"));
            y += 28.0f;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "CPU",           "Cortex-A53 @1.51GHz"); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "GPU",           "Mali-G31 (Panfrost)"); y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, "RAM",           "1 GB LPDDR4");         y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, tr("Almacenamiento", "Storage"),"microSD");              y += SI_ROW_H;
            SI_ROW(SI_RX, y, SI_RX + SI_CW_R, tr("Resolución", "Resolution"),    "640x480 @ 60Hz");

            /* ── BLOQUE 3 IZQ: SOFTWARE ──────────────────────────────────── */
            y = SI_SEP_H2 + 4.0f;
            SI_BLOCK_TITLE(SI_MX, y, tr("MOTOR DE EMULACIÓN", "EMULATION ENGINE"));
            y += 28.0f;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "RetroArch", s_retroarch); y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "Mesa",      s_mesa);      y += SI_ROW_H;
            SI_ROW(SI_MX, y, SI_MX + SI_CW_L, "SDL3",      s_sdl3);      y += SI_ROW_H;

            /* ── BLOQUE 3 DER: RED ───────────────────────────────────────── */
            y = SI_SEP_H2 + 4.0f;
            SI_BLOCK_TITLE(SI_RX, y, tr("CONECTIVIDAD", "CONNECTIVITY"));
            y += 28.0f;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "IP",          dev_ip);          y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "WiFi",        status_wifi_up ? tr("Conectado", "Connected") : tr("Desconectado", "Disconnected")); y += SI_ROW_H;
            SI_ROW_BAR(SI_RX, y, SI_RX + SI_CW_R, tr("Intensidad", "Signal"),  sysinfo_wifi_sig, sysinfo_wifi_pct >= 0 ? sysinfo_wifi_pct : 0); y += SI_ROW_H;
            SI_ROW    (SI_RX, y, SI_RX + SI_CW_R, "MAC",         sysinfo_mac);

#undef SI_BLOCK_TITLE
#undef SI_ROW
#undef SI_ROW_BAR

            /* Barra inferior */
            draw_line(ren, SI_MX, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            draw_footer(ren, f_sm, tr("[A] Volver", "[A] Back"), s_version);
        } else if (state == STATE_UPDATE) {
            const float UX = 20.0f;
            draw_text_truncated(ren, f_sm, tr("Menú > Actualización de sistema", "Menu > System Update"), c_green, UX, 20.0f, SCREEN_W - 190.0f);
            draw_statusbar(ren, f_sm, status_time, status_wifi_up, status_battery);
            draw_line(ren, UX, 44.0f, SCREEN_W - 20.0f, 44.0f, c_green);

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
                draw_line(ren, UX, 170.0f, SCREEN_W - 20.0f, 170.0f, c_dkgreen);
                draw_text(ren, f_med, tr("[B] Descargar e instalar", "[B] Download and install"), c_green,  UX,          188.0f);
                draw_text(ren, f_med, tr("[A] Cancelar", "[A] Cancel"),             c_gray,   UX + 260.0f, 188.0f);

            } else if (update_phase == UPD_DOWNLOADING) {
                draw_text_animdots(ren, f_sm, tr("Descargando actualización", "Downloading update"), c_white, UX, 100.0f, now_ticks);
                /* Barra de progreso */
                int pct = (int)(upd_progress * 100.0f);
                char pct_buf[8]; snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
                draw_bar(ren, f_sm, pct, c_green, c_dkgreen, UX, 122.0f, 30);
                draw_text(ren, f_sm, pct_buf, c_white, UX + 280.0f, 122.0f);
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

            draw_line(ren, UX, 438.0f, SCREEN_W - 20.0f, 438.0f, c_green);
            if (update_phase != UPD_DOWNLOADING)
                draw_footer(ren, f_sm, tr("[A] Volver", "[A] Back"), s_version);
            else
                draw_footer(ren, f_sm, "", s_version);

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
        SDL_Delay(16); /* ~60fps cap, evita CPU al 100% */
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
    if (joy) SDL_CloseJoystick(joy);
    TTF_CloseFont(f_sm);
    TTF_CloseFont(f_med);
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
            redirect_stdio_to_local_console();
            execl("/sbin/reboot", "reboot", (char *)NULL);
            fprintf(stderr, "armiga-launcher: no se pudo ejecutar reboot: %s\n",
                    strerror(errno));
            return 1;
        case EXEC_SHUTDOWN:
            redirect_stdio_to_local_console();
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
