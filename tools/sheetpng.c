// SPDX-License-Identifier: GPL-3.0-or-later
// Contact sheet for finding sprites: numbered grid of every frame whose name
// contains one of the given substrings. Prints "index name" to stdout.
// Usage: sheetpng <resdir> <out.png> <substr> [substr...]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

#include "../src/atlas.h"

static const char *k_digits[10] = {
    "111101101101111", "010110010010111", "111001111100111", "111001111001111",
    "101101111001001", "111100111001111", "111100111101111", "111001001001001",
    "111101111101111", "111101111001111"};

#define CELL 96
static unsigned char *g_px;
static int g_w, g_h;

static void put(int x, int y, unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (x < 0 || y < 0 || x >= g_w || y >= g_h || !a) return;
    unsigned char *p = g_px + ((size_t)y * g_w + x) * 4;
    p[0] = (unsigned char)((r * a + p[0] * (255 - a)) / 255);
    p[1] = (unsigned char)((g * a + p[1] * (255 - a)) / 255);
    p[2] = (unsigned char)((b * a + p[2] * (255 - a)) / 255);
    p[3] = 255;
}

static void number(int x, int y, int v)
{
    char s[8];
    snprintf(s, sizeof s, "%d", v);
    for (int i = 0; s[i]; i++)
        for (int gy = 0; gy < 5; gy++)
            for (int gx = 0; gx < 3; gx++)
                if (k_digits[s[i] - '0'][gy * 3 + gx] == '1')
                    for (int d = 0; d < 4; d++)
                        put(x + i * 14 + gx * 4 + d % 2 * 0, y + gy * 4 + d / 2, 255, 255, 0, 255),
                        put(x + i * 14 + gx * 4 + 1, y + gy * 4 + d / 2, 255, 255, 0, 255);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: sheetpng resdir out.png substr...\n"); return 2; }
    const char *sheets[] = {"GJ_GameSheet", "GJ_GameSheet02", "GJ_GameSheet03", "GJ_GameSheet04", "GJ_ParticleSheet"};
    GdAtlas a;
    if (gd_atlas_load(&a, argv[1], "-hd", sheets, 5)) return 1;

    int *pick = malloc(sizeof(int) * (size_t)a.nframes), n = 0;
    for (int i = 0; i < a.nframes; i++) {
        const char *nm = a.frames[i].name;
        if (!strstr(nm, "_001.png") && !strstr(nm, "_01.png")) {
            // keep single-frame names that end in a plain .png too
        }
        for (int k = 3; k < argc; k++)
            if (strstr(nm, argv[k])) { pick[n++] = i; break; }
    }
    int cols = 14, rows = (n + cols - 1) / cols;
    g_w = cols * CELL;
    g_h = (rows ? rows : 1) * CELL;
    g_px = calloc((size_t)g_w * g_h, 4);
    for (int i = 0; i < g_w * g_h; i++) {
        g_px[i * 4] = 60; g_px[i * 4 + 1] = 70; g_px[i * 4 + 2] = 110; g_px[i * 4 + 3] = 255;
    }
    for (int k = 0; k < n; k++) {
        const GdFrame *f = &a.frames[pick[k]];
        const GdSheet *sh = &a.sheets[f->sheet];
        int cx = (k % cols) * CELL, cy = (k / cols) * CELL;
        int fw = f->rotated ? f->h : f->w, fh = f->rotated ? f->w : f->h;
        float s = 1.0f;
        if (fw > CELL - 8 || fh > CELL - 8) s = (float)(CELL - 8) / (fw > fh ? fw : fh);
        int dw = (int)(fw * s), dh = (int)(fh * s);
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                int ix = (int)(x / s), iy = (int)(y / s);
                int sx, sy;
                if (f->rotated) { sx = f->x + iy; sy = f->y + (f->w - 1 - ix); }
                else { sx = f->x + ix; sy = f->y + iy; }
                const unsigned char *p = sh->rgba + ((size_t)sy * sh->w + sx) * 4;
                put(cx + (CELL - dw) / 2 + x, cy + (CELL - dh) / 2 + y, p[0], p[1], p[2], p[3]);
            }
        number(cx + 3, cy + 3, k);
        printf("%d %s\n", k, f->name);
    }
    stbi_write_png(argv[2], g_w, g_h, 4, g_px, g_w * 4);
    return 0;
}
