// SPDX-License-Identifier: GPL-3.0-or-later
// PC backend: SDL2 window and renderer.
#ifndef __SWITCH__
#include <stddef.h>

#include "gfx_backend.h"

static SDL_Renderer *s_r;
static int s_w, s_h;
static SDL_BlendMode s_blend; // premultiplied alpha

int gfxb_init(Gfx *g, int w, int h, int fullscreen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) return -1;
    Uint32 flags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (SDL_getenv("GD_HIDDEN")) flags = SDL_WINDOW_HIDDEN; // test runs without a visible window
    g->win = SDL_CreateWindow("Geometry Dash (Switch 2 port)", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!g->win) return -2;
    s_r = SDL_CreateRenderer(g->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!s_r) s_r = SDL_CreateRenderer(g->win, -1, 0);
    if (!s_r) return -3;
    s_blend = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
                                         SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
    if (SDL_SetRenderDrawBlendMode(s_r, s_blend) != 0) return -3;
    s_w = w;
    s_h = h;
    return 0;
}

void gfxb_quit(Gfx *g)
{
    if (s_r) { SDL_DestroyRenderer(s_r); s_r = NULL; }
    if (g->win) { SDL_DestroyWindow(g->win); g->win = NULL; }
}

GfxTex *gfxb_tex_create(const unsigned char *rgba, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(s_r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, rgba, w * 4);
    SDL_SetTextureBlendMode(t, s_blend);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return (GfxTex *)t;
}

void gfxb_tex_destroy(GfxTex *t) { SDL_DestroyTexture((SDL_Texture *)t); }

void gfxb_quad(GfxTex *tex, const SDL_FPoint pos[4], const SDL_FPoint uv[4], const SDL_Color col[4],
               int additive)
{
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        v[i].position = pos[i];
        v[i].color = col[i];
        v[i].tex_coord = uv[i];
    }
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_BlendMode mode = additive ? SDL_BLENDMODE_ADD : s_blend;
    if (tex) SDL_SetTextureBlendMode((SDL_Texture *)tex, mode);
    else SDL_SetRenderDrawBlendMode(s_r, mode);
    SDL_RenderGeometry(s_r, (SDL_Texture *)tex, v, 4, idx, 6);
}

void gfxb_clip(const SDL_Rect *r) { SDL_RenderSetClipRect(s_r, r); }

void gfxb_begin_frame(void)
{
    SDL_SetRenderDrawColor(s_r, 0, 0, 0, 255);
    SDL_RenderClear(s_r);
    SDL_RenderSetLogicalSize(s_r, s_w, s_h);
}

void gfxb_present(void) { SDL_RenderPresent(s_r); }

void gfxb_output_size(int *w, int *h) { SDL_GetRendererOutputSize(s_r, w, h); }

int gfxb_read_pixels(unsigned char *rgba, int w, int h)
{
    (void)h;
    return SDL_RenderReadPixels(s_r, NULL, SDL_PIXELFORMAT_ABGR8888, rgba, w * 4);
}
#endif
