// SPDX-License-Identifier: GPL-3.0-or-later
// Shared drawing code: turns sprites, images and text into quads for the platform backend.
#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/stb_image.h"
#include "gfx_backend.h"

// Colours reach the backends premultiplied, like the textures. This is how the original blends:
// additive sprites therefore count their opacity twice (once in the colour, once in the blend).
static SDL_Color col(GdColorF c)
{
    const float a = fminf(fmaxf(c.a, 0), 1);
    SDL_Color o;
    o.r = (Uint8)(fminf(fmaxf(c.r, 0), 255) * a + 0.5f);
    o.g = (Uint8)(fminf(fmaxf(c.g, 0), 255) * a + 0.5f);
    o.b = (Uint8)(fminf(fmaxf(c.b, 0), 255) * a + 0.5f);
    o.a = (Uint8)(a * 255 + 0.5f);
    return o;
}

static void premultiply(unsigned char *px, int w, int h)
{
    for (size_t i = 0, n = (size_t)w * h; i < n; i++, px += 4) {
        const unsigned a = px[3];
        if (a == 255) continue;
        px[0] = (unsigned char)((px[0] * a + 127) / 255);
        px[1] = (unsigned char)((px[1] * a + 127) / 255);
        px[2] = (unsigned char)((px[2] * a + 127) / 255);
    }
}

void gfx_set_video(Gfx *g, const GfxVideo *video) { gfxb_set_video(g, video); }

int gfx_max_height(const Gfx *g) { return gfxb_max_height(g); }

int gfx_refresh_hz(void) { return gfxb_refresh_hz(); }

int gfx_init(Gfx *g, const char *resdir, int w, int h, const GfxVideo *video)
{
    memset(g, 0, sizeof *g);
    snprintf(g->resdir, sizeof g->resdir, "%s", resdir);
    g->suffix = "-hd";
    g->fade = 1;
    int rc = gfxb_init(g, w, h, video);
    if (rc) {
        fprintf(stderr, "graphics init: %s\n", SDL_GetError());
        return rc;
    }
    gfxb_output_size(&g->w, &g->h);

    static const char *const sheets[] = {"GJ_GameSheet",     "GJ_GameSheet02",
                                         "GJ_GameSheet03",   "GJ_GameSheet04",
                                         "GJ_GameSheetGlow", "GJ_GameSheetIcons",
                                         "GJ_LaunchSheet", "FireSheet_01", "PixelSheet_01", "GJ_ParticleSheet"};
    if (gd_atlas_load(&g->atlas, resdir, g->suffix, sheets, 10)) return -4;
    for (int i = 0; i < g->atlas.nsheets; i++) {
        premultiply(g->atlas.sheets[i].rgba, g->atlas.sheets[i].w, g->atlas.sheets[i].h);
        g->sheet_tex[i] = gfxb_tex_create(g->atlas.sheets[i].rgba, g->atlas.sheets[i].w,
                                          g->atlas.sheets[i].h);
        if (!g->sheet_tex[i]) return -5;
        // the texture holds its own copy; free the CPU pixels to save memory (matters on the Switch)
        free(g->atlas.sheets[i].rgba);
        g->atlas.sheets[i].rgba = NULL;
    }
    g->icons = calloc(GFX_ICONS, sizeof *g->icons);
    return g->icons ? 0 : -6;
}

static void icon_drop(GfxIcon *ic)
{
    if (ic->tex) gfxb_tex_destroy(ic->tex);
    if (ic->ok) gd_atlas_free(&ic->atlas);
    memset(ic, 0, sizeof *ic);
}

void gfx_quit(Gfx *g)
{
    for (int i = 0; g->icons && i < GFX_ICONS; i++) icon_drop(&g->icons[i]);
    free(g->icons);
    g->icons = NULL;
    for (int i = 0; i < g->atlas.nsheets; i++)
        if (g->sheet_tex[i]) gfxb_tex_destroy(g->sheet_tex[i]);
    gd_atlas_free(&g->atlas);
    gfxb_quit(g);
    SDL_Quit();
}

void gfx_begin_frame(Gfx *g) { g->frame++; gfxb_begin_frame(g); }

void gfx_effects(Gfx *g, const GfxEffects *fx)
{
    (void)g;
    if (fx->any) gfxb_effects(fx->v);
}

void gfx_layer_begin(Gfx *g) { (void)g; gfxb_layer_begin(); }

void gfx_layer_end(Gfx *g, const GfxEffects *fx)
{
    (void)g;
    gfxb_layer_end(fx->any ? fx->v : NULL);
}

void gfx_window_point(Gfx *g, int px, int py, float *x, float *y) { gfxb_window_point(g, px, py, x, y); }

void gfx_present(Gfx *g) { (void)g; gfxb_present(); }

void gfx_set_clip(Gfx *g, const SDL_Rect *r) { (void)g; gfxb_clip(r); }

unsigned char *gfx_read_pixels(Gfx *g, int *w, int *h)
{
    (void)g;
    gfxb_output_size(w, h);
    unsigned char *px = malloc((size_t)*w * *h * 4);
    if (px && gfxb_read_pixels(px, *w, *h)) {
        free(px);
        px = NULL;
    }
    return px;
}

int gfx_load_image(Gfx *g, const char *file, GfxImage *out)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s", g->resdir, file);
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) return -1;
    premultiply(px, w, h);
    out->tex = gfxb_tex_create(px, w, h);
    out->w = w;
    out->h = h;
    stbi_image_free(px);
    return out->tex ? 0 : -2;
}

void gfx_free_image(GfxImage *im)
{
    if (im->tex) gfxb_tex_destroy(im->tex);
    im->tex = NULL;
}

const GdFrame *gfx_find(Gfx *g, const char *name) { return gd_atlas_find(&g->atlas, name); }

// The slot holding an icon file, loading it if needed; NULL if every slot is busy.
static GfxIcon *icon_slot(Gfx *g, const char *file)
{
    int pick = -1;
    unsigned oldest = 0;
    for (int i = 0; i < GFX_ICONS; i++) {
        GfxIcon *ic = &g->icons[i];
        if (!strcmp(ic->file, file)) { ic->used = g->frame; return ic; }
        // free slots first, then the one not drawn for the longest time (never one of this picture)
        const unsigned age = !ic->file[0] ? ~0u : g->frame - ic->used;
        if (age >= 2 && age > oldest) { oldest = age; pick = i; }
    }
    if (pick < 0) return NULL;
    GfxIcon *ic = &g->icons[pick];
    icon_drop(ic);
    snprintf(ic->file, sizeof ic->file, "%s", file);
    ic->used = g->frame;
    char path[640];
    snprintf(path, sizeof path, "%s/%s%s.plist", g->resdir, file, g->suffix);
    FILE *fp = fopen(path, "rb");
    if (!fp) return ic;
    fclose(fp);
    const char *const names[1] = {file};
    if (gd_atlas_load(&ic->atlas, g->resdir, g->suffix, names, 1)) {
        gd_atlas_free(&ic->atlas);
        return ic;
    }
    GdSheet *sh = &ic->atlas.sheets[0];
    premultiply(sh->rgba, sh->w, sh->h);
    ic->tex = gfxb_tex_create(sh->rgba, sh->w, sh->h);
    free(sh->rgba);
    sh->rgba = NULL;
    ic->ok = 1;
    if (!ic->tex) { gd_atlas_free(&ic->atlas); ic->ok = 0; return ic; }
    for (int i = 0; i < ic->atlas.nframes; i++) ic->atlas.frames[i].sheet = 16 + pick;
    return ic;
}

const GdFrame *gfx_icon_frame(Gfx *g, const char *type, int id, const char *part)
{
    char file[40], name[64];
    snprintf(file, sizeof file, "icons/%s_%02d", type, id);
    snprintf(name, sizeof name, "%s_%02d%s_001.png", type, id, part);
    const GfxIcon *ic = g->icons ? icon_slot(g, file) : NULL;
    return ic && ic->ok ? gd_atlas_find(&ic->atlas, name) : gfx_find(g, name);
}

int gfx_icon_exists(Gfx *g, const char *type, int id)
{
    char path[640], name[64];
    snprintf(path, sizeof path, "%s/icons/%s_%02d%s.plist", g->resdir, type, id, g->suffix);
    FILE *fp = fopen(path, "rb");
    if (fp) { fclose(fp); return 1; }
    const int multi = !strcmp(type, "robot") || !strcmp(type, "spider");
    snprintf(name, sizeof name, "%s_%02d%s_001.png", type, id, multi ? "_01" : "");
    return gfx_find(g, name) != NULL;
}

void gfx_icon_draw(Gfx *g, const char *type, int id, float x, float y, float rot, float sx, float sy,
                   GdColorF c1, GdColorF c2)
{
    const GdColorF white = {255, 255, 255, c1.a};
    // back to front: the dome of the UFO, second colour, first colour, white details
    static const char *const part[4] = {"_3", "_2", "", "_extra"};
    const GdColorF col4[4] = {white, c2, c1, white};
    for (int i = strcmp(type, "bird") ? 1 : 0; i < 4; i++) {
        const GdFrame *f = gfx_icon_frame(g, type, id, part[i]);
        if (f) gfx_frame(g, f, x, y, rot, sx, sy, col4[i], 0);
    }
}

// A corner as the mirror portal and the camera rotation show it.
static SDL_FPoint placed(const Gfx *g, float x, float y)
{
    if (g->mirror) x = g->mir_w - x;
    if (g->turned) {
        const float dx = x - g->turn_x, dy = y - g->turn_y;
        x = g->turn_x + dx * g->turn_cos - dy * g->turn_sin;
        y = g->turn_y + dx * g->turn_sin + dy * g->turn_cos;
    }
    return (SDL_FPoint){x, y};
}

void gfx_quad_colors(Gfx *g, const float px[4], const float py[4], const GdColorF c[4], int additive)
{
    SDL_FPoint pos[4], uv[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    SDL_Color cols[4];
    for (int i = 0; i < 4; i++) {
        pos[i] = placed(g, px[i], py[i]);
        cols[i] = col(c[i]);
    }
    gfxb_quad(NULL, pos, uv, cols, additive);
}

static void quad(Gfx *g, GfxTex *tex, const float px[4], const float py[4],
                 const float u[4], const float v[4], SDL_Color c, int additive)
{
    // polygon of up to 5 corners: the quad, possibly cut along a line
    float vx[5], vy[5], vu[5], vv[5];
    int n = 0;
    for (int i = 0; i < 4; i++) {
        const int j = (i + 1) & 3;
        const float di = g->cut ? g->cut_a * px[i] + g->cut_b * py[i] + g->cut_c : 1.0f;
        const float dj = g->cut ? g->cut_a * px[j] + g->cut_b * py[j] + g->cut_c : 1.0f;
        if (di >= 0 && n < 5) { vx[n] = px[i]; vy[n] = py[i]; vu[n] = u[i]; vv[n] = v[i]; n++; }
        if ((di < 0) != (dj < 0) && n < 5) {
            const float t = di / (di - dj);
            vx[n] = px[i] + (px[j] - px[i]) * t; vy[n] = py[i] + (py[j] - py[i]) * t;
            vu[n] = u[i] + (u[j] - u[i]) * t;    vv[n] = v[i] + (v[j] - v[i]) * t;
            n++;
        }
    }
    SDL_Color cols[4] = {c, c, c, c};
    for (int first = 1; first + 1 < n; first += 2) {   // fan around corner 0, two triangles per quad
        const int idx[4] = {0, first, first + 1, first + 2 < n ? first + 2 : first + 1};
        SDL_FPoint pos[4], uv[4];
        for (int k = 0; k < 4; k++) {
            const int i = idx[k];
            pos[k] = placed(g, vx[i], vy[i]);
            uv[k] = (SDL_FPoint){vu[i], vv[i]};
        }
        gfxb_quad(tex, pos, uv, cols, additive);
    }
}

void gfx_frame(Gfx *g, const GdFrame *f, float cx, float cy, float rot, float sx, float sy,
               GdColorF c, int additive)
{
    c.a *= g->fade;
    if (c.a <= 0.003f) return;
    const GfxIcon *ic = f->sheet >= 16 ? &g->icons[f->sheet - 16] : NULL;
    const GdSheet *sh = ic ? &ic->atlas.sheets[0] : &g->atlas.sheets[f->sheet];
    float hw = f->w * 0.5f, hh = f->h * 0.5f;
    // corners TL, TR, BR, BL in local (y-up) space
    float lx[4] = {-hw, hw, hw, -hw}, ly[4] = {hh, hh, -hh, -hh};
    float rad = rot * 3.14159265f / 180.0f, cs = cosf(rad), sn = sinf(rad);
    float px[4], py[4];
    for (int i = 0; i < 4; i++) {
        float x = (lx[i] + f->offx) * sx, y = -(ly[i] + f->offy) * sy;
        px[i] = cx + x * cs - y * sn;
        py[i] = cy + x * sn + y * cs;
    }
    float W = (float)sh->w, H = (float)sh->h;
    float u[4], v[4];
    if (!f->rotated) {
        u[0] = f->x / W; v[0] = f->y / H;
        u[1] = (f->x + f->w) / W; v[1] = v[0];
        u[2] = u[1]; v[2] = (f->y + f->h) / H;
        u[3] = u[0]; v[3] = v[2];
    } else {
        // stored rotated by 90 degrees: TL -> (x+h, y), TR -> (x+h, y+w), BR -> (x, y+w), BL -> (x, y)
        u[0] = (f->x + f->h) / W; v[0] = f->y / H;
        u[1] = (f->x + f->h) / W; v[1] = (f->y + f->w) / H;
        u[2] = f->x / W; v[2] = (f->y + f->w) / H;
        u[3] = f->x / W; v[3] = f->y / H;
    }
    quad(g, ic ? ic->tex : g->sheet_tex[f->sheet], px, py, u, v, col(c), additive);
}

void gfx_image_rect(Gfx *g, const GfxImage *im, SDL_FRect s, SDL_FRect d, GdColorF c)
{
    float px[4] = {d.x, d.x + d.w, d.x + d.w, d.x};
    float py[4] = {d.y, d.y, d.y + d.h, d.y + d.h};
    float u[4] = {s.x / im->w, (s.x + s.w) / im->w, (s.x + s.w) / im->w, s.x / im->w};
    float v[4] = {s.y / im->h, s.y / im->h, (s.y + s.h) / im->h, (s.y + s.h) / im->h};
    quad(g, im->tex, px, py, u, v, col(c), 0);
}

void gfx_ninepatch(Gfx *g, const GfxImage *im, float b, SDL_FRect d, float bd, GdColorF c)
{
    float sx[4] = {0, b, im->w - b, (float)im->w};
    float sy[4] = {0, b, im->h - b, (float)im->h};
    float dx[4] = {d.x, d.x + bd, d.x + d.w - bd, d.x + d.w};
    float dy[4] = {d.y, d.y + bd, d.y + d.h - bd, d.y + d.h};
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            SDL_FRect s = {sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]};
            SDL_FRect t = {dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j]};
            gfx_image_rect(g, im, s, t, c);
        }
}

static void fill(Gfx *g, SDL_FRect rc, GdColorF c, int additive)
{
    float px[4] = {rc.x, rc.x + rc.w, rc.x + rc.w, rc.x};
    float py[4] = {rc.y, rc.y, rc.y + rc.h, rc.y + rc.h};
    static const float zero[4] = {0, 0, 0, 0};
    quad(g, NULL, px, py, zero, zero, col(c), additive);
}

void gfx_fill(Gfx *g, SDL_FRect rc, GdColorF c) { fill(g, rc, c, 0); }

void gfx_glow_fill(Gfx *g, SDL_FRect rc, GdColorF c) { fill(g, rc, c, 1); }

void gfx_gradient_v(Gfx *g, SDL_FRect rc, GdColorF top, GdColorF bottom)
{
    (void)g; // not affected by the mirror portal
    SDL_Color a = col(top), b = col(bottom);
    SDL_FPoint pos[4] = {{rc.x, rc.y}, {rc.x + rc.w, rc.y}, {rc.x + rc.w, rc.y + rc.h}, {rc.x, rc.y + rc.h}};
    SDL_FPoint uv[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    SDL_Color cols[4] = {a, a, b, b};
    gfxb_quad(NULL, pos, uv, cols, 0);
}

// ---- bitmap font (AngelCode text format, as used by cocos2d) ----

static int field(const char *line, const char *key)
{
    const char *p = strstr(line, key);
    return p ? atoi(p + strlen(key)) : 0;
}

int gfx_font_load(Gfx *g, const char *name, GfxFont *f)
{
    memset(f, 0, sizeof *f);
    char path[640], img[160];
    snprintf(path, sizeof path, "%s/%s%s.fnt", g->resdir, name, g->suffix);
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        if (!strncmp(line, "common ", 7)) {
            f->line_h = field(line, "lineHeight=");
            f->base = field(line, "base=");
        } else if (!strncmp(line, "char ", 5)) {
            int id = field(line, " id=");
            if (id < 0 || id > 255) continue;
            f->has[id] = 1;
            f->ch[id].x = field(line, " x=");
            f->ch[id].y = field(line, " y=");
            f->ch[id].w = field(line, " width=");
            f->ch[id].h = field(line, " height=");
            f->ch[id].xoff = field(line, " xoffset=");
            f->ch[id].yoff = field(line, " yoffset=");
            f->ch[id].xadv = field(line, " xadvance=");
        }
    }
    fclose(fp);
    snprintf(img, sizeof img, "%s%s.png", name, g->suffix);
    return gfx_load_image(g, img, &f->img);
}

float gfx_text_width(const GfxFont *f, const char *s, float scale)
{
    float w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (f->has[c]) w += f->ch[c].xadv;
    }
    return w * scale;
}

void gfx_text(Gfx *g, const GfxFont *f, const char *s, float x, float y, float scale,
              int align, GdColorF c)
{
    if (!f->img.tex) return;
    float w = gfx_text_width(f, s, scale);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (!f->has[ch]) continue;
        SDL_FRect src = {(float)f->ch[ch].x, (float)f->ch[ch].y, (float)f->ch[ch].w,
                         (float)f->ch[ch].h};
        SDL_FRect dst = {x + f->ch[ch].xoff * scale, y + f->ch[ch].yoff * scale,
                         f->ch[ch].w * scale, f->ch[ch].h * scale};
        if (src.w > 0) gfx_image_rect(g, &f->img, src, dst, c);
        x += f->ch[ch].xadv * scale;
    }
}
