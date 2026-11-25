// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/stb_image.h"

#ifdef __SWITCH__
#include <switch.h>
static Framebuffer s_fb;
static int s_fb_ok;
#endif

static SDL_Texture *make_tex(SDL_Renderer *r, const unsigned char *rgba, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, rgba, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

int gfx_init(Gfx *g, const char *resdir, int w, int h, int fullscreen)
{
    memset(g, 0, sizeof *g);
    snprintf(g->resdir, sizeof g->resdir, "%s", resdir);
    g->suffix = "-hd";
#ifdef __SWITCH__
    // Switch 2: the Switch 1 SDL port renders through Mesa/EGL, which does not come up on the
    // Switch 2 (black screen). Render in software instead and copy to the libnx framebuffer.
    // SDL's video subsystem must stay off: the software renderer only needs a surface, and the
    // Switch SDL port has no "dummy" video driver.
    if (SDL_Init(SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    g->surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!g->surf) { fprintf(stderr, "surface: %s\n", SDL_GetError()); return -2; }
    g->r = SDL_CreateSoftwareRenderer(g->surf);
    if (!g->r) { fprintf(stderr, "renderer: %s\n", SDL_GetError()); return -3; }
    // Two buffers are required: with one, the display never releases it while it is shown, so the
    // second framebufferBegin() times out and libounce aborts the app.
    if (R_FAILED(framebufferCreate(&s_fb, nwindowGetDefault(), (u32)w, (u32)h, PIXEL_FORMAT_RGBA_8888, 2)) ||
        R_FAILED(framebufferMakeLinear(&s_fb))) {
        fprintf(stderr, "framebufferCreate failed\n");
        return -6;
    }
    s_fb_ok = 1;
    (void)fullscreen;
#else
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    Uint32 flags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    g->win = SDL_CreateWindow("Geometry Dash (Switch 2 port)", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!g->win) return -2;
    g->r = SDL_CreateRenderer(g->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g->r) g->r = SDL_CreateRenderer(g->win, -1, 0);
    if (!g->r) return -3;
#endif
    SDL_GetRendererOutputSize(g->r, &g->w, &g->h);

    static const char *const sheets[] = {"GJ_GameSheet",     "GJ_GameSheet02",
                                         "GJ_GameSheet03",   "GJ_GameSheet04",
                                         "GJ_GameSheetGlow", "GJ_GameSheetIcons",
                                         "GJ_LaunchSheet", "FireSheet_01", "PixelSheet_01", "GJ_ParticleSheet"};
    if (gd_atlas_load(&g->atlas, resdir, g->suffix, sheets, 10)) return -4;
    for (int i = 0; i < g->atlas.nsheets; i++) {
        g->sheet_tex[i] = make_tex(g->r, g->atlas.sheets[i].rgba, g->atlas.sheets[i].w,
                                   g->atlas.sheets[i].h);
        if (!g->sheet_tex[i]) return -5;
        // the texture holds its own copy; free the CPU pixels to save memory (matters on the Switch)
        free(g->atlas.sheets[i].rgba);
        g->atlas.sheets[i].rgba = NULL;
    }
    return 0;
}

void gfx_quit(Gfx *g)
{
    for (int i = 0; i < g->atlas.nsheets; i++)
        if (g->sheet_tex[i]) SDL_DestroyTexture(g->sheet_tex[i]);
    gd_atlas_free(&g->atlas);
    if (g->r) SDL_DestroyRenderer(g->r);
#ifdef __SWITCH__
    if (s_fb_ok) { framebufferClose(&s_fb); s_fb_ok = 0; }
    if (g->surf) SDL_FreeSurface(g->surf);
#endif
    if (g->win) SDL_DestroyWindow(g->win);
    SDL_Quit();
}

void gfx_present(Gfx *g)
{
#ifdef __SWITCH__
    SDL_RenderFlush(g->r);   // draw calls are batched; finish them before reading the surface
    u32 stride;
    unsigned char *dst = framebufferBegin(&s_fb, &stride);
    if (dst) {
        const unsigned char *src = g->surf->pixels;
        int rows = g->surf->h, bytes = g->surf->w * 4;
        for (int y = 0; y < rows; y++)
            memcpy(dst + (size_t)y * stride, src + (size_t)y * g->surf->pitch, bytes);
    }
    framebufferEnd(&s_fb);
#else
    SDL_RenderPresent(g->r);
#endif
}

int gfx_load_image(Gfx *g, const char *file, GfxImage *out)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s", g->resdir, file);
    int w, h, n;
    unsigned char *px = stbi_load(path, &w, &h, &n, 4);
    if (!px) return -1;
    out->tex = make_tex(g->r, px, w, h);
    out->w = w;
    out->h = h;
    stbi_image_free(px);
    return out->tex ? 0 : -2;
}

const GdFrame *gfx_find(Gfx *g, const char *name) { return gd_atlas_find(&g->atlas, name); }

static SDL_Color col(GdColorF c)
{
    SDL_Color o;
    o.r = (Uint8)fminf(fmaxf(c.r, 0), 255);
    o.g = (Uint8)fminf(fmaxf(c.g, 0), 255);
    o.b = (Uint8)fminf(fmaxf(c.b, 0), 255);
    o.a = (Uint8)(fminf(fmaxf(c.a, 0), 1) * 255);
    return o;
}

static void quad(Gfx *g, SDL_Texture *tex, const float px[4], const float py[4],
                 const float u[4], const float v[4], SDL_Color c, int additive)
{
    SDL_Vertex vt[4];
    for (int i = 0; i < 4; i++) {
        vt[i].position.x = (g->mirror ? g->mir_w - px[i] : px[i]);
        vt[i].position.y = py[i];
        vt[i].color = c;
        vt[i].tex_coord.x = u[i];
        vt[i].tex_coord.y = v[i];
    }
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetTextureBlendMode(tex, additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(g->r, tex, vt, 4, idx, 6);
}

void gfx_frame(Gfx *g, const GdFrame *f, float cx, float cy, float rot, float sx, float sy,
               GdColorF c, int additive)
{
    if (c.a <= 0.003f) return;
    const GdSheet *sh = &g->atlas.sheets[f->sheet];
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
    quad(g, g->sheet_tex[f->sheet], px, py, u, v, col(c), additive);
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

void gfx_fill(Gfx *g, SDL_FRect rc, GdColorF c)
{
    if (g->mirror) rc.x = g->mir_w - rc.x - rc.w;
    SDL_Color k = col(c);
    SDL_SetRenderDrawBlendMode(g->r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g->r, k.r, k.g, k.b, k.a);
    SDL_RenderFillRectF(g->r, &rc);
}

void gfx_gradient_v(Gfx *g, SDL_FRect rc, GdColorF top, GdColorF bottom)
{
    SDL_Vertex v[4];
    SDL_Color a = col(top), b = col(bottom);
    v[0].position = (SDL_FPoint){rc.x, rc.y};              v[0].color = a;
    v[1].position = (SDL_FPoint){rc.x + rc.w, rc.y};       v[1].color = a;
    v[2].position = (SDL_FPoint){rc.x + rc.w, rc.y + rc.h}; v[2].color = b;
    v[3].position = (SDL_FPoint){rc.x, rc.y + rc.h};       v[3].color = b;
    for (int i = 0; i < 4; i++) v[i].tex_coord = (SDL_FPoint){0, 0};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(g->r, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(g->r, NULL, v, 4, idx, 6);
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
