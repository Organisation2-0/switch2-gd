// SPDX-License-Identifier: GPL-3.0-or-later
// Thin SDL2 layer: window, sprite atlases, single textures, bitmap font.
#ifndef GD_GFX_H
#define GD_GFX_H

#include <SDL.h>

#include "atlas.h"

typedef struct { float r, g, b, a; } GdColorF; // 0..255 for rgb, 0..1 for alpha

typedef struct {
    SDL_Renderer *r;
    SDL_Window *win;
    SDL_Surface *surf;        // Switch 2: software render target copied to the framebuffer
    int mirror;               // draw everything horizontally mirrored inside [0, mir_w]
    float mir_w;
    int w, h;                // output size in pixels
    GdAtlas atlas;            // gameplay + UI frames from the shared sheets
    SDL_Texture *sheet_tex[16];
    char resdir[512];
    const char *suffix;
} Gfx;

typedef struct {
    SDL_Texture *tex;
    int w, h;
} GfxImage;

typedef struct {
    struct { int id, x, y, w, h, xoff, yoff, xadv; } ch[256];
    int has[256];
    int line_h, base;
    GfxImage img;
} GfxFont;

// Creates window + renderer and loads the shared sprite sheets from resdir.
int gfx_init(Gfx *g, const char *resdir, int w, int h, int fullscreen);
void gfx_quit(Gfx *g);

// Shows the finished frame (SDL_RenderPresent on PC, framebuffer copy on Switch 2).
void gfx_present(Gfx *g);

// Loads a stand-alone PNG from the resources folder (e.g. "GJ_gradientBG-hd.png").
int gfx_load_image(Gfx *g, const char *file, GfxImage *out);

// Draws an atlas frame. (cx,cy): screen position of the object origin.
// sx,sy: screen pixels per sheet pixel (negative flips). rot: degrees clockwise.
void gfx_frame(Gfx *g, const GdFrame *f, float cx, float cy, float rot,
               float sx, float sy, GdColorF c, int additive);

// Draws part of a stand-alone image into a destination rectangle.
void gfx_image_rect(Gfx *g, const GfxImage *im, SDL_FRect src, SDL_FRect dst, GdColorF c);

// Nine-slice panel from a stand-alone image with the given border (source px).
void gfx_ninepatch(Gfx *g, const GfxImage *im, float border_src, SDL_FRect dst,
                   float border_dst, GdColorF c);

void gfx_fill(Gfx *g, SDL_FRect rc, GdColorF c);
void gfx_gradient_v(Gfx *g, SDL_FRect rc, GdColorF top, GdColorF bottom);

int gfx_font_load(Gfx *g, const char *name, GfxFont *f); // name without extension
float gfx_text_width(const GfxFont *f, const char *s, float scale);
// align: 0 = left, 1 = centre, 2 = right
void gfx_text(Gfx *g, const GfxFont *f, const char *s, float x, float y, float scale,
              int align, GdColorF c);

const GdFrame *gfx_find(Gfx *g, const char *name);

#endif
