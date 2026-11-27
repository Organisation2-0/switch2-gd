// SPDX-License-Identifier: GPL-3.0-or-later
// Platform part of the graphics layer: gfx_sdl.c (PC) and gfx_gl.c (Switch 2 GPU).
#ifndef GD_GFX_BACKEND_H
#define GD_GFX_BACKEND_H

#include "gfx.h"

// Opens the screen. w,h: size of the coordinate system used for drawing.
// Returns 0 or a negative error code and sets the SDL error text.
int gfxb_init(Gfx *g, int w, int h, const GfxVideo *video);
void gfxb_quit(Gfx *g);

// See gfx_set_video, gfx_max_height and gfx_refresh_hz.
void gfxb_set_video(Gfx *g, const GfxVideo *video);
int gfxb_max_height(const Gfx *g);
int gfxb_refresh_hz(void);

// Texture from RGBA bytes (not premultiplied), linear filtering. NULL on failure.
GfxTex *gfxb_tex_create(const unsigned char *rgba, int w, int h);
void gfxb_tex_destroy(GfxTex *t);

// One quad, corners in order TL, TR, BR, BL. tex NULL = plain colour.
// blend: 0 normal (premultiplied), 1 additive, 2 multiply, 3 invert.
void gfxb_quad(GfxTex *tex, const SDL_FPoint pos[4], const SDL_FPoint uv[4], const SDL_Color col[4],
               int blend);

void gfxb_clip(const SDL_Rect *r); // NULL = no clipping
void gfxb_begin_frame(Gfx *g);     // clears to black
void gfxb_present(void);
void gfxb_effects(const float values[][4]);     // GFX_EFFECT_VALUES of them, see gfx_effects
void gfxb_layer_begin(void);                    // see gfx_layer_begin
void gfxb_layer_end(const float values[][4]);   // values NULL: no effects
void gfxb_window_point(Gfx *g, int px, int py, float *x, float *y);
void gfxb_output_size(int *w, int *h);
int gfxb_read_pixels(unsigned char *rgba, int w, int h); // top row first; 0 = ok

#endif
