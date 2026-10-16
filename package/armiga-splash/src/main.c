/* armiga-splash: splash de arranque de ARMIGA.
 *
 * Pinta en /dev/fb0:
 *   1. la imagen /usr/share/armiga/splash.png (o splash.jpg, si no hay PNG),
 *      centrada sobre fondo negro;
 *   2. dos lineas de texto abajo a la izquierda (version, build y fecha de
 *      /etc/armiga-release), con la fuente PSF2 del sistema (se lee con zlib);
 *   3. una barra de progreso que avanza por tiempo hasta PROGRESS_MAX.
 *
 * La barra restaura el fondo desde una copia en memoria, asi que funciona
 * con cualquier imagen. Para cambiar el splash basta con sustituir la imagen.
 * Se mantiene vivo hasta que armiga-launcher-wrapper lo mata (SIGTERM).
 *
 * Formato framebuffer: 32bpp, B,G,R,X en memoria (LE).
 *
 * Prueba en PC: gcc -DSPLASH_TEST main.c -o t -lz -lm
 *               ./t fichero_fb.raw <segundos_simulados>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <zlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#define FB_DEV        "/dev/fb0"
#define IMG_PNG       "/usr/share/armiga/splash.png"
#define IMG_JPG       "/usr/share/armiga/splash.jpg"
#define RELEASE_PATH  "/etc/armiga-release"
#define FONT_PATH     "/usr/share/consolefonts/lat4-10.psfu.gz"

/* --- Diseno: todo lo ajustable esta aqui --------------------------------- */
#define BAR_MARGIN_X   16        /* margen izquierdo y derecho de la barra */
#define BAR_H          6         /* alto de la barra, borde incluido */
#define BAR_BOTTOM     56        /* de la parte alta de la barra al borde inferior */
#define TEXT_MARGIN_X  16
#define TEXT_BOTTOM    40        /* de la primera linea de texto al borde inferior */
#define LINE_GAP       3

#define COL_TEXT   0xAAAAAAu
#define COL_TRACK  0x15181Fu
#define COL_BORDER 0x2A2F3Au
#define COL_FILL   0xE0193Au

/* La barra sube como 1-exp(-t/TAU), con tope en PROGRESS_MAX: el arranque
 * no tiene un progreso medible y asi nunca llega a 100 % antes de tiempo. */
#define PROGRESS_MAX   0.95
#define PROGRESS_TAU   4.0
#define FRAME_US       50000
/* ------------------------------------------------------------------------- */

#define PSF2_MAGIC 0x864ab572u

struct psf2_header {
    unsigned int magic, version, headersize, flags;
    unsigned int numglyph, bytesperglyph, height, width;
};

#ifndef SPLASH_TEST
static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }
#endif

static unsigned char *fbmem = NULL;   /* framebuffer mapeado */
static unsigned char *bg = NULL;      /* copia de lo que hay bajo la barra */
static int fb_w = 0, fb_h = 0, fb_stride = 0;

static unsigned char *g_glyphs = NULL;
static unsigned int g_font_w = 8, g_font_h = 16, g_bytes_per_row = 1;
static unsigned int g_bytes_per_glyph = 16, g_numglyph = 0;

#ifdef SPLASH_TEST
static const char *cfg(const char *env, const char *def)
{
    const char *v = getenv(env);
    return v ? v : def;
}
#else
#define cfg(env, def) (def)
#endif

static int load_psf_font(const char *path)
{
    gzFile gz = gzopen(path, "rb");
    if (!gz) return -1;

    size_t cap = 64 * 1024, len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) { gzclose(gz); return -1; }

    for (;;) {
        if (len == cap) {
            cap *= 2;
            unsigned char *nb = realloc(buf, cap);
            if (!nb) { free(buf); gzclose(gz); return -1; }
            buf = nb;
        }
        int n = gzread(gz, buf + len, (unsigned)(cap - len));
        if (n < 0) { free(buf); gzclose(gz); return -1; }
        if (n == 0) break;
        len += (size_t)n;
    }
    gzclose(gz);

    struct psf2_header h;
    if (len < sizeof(h)) { free(buf); return -1; }
    memcpy(&h, buf, sizeof(h));
    if (h.magic != PSF2_MAGIC) { free(buf); return -1; }

    g_font_w = h.width;
    g_font_h = h.height;
    g_bytes_per_row = (g_font_w + 7) / 8;
    g_bytes_per_glyph = h.bytesperglyph;
    g_numglyph = h.numglyph;

    size_t glyphs_size = (size_t)g_numglyph * g_bytes_per_glyph;
    if (h.headersize + glyphs_size > len) { free(buf); return -1; }

    g_glyphs = malloc(glyphs_size);
    if (!g_glyphs) { free(buf); return -1; }
    memcpy(g_glyphs, buf + h.headersize, glyphs_size);
    free(buf);
    return 0;
}

static void put_px(unsigned char *buf, int x, int y, unsigned int rgb)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    unsigned char *p = buf + (size_t)y * fb_stride + (size_t)x * 4;
    p[0] = rgb & 0xFF;
    p[1] = (rgb >> 8) & 0xFF;
    p[2] = (rgb >> 16) & 0xFF;
    p[3] = 0;
}

static void fill_rect(unsigned char *buf, int x, int y, int w, int h, unsigned int rgb)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            put_px(buf, x + xx, y + yy, rgb);
}

static void draw_char(unsigned char *buf, int x, int y, unsigned char ch)
{
    if (!g_glyphs || ch >= g_numglyph) return;
    const unsigned char *rows = g_glyphs + (size_t)ch * g_bytes_per_glyph;
    for (unsigned int ry = 0; ry < g_font_h; ry++) {
        const unsigned char *row = rows + ry * g_bytes_per_row;
        for (unsigned int rx = 0; rx < g_font_w; rx++)
            if (row[rx / 8] & (1 << (7 - (rx % 8))))
                put_px(buf, x + (int)rx, y + (int)ry, COL_TEXT);
    }
}

static void draw_text(unsigned char *buf, int x, int y, const char *s)
{
    for (; *s; s++, x += (int)g_font_w + 1)
        draw_char(buf, x, y, (unsigned char)*s);
}

static void read_release(char *version, size_t vsz, char *build, size_t bsz,
                         char *date, size_t dsz)
{
    snprintf(version, vsz, "?");
    snprintf(build, bsz, "?");
    snprintf(date, dsz, "?");
    FILE *f = fopen(cfg("ARMIGA_RELEASE", RELEASE_PATH), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[64], val[64];
        if (sscanf(line, "%63[^=]=%63[^\n]", key, val) == 2) {
            if (!strcmp(key, "ARMIGA_VERSION")) snprintf(version, vsz, "%s", val);
            if (!strcmp(key, "BUILD_NUMBER"))   snprintf(build, bsz, "%s", val);
            if (!strcmp(key, "BUILD_DATE"))     snprintf(date, dsz, "%s", val);
        }
    }
    fclose(f);
}

/* Copia la imagen en bg, centrada y recortada si no cabe. Fondo negro. */
static void blit_image(void)
{
    int iw = 0, ih = 0, n = 0;
    unsigned char *img = stbi_load(cfg("ARMIGA_SPLASH_IMG", IMG_PNG), &iw, &ih, &n, 4);
    if (!img) img = stbi_load(IMG_JPG, &iw, &ih, &n, 4);
    if (!img) return;

    int ox = (fb_w - iw) / 2, oy = (fb_h - ih) / 2;
    for (int y = 0; y < ih; y++) {
        int dy = y + oy;
        if (dy < 0 || dy >= fb_h) continue;
        for (int x = 0; x < iw; x++) {
            int dx = x + ox;
            if (dx < 0 || dx >= fb_w) continue;
            const unsigned char *s = img + ((size_t)y * iw + x) * 4;
            unsigned char *d = bg + (size_t)dy * fb_stride + (size_t)dx * 4;
            d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0;
        }
    }
    stbi_image_free(img);
}

static int bar_x, bar_y, bar_w;

static void draw_bar(double frac)
{
    /* restaura el fondo bajo la barra */
    for (int y = 0; y < BAR_H; y++) {
        size_t off = (size_t)(bar_y + y) * fb_stride + (size_t)bar_x * 4;
        memcpy(fbmem + off, bg + off, (size_t)bar_w * 4);
    }
    fill_rect(fbmem, bar_x, bar_y, bar_w, BAR_H, COL_BORDER);
    fill_rect(fbmem, bar_x + 1, bar_y + 1, bar_w - 2, BAR_H - 2, COL_TRACK);
    int fw = (int)((bar_w - 2) * frac);
    if (fw > 0)
        fill_rect(fbmem, bar_x + 1, bar_y + 1, fw, BAR_H - 2, COL_FILL);
}

#ifndef SPLASH_TEST
static double elapsed(const struct timespec *t0)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0->tv_sec) + (double)(t.tv_nsec - t0->tv_nsec) / 1e9;
}
#endif

static double progress_at(double t)
{
    return PROGRESS_MAX * (1.0 - exp(-t / PROGRESS_TAU));
}

#ifdef SPLASH_TEST
int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: %s fb.raw segundos\n", argv[0]); return 1; }
    fb_w = 640; fb_h = 480; fb_stride = 640 * 4;
    size_t fbsize = (size_t)fb_stride * fb_h;
    int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || ftruncate(fd, (off_t)fbsize) < 0) { perror("fb"); return 1; }
    fbmem = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbmem == MAP_FAILED) { perror("mmap"); return 1; }
    double t_sim = atof(argv[2]);
#else
int main(void)
{
    int fd = open(FB_DEV, O_RDWR);
    if (fd < 0) { perror("open fb0"); return 1; }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        perror("ioctl fb0");
        close(fd);
        return 1;
    }
    if (vinfo.bits_per_pixel != 32) {
        fprintf(stderr, "armiga-splash: se esperaba 32bpp (hay %u)\n", vinfo.bits_per_pixel);
        close(fd);
        return 1;
    }
    fb_w = vinfo.xres;
    fb_h = vinfo.yres;
    fb_stride = finfo.line_length;

    size_t fbsize = (size_t)fb_stride * fb_h;
    fbmem = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbmem == MAP_FAILED) { perror("mmap fb0"); close(fd); return 1; }
#endif

    bg = calloc(1, (size_t)fb_stride * fb_h);
    if (!bg) return 1;

    blit_image();

    /* el texto es opcional: sin fuente se pinta igual la imagen y la barra */
    if (load_psf_font(cfg("ARMIGA_FONT", FONT_PATH)) == 0) {
        char version[64], build[64], date[64], l1[80], l2[140];
        read_release(version, sizeof(version), build, sizeof(build), date, sizeof(date));
        snprintf(l1, sizeof(l1), "armiga %s", version);
        snprintf(l2, sizeof(l2), "build %s %s", build, date);
        int y1 = fb_h - TEXT_BOTTOM;
        draw_text(bg, TEXT_MARGIN_X, y1, l1);
        draw_text(bg, TEXT_MARGIN_X, y1 + (int)g_font_h + LINE_GAP, l2);
    }

    memcpy(fbmem, bg, (size_t)fb_stride * fb_h);

    bar_x = BAR_MARGIN_X;
    bar_w = fb_w - 2 * BAR_MARGIN_X;
    bar_y = fb_h - BAR_BOTTOM;

#ifdef SPLASH_TEST
    draw_bar(progress_at(t_sim));
#else
    signal(SIGTERM, on_sigterm);
    signal(SIGINT, on_sigterm);

    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int last_fw = -1;
    while (!g_stop) {
        double frac = progress_at(elapsed(&t0));
        int fw = (int)((bar_w - 2) * frac);
        if (fw != last_fw) {
            draw_bar(frac);
            last_fw = fw;
        }
        usleep(FRAME_US);
    }
#endif

    munmap(fbmem, (size_t)fb_stride * fb_h);
    close(fd);
    return 0;
}
