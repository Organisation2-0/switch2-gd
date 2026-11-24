// SPDX-License-Identifier: GPL-3.0-or-later
// CPU renderer used while developing: draws a slice of a level into a PNG.
// Usage: levelpng <level> <out.png> <resdir> <x0> <width_units> [rows=14]
// Unknown object ids are drawn as magenta boxes with the id written in them.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

#include "../src/atlas.h"
#include "../src/level.h"
#include "../src/objects.h"

static const char *k_digits[10] = { // 3x5 font
    "111101101101111", "010110010010111", "111001111100111", "111001111001111",
    "101101111001001", "111100111001111", "111100111101111", "111001001001001",
    "111101111101111", "111101111001111"};

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

static void draw_id_box(int cx, int cy, int size, int id)
{
    for (int y = -size / 2; y < size / 2; y++)
        for (int x = -size / 2; x < size / 2; x++)
            put(cx + x, cy + y, 200, 0, 200, 150);
    char s[8];
    snprintf(s, sizeof s, "%d", id);
    int n = (int)strlen(s), sc = size >= 40 ? 3 : 2;
    int x0 = cx - n * 4 * sc / 2;
    for (int i = 0; i < n; i++)
        for (int gy = 0; gy < 5; gy++)
            for (int gx = 0; gx < 3; gx++)
                if (k_digits[s[i] - '0'][gy * 3 + gx] == '1')
                    for (int dy = 0; dy < sc; dy++)
                        for (int dx = 0; dx < sc; dx++)
                            put(x0 + i * 4 * sc + gx * sc + dx, cy - 2 * sc + gy * sc + dy,
                                255, 255, 255, 255);
}

// Draws one frame centred at (cx,cy) pixels, rotated by deg (clockwise), with flips.
static void draw_frame(const GdAtlas *a, const GdFrame *f, float cx, float cy,
                       float deg, int flipx, int flipy, float scale)
{
    const GdSheet *sh = &a->sheets[f->sheet];
    float rad = deg * 3.14159265f / 180.0f;
    float c = cosf(rad), s = sinf(rad);
    // Trimmed sprite is centred at (offx, offy) relative to the object origin, y up.
    float ox = f->offx * scale / a->px_per_unit * a->px_per_unit; // already in sheet pixels
    (void)ox;
    float sw = (float)f->w, sh_ = (float)f->h;
    float cxo = f->offx * (flipx ? -1.f : 1.f);
    float cyo = f->offy * (flipy ? -1.f : 1.f);
    // object-local centre of the trimmed sprite in sheet pixels (y up)
    float lx = cxo, ly = cyo;
    // rotate centre offset into screen space (screen y is down)
    float rx = lx * c + ly * s;
    float ry = -lx * s + ly * c;
    float dcx = cx + rx * scale, dcy = cy - ry * scale;
    float radius = (sw > sh_ ? sw : sh_) * 0.75f * scale + 2;
    for (int y = (int)(dcy - radius); y <= (int)(dcy + radius); y++) {
        for (int x = (int)(dcx - radius); x <= (int)(dcx + radius); x++) {
            // inverse: screen -> sprite-local (y up)
            float px = (x + 0.5f - dcx) / scale, py = -(y + 0.5f - dcy) / scale;
            float u = px * c - py * s;   // rotate back
            float v = px * s + py * c;
            if (flipx) u = -u;
            if (flipy) v = -v;
            float ix = u + sw / 2, iy = sh_ / 2 - v; // image coords, y down
            if (ix < 0 || iy < 0 || ix >= sw || iy >= sh_) continue;
            int sx, sy;
            if (f->rotated) { sx = f->x + (int)iy; sy = f->y + (int)(sw - 1 - ix); }
            else { sx = f->x + (int)ix; sy = f->y + (int)iy; }
            if (sx < 0 || sy < 0 || sx >= sh->w || sy >= sh->h) continue;
            const unsigned char *p = sh->rgba + ((size_t)sy * sh->w + sx) * 4;
            put(x, y, p[0], p[1], p[2], p[3]);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s level out.png resdir x0 width [rows]\n", argv[0]);
        return 2;
    }
    GdLevel lvl;
    if (gd_level_load(argv[1], &lvl)) { fprintf(stderr, "level load failed\n"); return 1; }
    const char *sheets[] = {"GJ_GameSheet", "GJ_GameSheet02", "GJ_GameSheet03",
                            "GJ_GameSheet04", "GJ_GameSheetGlow"};
    GdAtlas atlas;
    if (gd_atlas_load(&atlas, argv[3], "-hd", sheets, 5)) return 1;
    fprintf(stderr, "atlas: %d frames\n", atlas.nframes);

    float x0 = (float)atof(argv[4]), wu = (float)atof(argv[5]);
    int rows = argc > 6 ? atoi(argv[6]) : 14;
    float scale = 1.0f; // sheet pixels -> output pixels (HD sheet: 2 px per unit)
    float ppu = atlas.px_per_unit * scale;
    g_w = (int)(wu * ppu);
    g_h = (int)(rows * 30 * ppu);
    g_px = malloc((size_t)g_w * g_h * 4);
    for (int i = 0; i < g_w * g_h; i++) {
        g_px[i * 4] = 20; g_px[i * 4 + 1] = 40; g_px[i * 4 + 2] = 120; g_px[i * 4 + 3] = 255;
    }
    int unknown[2048] = {0}, drawn = 0;
    for (size_t i = 0; i < lvl.count; i++) {
        const GdObject *o = &lvl.objects[i];
        if (o->x < x0 - 60 || o->x > x0 + wu + 60) continue;
        float cx = (o->x - x0) * ppu;
        float cy = g_h - (o->y) * ppu; // y grows upward
        const char *name = gd_object_frame(o->id);
        const GdFrame *f = name ? gd_atlas_find(&atlas, name) : NULL;
        if (f) {
            draw_frame(&atlas, f, cx, cy, o->rot, o->flipx, o->flipy, scale);
            drawn++;
        } else {
            if (o->id > 0 && o->id < 2048) unknown[o->id]++;
            draw_id_box((int)cx, (int)cy, (int)(30 * ppu), o->id);
        }
    }
    fprintf(stderr, "drawn sprites: %d\nunmapped ids in view:", drawn);
    for (int i = 0; i < 2048; i++)
        if (unknown[i]) fprintf(stderr, " %d(x%d)", i, unknown[i]);
    fprintf(stderr, "\n");
    stbi_write_png(argv[2], g_w, g_h, 4, g_px, g_w * 4);
    gd_level_free(&lvl);
    return 0;
}
