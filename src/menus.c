// SPDX-License-Identifier: GPL-3.0-or-later
// The menus, laid out like the original's, and their input (mouse, keys, controller).
//
// Positions are given in the original's coordinates: points, 320 of them from the bottom to the
// top of the screen, measured in pictures of the game. Only what works in the port is shown.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "app.h"
#include "coins.h"
#include "audio.h"

// ------------------------------------------------------------ primitives

static float pt_scale(const App *a) { return a->lh / 320.0f; }   // pixels per point
float pt_width(const App *a) { return a->lw / pt_scale(a); }

static GdColorF pal(int idx)
{
    float c[3];
    palette_color(idx, c);
    return C(c[0], c[1], c[2], 1);
}

static void hot_add(App *a, float x, float y, float w, float h, int id, int arg)
{
    const float p = pt_scale(a);
    if (id && a->nhot < MAX_HOT)
        a->hot[a->nhot++] = (Hot){{x * p, (320.0f - y) * p, w * p, h * p}, id, arg};
}

static int focused(const App *a, int id, int arg)
{
    return a->show_focus && a->focus_id == id && a->focus_arg == arg;
}

// The chosen button swells and shrinks.
static float swell(const App *a, int id, int arg)
{
    return focused(a, id, arg) ? 1.14f + 0.05f * sinf(a->t * 7.0f) : 1.0f;
}

// Sprite centred at (x,y) with separate scales (negative flips); NULL if the sheets lack it.
static const GdFrame *spr_xy(App *a, const char *name, float x, float y, float sx, float sy, GdColorF c)
{
    const GdFrame *f = gfx_find(&a->g, name);
    if (!f) return NULL;
    const float p = pt_scale(a), k = p / a->g.atlas.px_per_unit;
    gfx_frame(&a->g, f, x * p, (320.0f - y) * p, 0, sx * k, sy * k, c, 0);
    return f;
}

static const GdFrame *spr(App *a, const char *name, float x, float y, float scale, GdColorF c, int flip)
{
    return spr_xy(a, name, x, y, flip ? -scale : scale, scale, c);
}

static void spr_button(App *a, const char *name, float x, float y, float scale, int id, int arg, int flip)
{
    const GdFrame *f = spr(a, name, x, y, scale * swell(a, id, arg), WHITE, flip);
    if (f) hot_add(a, x, y, f->srcw * scale / a->g.atlas.px_per_unit, f->srch * scale / a->g.atlas.px_per_unit, id, arg);
}

void label(App *a, const GfxFont *f, const char *s, float x, float y, float scale, int align, GdColorF c,
           float max_w)
{
    const float p = pt_scale(a);
    float k = scale * p * 0.5f;   // the fonts are stored at two pixels per point
    const float w = gfx_text_width(f, s, k);
    if (max_w > 0 && w > max_w * p) k *= max_w * p / w;
    gfx_text(&a->g, f, s, x * p, (320.0f - y) * p - f->line_h * k * 0.5f, k, align, c);
}

// Rounded panel (nine-slice) covering x0..x0+w, y0..y0+h; corner: width of the border in points
// (0: as large as the image allows).
static void panel(App *a, const GfxImage *im, float x0, float y0, float w, float h, float corner, GdColorF c)
{
    const float p = pt_scale(a);
    const SDL_FRect r = {x0 * p, (320.0f - y0 - h) * p, w * p, h * p};
    if (!im->tex) { gfx_fill(&a->g, r, c); return; }
    const float border = im->w / 3.0f;
    if (corner <= 0) corner = border * 0.5f;
    corner = fminf(corner, fminf(w, h) * 0.5f);
    gfx_ninepatch(&a->g, im, border, r, corner * p, c);
}

static void box(App *a, float x0, float y0, float w, float h, GdColorF c)
{
    if (w <= 0) return;
    const float p = pt_scale(a);
    gfx_fill(&a->g, (SDL_FRect){x0 * p, (320.0f - y0 - h) * p, w * p, h * p}, c);
}

// A labelled button like the original's (ButtonSprite).
static void text_button(App *a, const GfxImage *im, const GfxFont *f, const char *s, float x, float y,
                        float w, float h, float font, int id, int arg, GdColorF text)
{
    const float k = swell(a, id, arg);
    panel(a, im, x - w * k * 0.5f, y - h * k * 0.5f, w * k, h * k, 9.0f, WHITE);
    label(a, f, s, x, y + 0.5f, font * k, 1, text, w * k - 6.0f);
    hot_add(a, x, y, w, h, id, arg);
}

// Progress bar with its percentage, w points wide, centred at (x,y).
static void bar(App *a, float x, float y, float w, float percent, GdColorF fill)
{
    const float p = pt_scale(a), h = w * 20.0f / 340.0f;
    const float left = (x - w * 0.5f) * p, top = (320.0f - y - h * 0.5f) * p;
    if (a->bar.tex) {
        gfx_image_rect(&a->g, &a->bar, (SDL_FRect){0, 0, (float)a->bar.w, (float)a->bar.h},
                       (SDL_FRect){left, top, w * p, h * p}, C(0, 0, 0, 125.0f / 255.0f));
        const float part = fminf(fmaxf(percent / 100.0f, 0.0f), 1.0f);
        if (part > 0)
            gfx_image_rect(&a->g, &a->bar, (SDL_FRect){0, 0, a->bar.w * part, (float)a->bar.h},
                           (SDL_FRect){left + 1.4f * p, top + 1.4f * p, (w - 2.8f) * part * p, (h - 2.8f) * p}, fill);
    }
    char s[16];
    snprintf(s, sizeof s, "%d%%", (int)percent);
    label(a, &a->big, s, x, y - 0.5f, h * 0.03f, 1, WHITE, 0);
}

// A volume (which: 0 music, 1 effects) as a slider like the original's, from x0 to x1 at height y.
static void volume_slider(App *a, int which, float x0, float x1, float y)
{
    const float p = pt_scale(a), w = x1 - x0, h = w * a->slider.h / (a->slider.w ? a->slider.w : 1);
    const float at = (which ? a->cfg.sfx : a->cfg.music) / 100.0f;
    a->slider_x0[which] = x0;
    a->slider_x1[which] = x1;
    if (a->slider.tex) {
        box(a, x0 + 2.0f, y - h * 0.22f, (w - 4.0f) * at, h * 0.44f, C(60, 200, 255, 1));
        gfx_image_rect(&a->g, &a->slider, (SDL_FRect){0, 0, (float)a->slider.w, (float)a->slider.h},
                       (SDL_FRect){x0 * p, (320.0f - y - h * 0.5f) * p, w * p, h * p}, WHITE);
    }
    const float d = 18.0f * swell(a, H_VOLUME, which);
    if (a->thumb.tex)
        gfx_image_rect(&a->g, &a->thumb, (SDL_FRect){0, 0, (float)a->thumb.w, (float)a->thumb.h},
                       (SDL_FRect){(x0 + w * at - d * 0.5f) * p, (320.0f - y - d * 0.5f) * p, d * p, d * p}, WHITE);
    hot_add(a, (x0 + x1) * 0.5f, y, w + 16.0f, 24.0f, H_VOLUME, which);
}

static void gradient(App *a, GdColorF c)
{
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 1));
    if (a->gradient.tex)
        gfx_image_rect(&a->g, &a->gradient, (SDL_FRect){0, 0, (float)a->gradient.w, (float)a->gradient.h},
                       (SDL_FRect){-2, -2, a->lw + 4.0f, a->lh + 4.0f}, c);
}

static void dim(App *a, float alpha)
{
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(0, 0, 0, alpha));
}

static void bottom_corners(App *a)
{
    const float W = pt_width(a);
    spr(a, "GJ_sideArt_001.png", 35.0f, 35.0f, 1.0f, WHITE, 0);
    spr(a, "GJ_sideArt_001.png", W - 35.0f, 35.0f, 1.0f, WHITE, 1);
}

// ------------------------------------------------------------ player icons

// icon numbers of a form, looked up the first time
static void find_icons(App *a, int form)
{
    if (a->icons[form]) return;
    int *list = malloc(sizeof(int) * 600);
    int n = 0;
    for (int id = 1; id < 600; id++)
        if (gfx_icon_exists(&a->g, g_icon_types[form], id)) list[n++] = id;
    if (!n) list[n++] = 1;
    a->icons[form] = list;
    a->nicons[form] = n;
}

// size of the icons of a form in the grid of the garage
static const float k_icon_size[IC_COUNT] = {0.8f, 0.65f, 0.75f, 0.6f, 0.8f, 0.6f, 0.55f, 0.65f};
// ... above the floor of the garage, and in the row of the colour page
static const float k_icon_big[IC_COUNT] = {1.6f, 1.4f, 1.5f, 1.35f, 1.6f, 1.35f, 1.25f, 1.45f};
static const float k_icon_row[IC_COUNT] = {1.15f, 0.95f, 1.1f, 0.95f, 1.15f, 0.95f, 0.85f, 1.05f};

static void icon_at(App *a, int form, int id, float x, float y, float scale, GdColorF c1, GdColorF c2)
{
    const float p = pt_scale(a), k = scale * p / a->g.atlas.px_per_unit;
    if (form == IC_ROBOT || form == IC_SPIDER) {
        if (!a->pose_loaded) {
            anim_load(&a->pose[0], &a->g, "Robot_AnimDesc.plist", "Robot_idle");
            anim_load(&a->pose[1], &a->g, "Spider_AnimDesc.plist", "Spider_idle");
            a->pose_loaded = 1;
        }
        anim_draw_icon(&a->g, &a->pose[form == IC_SPIDER], 0, form == IC_SPIDER, id, x * p, (320.0f - y) * p,
                       0, scale * p, k, 1.0f, c1, c2);
    } else {
        gfx_icon_draw(&a->g, g_icon_types[form], id, x * p, (320.0f - y) * p, 0, k, k, c1, c2);
    }
}

// ------------------------------------------------------------ title screen

// Scrolling ground whose surface lies at height `top`.
static void menu_ground(App *a, float top, GdColorF c, float scroll)
{
    const float p = pt_scale(a), tile = 128.0f * p, y = (320.0f - top) * p;
    if (a->ground_menu.tex) {
        const float off = fmodf(scroll * p, tile);
        for (float x = -off; x < a->lw; x += tile)
            gfx_image_rect(&a->g, &a->ground_menu,
                           (SDL_FRect){0, 0, (float)a->ground_menu.w, (float)a->ground_menu.h},
                           (SDL_FRect){x, y, tile, tile}, c);
    } else {
        gfx_fill(&a->g, (SDL_FRect){0, y, a->lw, a->lh - y}, c);
    }
    const GdFrame *line = gfx_find(&a->g, "floorLine_01_001.png");
    if (line)
        gfx_frame(&a->g, line, a->lw * 0.5f, y, 0, a->lw / (float)line->w, p / a->g.atlas.px_per_unit, WHITE, 1);
}

// The colour of the title screen wanders around the colour circle.
static GdColorF title_colour(float t)
{
    static const unsigned char k[12][3] = {
        {255, 0, 255}, {255, 0, 125}, {255, 0, 0},   {255, 125, 0}, {255, 255, 0}, {125, 255, 0},
        {0, 255, 0},   {0, 255, 125}, {0, 255, 255}, {0, 125, 255}, {0, 0, 255},   {125, 0, 255}};
    const float at = t / 4.0f;
    const int i = (int)at % 12, j = (i + 1) % 12;
    const float f = at - floorf(at);
    return C(k[i][0] + (k[j][0] - k[i][0]) * f, k[i][1] + (k[j][1] - k[i][1]) * f,
             k[i][2] + (k[j][2] - k[i][2]) * f, 1);
}

// An icon that crosses the title screen on the ground, jumping now and then.
static void title_runner(App *a)
{
    const float period = 5.0f;
    const int n = (int)(a->t / period);
    const float phase = a->t - n * period;
    const float x = -60.0f + phase * 311.6f;
    if (x > pt_width(a) + 60.0f) return;
    find_icons(a, IC_CUBE);
    const int icon = a->icons[IC_CUBE][(n * 37 + 5) % a->nicons[IC_CUBE]];
    const GdColorF c1 = pal((n * 29 + 38) % palette_size()), c2 = pal((n * 17 + 51) % palette_size());
    // one jump of the cube lasts 0.43 s and is 65 units high
    const float j = fmodf(phase + (n % 3) * 0.3f, 1.1f) / 0.43f;
    const float up = j < 1.0f ? 260.0f * j * (1.0f - j) : 0.0f, rot = j < 1.0f ? 180.0f * j : 0.0f;
    const float p = pt_scale(a), s = p / a->g.atlas.px_per_unit;
    gfx_icon_draw(&a->g, "player", icon, x * p, (320.0f - 90.0f - 15.0f - up) * p, rot, s, s, c1, c2);
}

static void draw_title(App *a)
{
    const float p = pt_scale(a), W = pt_width(a), mid = W * 0.5f;
    const GdColorF c = title_colour(a->t);
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, c);
    if (a->title_bg.tex) {
        const float step = 1024.0f / 1.875f * p;
        const float off = fmodf(a->t * 31.2f * p, step);
        for (float x = -off; x < a->lw; x += step)
            gfx_image_rect(&a->g, &a->title_bg, (SDL_FRect){0, 0, (float)a->title_bg.w, (float)a->title_bg.h},
                           (SDL_FRect){x, a->lh - step, step, step}, c);
    }
    title_runner(a);
    menu_ground(a, 90.0f, C(c.r * 0.72f, c.g * 0.72f, c.b * 0.72f, 1), a->t * 311.6f);

    label(a, &a->gold, "Ounce Dash", mid, 266.0f, 2.1f, 1, WHITE, W - 90.0f);
    spr_button(a, "GJ_closeBtn_001.png", 18.0f, 302.0f, 0.72f, H_QUIT, 0, 0);
    spr_button(a, "GJ_playBtn_001.png", mid, 170.0f, 1.0f, H_PLAY, 0, 0);
    spr_button(a, "GJ_garageBtn_001.png", mid - 110.0f, 170.0f, 1.0f, H_GARAGE, 0, 0);
    spr_button(a, "GJ_creatorBtn_001.png", mid + 110.0f, 170.0f, 1.0f, H_ONLINE, 0, 0);
    spr_button(a, "GJ_optionsBtn_001.png", mid, 45.0f, 1.0f, H_SETTINGS, 0, 0);
    label(a, &a->big, "Fan-made port - not affiliated with RobTop Games", mid, 9.0f, 0.24f, 1,
          C(255, 255, 255, 0.75f), 0);
}

// ------------------------------------------------------------ level select (official levels)

// The coins of a level in a row around (x,y): gold or silver ones for those owned, grey for the rest.
static void coin_marks(App *a, const LevelEntry *e, int count, float x, float y, float scale)
{
    char key[SONG_NAME + 8];
    level_coin_key(e, key, sizeof key);
    const int owned = coins_owned(key);
    for (int k = 0; k < count && k < GD_MAX_COINS; k++) {
        const char *name = !((owned >> k) & 1) ? "GJ_coinsIcon_gray_001.png"
                           : e->official ? "GJ_coinsIcon_001.png" : "GJ_coinsIcon2_001.png";
        spr(a, name, x + (k - (count - 1) * 0.5f) * 25.0f * scale, y, scale, WHITE, 0);
    }
}

static void draw_select(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    static const unsigned char k_page[9][3] = {{0, 0, 255},   {255, 0, 255}, {255, 0, 125}, {255, 0, 0},
                                               {255, 125, 0}, {255, 255, 0}, {0, 255, 0},   {0, 255, 255},
                                               {0, 125, 255}};
    const unsigned char *pc = k_page[a->sel % 9];
    const GdColorF c = C(pc[0], pc[1], pc[2], 1);
    gradient(a, c);
    menu_ground(a, 50.0f, C(c.r * 0.55f, c.g * 0.55f, c.b * 0.55f, 1), 0);
    spr(a, "GJ_topBar_001.png", mid, 303.0f, 1.0f, WHITE, 0);
    bottom_corners(a);
    spr_button(a, "GJ_arrow_01_001.png", 25.0f, 298.0f, 1.0f, H_BACK, 0, 0);
    if (!a->nofficial) return;

    const LevelEntry *e = &a->levels[a->sel];
    panel(a, &a->dark, mid - 170.0f, 172.5f, 340.0f, 95.0f, 0, C(0, 0, 0, 125.0f / 255.0f));
    hot_add(a, mid, 220.0f, 340.0f, 95.0f, H_CARD, 0);
    char name[48];
    snprintf(name, sizeof name, "diffIcon_%02d_btn_001.png", e->diff > 0 ? e->diff : 0);
    const float tw = fminf(gfx_text_width(&a->big, e->name, 0.9f * 0.5f), 244.0f);
    const float left = mid - (42.0f + tw) * 0.5f - 4.0f;
    spr(a, name, left + 13.0f, 223.0f, 1.1f, WHITE, 0);
    label(a, &a->big, e->name, left + 41.0f, 221.5f, 0.915f, 0, WHITE, 248.0f);
    if (e->stars > 0) {
        char s[16];
        snprintf(s, sizeof s, "%d", e->stars);
        spr(a, "GJ_starsIcon_001.png", mid + 155.0f, 255.0f, 0.8f, WHITE, 0);
        label(a, &a->big, s, mid + 143.0f, 254.0f, 0.6f, 2, C(255, 230, 40, 1), 0);
    }
    coin_marks(a, e, GD_MAX_COINS, mid + 134.0f, 186.0f, 0.9f);   // every official level has three
    label(a, &a->big, "Normal Mode", mid, 149.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 130.0f, 340.0f, e->best, C(0, 255, 0, 1));
    label(a, &a->big, "Practice Mode", mid, 99.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 80.0f, 340.0f, e->practice, C(0, 255, 255, 1));
    const float step = a->nofficial > 1 ? fminf(16.0f, (W - 60.0f) / (a->nofficial - 1)) : 16.0f;
    for (int i = 0; i < a->nofficial && a->dot.tex; i++) {
        const float p = pt_scale(a), x = mid + (i - (a->nofficial - 1) * 0.5f) * step, d = 8.0f;
        gfx_image_rect(&a->g, &a->dot, (SDL_FRect){0, 0, (float)a->dot.w, (float)a->dot.h},
                       (SDL_FRect){(x - d * 0.5f) * p, (320.0f - 15.0f - d * 0.5f) * p, d * p, d * p},
                       i == a->sel ? WHITE : C(125, 125, 125, 1));
        hot_add(a, x, 15.0f, step, 14.0f, H_PAGEDOT, i);
    }
    spr(a, "navArrowBtn_001.png", 25.0f, 160.0f, 1.0f, WHITE, 1);
    spr(a, "navArrowBtn_001.png", W - 25.0f, 160.0f, 1.0f, WHITE, 0);
    hot_add(a, 25.0f, 160.0f, 50.0f, 90.0f, H_PREV, 0);
    hot_add(a, W - 25.0f, 160.0f, 50.0f, 90.0f, H_NEXT, 0);
}

// ------------------------------------------------------------ online levels (custom levels)

#define LIST_TOP 264.0f
#define LIST_BOTTOM 50.0f
#define ROW_H 90.0f

static const char *const k_length[6] = {"Tiny", "Short", "Medium", "Long", "XL", "Plat."};

static int n_custom(const App *a) { return a->nlevels - a->nofficial; }

static void face_sprite(App *a, int face, float x, float y, float scale)
{
    char name[48];
    if (face < 0) snprintf(name, sizeof name, "difficulty_auto_btn_001.png");
    else snprintf(name, sizeof name, "difficulty_%02d_btn_001.png", face);
    spr(a, name, x, y, scale, WHITE, 0);
}

// "1234" -> "1234", "27123" -> "27.1K"
static void short_number(int v, char *out, size_t n)
{
    if (v >= 1000000) snprintf(out, n, "%.1fM", v / 1000000.0);
    else if (v >= 10000) snprintf(out, n, "%dK", v / 1000);
    else if (v >= 1000) snprintf(out, n, "%.1fK", v / 1000.0);
    else snprintf(out, n, "%d", v);
}

// One line "length, downloads, likes" starting at x; items that are not known are left out.
static void stat_line(App *a, const GdLevelInfo *l, float x, float y)
{
    char s[24];
    if (l->length >= 0 && l->length < 6) {
        spr(a, "GJ_timeIcon_001.png", x + 7.0f, y, 0.6f, WHITE, 0);
        label(a, &a->big, k_length[l->length], x + 17.0f, y, 0.45f, 0, WHITE, 52.0f);
    }
    if (!l->has_stats) return;
    short_number(l->downloads, s, sizeof s);
    spr(a, "GJ_downloadsIcon_001.png", x + 81.0f, y, 0.6f, WHITE, 0);
    label(a, &a->big, s, x + 91.0f, y, 0.45f, 0, WHITE, 48.0f);
    short_number(l->likes, s, sizeof s);
    spr(a, "GJ_likesIcon_001.png", x + 149.0f, y, 0.6f, WHITE, 0);
    label(a, &a->big, s, x + 159.0f, y, 0.45f, 0, WHITE, 44.0f);
}

static float list_limit(const App *a) { return fmaxf(0.0f, n_custom(a) * ROW_H - (LIST_TOP - LIST_BOTTOM)); }

// Scrolls so that the chosen row is completely visible.
static void list_follow(App *a)
{
    const float top = a->online_sel * ROW_H, view = LIST_TOP - LIST_BOTTOM;
    if (a->scroll > top) a->scroll = top;
    if (a->scroll < top + ROW_H - view) a->scroll = top + ROW_H - view;
    a->scroll = fminf(fmaxf(a->scroll, 0.0f), list_limit(a));
}

// The frame around a list, with its title.
static void table_frame(App *a, const char *title)
{
    const float mid = pt_width(a) * 0.5f, side_h = (LIST_TOP - LIST_BOTTOM) / 64.0f;
    spr_xy(a, "GJ_table_side_001.png", mid - 183.0f, (LIST_TOP + LIST_BOTTOM) * 0.5f, 1.0f, side_h, WHITE);
    spr_xy(a, "GJ_table_side_001.png", mid + 183.0f, (LIST_TOP + LIST_BOTTOM) * 0.5f, -1.0f, side_h, WHITE);
    spr(a, "GJ_table_top_001.png", mid, 276.0f, 1.0f, WHITE, 0);
    spr(a, "GJ_table_bottom_001.png", mid, 36.0f, 1.0f, WHITE, 0);
    label(a, &a->big, title, mid, 281.0f, 0.82f, 1, WHITE, 260.0f);
}

static void draw_online(App *a)
{
    const float p = pt_scale(a), W = pt_width(a), mid = W * 0.5f;
    const int n = n_custom(a);
    gradient(a, C(0, 102, 255, 1));
    bottom_corners(a);
    box(a, mid - 176.0f, LIST_BOTTOM, 352.0f, LIST_TOP - LIST_BOTTOM, C(161, 88, 44, 1));

    const SDL_Rect clip = {(int)((mid - 176.0f) * p), (int)((320.0f - LIST_TOP) * p), (int)(352.0f * p),
                           (int)((LIST_TOP - LIST_BOTTOM) * p + 0.5f)};
    gfx_set_clip(&a->g, &clip);
    for (int i = 0; i < n; i++) {
        const float top = LIST_TOP - i * ROW_H + a->scroll;
        if (top <= LIST_BOTTOM || top - ROW_H >= LIST_TOP) continue;
        const LevelEntry *e = &a->levels[a->nofficial + i];
        const GdLevelInfo *l = &e->info;
        const int chosen = a->show_focus && i == a->online_sel;
        box(a, mid - 176.0f, top - ROW_H, 352.0f, ROW_H, i & 1 ? C(194, 114, 62, 1) : C(161, 88, 44, 1));
        box(a, mid - 176.0f, top - ROW_H, 352.0f, 0.6f, C(60, 30, 12, 1));
        face_sprite(a, l->face, mid - 152.0f, top - 40.0f, 0.95f);
        if (l->stars > 0) {
            char s[16];
            snprintf(s, sizeof s, "%d", l->stars);
            label(a, &a->big, s, mid - 151.0f, top - 72.0f, 0.45f, 2, WHITE, 0);
            spr(a, "GJ_starsIcon_001.png", mid - 143.0f, top - 72.0f, 0.55f, WHITE, 0);
        }
        label(a, &a->big, e->name, mid - 127.0f, top - 20.0f, 0.68f, 0, WHITE, 216.0f);
        if (l->creator[0]) {
            char s[64];
            snprintf(s, sizeof s, "By %s", l->creator);
            label(a, &a->gold, s, mid - 127.0f, top - 45.0f, 0.62f, 0, WHITE, 205.0f);
        }
        stat_line(a, l, mid - 127.0f, top - 71.0f);
        // the view button and the whole row open the level; only what is visible can be clicked
        const float k = chosen ? 1.12f + 0.04f * sinf(a->t * 7.0f) : 1.0f;
        panel(a, &a->green_btn, mid + 136.0f - 32.0f * k, top - 45.0f - 14.0f * k, 64.0f * k, 28.0f * k, 9.0f, WHITE);
        label(a, &a->big, "View", mid + 136.0f, top - 44.5f, 0.75f * k, 1, WHITE, 0);
        const float hi = fminf(top, LIST_TOP), lo = fmaxf(top - ROW_H, LIST_BOTTOM);
        if (hi - lo > 8.0f) hot_add(a, mid, (hi + lo) * 0.5f, 352.0f, hi - lo, H_ROW, i);
    }
    gfx_set_clip(&a->g, NULL);
    if (!n) {
        label(a, &a->big, "No levels yet", mid, 180.0f, 0.8f, 1, WHITE, 320.0f);
        label(a, &a->big, "Put .gdlvl files into the levels folder:", mid, 140.0f, 0.4f, 1, WHITE, 320.0f);
        label(a, &a->gold, a->custom_dir, mid, 120.0f, 0.5f, 1, WHITE, 320.0f);
    }

    table_frame(a, "Online Levels");
    if (n) {
        char s[32];
        snprintf(s, sizeof s, n == 1 ? "%d level" : "%d levels", n);
        label(a, &a->gold, s, W - 8.0f, 309.0f, 0.5f, 2, WHITE, 0);
    }
    spr_button(a, "GJ_arrow_01_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);
}

// The custom level shown, and the song chosen for it.
static const LevelEntry *shown_level(const App *a)
{
    return a->online_sel < n_custom(a) ? &a->levels[a->nofficial + a->online_sel] : NULL;
}

static const char *current_choice(const App *a)
{
    return shown_level(a) ? level_choice(shown_level(a)) : "none";
}

// UTF-8 as the fonts of the game can show it: umlauts spelled out, other special letters as "?".
static void plain_letters(const char *in, char *out, size_t cap)
{
    static const struct { unsigned char second; const char *as; } umlaut[] = {
        {0xA4, "ae"}, {0xB6, "oe"}, {0xBC, "ue"}, {0x84, "Ae"}, {0x96, "Oe"}, {0x9C, "Ue"}, {0x9F, "ss"}};
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)in; *s && o + 3 < cap; s++) {
        if (*s < 0x80) { out[o++] = (char)*s; continue; }
        const char *as = NULL;
        for (int i = 0; *s == 0xC3 && i < 7; i++)
            if (s[1] == umlaut[i].second) as = umlaut[i].as;
        if (as) { out[o++] = as[0]; out[o++] = as[1]; s++; }
        else if (*s >= 0xC0) out[o++] = '?';
    }
    out[o] = 0;
}

// Name of a choice and what kind of song it is; a file that has gone counts as none.
static void song_title(const App *a, const char *choice, int verify, char *title, size_t n, char *kind, size_t m)
{
    char path[720];
    snprintf(title, n, "No music");
    snprintf(kind, m, "No song chosen");
    if (!strcmp(choice, "none")) return;
    if (!strcmp(choice, "bundle")) {
        const LevelEntry *e = shown_level(a);
        if (!e) return;
        plain_letters(e->song_name, title, n);
        char *dot = strrchr(title, '.');
        if (dot) *dot = 0;
        snprintf(kind, m, "Song of this level file");
    } else if (!songs_path(choice, a->resdir, a->music_dir, path, sizeof path) || (verify && !songs_present(path))) {
        snprintf(kind, m, "File not found");
    } else if (!strncmp(choice, "official:", 9)) {
        snprintf(title, n, "%s", g_official_songs[atoi(choice + 9)].title);
        snprintf(kind, m, "Official song");
    } else {
        plain_letters(choice + 5, title, n);
        char *dot = strrchr(title, '.');
        if (dot) *dot = 0;
        snprintf(kind, m, "Your music file");
    }
}

static void draw_level(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    gradient(a, C(0, 102, 255, 1));
    bottom_corners(a);
    spr_button(a, "GJ_arrow_01_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);
    if (a->online_sel >= n_custom(a)) return;
    const LevelEntry *e = &a->levels[a->nofficial + a->online_sel];
    const GdLevelInfo *l = &e->info;
    label(a, &a->big, e->name, mid, 301.0f, 0.95f, 1, WHITE, W - 110.0f);
    if (l->creator[0]) {
        char s[64];
        snprintf(s, sizeof s, "By %s", l->creator);
        label(a, &a->gold, s, mid, 275.0f, 0.8f, 1, WHITE, W - 110.0f);
    }
    face_sprite(a, l->face, mid - 100.0f, 214.0f, 1.0f);
    if (l->stars > 0) {
        char s[16];
        snprintf(s, sizeof s, "%d", l->stars);
        label(a, &a->big, s, mid - 99.0f, 183.0f, 0.5f, 2, WHITE, 0);
        spr(a, "GJ_starsIcon_001.png", mid - 90.0f, 183.0f, 0.65f, WHITE, 0);
    }
    spr_button(a, "GJ_playBtn2_001.png", mid, 211.0f, 0.95f, H_START, 0, 0);
    {   // known once the level has been finished
        char key[SONG_NAME + 8];
        level_coin_key(e, key, sizeof key);
        coin_marks(a, e, coins_count(key), mid, 255.0f, 0.75f);
    }
    char s[24];
    if (l->has_stats) {
        short_number(l->downloads, s, sizeof s);
        spr(a, "GJ_downloadsIcon_001.png", mid + 91.0f, 241.0f, 1.0f, WHITE, 0);
        label(a, &a->big, s, mid + 112.0f, 241.0f, 0.6f, 0, WHITE, 70.0f);
        short_number(l->likes, s, sizeof s);
        spr(a, "GJ_likesIcon_001.png", mid + 91.0f, 211.0f, 1.0f, WHITE, 0);
        label(a, &a->big, s, mid + 112.0f, 211.0f, 0.6f, 0, WHITE, 70.0f);
    }
    if (l->length >= 0 && l->length < 6) {
        spr(a, "GJ_timeIcon_001.png", mid + 91.0f, 181.0f, 1.0f, WHITE, 0);
        label(a, &a->big, k_length[l->length], mid + 112.0f, 181.0f, 0.6f, 0, WHITE, 70.0f);
    }
    label(a, &a->big, "Normal Mode", mid, 152.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 137.0f, 238.0f, e->best, C(0, 255, 0, 1));
    label(a, &a->big, "Practice Mode", mid, 119.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 104.0f, 238.0f, e->practice, C(0, 255, 255, 1));

    // the song, where the original shows it
    char title[SONG_NAME], kind[32];
    song_title(a, current_choice(a), 1, title, sizeof title, kind, sizeof kind);
    panel(a, &a->brown, mid - 150.0f, 14.0f, 300.0f, 78.0f, 12.0f, WHITE);
    label(a, &a->big, title, mid - 137.0f, 68.0f, 0.7f, 0, WHITE, 274.0f);
    label(a, &a->gold, kind, mid - 137.0f, 37.0f, 0.55f, 0, WHITE, 180.0f);
    text_button(a, &a->green_btn, &a->big, "Change", mid + 102.0f, 37.0f, 68.0f, 26.0f, 0.55f, H_SONG, 0, WHITE);
}

// ------------------------------------------------------------ song list

#define SONG_TOP 264.0f
#define SONG_H 36.0f

// rows: the song inside the level file (if it has one), no music, the player's files, the songs
// of the original
static int bundled(const App *a) { return shown_level(a) && shown_level(a)->song_name[0]; }

static int song_rows(const App *a) { return bundled(a) + 1 + a->nsongs + SONGS_OFFICIAL; }

static void song_choice_of_row(const App *a, int row, char *out, size_t cap)
{
    row -= bundled(a);
    if (row < 0) snprintf(out, cap, "bundle");
    else if (row == 0) snprintf(out, cap, "none");
    else if (row <= a->nsongs) snprintf(out, cap, "file:%s", a->songs[row - 1]);
    else snprintf(out, cap, "official:%d", row - 1 - a->nsongs);
}

static float song_limit(const App *a) { return fmaxf(0.0f, song_rows(a) * SONG_H - (SONG_TOP - LIST_BOTTOM)); }

static void song_follow(App *a)
{
    const float top = a->song_sel * SONG_H, view = SONG_TOP - LIST_BOTTOM;
    if (a->song_scroll > top) a->song_scroll = top;
    if (a->song_scroll < top + SONG_H - view) a->song_scroll = top + SONG_H - view;
    a->song_scroll = fminf(fmaxf(a->song_scroll, 0.0f), song_limit(a));
}

static void draw_songs(App *a)
{
    const float p = pt_scale(a), W = pt_width(a), mid = W * 0.5f;
    gradient(a, C(0, 102, 255, 1));
    bottom_corners(a);
    box(a, mid - 176.0f, LIST_BOTTOM, 352.0f, SONG_TOP - LIST_BOTTOM, C(161, 88, 44, 1));
    const SDL_Rect clip = {(int)((mid - 176.0f) * p), (int)((320.0f - SONG_TOP) * p), (int)(352.0f * p),
                           (int)((SONG_TOP - LIST_BOTTOM) * p + 0.5f)};
    gfx_set_clip(&a->g, &clip);
    const char *chosen = current_choice(a);
    for (int i = 0; i < song_rows(a); i++) {
        const float top = SONG_TOP - i * SONG_H + a->song_scroll;
        if (top <= LIST_BOTTOM || top - SONG_H >= SONG_TOP) continue;
        char choice[SONG_NAME + 12], title[SONG_NAME], kind[32];
        song_choice_of_row(a, i, choice, sizeof choice);
        song_title(a, choice, 0, title, sizeof title, kind, sizeof kind);   // files just listed
        const int mine = !strcmp(choice, chosen), cursor = a->show_focus && i == a->song_sel;
        box(a, mid - 176.0f, top - SONG_H, 352.0f, SONG_H,
            cursor ? C(224, 150, 92, 1) : i & 1 ? C(194, 114, 62, 1) : C(161, 88, 44, 1));
        box(a, mid - 176.0f, top - SONG_H, 352.0f, 0.6f, C(60, 30, 12, 1));
        label(a, &a->big, title, mid - 164.0f, top - 13.0f, 0.55f, 0, WHITE, 285.0f);
        label(a, &a->gold, kind, mid - 164.0f, top - 27.0f, 0.34f, 0, WHITE, 285.0f);
        if (mine) spr(a, "GJ_completesIcon_001.png", mid + 150.0f, top - SONG_H * 0.5f, 0.9f, WHITE, 0);
        const float hi = fminf(top, SONG_TOP), lo = fmaxf(top - SONG_H, LIST_BOTTOM);
        if (hi - lo > 8.0f) hot_add(a, mid, (hi + lo) * 0.5f, 352.0f, hi - lo, H_SONGROW, i);
    }
    gfx_set_clip(&a->g, NULL);
    table_frame(a, "Songs");
    label(a, &a->big, "Your own songs: put .mp3, .ogg or .wav files into", mid, 15.0f, 0.3f, 1, WHITE, W - 150.0f);
    label(a, &a->gold, a->music_dir, mid, 5.5f, 0.35f, 1, WHITE, W - 150.0f);
    spr_button(a, "GJ_arrow_01_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);
}

// ------------------------------------------------------------ settings

static void draw_settings(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    draw_title(a);
    dim(a, 0.55f);
    a->nhot = 0;
    box(a, mid - 176.0f, LIST_BOTTOM, 352.0f, LIST_TOP - LIST_BOTTOM, C(0, 0, 0, 0.6f));
    table_frame(a, "Settings");
    text_button(a, &a->green_btn, &a->gold, "Keys", mid - 82.0f, 212.0f, 140.0f, 30.0f, 0.85f, H_KEYS, 0, WHITE);
    text_button(a, &a->green_btn, &a->gold, "Graphics", mid + 82.0f, 212.0f, 140.0f, 30.0f, 0.85f, H_GRAPHICS, 0,
                WHITE);
    label(a, &a->big, "Music", mid, 140.0f, 0.7f, 1, WHITE, 0);
    volume_slider(a, 0, mid - 104.0f, mid + 104.0f, 117.0f);
    label(a, &a->big, "SFX", mid, 92.0f, 0.7f, 1, WHITE, 0);
    volume_slider(a, 1, mid - 104.0f, mid + 104.0f, 69.0f);
    spr_button(a, "GJ_arrow_03_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);
}

// ------------------------------------------------------------ graphics

enum { OPT_RESOLUTION, OPT_FULLSCREEN, OPT_VSYNC, OPT_FPS, OPT_SHOW_FPS, OPT_GLOW, OPT_PARTICLES, OPT_SHAKE };

// the rows of the menu: the Switch 2 always fills its screen, the PC follows its monitor's rate
#ifdef __SWITCH__
static const int k_options[] = {OPT_RESOLUTION, OPT_VSYNC, OPT_FPS, OPT_SHOW_FPS, OPT_GLOW, OPT_PARTICLES, OPT_SHAKE};
static const int k_heights[] = {720, 1080, 1440, 2160};
#else
static const int k_options[] = {OPT_RESOLUTION, OPT_FULLSCREEN, OPT_VSYNC, OPT_SHOW_FPS, OPT_GLOW, OPT_PARTICLES,
                                OPT_SHAKE};
static const int k_heights[] = {720, 900, 1080, 1440, 2160};
#endif
#define OPTION_COUNT ((int)(sizeof k_options / sizeof k_options[0]))
#define HEIGHT_COUNT ((int)(sizeof k_heights / sizeof k_heights[0]))

static int is_fullscreen(const App *a) { return a->cfg.fullscreen || a->fullscreen_arg; }

// the setting behind an on / off row, else NULL
static int *option_switch(App *a, int opt)
{
    switch (opt) {
    case OPT_VSYNC: return &a->cfg.vsync;
    case OPT_SHOW_FPS: return &a->cfg.show_fps;
    case OPT_GLOW: return &a->cfg.glow;
    case OPT_PARTICLES: return &a->cfg.particles;
    case OPT_SHAKE: return &a->cfg.shake;
    default: return NULL;
    }
}

// next (dir 1) or previous (dir -1) value of a row
static void option_step(App *a, int opt, int dir)
{
    int *on = option_switch(a, opt);
    if (on) *on = !*on;
    else if (opt == OPT_FPS) a->cfg.fps = a->cfg.fps == 120 ? 60 : 120;
    else if (opt == OPT_FULLSCREEN) {
        a->cfg.fullscreen = !is_fullscreen(a);
        a->fullscreen_arg = 0;
    } else {
#ifndef __SWITCH__
        if (is_fullscreen(a)) return;   // the size is the screen's
#endif
        int n = 1, at = 0;   // how many of the sizes fit this screen, and the present one
        while (n < HEIGHT_COUNT && k_heights[n] <= gfx_max_height(&a->g)) n++;
        for (int i = 1; i < n; i++)
            if (k_heights[i] <= a->cfg.height) at = i;
        a->cfg.height = k_heights[(at + dir + n) % n];
    }
    apply_graphics(a);
    config_save(&a->cfg, a->cfg_path);
}

static void draw_graphics(App *a)
{
    static const struct { const char *name, *hint; } k_text[] = {
        {"Resolution", "Higher: smoother edges, more work for the graphics chip"},
        {"Fullscreen", "Fills the whole screen"},
        {"VSync", "Off: less input delay, uses more power"},
        {"Frame Rate", "120 needs a screen that can do 120 Hz"},
        {"FPS Counter", "Shown in the lower left corner"},
        {"Glow", "The glow around objects"},
        {"Particles", "Sparks and dust of the icon, orbs and portals"},
        {"Screen Shake", "The camera shaking in some levels"}};
    const float W = pt_width(a), mid = W * 0.5f;
    gradient(a, C(0, 102, 255, 1));
    bottom_corners(a);
    box(a, mid - 176.0f, LIST_BOTTOM, 352.0f, LIST_TOP - LIST_BOTTOM, C(0, 0, 0, 0.6f));
    table_frame(a, "Graphics");
    const char *hint = "";
    for (int i = 0; i < OPTION_COUNT; i++) {
        const int opt = k_options[i];
        const float y = LIST_TOP - 20.0f - i * 29.0f;
        const int *on = option_switch(a, opt);
        char value[24];
        int lit = 1;
        if (on) lit = *on;
        else if (opt == OPT_FULLSCREEN) lit = is_fullscreen(a);
        if (opt == OPT_FPS) snprintf(value, sizeof value, "%d FPS", a->cfg.fps);
        else if (opt != OPT_RESOLUTION) snprintf(value, sizeof value, "%s", lit ? "On" : "Off");
#ifndef __SWITCH__
        else if (is_fullscreen(a)) { snprintf(value, sizeof value, "Screen"); lit = 0; }
#endif
        else snprintf(value, sizeof value, "%d x %d", a->cfg.height * 16 / 9, a->cfg.height);
        label(a, &a->big, k_text[opt].name, mid - 158.0f, y, 0.55f, 0, WHITE, 160.0f);
        text_button(a, lit ? &a->green_btn : &a->grey_btn, &a->big, value, mid + 92.0f, y, 132.0f, 23.0f, 0.5f,
                    H_OPTION, opt, WHITE);
        if (focused(a, H_OPTION, opt)) hint = k_text[opt].hint;
    }
#ifdef __SWITCH__
    if (a->cfg.height != a->g.h) hint = "The new resolution is used at the next start";
#endif
    label(a, &a->gold, hint, mid, 17.0f, 0.45f, 1, WHITE, W - 150.0f);
    spr_button(a, "GJ_arrow_03_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);
}

// ------------------------------------------------------------ garage

static const char *const k_tab_off[IC_COUNT] = {
    "gj_iconBtn_off_001.png", "gj_shipBtn_off_001.png",  "gj_ballBtn_off_001.png",   "gj_birdBtn_off_001.png",
    "gj_dartBtn_off_001.png", "gj_robotBtn_off_001.png", "gj_spiderBtn_off_001.png", "gj_swingBtn_off_001.png"};
static const char *const k_tab_on[IC_COUNT] = {
    "gj_iconBtn_on_001.png", "gj_shipBtn_on_001.png",  "gj_ballBtn_on_001.png",   "gj_birdBtn_on_001.png",
    "gj_dartBtn_on_001.png", "gj_robotBtn_on_001.png", "gj_spiderBtn_on_001.png", "gj_swingBtn_on_001.png"};

#define GRID_COLS 12
#define GRID_ROWS 3
#define PER_PAGE (GRID_COLS * GRID_ROWS)

static int pages(const App *a) { return (a->nicons[a->tab] + PER_PAGE - 1) / PER_PAGE; }

static void show_tab(App *a, int tab)
{
    a->tab = (tab + IC_COUNT) % IC_COUNT;
    find_icons(a, a->tab);
    a->page = 0;
    for (int i = 0; i < a->nicons[a->tab]; i++)
        if (a->icons[a->tab][i] == a->cfg.icon[a->tab]) a->page = i / PER_PAGE;
}

static void turn_page(App *a, int dir)
{
    a->page = (a->page + dir + pages(a)) % pages(a);
    if (a->focus_id == H_ICON) {   // stay at the same place of the grid
        int slot = 0;
        for (int i = 0; i < a->nicons[a->tab]; i++)
            if (a->icons[a->tab][i] == a->focus_arg) slot = i % PER_PAGE;
        int at = a->page * PER_PAGE + slot;
        if (at >= a->nicons[a->tab]) at = a->nicons[a->tab] - 1;
        a->focus_arg = a->icons[a->tab][at];
    }
}

static void draw_garage(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    find_icons(a, a->tab);
    gradient(a, C(150, 150, 150, 1));
    bottom_corners(a);
    spr_xy(a, "GJ_sideArt_001.png", 35.0f, 285.0f, 1.0f, -1.0f, WHITE);
    spr_button(a, "GJ_arrow_03_001.png", 24.0f, 297.0f, 1.0f, H_BACK, 0, 0);

    const GdColorF c1 = pal(a->cfg.col1), c2 = pal(a->cfg.col2);
    if (a->cfg.name[0]) label(a, &a->gold, a->cfg.name, mid, 283.0f, 0.95f, 1, WHITE, W - 140.0f);
    icon_at(a, a->tab, a->cfg.icon[a->tab], mid, 232.0f, k_icon_big[a->tab], c1, c2);
    spr(a, "floorLine_01_001.png", mid, 210.0f, 1.0f, WHITE, 0);
    spr_button(a, "GJ_paintBtn_001.png", 30.0f, 200.0f, 1.0f, H_COLORS, 0, 0);

    for (int i = 0; i < IC_COUNT; i++)
        spr_button(a, i == a->tab ? k_tab_on[i] : k_tab_off[i], mid + (i - (IC_COUNT - 1) * 0.5f) * 34.0f, 189.0f,
                   0.9f, H_TAB, i, 0);

    panel(a, &a->dark, mid - 192.5f, 45.0f, 385.0f, 100.0f, 10.0f, C(0, 0, 0, 90.0f / 255.0f));
    const GdColorF grid2 = C(175, 175, 175, 1);
    for (int i = 0; i < PER_PAGE; i++) {
        const int at = a->page * PER_PAGE + i;
        if (at >= a->nicons[a->tab]) break;
        const int id = a->icons[a->tab][at];
        const float x = mid - 165.0f + (i % GRID_COLS) * 30.0f, y = 125.0f - (i / GRID_COLS) * 30.0f;
        icon_at(a, a->tab, id, x, y, k_icon_size[a->tab] * swell(a, H_ICON, id), WHITE, grid2);
        if (id == a->cfg.icon[a->tab]) spr(a, "GJ_select_001.png", x, y, 0.82f, WHITE, 0);
        if (focused(a, H_ICON, id)) spr(a, "GJ_select_001.png", x, y, 0.9f, C(255, 230, 40, 1), 0);
        hot_add(a, x, y, 30.0f, 30.0f, H_ICON, id);
    }
    if (pages(a) > 1) {
        spr_button(a, "GJ_arrow_01_001.png", mid - 220.0f, 95.0f, 1.0f, H_TURN, -1, 0);
        spr_button(a, "GJ_arrow_01_001.png", mid + 220.0f, 95.0f, 1.0f, H_TURN, 1, 1);
        const float step = fminf(24.4f, (W - 20.0f) / pages(a));
        for (int i = 0; i < pages(a); i++)
            spr_button(a, i == a->page ? "gj_navDotBtn_on_001.png" : "gj_navDotBtn_off_001.png",
                       mid + (i - (pages(a) - 1) * 0.5f) * step, 24.0f, 0.85f * fminf(1.0f, step / 20.0f), H_DOT, i, 0);
    }
}

// first swatch of each group of the colour page, from the middle of the screen
static void swatch_position(int i, float mid, float *x, float *y)
{
    static const float gx[4] = {-198.0f, -90.0f, 18.0f, 126.0f};
    const int groups = 8;
    int first = 0;
    for (int g = 0; g < groups; g++) {
        const int n = g == 5 ? 16 : 12;
        if (i < first + n) {
            *x = mid + gx[g % 4] + ((i - first) % 4) * 24.0f;
            *y = (g < 4 ? 197.3f : 115.3f) - ((i - first) / 4) * 24.1f;
            return;
        }
        first += n;
    }
    *x = mid + 54.0f + (i - first) * 24.0f;
    *y = 34.0f;
}

static void draw_colors(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    dim(a, 0.6f);
    a->nhot = 0;   // the garage behind cannot be clicked
    panel(a, &a->grey, mid - 225.0f, 10.0f, 450.0f, 300.0f, 0, WHITE);
    const GdColorF c1 = pal(a->cfg.col1), c2 = pal(a->cfg.col2);
    text_button(a, a->which == 0 ? &a->green_btn : &a->grey_btn, &a->big, "Col1", mid + 88.0f, 292.0f, 42.0f,
                18.0f, 0.45f, H_WHICH, 0, WHITE);
    text_button(a, a->which == 1 ? &a->green_btn : &a->grey_btn, &a->big, "Col2", mid + 138.0f, 292.0f, 42.0f,
                18.0f, 0.45f, H_WHICH, 1, WHITE);
    panel(a, &a->dark, mid - 210.0f, 218.0f, 420.0f, 60.0f, 10.0f, C(0, 0, 0, 90.0f / 255.0f));
    for (int i = 0; i < IC_COUNT; i++)
        icon_at(a, i, a->cfg.icon[i], mid - 182.0f + i * 52.0f, 248.0f, k_icon_row[i], c1, c2);
    const int sel = a->which ? a->cfg.col2 : a->cfg.col1;
    for (int i = 0; i < palette_size(); i++) {
        float x, y;
        swatch_position(i, mid, &x, &y);
        const float k = swell(a, H_SWATCH, i);
        panel(a, &a->white, x - 11.0f * k, y - 11.0f * k, 22.0f * k, 22.0f * k, 5.0f, C(0, 0, 0, 1));
        panel(a, &a->white, x - 9.5f * k, y - 9.5f * k, 19.0f * k, 19.0f * k, 4.0f, pal(i));
        if (i == sel) spr(a, "GJ_select_001.png", x, y, 0.72f * k, WHITE, 0);
        hot_add(a, x, y, 23.0f, 23.0f, H_SWATCH, i);
    }
    spr_button(a, "GJ_closeBtn_001.png", mid - 215.0f, 299.0f, 0.75f, H_CLOSE, 0, 0);
}

// ------------------------------------------------------------ key bindings

static const char *const k_pad_names[16] = {"A", "B", "X", "Y", "L3", "R3", "L", "R",
                                            "ZL", "ZR", "+", "-", "Left", "Up", "Right", "Down"};

static void binding_label(const Config *c, int act, int col, char *out, size_t n)
{
    if (col < 2) {
        int k = c->bind[act].key[col];
        if (!k) { snprintf(out, n, "-"); return; }
        snprintf(out, n, "%.14s", SDL_GetKeyName(k));
    } else {
        int b = c->bind[act].joy[col - 2];
        if (b < 0) snprintf(out, n, "-");
        else if (b < 16) snprintf(out, n, "%s", k_pad_names[b]);
        else snprintf(out, n, "Btn %d", b);
    }
}

static void draw_controls(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    draw_title(a);
    dim(a, 0.65f);
    a->nhot = 0;
    panel(a, &a->white, mid - 220.0f, 18.0f, 440.0f, 284.0f, 12.0f, WHITE);
    panel(a, &a->white, mid - 218.0f, 20.0f, 436.0f, 280.0f, 11.0f, C(39, 35, 52, 1));
    label(a, &a->gold, "Keybinds", mid, 277.0f, 0.85f, 1, WHITE, 0);
    panel(a, &a->white, mid - 176.0f, 62.0f, 352.0f, 192.0f, 8.0f, C(28, 26, 36, 1));

    static const char *const heads[4] = {"Key 1", "Key 2", "Pad 1", "Pad 2"};
    for (int c = 0; c < 4; c++)
        label(a, &a->big, heads[c], mid - 48.0f + c * 58.0f, 239.0f, 0.4f, 1, C(255, 255, 255, 0.7f), 0);
    for (int r = 0; r < ACT_COUNT; r++) {
        const float y = 219.0f - r * 23.0f;
        label(a, &a->big, g_action_names[r], mid - 164.0f, y, 0.45f, 0, WHITE, 76.0f);
        for (int c = 0; c < 4; c++) {
            const int id = r * 4 + c, chosen = focused(a, H_BIND, id);
            const float x = mid - 48.0f + c * 58.0f;
            panel(a, &a->white, x - 27.0f, y - 9.5f, 54.0f, 19.0f, 5.0f,
                  chosen ? C(150, 125, 175, 1) : C(60, 54, 80, 1));
            char s[40];
            if (chosen && a->capturing) snprintf(s, sizeof s, "...");
            else binding_label(&a->cfg, r, c, s, sizeof s);
            label(a, &a->big, s, x, y, 0.4f, 1, WHITE, 48.0f);
            hot_add(a, x, y, 54.0f, 20.0f, H_BIND, id);
        }
    }
    text_button(a, &a->grey_btn, &a->gold, "Reset All", mid - 176.0f + 34.0f, 41.0f, 68.0f, 22.0f, 0.55f, H_RESET, 0,
                WHITE);
    label(a, &a->big, a->capturing ? "Press a key or button (Esc: cancel)" : "A: change   X: clear   Y: reset all",
          mid + 176.0f, 41.0f, 0.34f, 2, C(255, 255, 255, 0.85f), 250.0f);
    spr_button(a, "GJ_closeBtn_001.png", mid - 213.0f, 296.0f, 0.75f, H_BACK, 0, 0);
}

// ------------------------------------------------------------ in the level

void ui_draw_pause(App *a)
{
    const Game *gm = &a->game;
    const float W = pt_width(a), mid = W * 0.5f;
    a->nhot = 0;
    panel(a, &a->dark, 10.0f, 10.0f, W - 20.0f, 300.0f, 0, C(0, 0, 0, 0.75f));
    label(a, &a->big, gm->name, mid, 288.0f, 0.8f, 1, WHITE, W - 120.0f);
    // the best of both modes; the running one counts at once
    const LevelEntry *e = &a->levels[a->cur];
    const float normal = a->practice ? e->best : fmaxf(e->best, gm->best_percent);
    const float practice = a->practice ? fmaxf(e->practice, gm->best_percent) : e->practice;
    label(a, &a->big, "Normal Mode", mid, 254.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 236.0f, 340.0f, normal, C(0, 255, 0, 1));
    label(a, &a->big, "Practice Mode", mid, 208.0f, 0.5f, 1, WHITE, 0);
    bar(a, mid, 190.0f, 340.0f, practice, C(0, 255, 255, 1));
    for (int k = 0; k < gm->ncoins; k++) {   // picked up now, owned from before, or missing
        const char *state = (game_coins(gm) >> k) & 1 ? "on" : (gm->coins_owned >> k) & 1 ? "collected" : "off";
        char name[48];
        snprintf(name, sizeof name, "gjCoinUI_%02d_%s_001.png", gm->obj[gm->coin_obj[k]].id == 1329 ? 2 : 1, state);
        spr(a, name, mid + (k - (gm->ncoins - 1) * 0.5f) * 20.0f, 172.5f, 0.56f, WHITE, 0);
    }
    spr_button(a, a->practice ? "GJ_normalBtn_001.png" : "GJ_practiceBtn_001.png", mid - 123.0f, 128.0f, 0.92f,
               H_PRACTICE, 0, 0);
    spr_button(a, "GJ_playBtn2_001.png", mid - 38.0f, 128.0f, 1.0f, H_RESUME, 0, 0);
    spr_button(a, "GJ_menuBtn_001.png", mid + 47.0f, 128.0f, 0.92f, H_MENU, 0, 0);
    spr_button(a, "GJ_replayBtn_001.png", mid + 123.0f, 128.0f, 0.92f, H_RESTART, 0, 0);
    spr(a, "gj_songIcon_001.png", mid - 191.0f, 70.0f, 0.8f, WHITE, 0);
    volume_slider(a, 0, mid - 173.0f, mid - 27.0f, 70.0f);
    spr(a, "GJ_sfxIcon_001.png", mid + 10.0f, 70.0f, 0.8f, WHITE, 0);
    volume_slider(a, 1, mid + 27.0f, mid + 173.0f, 70.0f);
}

void ui_draw_complete(App *a)
{
    const Game *gm = &a->game;
    const float W = pt_width(a), mid = W * 0.5f;
    a->nhot = 0;
    dim(a, 100.0f / 255.0f);
    panel(a, &a->dark, mid - 190.0f, 45.0f, 380.0f, 235.0f, 0, C(0, 0, 0, 180.0f / 255.0f));
    if (a->practice) label(a, &a->gold, "Practice Complete", mid, 250.0f, 1.1f, 1, WHITE, 300.0f);
    else spr(a, "GJ_levelComplete_001.png", mid, 250.0f, 0.8f, WHITE, 0);
    char s[64];
    const int coins = gm->ncoins > 0;   // with coins the lines move together to make room
    snprintf(s, sizeof s, "Attempts: %d", gm->attempts);
    label(a, &a->gold, s, mid, coins ? 211.0f : 200.0f, coins ? 0.7f : 0.8f, 1, WHITE, 0);
    snprintf(s, sizeof s, "Jumps: %d", gm->jumps);
    label(a, &a->gold, s, mid, coins ? 191.0f : 175.0f, coins ? 0.7f : 0.8f, 1, WHITE, 0);
    const int sec = (int)gm->time;
    snprintf(s, sizeof s, "Time: %02d:%02d", sec / 60, sec % 60);
    label(a, &a->gold, s, mid, coins ? 171.0f : 150.0f, coins ? 0.7f : 0.8f, 1, WHITE, 0);
    // practice mode keeps no coins, so there only the owned ones are filled
    const int have = gm->coins_owned | (a->practice ? 0 : game_coins(gm));
    for (int k = 0; k < gm->ncoins; k++) {
        const int user = gm->obj[gm->coin_obj[k]].id == 1329;
        const char *name = (have >> k) & 1 ? user ? "secretCoinUI2_001.png" : "secretCoinUI_001.png"
                           : user ? "secretCoin_2_b_01_001.png" : "secretCoin_b_01_001.png";
        spr(a, name, mid + (k - (gm->ncoins - 1) * 0.5f) * 50.0f, 135.0f, 0.85f, WHITE, 0);
    }
    label(a, &a->big, "Awesome!", mid, coins ? 99.0f : 112.0f, coins ? 0.7f : 0.8f, 1, WHITE, 0);
    spr_button(a, "GJ_replayBtn_001.png", mid - 95.0f, 52.0f, 1.0f, H_RESTART, 0, 0);
    spr_button(a, "GJ_menuBtn_001.png", mid + 95.0f, 52.0f, 1.0f, H_MENU, 0, 0);
}

// Set and remove checkpoints (also Z / X on the keyboard, ZR / ZL on the controller).
void ui_draw_checkpoint_buttons(App *a)
{
    const float mid = pt_width(a) * 0.5f;
    a->nhot = 0;
    spr_button(a, "GJ_checkpointBtn_001.png", mid - 36.0f, 32.0f, 0.9f, H_CP_ADD, 0, 0);
    spr_button(a, "GJ_removeCheckBtn_001.png", mid + 36.0f, 32.0f, 0.9f, H_CP_DEL, 0, 0);
}

void ui_draw(App *a)
{
    switch (a->scene) {
    case SC_TITLE: draw_title(a); break;
    case SC_SELECT: draw_select(a); break;
    case SC_GARAGE:
        draw_garage(a);
        if (a->colors) draw_colors(a);
        break;
    case SC_CONTROLS: draw_controls(a); break;
    case SC_ONLINE: draw_online(a); break;
    case SC_LEVEL: draw_level(a); break;
    case SC_SETTINGS: draw_settings(a); break;
    case SC_SONGS: draw_songs(a); break;
    case SC_GRAPHICS: draw_graphics(a); break;
    case SC_PLAY: break;
    }
}

void ui_load(App *a)
{
    gfx_font_load(&a->g, "bigFont", &a->big);
    gfx_font_load(&a->g, "goldFont", &a->gold);
    const struct { GfxImage *im; const char *file; } images[] = {
        {&a->gradient, "GJ_gradientBG-hd.png"},   {&a->dark, "square02_001-hd.png"},
        {&a->bar, "GJ_progressBar_001-hd.png"},   {&a->title_bg, "game_bg_01_001-hd.png"},
        {&a->dot, "smallDot-hd.png"},             {&a->ground_menu, "groundSquare_01_001-hd.png"},
        {&a->groove, "slidergroove2-hd.png"},     {&a->stripes, "sliderBar2-hd.png"},
        {&a->grey, "GJ_square05-hd.png"},         {&a->green_btn, "GJ_button_01-hd.png"},
        {&a->grey_btn, "GJ_button_04-hd.png"},    {&a->white, "square02b_001-hd.png"},
        {&a->brown, "GJ_square01-hd.png"},        {&a->slider, "slidergroove-hd.png"},
        {&a->thumb, "sliderthumb-hd.png"}};
    for (size_t i = 0; i < sizeof images / sizeof images[0]; i++)
        if (gfx_load_image(&a->g, images[i].file, images[i].im))
            fprintf(stderr, "missing %s (run prepare-sd.ps1 again?)\n", images[i].file);
}

// ------------------------------------------------------------ actions

static void save_config(App *a) { config_save(&a->cfg, a->cfg_path); }

// the button that is chosen first on a screen
static void default_focus(App *a)
{
    a->focus_arg = 0;
    switch (a->scene) {
    case SC_TITLE: a->focus_id = H_PLAY; break;
    case SC_GARAGE:
        if (a->colors) { a->focus_id = H_SWATCH; a->focus_arg = a->which ? a->cfg.col2 : a->cfg.col1; }
        else { a->focus_id = H_ICON; a->focus_arg = a->cfg.icon[a->tab]; }
        break;
    case SC_CONTROLS: a->focus_id = H_BIND; break;
    case SC_SETTINGS: a->focus_id = H_KEYS; break;
    case SC_GRAPHICS: a->focus_id = H_OPTION; a->focus_arg = OPT_RESOLUTION; break;
    case SC_LEVEL: a->focus_id = H_START; break;
    case SC_PLAY: a->focus_id = a->game.finished ? H_RESTART : H_RESUME; break;
    default: a->focus_id = H_NONE; break;
    }
}

void ui_go(App *a, Scene s)
{
    a->scene = s;
    a->capturing = 0;
    a->colors = 0;
    default_focus(a);
}

void ui_open_garage(App *a, int tab, int colors)
{
    ui_go(a, SC_GARAGE);
    show_tab(a, tab);
    a->colors = colors;
    default_focus(a);
}

static void pause_game(App *a, int on)
{
    a->paused = on;
    if (on) default_focus(a);
}

static void back(App *a)
{
    switch (a->scene) {
    case SC_TITLE: a->quit = 1; break;
    case SC_SELECT:
    case SC_ONLINE: ui_go(a, SC_TITLE); break;
    case SC_LEVEL: ui_go(a, SC_ONLINE); break;
    case SC_SETTINGS:
        save_config(a);
        ui_go(a, SC_TITLE);
        break;
    case SC_SONGS:
        a->preview[0] = 0;
        ui_go(a, SC_LEVEL);
        break;
    case SC_GRAPHICS:
        save_config(a);
        ui_go(a, SC_SETTINGS);
        a->focus_id = H_GRAPHICS;
        break;
    case SC_GARAGE:
        save_config(a);
        if (a->colors) { a->colors = 0; default_focus(a); }
        else ui_go(a, SC_TITLE);
        break;
    case SC_CONTROLS:
        save_config(a);
        ui_go(a, SC_SETTINGS);
        break;
    case SC_PLAY:
        if (a->game.finished || a->paused) leave_level(a);
        else pause_game(a, 1);
        break;
    }
}

static void open_level(App *a, int i)
{
    if (i < 0 || i >= n_custom(a)) return;
    a->online_sel = i;
    list_follow(a);
    ui_go(a, SC_LEVEL);
}

static void play(App *a, int idx)
{
    if (idx < 0 || idx >= a->nlevels) return;
    if (start_level(a, idx) != 0) { fprintf(stderr, "level load failed\n"); return; }
    a->intro = LEVEL_INTRO;
    play_effect(a, SFX_PLAY);
}

void ui_open_songs(App *a)
{
    // made on first use, so that it is there to put files into
#ifdef _WIN32
    _mkdir(a->music_dir);
#else
    mkdir(a->music_dir, 0777);
#endif
    a->nsongs = songs_scan(a->music_dir, a->songs, SONGS_MAX_FILES);
    ui_go(a, SC_SONGS);
    a->preview[0] = 0;
    a->song_sel = 0;
    for (int i = 0; i < song_rows(a); i++) {
        char choice[SONG_NAME + 12];
        song_choice_of_row(a, i, choice, sizeof choice);
        if (!strcmp(choice, current_choice(a))) a->song_sel = i;
    }
    a->song_scroll = 0;
    song_follow(a);
}

// Chooses a row of the song list and plays it.
static void choose_song(App *a, int row)
{
    if (row < 0 || row >= song_rows(a) || a->online_sel >= n_custom(a)) return;
    char key[SONG_NAME + 8], choice[SONG_NAME + 12];
    level_song_key(&a->levels[a->nofficial + a->online_sel], key, sizeof key);
    song_choice_of_row(a, row, choice, sizeof choice);
    songs_choose(key, choice);
    a->song_sel = row;
    if (!level_song(a, a->nofficial + a->online_sel, a->preview, sizeof a->preview)) a->preview[0] = 0;
}

static void set_volume(App *a, int which, int v)
{
    *(which ? &a->cfg.sfx : &a->cfg.music) = v < 0 ? 0 : v > 100 ? 100 : v;
}

// volume from a horizontal position on the screen (UI px)
static void slide_to(App *a, int which, float x)
{
    const float at = x / pt_scale(a), w = a->slider_x1[which] - a->slider_x0[which];
    if (w > 0) set_volume(a, which, (int)((at - a->slider_x0[which]) / w * 100.0f + 0.5f));
}

static void step_level(App *a, int dir)
{
    if (a->nofficial) a->sel = (a->sel + dir + a->nofficial) % a->nofficial;   // the list wraps around
}

static void activate(App *a, int id, int arg)
{
    switch (id) {
    case H_PLAY: ui_go(a, SC_SELECT); break;
    case H_GARAGE: ui_open_garage(a, IC_CUBE, 0); break;
    case H_ONLINE: ui_go(a, SC_ONLINE); list_follow(a); break;
    case H_SETTINGS: ui_go(a, SC_SETTINGS); break;
    case H_KEYS: ui_go(a, SC_CONTROLS); break;
    case H_GRAPHICS: ui_go(a, SC_GRAPHICS); break;
    case H_OPTION: option_step(a, arg, 1); break;
    case H_SONG: ui_open_songs(a); break;
    case H_SONGROW: choose_song(a, arg); break;
    case H_QUIT: a->quit = 1; break;
    case H_BACK: back(a); break;
    case H_PREV: step_level(a, -1); break;
    case H_NEXT: step_level(a, 1); break;
    case H_PAGEDOT: a->sel = arg; break;
    case H_CARD: play(a, a->sel); break;
    case H_RESUME: a->paused = 0; break;
    case H_PRACTICE: practice_toggle(a); break;
    case H_CP_ADD: practice_place(a); break;
    case H_CP_DEL: practice_remove(a); break;
    case H_RESTART: restart(a); break;
    case H_MENU: leave_level(a); break;
    case H_TAB: show_tab(a, arg); break;
    case H_ICON: a->cfg.icon[a->tab] = arg; save_config(a); break;
    case H_TURN: turn_page(a, arg); break;
    case H_DOT: turn_page(a, arg - a->page); break;
    case H_COLORS: a->colors = 1; default_focus(a); break;
    case H_CLOSE: back(a); break;
    case H_WHICH: a->which = arg; break;
    case H_SWATCH:
        if (a->which) a->cfg.col2 = arg;
        else a->cfg.col1 = arg;
        save_config(a);
        break;
    case H_ROW: open_level(a, arg); break;
    case H_START: play(a, a->nofficial + a->online_sel); break;
    case H_BIND: a->ctl_row = arg / 4; a->ctl_col = arg % 4; a->capturing = 1; break;
    case H_RESET: config_reset_bindings(&a->cfg); save_config(a); break;
    default: break;
    }
}

// Enter / A / jump in a menu.
static void confirm(App *a)
{
    switch (a->scene) {
    case SC_SELECT: play(a, a->sel); break;
    case SC_ONLINE: open_level(a, a->online_sel); break;
    case SC_LEVEL:
        if (a->show_focus) activate(a, a->focus_id, a->focus_arg);
        else activate(a, H_START, 0);
        break;
    case SC_SONGS: choose_song(a, a->song_sel); break;
    case SC_GRAPHICS:   // the first press only shows which row is chosen
    case SC_GARAGE:
        if (!a->show_focus) { a->show_focus = 1; default_focus(a); }
        else activate(a, a->focus_id, a->focus_arg);
        break;
    default:
        if (!a->show_focus) default_focus(a);   // without a chosen button: the main one
        activate(a, a->focus_id, a->focus_arg);
        break;
    }
}

// Moves to the nearest button in direction (dx, dy); dy = 1 is down.
static void navigate(App *a, int dx, int dy)
{
    if (a->scene == SC_SELECT) { if (dx) step_level(a, dx); return; }
    if (a->scene == SC_SONGS) {
        if (a->show_focus) a->song_sel += dy;
        a->show_focus = 1;
        if (a->song_sel < 0) a->song_sel = 0;
        if (a->song_sel >= song_rows(a)) a->song_sel = song_rows(a) - 1;
        song_follow(a);
        return;
    }
    // left / right move the volume while its slider is chosen
    if (dx && a->show_focus && a->focus_id == H_VOLUME) {
        set_volume(a, a->focus_arg, (a->focus_arg ? a->cfg.sfx : a->cfg.music) + 10 * dx);
        save_config(a);
        return;
    }
    // ... and change the value of a row in the graphics menu
    if (dx && a->show_focus && a->focus_id == H_OPTION) {
        option_step(a, a->focus_arg, dx);
        return;
    }
    // in the pause menu the two sliders stand side by side: down / up go from one to the other
    if (dy && a->scene == SC_PLAY && a->show_focus && a->focus_id == H_VOLUME && a->focus_arg == (dy < 0)) {
        a->focus_arg = dy > 0;
        return;
    }
    if (a->scene == SC_ONLINE) {
        if (!n_custom(a)) return;
        if (a->show_focus) a->online_sel += dy;
        a->show_focus = 1;
        if (a->online_sel < 0) a->online_sel = 0;
        if (a->online_sel >= n_custom(a)) a->online_sel = n_custom(a) - 1;
        list_follow(a);
        return;
    }
    const Hot *from = NULL;
    for (int i = 0; i < a->nhot; i++)
        if (a->hot[i].id == a->focus_id && a->hot[i].arg == a->focus_arg) from = &a->hot[i];
    if (!a->show_focus || !from) {
        a->show_focus = 1;
        if (!from) default_focus(a);
        return;
    }
    const Hot *best = NULL;
    float far = 0;
    for (int i = 0; i < a->nhot; i++) {
        const Hot *h = &a->hot[i];
        if (h == from) continue;
        const float vx = h->r.x - from->r.x, vy = h->r.y - from->r.y;
        const float along = vx * dx + vy * dy, across = fabsf(vx * dy - vy * dx);
        if (along < 4.0f || across > along * 4.0f + 80.0f) continue;
        const float score = along + 2.5f * across;
        if (!best || score < far) { best = h; far = score; }
    }
    if (best) { a->focus_id = best->id; a->focus_arg = best->arg; }
}

// the clickable area at a screen position, the one drawn last first
static const Hot *hot_at(const App *a, float x, float y)
{
    for (int i = a->nhot - 1; i >= 0; i--) {
        const Rect r = a->hot[i].r;
        if (x >= r.x - r.w / 2 && x <= r.x + r.w / 2 && y >= r.y - r.h / 2 && y <= r.y + r.h / 2) return &a->hot[i];
    }
    return NULL;
}

static void click(App *a, float x, float y, int right)
{
    const Hot *hit = hot_at(a, x, y);
    if (a->capturing) { a->capturing = 0; return; }
    if (!hit) return;
    const int id = hit->id, arg = hit->arg;
    a->show_focus = 0;
    a->focus_id = id;
    a->focus_arg = arg;
    if (id == H_SWATCH && right) {   // right click: the second colour
        a->cfg.col2 = arg;
        save_config(a);
        return;
    }
    if (id == H_OPTION && right) {   // right click: the previous value
        option_step(a, arg, -1);
        return;
    }
    if (id == H_VOLUME) {   // jumps to the click and follows the mouse
        slide_to(a, arg, x);
        a->dragging = arg + 1;
        return;
    }
    if (id == H_BIND) a->show_focus = 1;   // shows which binding waits for the key
    activate(a, id, arg);
}

// ------------------------------------------------------------ input

static int playing(const App *a) { return a->scene == SC_PLAY && !a->paused && !a->game.finished; }

// The checkpoint button of practice mode at a screen position, or H_NONE.
static int checkpoint_button(const App *a, float x, float y)
{
    for (int i = 0; i < a->nhot; i++) {
        const Rect r = a->hot[i].r;
        if ((a->hot[i].id == H_CP_ADD || a->hot[i].id == H_CP_DEL) && x >= r.x - r.w / 2 && x <= r.x + r.w / 2 &&
            y >= r.y - r.h / 2 && y <= r.y + r.h / 2)
            return a->hot[i].id;
    }
    return H_NONE;
}

// ------------------------------------------------------------ touch
//
// In the level a finger acts the moment it touches. In the menus a button reacts when the finger is
// lifted again without having moved, so that lists can be dragged and pages swiped.

#define TOUCH_SLOP 24.0f     // px a finger may wander and still tap
#define TOUCH_SWIPE 100.0f   // px sideways that turn a page

enum { FINGER_JUMP, FINGER_TAP, FINGER_DRAG, FINGER_SLIDER, FINGER_USED };

static int same_screen(const App *a, const Touch *t)
{
    return t->scene == a->scene && t->paused == a->paused && t->colors == a->colors;
}

static void touch_down(App *a, Touch *t)
{
    t->mode = FINGER_USED;
    if (playing(a)) {
        if (t->x > a->lw - 100 && t->y < 100) { pause_game(a, 1); return; }   // the pause button
        const int button = a->practice ? checkpoint_button(a, t->x, t->y) : H_NONE;
        if (button) { activate(a, button, 0); return; }
        t->mode = FINGER_JUMP;
        if (a->game.input) game_set_hold(&a->game, 0);   // a second finger clicks again
        game_set_hold(&a->game, 1);
        return;
    }
    const Hot *hit = hot_at(a, t->x, t->y);
    if (hit && hit->id == H_VOLUME && !a->capturing) {
        t->mode = FINGER_SLIDER;
        t->slider = hit->arg;
        click(a, t->x, t->y, 0);
        return;
    }
    t->mode = FINGER_TAP;
}

static void touch_move(App *a, Touch *t, float x, float y)
{
    float dy = y - t->y;
    t->x = x;
    t->y = y;
    if (t->mode == FINGER_SLIDER) {
        if (same_screen(a, t)) slide_to(a, t->slider, x);
        return;
    }
    if (t->mode == FINGER_TAP && (fabsf(x - t->x0) > TOUCH_SLOP || fabsf(y - t->y0) > TOUCH_SLOP)) {
        t->mode = FINGER_DRAG;
        dy = y - t->y0;
    }
    if (t->mode != FINGER_DRAG || !same_screen(a, t)) return;
    // the lists follow the finger
    if (a->scene == SC_ONLINE) {
        a->show_focus = 0;
        a->scroll = fminf(fmaxf(a->scroll - dy / pt_scale(a), 0.0f), list_limit(a));
    } else if (a->scene == SC_SONGS) {
        a->show_focus = 0;
        a->song_scroll = fminf(fmaxf(a->song_scroll - dy / pt_scale(a), 0.0f), song_limit(a));
    }
}

static void touch_up(App *a, Touch *t)
{
    t->active = 0;
    if (t->mode == FINGER_JUMP) {   // the jump button is let go with the last finger
        for (int i = 0; i < MAX_TOUCH; i++)
            if (a->touch[i].active && a->touch[i].mode == FINGER_JUMP) return;
        if (a->scene == SC_PLAY) game_set_hold(&a->game, 0);
    } else if (t->mode == FINGER_SLIDER) {
        a->dragging = 0;
        save_config(a);
    } else if (t->mode == FINGER_TAP && same_screen(a, t)) {
        click(a, t->x0, t->y0, 0);
    } else if (t->mode == FINGER_DRAG && same_screen(a, t)) {
        const float dx = t->x - t->x0;
        if (fabsf(dx) < TOUCH_SWIPE || fabsf(dx) < fabsf(t->y - t->y0)) return;
        if (a->scene == SC_SELECT) step_level(a, dx < 0 ? 1 : -1);
        else if (a->scene == SC_GARAGE && !a->colors) turn_page(a, dx < 0 ? 1 : -1);
    }
}

void ui_touch(App *a, int phase, int id, float x, float y)
{
    Touch *t = NULL, *spare = NULL;
    for (int i = 0; i < MAX_TOUCH; i++) {
        if (a->touch[i].active && a->touch[i].id == id) t = &a->touch[i];
        else if (!a->touch[i].active && !spare) spare = &a->touch[i];
    }
    if (phase == TOUCH_DOWN) {
        if (!t) t = spare;
        if (!t) return;
        *t = (Touch){1, id, FINGER_USED, 0, a->scene, a->paused, a->colors, x, y, x, y};
        touch_down(a, t);
    } else if (t && phase == TOUCH_MOVE) {
        touch_move(a, t, x, y);
    } else if (t) {
        touch_up(a, t);
    }
}

static void clear_binding(App *a)
{
    if (a->focus_id != H_BIND) return;
    Binding *b = &a->cfg.bind[a->focus_arg / 4];
    const int col = a->focus_arg % 4;
    if (col < 2) b->key[col] = 0;
    else b->joy[col - 2] = -1;
    save_config(a);
}

// Stores a captured key / pad button into the selected binding.
static void bind_capture(App *a, int is_joy, int code)
{
    Binding *b = &a->cfg.bind[a->ctl_row];
    const int col = a->ctl_col;
    if (is_joy) { if (col >= 2) b->joy[col - 2] = code; else b->joy[0] = code; }
    else { if (col < 2) b->key[col] = code; else b->key[0] = code; }
    a->capturing = 0;
    save_config(a);
}

static void action_down(App *a, int act)
{
    switch (act) {
    case ACT_JUMP:
        if (playing(a)) game_set_hold(&a->game, 1);
        else confirm(a);
        break;
    case ACT_PAUSE:
        if (a->scene == SC_PLAY && !a->game.finished) pause_game(a, !a->paused);
        break;
    case ACT_RESTART:
        if (a->scene == SC_PLAY) restart(a);
        else if (a->scene == SC_CONTROLS) activate(a, H_RESET, 0);
        break;
    case ACT_BACK: back(a); break;
    case ACT_CONFIRM:
        if (!playing(a)) confirm(a);
        break;
    case ACT_LEFT:
        if (!playing(a)) navigate(a, -1, 0);
        break;
    case ACT_RIGHT:
        if (!playing(a)) navigate(a, 1, 0);
        break;
    default: break;
    }
}

static void action_up(App *a, int act)
{
    if (act == ACT_JUMP && a->scene == SC_PLAY) game_set_hold(&a->game, 0);
}

// a mouse position in drawing coordinates, whatever the size and shape of the window
static void to_ui(App *a, int px, int py, float *ux, float *uy) { gfx_window_point(&a->g, px, py, ux, uy); }

void ui_event(App *a, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT: a->quit = 1; break;
    case SDL_KEYDOWN: {
        if (e->key.repeat) break;
        const int sym = e->key.keysym.sym;
        if (a->capturing) {
            if (sym == SDLK_ESCAPE) a->capturing = 0;
            else bind_capture(a, 0, sym);
            break;
        }
        // non-rebindable shortcuts of the menus
        if (!playing(a)) {
            if (a->scene == SC_TITLE && sym == SDLK_g) { activate(a, H_GARAGE, 0); break; }
            if (a->scene == SC_TITLE && sym == SDLK_o) { activate(a, H_SETTINGS, 0); break; }
            if (a->scene == SC_LEVEL && sym == SDLK_m) { activate(a, H_SONG, 0); break; }
            if (a->scene == SC_CONTROLS && sym == SDLK_DELETE) { clear_binding(a); break; }
            if (a->scene == SC_GARAGE && !a->colors && (sym == SDLK_PAGEUP || sym == SDLK_PAGEDOWN)) {
                turn_page(a, sym == SDLK_PAGEUP ? -1 : 1);
                break;
            }
            // the cursor keys always move around the menus, even if they are bound to actions
            if (sym == SDLK_UP) { navigate(a, 0, -1); break; }
            if (sym == SDLK_DOWN) { navigate(a, 0, 1); break; }
            if (sym == SDLK_LEFT) { navigate(a, -1, 0); break; }
            if (sym == SDLK_RIGHT) { navigate(a, 1, 0); break; }
        }
        if (playing(a) && a->practice && (sym == SDLK_z || sym == SDLK_x)) {
            activate(a, sym == SDLK_z ? H_CP_ADD : H_CP_DEL, 0);
            break;
        }
        int act = config_action_for_key(&a->cfg, sym);
        if (act < 0 && sym == SDLK_ESCAPE) act = ACT_BACK;
        if (act >= 0) action_down(a, act);
        break;
    }
    case SDL_KEYUP: {
        const int act = config_action_for_key(&a->cfg, e->key.keysym.sym);
        if (act >= 0) action_up(a, act);
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        float x, y;
        to_ui(a, e->button.x, e->button.y, &x, &y);
        if (playing(a)) {
            const int button = a->practice ? checkpoint_button(a, x, y) : H_NONE;
            if (button) activate(a, button, 0);
            else game_set_hold(&a->game, 1);
        } else {
            click(a, x, y, e->button.button == SDL_BUTTON_RIGHT);
        }
        break;
    }
    case SDL_MOUSEBUTTONUP:
        if (a->dragging) { a->dragging = 0; save_config(a); }
        if (a->scene == SC_PLAY) game_set_hold(&a->game, 0);
        break;
    case SDL_MOUSEMOTION:
        if (a->dragging) {
            float x, y;
            to_ui(a, e->motion.x, e->motion.y, &x, &y);
            slide_to(a, a->dragging - 1, x);
        }
        break;
    case SDL_MOUSEWHEEL:
        if (a->scene == SC_ONLINE) {
            a->show_focus = 0;
            a->scroll = fminf(fmaxf(a->scroll - e->wheel.y * 30.0f, 0.0f), list_limit(a));
        } else if (a->scene == SC_SONGS) {
            a->show_focus = 0;
            a->song_scroll = fminf(fmaxf(a->song_scroll - e->wheel.y * 30.0f, 0.0f), song_limit(a));
        } else if (a->scene == SC_GARAGE && !a->colors && e->wheel.y) {
            turn_page(a, e->wheel.y > 0 ? -1 : 1);
        } else if (a->scene == SC_SELECT && e->wheel.y) {
            step_level(a, e->wheel.y > 0 ? -1 : 1);
        }
        break;
    case SDL_JOYBUTTONDOWN: {
        const int b = e->jbutton.button;
        if (a->capturing) { bind_capture(a, 1, b); break; }
        if (!playing(a)) {
            if (a->scene == SC_TITLE && b == 2) { activate(a, H_GARAGE, 0); break; }
            if (a->scene == SC_TITLE && b == 3) { activate(a, H_SETTINGS, 0); break; }
            if (a->scene == SC_LEVEL && b == 3) { activate(a, H_SONG, 0); break; }
            if (a->scene == SC_CONTROLS && b == 3) { activate(a, H_RESET, 0); break; }
            if (a->scene == SC_CONTROLS && b == 2) { clear_binding(a); break; }
            if (a->scene == SC_GARAGE && b >= 6 && b <= 9) {   // L / R: page, ZL / ZR: form
                if (a->colors) a->which = b & 1;
                else if (b < 8) turn_page(a, b == 6 ? -1 : 1);
                else { show_tab(a, a->tab + (b == 8 ? -1 : 1)); a->focus_id = H_ICON; a->focus_arg = a->cfg.icon[a->tab]; }
                break;
            }
            // the direction pad always moves around the menus
            if (b == 12 || b == 14) { navigate(a, b == 12 ? -1 : 1, 0); break; }
            if (b == 13 || b == 15) { navigate(a, 0, b == 13 ? -1 : 1); break; }
        }
        if (playing(a) && a->practice && (b == 8 || b == 9)) {   // ZR sets a checkpoint, ZL removes it
            activate(a, b == 9 ? H_CP_ADD : H_CP_DEL, 0);
            break;
        }
        const int act = config_action_for_joy(&a->cfg, b);
        if (act >= 0) action_down(a, act);
        break;
    }
    case SDL_JOYBUTTONUP: {
        const int act = config_action_for_joy(&a->cfg, e->jbutton.button);
        if (act >= 0) action_up(a, act);
        break;
    }
    default: break;
    }
}
