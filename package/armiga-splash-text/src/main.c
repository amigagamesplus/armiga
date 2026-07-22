/* armiga-splash-text: dibuja version/build/fecha + "loading, please wait..."
 * sobre el framebuffer, esquina inferior izquierda, sin dependencias.
 * Formato asumido: 640x480, 32bpp, orden en memoria B,G,R,X (XRGB8888 LE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <signal.h>

#include "font6x10.h"

static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }

#define FB_DEV       "/dev/fb0"
#define RELEASE_PATH "/etc/armiga-release"

#define MARGIN_X     16
#define MARGIN_Y     14
#define LINE_GAP     3
#define SCALE        1

#define COL_R 170
#define COL_G 170
#define COL_B 170

static unsigned char *fbmem = NULL;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_stride = 0;

static void put_pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    if (fb_bpp != 32) return;
    unsigned char *p = fbmem + y * fb_stride + x * 4;
    p[0] = b;
    p[1] = g;
    p[2] = r;
    p[3] = 0;
}

static void draw_char(int x, int y, char ch)
{
    if ((unsigned char)ch < FONT_FIRST_CHAR || (unsigned char)ch > FONT_LAST_CHAR) ch = ' ';
    const unsigned char *rows = font6x10[(unsigned char)ch - FONT_FIRST_CHAR];
    for (int ry = 0; ry < FONT_CHAR_H; ry++) {
        unsigned char bits = rows[ry];
        for (int rx = 0; rx < FONT_CHAR_W; rx++) {
            if (bits & (0x80 >> rx)) {
                for (int sy = 0; sy < SCALE; sy++)
                    for (int sx = 0; sx < SCALE; sx++)
                        put_pixel(x + rx * SCALE + sx, y + ry * SCALE + sy,
                                  COL_R, COL_G, COL_B);
            }
        }
    }
}

static void draw_text(int x, int y, const char *s)
{
    int cx = x;
    for (const char *p = s; *p; p++) {
        draw_char(cx, y, *p);
        cx += (FONT_CHAR_W + 1) * SCALE;
    }
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
    snprintf(line1, sizeof(line1), "armiga v%s", version);
    snprintf(line2, sizeof(line2), "build %s  %s", build, date);

    int line_h = FONT_CHAR_H * SCALE + LINE_GAP;
    int y_base = fb_h - MARGIN_Y - line_h * 3 + LINE_GAP;
    int y3 = y_base + line_h * 2;

    draw_text(MARGIN_X, y_base, line1);
    draw_text(MARGIN_X, y_base + line_h, line2);

    signal(SIGTERM, on_sigterm);
    signal(SIGINT, on_sigterm);

    const char *base_txt = "loading, please wait";
    int max_dots = 3;
    int cell_w = (FONT_CHAR_W + 1) * SCALE;
    int line3_w = (int)(strlen(base_txt) + max_dots) * cell_w;

    int dots = 0;
    while (!g_stop) {
        for (int yy = 0; yy < line_h; yy++)
            for (int xx = 0; xx < line3_w; xx++)
                put_pixel(MARGIN_X + xx, y3 + yy, 0, 0, 0);

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
