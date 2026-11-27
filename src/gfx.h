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

// How the picture is shown (graphics menu).
typedef struct {
    int height;       // pixel height of the window (PC) or screen picture (Switch 2); 0: drawing size
    int fullscreen;   // PC
    int vsync;        // 1: wait for the display, 0: always show the newest picture
    int fps;          // Switch 2: 60 or 120 pictures per second
} GfxVideo;

typedef struct {
    SDL_Window *win;          // NULL on Switch 2
    int no_glow, no_particles, no_shake;   // effects switched off in the graphics menu
    int mirror;              // draw everything horizontally mirrored inside [0, mir_w]
    float fade;               // opacity multiplier for sprites (edge transitions)
    int cut;                  // sprites are cut to the half plane cut_a * x + cut_b * y + cut_c >= 0
    float cut_a, cut_b, cut_c;
    float mir_w;
    int turned;               // draw everything turned (camera rotation) around (turn_x, turn_y) ...
    float turn_x, turn_y, turn_cos, turn_sin;   // ... clockwise by the angle with this cosine and sine
    int w, h;               // output size in pixels
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

// Opens the screen and loads the shared sprite sheets from resdir. w,h: drawing coordinates.
int gfx_init(Gfx *g, const char *resdir, int w, int h, const GfxVideo *video);
void gfx_quit(Gfx *g);

// Changes the picture settings while running. The Switch 2 applies a new height at the next start.
void gfx_set_video(Gfx *g, const GfxVideo *video);
int gfx_max_height(const Gfx *g);   // the largest picture height that makes sense here
int gfx_refresh_hz(void);           // Switch 2: rate pictures are presented at; 0 if unknown

// Start a frame (clears the screen) / show the finished frame.
void gfx_begin_frame(Gfx *g);
void gfx_present(Gfx *g);

// Runs the shader effects over everything drawn so far. Positions are fractions of the picture,
// from the lower left corner. All values zero: nothing happens. values[i] (x, y, z, w):
// "Widths": distances count in widths of the picture, up and down in the same measure.
//  0 time, width/height               1 split screen: pictures across, up
//  2 bulge: centre, radius, bend      3 bulge factor, pinch radius
//  4 pinch: centre, factor x, y       5 pinch scale x, y
//  6 shock wave: centre, phase, frequency      7 amplitude, fading with distance, outer, inner
//  8 wave edges                       9 wave turned inwards
// 10 shock line: place, 1 = across the height, flipped, both ways
// 11 line phase, frequency, amplitude, fading  12 line edges
// 13 glitch: slice bottom, top, shift, colour  14 colour shift x, y, pixel block x, y
// 15 hard pixels, lens adds light, chromatic x, y
// 16 line glitch: red/blue shift, strength, time, cells   17 line threshold, darkness, frequency
// 18 radial blur: centre, step, fade          19 motion blur: x, y, fade, both ways
// 20 grayscale, by luminance, sepia           21 grayscale tint
// 22 invert shares                            23 hue: cosine, sine, on
// 24 colour change: factor - 1                25 colour change: added
// 26 lens circle: centre, inner and outer radius   27 tint, strength
#define GFX_EFFECT_VALUES 28
typedef struct { int any; float v[GFX_EFFECT_VALUES][4]; } GfxEffects;
void gfx_effects(Gfx *g, const GfxEffects *fx);
// The same for part of the picture only: what is drawn between the two calls is collected on its own,
// changed by the effects and then laid over what was there before.
void gfx_layer_begin(Gfx *g);
void gfx_layer_end(Gfx *g, const GfxEffects *fx);

// A position in the window (mouse events) in drawing coordinates.
void gfx_window_point(Gfx *g, int px, int py, float *x, float *y);

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

// A plain quad (corners in order TL, TR, BR, BL) with a colour of its own at each corner.
// blend: 0 normal, 1 additive, 2 multiply, 3 invert.
void gfx_quad_colors(Gfx *g, const float px[4], const float py[4], const GdColorF c[4], int blend);
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
