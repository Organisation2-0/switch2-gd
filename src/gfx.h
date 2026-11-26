// SPDX-License-Identifier: GPL-3.0-or-later
// Drawing layer: sprite atlases, single textures, bitmap font (platform part: gfx_backend.h).
#ifndef GD_GFX_H
#define GD_GFX_H

#include <SDL.h>

#include "atlas.h"

typedef struct { float r, g, b, a; } GdColorF; // 0..255 for rgb, 0..1 for alpha
typedef struct GfxTex GfxTex;                  // texture of the platform backend

#define GFX_ICONS 192   // icon files kept loaded; the least recently drawn one makes room

typedef struct {
    char file[40];            // "icons/player_01", empty = free slot
    int ok;                   // 0: the file is not there
    unsigned used;            // frame number of the last use
    GdAtlas atlas;
    GfxTex *tex;
} GfxIcon;

typedef struct {
    SDL_Window *win;          // NULL on Switch 2
    int mirror;               // draw everything horizontally mirrored inside [0, mir_w]
    float fade;               // opacity multiplier for sprites (edge transitions)
    int cut;                  // sprites are cut to the half plane cut_a * x + cut_b * y + cut_c >= 0
    float cut_a, cut_b, cut_c;
    float mir_w;
    int w, h;                // output size in pixels
    GdAtlas atlas;            // gameplay + UI frames from the shared sheets
    GfxTex *sheet_tex[16];
    GfxIcon *icons;           // GFX_ICONS entries; their frames have sheet numbers from 16 on
    unsigned frame;
    char resdir[512];
    const char *suffix;
} Gfx;

typedef struct {
    GfxTex *tex;
    int w, h;
} GfxImage;

typedef struct {
    struct { int id, x, y, w, h, xoff, yoff, xadv; } ch[256];
    int has[256];
    int line_h, base;
    GfxImage img;
} GfxFont;

// Opens the screen and loads the shared sprite sheets from resdir.
int gfx_init(Gfx *g, const char *resdir, int w, int h, int fullscreen);
void gfx_quit(Gfx *g);

// Start a frame (clears the screen) / show the finished frame.
void gfx_begin_frame(Gfx *g);
void gfx_present(Gfx *g);

// Restricts drawing to a rectangle (NULL = whole screen).
void gfx_set_clip(Gfx *g, const SDL_Rect *r);

// Copy of the current frame as RGBA, top row first (free() it); NULL on failure.
unsigned char *gfx_read_pixels(Gfx *g, int *w, int *h);

// Loads a stand-alone PNG from the resources folder (e.g. "GJ_gradientBG-hd.png").
int gfx_load_image(Gfx *g, const char *file, GfxImage *out);
void gfx_free_image(GfxImage *im);

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
void gfx_glow_fill(Gfx *g, SDL_FRect rc, GdColorF c);   // the same, blended additively
void gfx_gradient_v(Gfx *g, SDL_FRect rc, GdColorF top, GdColorF bottom);

int gfx_font_load(Gfx *g, const char *name, GfxFont *f); // name without extension
float gfx_text_width(const GfxFont *f, const char *s, float scale);
// align: 0 = left, 1 = centre, 2 = right
void gfx_text(Gfx *g, const GfxFont *f, const char *s, float x, float y, float scale,
              int align, GdColorF c);

const GdFrame *gfx_find(Gfx *g, const char *name);

// Player icons. type: "player", "ship", "player_ball", "bird", "dart", "robot", "spider",
// "swing"; part: "", "_2", "_3", "_extra" (robot / spider: "_01", "_01_2", ...). Each icon comes
// from its own file in Resources/icons, like in the original; without it the shared sheet is used.
const GdFrame *gfx_icon_frame(Gfx *g, const char *type, int id, const char *part);
int gfx_icon_exists(Gfx *g, const char *type, int id);
// All layers of a one-piece icon (not robot / spider), first colour c1, second colour c2.
void gfx_icon_draw(Gfx *g, const char *type, int id, float x, float y, float rot, float sx, float sy,
                   GdColorF c1, GdColorF c2);

#endif
