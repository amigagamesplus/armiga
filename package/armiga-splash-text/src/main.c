/* armiga-splash-text: dibuja version/build/fecha + "loading, please wait..."
 * sobre el framebuffer YA PINTADO por S05splash (splash.raw), esquina
 * inferior izquierda. Solo escribe los pixeles de su propio texto —
 * nunca limpia ni toca el resto del framebuffer.
 *
 * La fuente usada es la MISMA que el sistema ya tiene instalada para la
 * consola (Terminus, /usr/share/consolefonts/Lat2-Terminus16.psfu.gz).
 * Se lee y descomprime en runtime con zlib, sin tabla propia embebida.
 *
 * Formato framebuffer: 32bpp, B,G,R,X en memoria (LE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <signal.h>
#include <zlib.h>

#define FB_DEV       "/dev/fb0"
#define RELEASE_PATH "/etc/armiga-release"
#define FONT_PATH    "/usr/share/consolefonts/Lat2-Terminus16.psfu.gz"

#define MARGIN_X     16
#define MARGIN_Y     14
#define LINE_GAP     3

#define COL_R 170
#define COL_G 170
#define COL_B 170
#define BGCOL_R 10
#define BGCOL_G 10
#define BGCOL_B 10

#define PSF2_MAGIC 0x864ab572u

struct psf2_header {
    unsigned int magic;
    unsigned int version;
    unsigned int headersize;
    unsigned int flags;
    unsigned int numglyph;
    unsigned int bytesperglyph;
    unsigned int height;
    unsigned int width;
};

static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }

static unsigned char *fbmem = NULL;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_stride = 0;

static unsigned char *g_glyphs = NULL;
static unsigned int g_font_w = 8, g_font_h = 16, g_bytes_per_row = 1;
static unsigned int g_bytes_per_glyph = 16;
static unsigned int g_numglyph = 0;

static int load_psf_font(const char *path)
{
    gzFile gz = gzopen(path, "rb");
    if (!gz) return -1;

    size_t cap = 64 * 1024;
    size_t len = 0;
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

    if (len < sizeof(struct psf2_header)) { free(buf); return -1; }
    struct psf2_header h;
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

static void put_pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    if (fb_bpp != 32) return;
    unsigned char *p = fbmem + (size_t)y * fb_stride + (size_t)x * 4;
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = 0;
}

static void draw_char(int x, int y, unsigned char ch)
{
    if (!g_glyphs || ch >= g_numglyph) return;
    const unsigned char *rows = g_glyphs + (size_t)ch * g_bytes_per_glyph;
    for (unsigned int ry = 0; ry < g_font_h; ry++) {
        const unsigned char *row = rows + ry * g_bytes_per_row;
        for (unsigned int rx = 0; rx < g_font_w; rx++) {
            unsigned int byte_idx = rx / 8;
            unsigned int bit_idx = 7 - (rx % 8);
            if (row[byte_idx] & (1 << bit_idx))
                put_pixel(x + (int)rx, y + (int)ry, COL_R, COL_G, COL_B);
        }
    }
}

static void draw_text(int x, int y, const char *s)
{
    int cx = x;
    for (const char *p = s; *p; p++) {
        draw_char(cx, y, (unsigned char)*p);
        cx += (int)g_font_w + 1;
    }
}

static void clear_rect(int x, int y_top, int w, int h)
{
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            put_pixel(x + xx, y_top + yy, BGCOL_R, BGCOL_G, BGCOL_B);
}

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

int main(void)
{
    if (load_psf_font(FONT_PATH) != 0) {
        fprintf(stderr, "armiga-splash-text: no se pudo cargar %s\n", FONT_PATH);
        return 1;
    }

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

    fb_w = vinfo.xres;
    fb_h = vinfo.yres;
    fb_bpp = vinfo.bits_per_pixel;
    fb_stride = finfo.line_length;

    size_t fbsize = (size_t)fb_stride * fb_h;
    fbmem = mmap(NULL, fbsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fbmem == MAP_FAILED) { perror("mmap fb0"); close(fd); return 1; }

    char version[32], build[16], date[24];
    read_release(version, sizeof(version), build, sizeof(build), date, sizeof(date));

    char line1[64], line2[64];
    snprintf(line1, sizeof(line1), "armiga %s", version);
    snprintf(line2, sizeof(line2), "build %s  %s", build, date);

    int line_h = (int)g_font_h + LINE_GAP;
    int y_base = fb_h - MARGIN_Y - line_h * 3 + LINE_GAP;
    int y3 = y_base + line_h * 2;

    draw_text(MARGIN_X, y_base, line1);
    draw_text(MARGIN_X, y_base + line_h, line2);

    signal(SIGTERM, on_sigterm);
    signal(SIGINT, on_sigterm);

    const char *base_txt = "loading, please wait";
    int max_dots = 3;
    int cell_w = (int)g_font_w + 1;
    int line3_w = (int)(strlen(base_txt) + (size_t)max_dots) * cell_w;

    int dots = 0;
    while (!g_stop) {
        clear_rect(MARGIN_X, y3, line3_w, line_h);

        char line3[40];
        char dotbuf[4] = {0};
        for (int i = 0; i < dots; i++) dotbuf[i] = '.';
        snprintf(line3, sizeof(line3), "%s%s", base_txt, dotbuf);
        draw_text(MARGIN_X, y3, line3);

        dots = (dots + 1) % (max_dots + 1);

        for (int i = 0; i < 5 && !g_stop; i++)
            usleep(100000);
    }

    munmap(fbmem, fbsize);
    close(fd);
    return 0;
}
