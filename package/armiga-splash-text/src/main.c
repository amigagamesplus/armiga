/* armiga-splash-text: dibuja version/build/fecha + "loading, please wait..."
 * sobre el framebuffer YA PINTADO por S05splash (splash.raw), esquina
 * inferior izquierda. Solo escribe los pixeles de su propio texto —
 * nunca limpia ni toca el resto del framebuffer, para que la imagen
 * de fondo permanezca visible en todo momento (incluida la transicion
 * al launcher, que sobreescribe todo al arrancar).
 *
 * Usa freetype directamente para rasterizar la misma fuente TTF que
 * el launcher, sin pasar por SDL (que exige limpiar/controlar el
 * framebuffer completo).
 *
 * Formato framebuffer asumido: 32bpp, orden en memoria B,G,R,X (LE). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <signal.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#define FB_DEV       "/dev/fb0"
#define RELEASE_PATH "/etc/armiga-release"
#define FONT_PATH    "/usr/share/armiga/fonts/JetBrainsMonoNL-ExtraBold.ttf"
#define FONT_PX      12

#define MARGIN_X     16
#define MARGIN_Y     14
#define LINE_GAP     4

#define COL_R 170
#define COL_G 170
#define COL_B 170
#define BGCOL_R 10
#define BGCOL_G 10
#define BGCOL_B 10

static volatile sig_atomic_t g_stop = 0;
static void on_sigterm(int sig) { (void)sig; g_stop = 1; }

static unsigned char *fbmem = NULL;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_stride = 0;

static FT_Library ft_lib;
static FT_Face ft_face;

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

static void blend_pixel(int x, int y, unsigned char a)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    if (fb_bpp != 32) return;
    unsigned char *p = fbmem + (size_t)y * fb_stride + (size_t)x * 4;
    p[0] = (unsigned char)((COL_B * a + p[0] * (255 - a)) / 255);
    p[1] = (unsigned char)((COL_G * a + p[1] * (255 - a)) / 255);
    p[2] = (unsigned char)((COL_R * a + p[2] * (255 - a)) / 255);
}

static int text_width(const char *s)
{
    int w = 0;
    for (const char *p = s; *p; p++) {
        if (FT_Load_Char(ft_face, (FT_ULong)*p, FT_LOAD_DEFAULT) != 0) continue;
        w += (int)(ft_face->glyph->advance.x >> 6);
    }
    return w;
}

static void draw_text(int x, int y_baseline, const char *s)
{
    int cx = x;
    for (const char *p = s; *p; p++) {
        if (FT_Load_Char(ft_face, (FT_ULong)*p, FT_LOAD_RENDER) != 0) continue;
        FT_GlyphSlot g = ft_face->glyph;
        int gx = cx + g->bitmap_left;
        int gy = y_baseline - g->bitmap_top;
        for (unsigned int ry = 0; ry < g->bitmap.rows; ry++) {
            for (unsigned int rx = 0; rx < g->bitmap.width; rx++) {
                unsigned char a = g->bitmap.buffer[ry * g->bitmap.pitch + rx];
                if (a) blend_pixel(gx + (int)rx, gy + (int)ry, a);
            }
        }
        cx += (int)(g->advance.x >> 6);
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

    if (FT_Init_FreeType(&ft_lib) != 0) {
        fprintf(stderr, "FT_Init_FreeType failed\n");
        return 1;
    }
    if (FT_New_Face(ft_lib, FONT_PATH, 0, &ft_face) != 0) {
        fprintf(stderr, "FT_New_Face failed: %s\n", FONT_PATH);
        return 1;
    }
    FT_Set_Pixel_Sizes(ft_face, 0, FONT_PX);

    char version[32], build[16], date[24];
    read_release(version, sizeof(version), build, sizeof(build), date, sizeof(date));

    char line1[64], line2[64];
    snprintf(line1, sizeof(line1), "armiga v%s", version);
    snprintf(line2, sizeof(line2), "build %s  %s", build, date);

    int ascender = (int)(ft_face->size->metrics.ascender >> 6);
    int font_h   = (int)((ft_face->size->metrics.ascender -
                          ft_face->size->metrics.descender) >> 6);
    int line_h   = font_h + LINE_GAP;

    int y_base_top = fb_h - MARGIN_Y - line_h * 3 + LINE_GAP;
    int y1_base = y_base_top + ascender;
    int y2_base = y_base_top + line_h + ascender;
    int y3_top  = y_base_top + line_h * 2;
    int y3_base = y3_top + ascender;

    draw_text(MARGIN_X, y1_base, line1);
    draw_text(MARGIN_X, y2_base, line2);

    signal(SIGTERM, on_sigterm);
    signal(SIGINT, on_sigterm);

    const char *base_txt = "loading, please wait";
    int max_dots = 3;
    int line3_w = text_width("loading, please wait...") + 8;

    int dots = 0;
    while (!g_stop) {
        clear_rect(MARGIN_X, y3_top, line3_w, line_h);

        char line3[40];
        char dotbuf[4] = {0};
        for (int i = 0; i < dots; i++) dotbuf[i] = '.';
        snprintf(line3, sizeof(line3), "%s%s", base_txt, dotbuf);
        draw_text(MARGIN_X, y3_base, line3);

        dots = (dots + 1) % (max_dots + 1);

        for (int i = 0; i < 5 && !g_stop; i++)
            usleep(100000);
    }

    FT_Done_Face(ft_face);
    FT_Done_FreeType(ft_lib);
    munmap(fbmem, fbsize);
    close(fd);
    return 0;
}
