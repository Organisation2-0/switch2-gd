// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry Dash fan port: application, menus and main loop (SDL2).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../vendor/stb_image_write.h"
#ifdef GD_ALIGN
#include <dirent.h>
#include "../vendor/stb_image.h"
#endif
#include "bot.h"
#include "config.h"
#include "game.h"
#include "gfx.h"

#define UI_W 1280.0f
#define UI_H 720.0f
#define VIEW_H 320.0f

typedef enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_GARAGE, SC_CONTROLS } Scene;

typedef struct {
    char path[600];
    char name[96];
    int diff;    // difficulty face index (0 = N/A)
    int stars;
    int official;
    float best;
} LevelEntry;

static const struct { const char *name; int diff, stars; } k_official[] = {
    {"Stereo Madness", 1, 1},   {"Back On Track", 1, 2},       {"Polargeist", 2, 3},
    {"Dry Out", 2, 4},          {"Base After Base", 3, 5},     {"Can't Let Go", 3, 6},
    {"Jumper", 4, 7},           {"Time Machine", 4, 8},        {"Cycles", 4, 9},
    {"xStep", 5, 10},           {"Clutterfunk", 5, 11},        {"Theory of Everything", 5, 12},
    {"Electroman Adventures", 5, 10}, {"Clubstep", 6, 14},     {"Electrodynamix", 5, 12},
    {"Hexagon Force", 5, 12},   {"Blast Processing", 4, 10},   {"Theory of Everything 2", 6, 14},
    {"Geometrical Dominator", 4, 10}, {"Deadlocked", 6, 15},   {"Fingerdash", 5, 12},
    {"Dash", 5, 12},
};

static const char *const k_pad_names[16] = {"A", "B", "X", "Y", "L3", "R3", "L", "R",
                                            "ZL", "ZR", "+", "-", "Left", "Up", "Right", "Down"};

typedef struct { float x, y, w, h; } Rect;     // centre and size
typedef struct { Rect r; int id; } Hot;        // clickable area of the screen shown last
enum { H_NONE, H_PLAY, H_GARAGE, H_SETTINGS, H_QUIT, H_BACK, H_PREV, H_NEXT, H_CARD, H_RESUME, H_RESTART, H_MENU };

typedef struct {
    Gfx g;
    GfxFont big, gold;
    GfxImage gradient, dark, bar, title_bg, bg, ground, ground2, ground_menu, groove, stripes, dot, slider, thumb;
    float lw, lh;     // drawing size (1280x720; the level tour renders 16:10)
    Hot hot[24];
    int nhot;
    Scene shown;      // screen of the last picture, and when it appeared
    float shown_since;
    LevelEntry *levels;
    int nlevels, sel;
    Scene scene;
    Game game;
    int game_loaded;
    int paused;
    float t;          // seconds since start (menu animation)
    float press;      // button press animation
    int quit;
    char resdir[512];
    char custom_dir[512];
    char cfg_path[512];

    Config cfg;
    int *cube_icons, n_cube;   // available player_NN indices
    int *ship_icons, n_ship;
    int garage_row;            // 0 cube, 1 ship, 2 colour 1, 3 colour 2
    int ctl_row, ctl_col;      // controls: row = action, col 0..3 (key1,key2,pad1,pad2)
    int capturing;             // waiting for a key / pad button to bind
    double dbg_sum_dt, dbg_sum_ds;  // development: sum of real vs smoothed frame times
    int dbg_frames;
} App;

static GdColorF C(float r, float g, float b, float a) { return (GdColorF){r, g, b, a}; }
static const GdColorF WHITE = {255, 255, 255, 1};

// ------------------------------------------------------------ helpers

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static void scan_levels(App *a)
{
    a->levels = calloc(256, sizeof(LevelEntry));
    for (int i = 0; i < 22; i++) {
        LevelEntry *e = &a->levels[a->nlevels];
        snprintf(e->path, sizeof e->path, "%s/levels/%d.txt", a->resdir, i + 1);
        if (!file_exists(e->path)) continue;
        snprintf(e->name, sizeof e->name, "%s", k_official[i].name);
        e->diff = k_official[i].diff;
        e->stars = k_official[i].stars;
        e->official = 1;
        a->nlevels++;
    }
#ifdef _WIN32
    char pat[640];
    snprintf(pat, sizeof pat, "%s\\*.gdlvl", a->custom_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            LevelEntry *e = &a->levels[a->nlevels];
            snprintf(e->path, sizeof e->path, "%s/%s", a->custom_dir, fd.cFileName);
            snprintf(e->name, sizeof e->name, "%.90s", fd.cFileName);
            char *dot = strrchr(e->name, '.');
            if (dot) *dot = 0;
            e->diff = 0;
            a->nlevels++;
        } while (FindNextFileA(h, &fd) && a->nlevels < 255);
        FindClose(h);
    }
#else
    // On the console, custom levels are listed from a fixed file: levels.txt (one path per line).
    char lst[640];
    snprintf(lst, sizeof lst, "%s/levels.txt", a->custom_dir);
    FILE *f = fopen(lst, "rb");
    if (f) {
        char line[600];
        while (fgets(line, sizeof line, f) && a->nlevels < 255) {
            line[strcspn(line, "\r\n")] = 0;
            if (!*line) continue;
            LevelEntry *e = &a->levels[a->nlevels++];
            snprintf(e->path, sizeof e->path, "%s", line);
            const char *s = strrchr(line, '/');
            snprintf(e->name, sizeof e->name, "%.90s", s ? s + 1 : line);
            char *dot = strrchr(e->name, '.');
            if (dot) *dot = 0;
        }
        fclose(f);
    }
#endif
}

// Finds which icon numbers exist in the sprite sheets.
static int *scan_icons(App *a, const char *fmt, int max, int *count)
{
    int *list = malloc(sizeof(int) * (size_t)max);
    int n = 0;
    char name[64];
    for (int i = 1; i <= max; i++) {
        snprintf(name, sizeof name, fmt, i);
        if (gfx_find(&a->g, name)) list[n++] = i;
    }
    *count = n;
    return list;
}

static int list_index(const int *list, int n, int value)
{
    for (int i = 0; i < n; i++) if (list[i] == value) return i;
    return 0;
}

static void apply_skin(App *a)
{
    if (!a->game_loaded) return;
    float c1[3], c2[3];
    palette_color(a->cfg.col1, c1);
    palette_color(a->cfg.col2, c2);
    game_set_skin(&a->game, &a->g, a->cfg.icon_cube, a->cfg.icon_ship, c1, c2);
    game_reset(&a->game);
}

static void load_bg_images(App *a, const Game *gm)
{
    char name[128];
    gfx_free_image(&a->bg);
    gfx_free_image(&a->ground);
    snprintf(name, sizeof name, "game_bg_%02d_001-hd.png", gm->bg_id);
    if (gfx_load_image(&a->g, name, &a->bg)) gfx_load_image(&a->g, "game_bg_01_001-hd.png", &a->bg);
    snprintf(name, sizeof name, "groundSquare_%02d_001-hd.png", gm->ground_id);
    if (gfx_load_image(&a->g, name, &a->ground))
        gfx_load_image(&a->g, "groundSquare_01_001-hd.png", &a->ground);
    gfx_free_image(&a->ground2);
    if (gm->ground_id >= 8) {   // grounds from 8 on come in two layers
        snprintf(name, sizeof name, "groundSquare_%02d_2_001-hd.png", gm->ground_id);
        if (gfx_load_image(&a->g, name, &a->ground2)) memset(&a->ground2, 0, sizeof a->ground2);
    }
}

static int start_level(App *a, int idx)
{
    if (a->game_loaded) game_free(&a->game);
    if (game_load(&a->game, &a->g, a->levels[idx].path)) { a->game_loaded = 0; return -1; }
    a->game_loaded = 1;
    snprintf(a->game.name, sizeof a->game.name, "%s", a->levels[idx].name);
    a->game.best_percent = a->levels[idx].best;
    load_bg_images(a, &a->game);
    a->game.ground2 = &a->ground2;
    apply_skin(a);
    a->scene = SC_PLAY;
    a->paused = 0;
    return 0;
}

// ------------------------------------------------------------ UI primitives

static int inside(Rect r, float x, float y)
{
    return x >= r.x - r.w / 2 && x <= r.x + r.w / 2 && y >= r.y - r.h / 2 && y <= r.y + r.h / 2;
}

// Draws a sprite button centred at (x,y); returns its hit rect (UI px).
static Rect button(App *a, const char *frame, float x, float y, float scale, int hot)
{
    const GdFrame *f = gfx_find(&a->g, frame);
    Rect r = {x, y, 0, 0};
    if (!f) return r;
    float s = scale * (hot ? 1.0f + 0.12f * (a->press > 0 ? 0.0f : 1.0f) - a->press * 0.15f : 1.0f);
    gfx_frame(&a->g, f, x, y, 0, s, s, WHITE, 0);
    r.w = f->srcw * scale;
    r.h = f->srch * scale;
    return r;
}

static void text(App *a, const char *s, float x, float y, float sc, int align, GdColorF c)
{
    // shadow for readability
    gfx_text(&a->g, &a->big, s, x + 2 * sc * 2, y + 2 * sc * 2, sc, align, C(0, 0, 0, 0.35f * c.a));
    gfx_text(&a->g, &a->big, s, x, y, sc, align, c);
}

static void panel(App *a, SDL_FRect r)
{
    const GdColorF c = C(0, 0, 0, 125.0f / 255.0f);
    if (a->dark.tex) gfx_ninepatch(&a->g, &a->dark, a->dark.w / 3.0f, r, 36, c);
    else gfx_fill(&a->g, r, c);
}

// plain background of the garage and the settings
static void draw_menu_background(App *a)
{
    const GdColorF c = C(110, 110, 110, 1);
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 1));
    if (a->gradient.tex)
        gfx_image_rect(&a->g, &a->gradient, (SDL_FRect){0, 0, (float)a->gradient.w, (float)a->gradient.h},
                       (SDL_FRect){-2, -2, a->lw + 4.0f, a->lh + 4.0f}, c);
}

static GdColorF pal(int idx)
{
    float c[3];
    palette_color(idx, c);
    return C(c[0], c[1], c[2], 1);
}

// Draws a player icon (cube or ship) at the given place using the skin settings.
static void draw_icon(App *a, int ship, int icon, float x, float y, float sc, GdColorF c1, GdColorF c2)
{
    char n[64], n2[64];
    snprintf(n, sizeof n, ship ? "ship_%02d_001.png" : "player_%02d_001.png", icon);
    snprintf(n2, sizeof n2, ship ? "ship_%02d_2_001.png" : "player_%02d_2_001.png", icon);
    const GdFrame *f1 = gfx_find(&a->g, n), *f2 = gfx_find(&a->g, n2);
    if (f2) gfx_frame(&a->g, f2, x, y, 0, sc, sc, c2, 0);
    if (f1) gfx_frame(&a->g, f1, x, y, 0, sc, sc, c1, 0);
}

// ------------------------------------------------------------ scenes

// ------------------------------------------------------------ the screens of the original
//
// Laid out in the original's coordinates: points, 320 of them from the bottom to the top of the
// screen, positions measured in recordings of the game.

static float pt_scale(const App *a) { return a->lh / 320.0f; }   // pixels per point
static float pt_width(const App *a) { return a->lw / pt_scale(a); }

static void hot_add(App *a, float x, float y, float w, float h, int id)
{
    const float p = pt_scale(a);
    if (id && a->nhot < (int)(sizeof a->hot / sizeof a->hot[0]))
        a->hot[a->nhot++] = (Hot){{x * p, (320.0f - y) * p, w * p, h * p}, id};
}

// Sprite centred at (x,y); returns its frame (NULL if the sheets do not have it).
static const GdFrame *spr(App *a, const char *name, float x, float y, float scale, GdColorF c, int flip)
{
    const GdFrame *f = gfx_find(&a->g, name);
    if (!f) return NULL;
    const float p = pt_scale(a), s = scale * p / a->g.atlas.px_per_unit;
    gfx_frame(&a->g, f, x * p, (320.0f - y) * p, 0, flip ? -s : s, s, c, 0);
    return f;
}

static void spr_button(App *a, const char *name, float x, float y, float scale, int id)
{
    const GdFrame *f = spr(a, name, x, y, scale, WHITE, 0);
    if (f) hot_add(a, x, y, f->srcw * scale / a->g.atlas.px_per_unit, f->srch * scale / a->g.atlas.px_per_unit, id);
}

// Empty secret coin, d points wide.
static void spr_coin(App *a, float x, float y, float d)
{
    const GdFrame *c = gfx_find(&a->g, "gjCoinUI_01_off_001.png");
    if (c) spr(a, "gjCoinUI_01_off_001.png", x, y, d * a->g.atlas.px_per_unit / c->srcw, WHITE, 0);
}

// Text centred vertically at y; align as in gfx_text; narrowed to max_w points if it is wider.
static void label(App *a, const GfxFont *f, const char *s, float x, float y, float scale, int align, GdColorF c,
                  float max_w)
{
    const float p = pt_scale(a);
    float k = scale * p * 0.5f;   // the fonts are stored at two pixels per point
    const float w = gfx_text_width(f, s, k);
    if (max_w > 0 && w > max_w * p) k *= max_w * p / w;
    gfx_text(&a->g, f, s, x * p, (320.0f - y) * p - f->line_h * k * 0.5f, k, align, c);
}

// Rounded panel (nine-slice) covering x0..x0+w, y0..y0+h.
static void pt_panel(App *a, const GfxImage *im, float x0, float y0, float w, float h, GdColorF c)
{
    const float p = pt_scale(a);
    const SDL_FRect r = {x0 * p, (320.0f - y0 - h) * p, w * p, h * p};
    if (!im->tex) { gfx_fill(&a->g, r, c); return; }
    const float border = im->w / 3.0f;
    gfx_ninepatch(&a->g, im, border, r, fminf(border * 0.5f, h * 0.5f) * p, c);
}

// Progress bar with its percentage, centred at (x,y).
static void pt_bar(App *a, float x, float y, float percent, GdColorF fill)
{
    const float p = pt_scale(a), w = 340.0f, h = 20.0f;
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
    label(a, &a->big, s, x, y - 0.5f, 0.6f, 1, WHITE, 0);
}

// Volume slider as shown in the pause menu (there is no sound, it only shows the original's place).
static void pt_slider(App *a, float x0, float x1, float y, float at)
{
    const float p = pt_scale(a), w = x1 - x0, h = w * a->slider.h / (a->slider.w ? a->slider.w : 1);
    if (!a->slider.tex || !a->thumb.tex) return;
    gfx_fill(&a->g, (SDL_FRect){(x0 + 2.0f) * p, (320.0f - y - h * 0.22f) * p, (w - 4.0f) * at * p, h * 0.44f * p},
             C(60, 200, 255, 1));
    gfx_image_rect(&a->g, &a->slider, (SDL_FRect){0, 0, (float)a->slider.w, (float)a->slider.h},
                   (SDL_FRect){x0 * p, (320.0f - y - h * 0.5f) * p, w * p, h * p}, WHITE);
    const float d = 18.0f;
    gfx_image_rect(&a->g, &a->thumb, (SDL_FRect){0, 0, (float)a->thumb.w, (float)a->thumb.h},
                   (SDL_FRect){(x0 + w * at - d * 0.5f) * p, (320.0f - y - d * 0.5f) * p, d * p, d * p}, WHITE);
}

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
    const int icon = a->cube_icons[(n * 37 + 5) % a->n_cube];
    const GdColorF c1 = pal((n * 11 + 3) % palette_size()), c2 = pal((n * 7 + 12) % palette_size());
    // one jump of the cube lasts 0.43 s and is 65 units high
    const float j = fmodf(phase + (n % 3) * 0.3f, 1.1f) / 0.43f;
    const float up = j < 1.0f ? 260.0f * j * (1.0f - j) : 0.0f, rot = j < 1.0f ? 180.0f * j : 0.0f;
    const float p = pt_scale(a), s = p / a->g.atlas.px_per_unit;
    char n1[64], n2[64];
    snprintf(n1, sizeof n1, "player_%02d_001.png", icon);
    snprintf(n2, sizeof n2, "player_%02d_2_001.png", icon);
    const GdFrame *f1 = gfx_find(&a->g, n1), *f2 = gfx_find(&a->g, n2);
    const float px = x * p, py = (320.0f - 90.0f - 15.0f - up) * p;
    if (f2) gfx_frame(&a->g, f2, px, py, rot, s, s, c2, 0);
    if (f1) gfx_frame(&a->g, f1, px, py, rot, s, s, c1, 0);
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

    spr(a, "GJ_logo_001.png", mid, 270.0f, 1.0f, WHITE, 0);
    spr_button(a, "GJ_closeBtn_001.png", 18.0f, 302.0f, 0.72f, H_QUIT);
    spr_button(a, "GJ_playBtn_001.png", mid, 170.0f, 1.0f, H_PLAY);
    spr_button(a, "GJ_garageBtn_001.png", mid - 110.0f, 170.0f, 1.0f, H_GARAGE);
    spr_button(a, "GJ_creatorBtn_001.png", mid + 110.0f, 170.0f, 1.0f, H_NONE);
    spr_button(a, "GJ_dailyRewardBtn_001.png", W - 40.0f, 178.0f, 1.0f, H_NONE);
    spr_button(a, "GJ_profileButton_001.png", 45.0f, 105.0f, 1.0f, H_GARAGE);
    if (a->cfg.name[0]) label(a, &a->gold, a->cfg.name, 47.0f, 140.0f, 0.5f, 1, WHITE, 86.0f);
    static const struct { const char *name; float x, y, s; } social[] = {
        {"gj_fbIcon_001.png", 22, 55, 0.8f},      {"gj_twIcon_001.png", 51, 55, 0.8f},
        {"gj_ytIcon_001.png", 80, 55, 0.8f},      {"gj_twitchIcon_001.png", 109, 55, 0.8f},
        {"gj_discordIcon_001.png", 109, 26, 0.8f}, {"robtoplogo_small.png", 50, 25, 0.8f}};
    for (int i = 0; i < 6; i++) spr(a, social[i].name, social[i].x, social[i].y, social[i].s, WHITE, 0);
    spr_button(a, "GJ_achBtn_001.png", mid - 83.5f, 45.0f, 1.0f, H_NONE);
    spr_button(a, "GJ_optionsBtn_001.png", mid - 28.0f, 45.0f, 1.0f, H_SETTINGS);
    spr_button(a, "GJ_statsBtn_001.png", mid + 27.5f, 45.0f, 1.0f, H_NONE);
    spr_button(a, "GJ_ngBtn_001.png", mid + 83.0f, 45.0f, 1.0f, H_NONE);
    spr_button(a, "GJ_newsBtn_001.png", W - 43.0f, 45.0f, 0.88f, H_NONE);
    spr(a, "exMark_001.png", W - 28.0f, 67.0f, 0.7f, WHITE, 0);
    label(a, &a->big, "Fan-made port - not affiliated with RobTop Games", mid, 9.0f, 0.24f, 1,
          C(255, 255, 255, 0.75f), 0);
}

static void draw_select(App *a)
{
    const float W = pt_width(a), mid = W * 0.5f;
    static const unsigned char k_page[9][3] = {{0, 0, 255},   {255, 0, 255}, {255, 0, 125}, {255, 0, 0},
                                               {255, 125, 0}, {255, 255, 0}, {0, 255, 0},   {0, 255, 255},
                                               {0, 125, 255}};
    const unsigned char *pc = k_page[a->sel % 9];
    const GdColorF c = C(pc[0], pc[1], pc[2], 1);
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(c.r * 0.6f, c.g * 0.6f, c.b * 0.6f, 1));
    if (a->gradient.tex)
        gfx_image_rect(&a->g, &a->gradient, (SDL_FRect){0, 0, (float)a->gradient.w, (float)a->gradient.h},
                       (SDL_FRect){-2, -2, a->lw + 4.0f, a->lh + 4.0f}, c);
    menu_ground(a, 50.0f, C(c.r * 0.55f, c.g * 0.55f, c.b * 0.55f, 1), 0);
    spr(a, "GJ_topBar_001.png", mid, 303.0f, 1.0f, WHITE, 0);
    spr(a, "GJ_sideArt_001.png", 35.0f, 35.0f, 1.0f, WHITE, 0);
    spr(a, "GJ_sideArt_001.png", W - 35.0f, 35.0f, 1.0f, WHITE, 1);

    const LevelEntry *e = &a->levels[a->sel];
    pt_panel(a, &a->dark, mid - 170.0f, 172.5f, 340.0f, 95.0f, C(0, 0, 0, 125.0f / 255.0f));
    hot_add(a, mid, 220.0f, 340.0f, 95.0f, H_CARD);
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
    if (e->official)
        for (int i = 0; i < 3; i++)
            spr_coin(a, mid + 102.0f + i * 26.0f, 188.5f, 21.0f);
    label(a, &a->big, "Normal Mode", mid, 149.0f, 0.5f, 1, WHITE, 0);
    pt_bar(a, mid, 130.0f, e->best, C(0, 255, 0, 1));
    label(a, &a->big, "Practice Mode", mid, 99.0f, 0.5f, 1, WHITE, 0);
    pt_bar(a, mid, 80.0f, 0, C(0, 255, 255, 1));
    label(a, &a->big, "Download the soundtrack", mid, 34.0f, 0.5f, 1, WHITE, 0);
    const float step = a->nlevels > 1 ? fminf(16.0f, (W - 60.0f) / (a->nlevels - 1)) : 16.0f;
    for (int i = 0; i < a->nlevels && a->dot.tex; i++) {
        const float p = pt_scale(a), x = mid + (i - (a->nlevels - 1) * 0.5f) * step, d = 8.0f;
        gfx_image_rect(&a->g, &a->dot, (SDL_FRect){0, 0, (float)a->dot.w, (float)a->dot.h},
                       (SDL_FRect){(x - d * 0.5f) * p, (320.0f - 15.0f - d * 0.5f) * p, d * p, d * p},
                       i == a->sel ? WHITE : C(125, 125, 125, 1));
    }
    spr(a, "sideBtn_death_001.png", W - 20.0f, 260.0f, 1.0f, WHITE, 0);

    spr_button(a, "GJ_arrow_01_001.png", 25.0f, 298.0f, 1.0f, H_BACK);
    spr(a, "GJ_infoIcon_001.png", W - 20.0f, 300.0f, 1.0f, WHITE, 0);
    spr(a, "navArrowBtn_001.png", 25.0f, 160.0f, 1.0f, WHITE, 1);
    spr(a, "navArrowBtn_001.png", W - 25.0f, 160.0f, 1.0f, WHITE, 0);
    hot_add(a, 25.0f, 160.0f, 50.0f, 90.0f, H_PREV);
    hot_add(a, W - 25.0f, 160.0f, 50.0f, 90.0f, H_NEXT);
}

static void draw_pause(App *a)
{
    const Game *gm = &a->game;
    const float W = pt_width(a), mid = W * 0.5f;
    pt_panel(a, &a->dark, 10.0f, 10.0f, W - 20.0f, 300.0f, C(0, 0, 0, 0.75f));
    label(a, &a->big, gm->name, mid, 288.0f, 0.8f, 1, WHITE, W - 120.0f);
    char s[64];
    snprintf(s, sizeof s, "Best: %d%%", (int)gm->best_percent);
    label(a, &a->gold, s, mid, 253.0f, 0.5f, 1, WHITE, 0);
    label(a, &a->big, "Normal Mode", mid, 235.0f, 0.5f, 1, WHITE, 0);
    pt_bar(a, mid, 215.0f, gm->percent, C(0, 255, 0, 1));
    spr_button(a, "GJ_practiceBtn_001.png", mid - 123.0f, 130.0f, 0.92f, H_NONE);
    spr_button(a, "GJ_playBtn2_001.png", mid - 38.0f, 130.0f, 1.0f, H_RESUME);
    spr_button(a, "GJ_menuBtn_001.png", mid + 47.0f, 130.0f, 0.92f, H_MENU);
    spr_button(a, "GJ_replayBtn_001.png", mid + 123.0f, 130.0f, 0.92f, H_RESTART);
    spr(a, "gj_songIcon_001.png", mid - 191.0f, 70.0f, 0.8f, WHITE, 0);
    pt_slider(a, mid - 173.0f, mid - 27.0f, 70.0f, 0.2f);
    spr(a, "GJ_sfxIcon_001.png", mid + 10.0f, 70.0f, 0.8f, WHITE, 0);
    pt_slider(a, mid + 27.0f, mid + 173.0f, 70.0f, 0.2f);
    spr(a, "sideBtn_death_001.png", 36.0f, 284.0f, 1.0f, WHITE, 0);
    spr(a, "sideBtn_marker_001.png", 36.0f, 250.0f, 1.0f, WHITE, 0);
    spr(a, "sideBtn_quests_001.png", 36.0f, 216.0f, 1.0f, WHITE, 0);
    spr_button(a, "sideBtn_options_001.png", W - 36.0f, 284.0f, 1.0f, H_NONE);
    spr(a, "sideBtn_hide_001.png", W - 36.0f, 250.0f, 1.0f, WHITE, 0);
    for (int i = 0; i < 3; i++)
        spr_coin(a, mid + (i - 1) * 45.5f, 33.0f, 24.0f);
}

static void draw_complete(App *a)
{
    const Game *gm = &a->game;
    const float W = pt_width(a), mid = W * 0.5f;
    gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(0, 0, 0, 100.0f / 255.0f));
    pt_panel(a, &a->dark, mid - 190.0f, 45.0f, 380.0f, 235.0f, C(0, 0, 0, 180.0f / 255.0f));
    spr(a, "GJ_levelComplete_001.png", mid, 250.0f, 0.8f, WHITE, 0);
    char s[64];
    snprintf(s, sizeof s, "Attempts: %d", gm->attempts);
    label(a, &a->gold, s, mid, 200.0f, 0.8f, 1, WHITE, 0);
    snprintf(s, sizeof s, "Jumps: %d", gm->jumps);
    label(a, &a->gold, s, mid, 175.0f, 0.8f, 1, WHITE, 0);
    const int sec = (int)gm->time;
    snprintf(s, sizeof s, "Time: %02d:%02d", sec / 60, sec % 60);
    label(a, &a->gold, s, mid, 150.0f, 0.8f, 1, WHITE, 0);
    label(a, &a->big, "Awesome!", mid, 112.0f, 0.8f, 1, WHITE, 0);
    spr_button(a, "GJ_replayBtn_001.png", mid - 95.0f, 52.0f, 1.0f, H_RESTART);
    spr_button(a, "GJ_menuBtn_001.png", mid + 95.0f, 52.0f, 1.0f, H_MENU);
}

// ---- garage (skins) ----

static const float k_garage_row_y[4] = {210, 325, 435, 520};
#define PAL_X 690.0f
#define PAL_Y 215.0f
#define PAL_CELL 40.0f
#define PAL_COLS 12

static void draw_garage(App *a)
{
    Gfx *g = &a->g;
    draw_menu_background(a);
    panel(a, (SDL_FRect){100, 120, 1080, 500});
    text(a, "Garage", UI_W / 2, 130, 0.8f, 1, WHITE);
    button(a, "GJ_arrow_01_001.png", 56, 55, 1.15f, 0);

    GdColorF c1 = pal(a->cfg.col1), c2 = pal(a->cfg.col2);
    const GdFrame *ar = gfx_find(g, "GJ_arrow_01_001.png");
    static const char *const labels[4] = {"Cube", "Ship", "Color 1", "Color 2"};
    for (int r = 0; r < 4; r++) {
        float y = k_garage_row_y[r];
        if (r == a->garage_row) gfx_fill(g, (SDL_FRect){130, y - 50, 560, 100}, C(255, 255, 255, 0.16f));
        text(a, labels[r], 160, y - 22, 0.4f, 0, WHITE);
        if (r < 2) {
            int ship = r == 1;
            int icon = ship ? a->cfg.icon_ship : a->cfg.icon_cube;
            int idx = list_index(ship ? a->ship_icons : a->cube_icons, ship ? a->n_ship : a->n_cube, icon);
            int n = ship ? a->n_ship : a->n_cube;
            if (ar) {
                gfx_frame(g, ar, 400, y, 0, 0.7f, 0.7f, WHITE, 0);
                gfx_frame(g, ar, 640, y, 0, -0.7f, 0.7f, WHITE, 0);
            }
            draw_icon(a, ship, icon, 520, y, ship ? 1.1f : 1.25f, c1, c2);
            char s[32];
            snprintf(s, sizeof s, "%d/%d", idx + 1, n);
            text(a, s, 520, y + 36, 0.26f, 1, C(255, 255, 255, 0.8f));
        } else {
            GdColorF c = r == 2 ? c1 : c2;
            gfx_fill(g, (SDL_FRect){440, y - 26, 160, 52}, C(255, 255, 255, 1));
            gfx_fill(g, (SDL_FRect){443, y - 23, 154, 46}, c);
            char s[32];
            snprintf(s, sizeof s, "%d", (r == 2 ? a->cfg.col1 : a->cfg.col2) + 1);
            text(a, s, 520, y + 34, 0.26f, 1, C(255, 255, 255, 0.8f));
        }
    }

    // palette: left click = colour 1, right click = colour 2
    int n = palette_size();
    for (int i = 0; i < n; i++) {
        float x = PAL_X + (i % PAL_COLS) * PAL_CELL, y = PAL_Y + (i / PAL_COLS) * PAL_CELL;
        gfx_fill(g, (SDL_FRect){x, y, PAL_CELL - 3, PAL_CELL - 3}, pal(i));
        if (i == a->cfg.col1) gfx_fill(g, (SDL_FRect){x, y, 10, 10}, C(0, 0, 0, 0.9f));
        if (i == a->cfg.col2) gfx_fill(g, (SDL_FRect){x + PAL_CELL - 13, y, 10, 10}, C(255, 255, 255, 0.95f));
    }
    text(a, "Left click: color 1   Right click: color 2", PAL_X + 235, PAL_Y + 175, 0.22f, 1,
         C(255, 255, 255, 0.8f));
    // big preview
    draw_icon(a, 0, a->cfg.icon_cube, 800, 480, 1.7f, c1, c2);
    draw_icon(a, 1, a->cfg.icon_ship, 1010, 480, 1.5f, c1, c2);
    text(a, "Up/Down: choose row   Left/Right: change   B: back", UI_W / 2, 598, 0.26f, 1,
         C(255, 255, 255, 0.85f));
}

// ---- controls (key bindings) ----

static const float k_ctl_x[4] = {620, 780, 940, 1100};
#define CTL_Y0 215.0f
#define CTL_ROW 58.0f

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
    Gfx *g = &a->g;
    draw_menu_background(a);
    panel(a, (SDL_FRect){100, 110, 1080, 520});
    text(a, "Controls", UI_W / 2, 118, 0.8f, 1, WHITE);
    button(a, "GJ_arrow_01_001.png", 56, 55, 1.15f, 0);

    static const char *const heads[4] = {"Key 1", "Key 2", "Pad 1", "Pad 2"};
    for (int c = 0; c < 4; c++) text(a, heads[c], k_ctl_x[c], 172, 0.28f, 1, C(255, 255, 255, 0.8f));
    for (int r = 0; r < ACT_COUNT; r++) {
        float y = CTL_Y0 + r * CTL_ROW;
        text(a, g_action_names[r], 160, y - 12, 0.38f, 0, WHITE);
        for (int c = 0; c < 4; c++) {
            int selc = (r == a->ctl_row && c == a->ctl_col);
            float w = 140, h = 44;
            gfx_fill(g, (SDL_FRect){k_ctl_x[c] - w / 2 - 2, y - h / 2 - 2 + 8, w + 4, h + 4},
                     selc ? C(255, 255, 80, 1) : C(0, 0, 0, 0.4f));
            gfx_fill(g, (SDL_FRect){k_ctl_x[c] - w / 2, y - h / 2 + 8, w, h},
                     selc ? C(40, 90, 200, 1) : C(0, 0, 0, 0.55f));
            char s[40];
            if (selc && a->capturing) snprintf(s, sizeof s, "...");
            else binding_label(&a->cfg, r, c, s, sizeof s);
            float sc = 0.3f;
            if (gfx_text_width(&a->big, s, sc) > w - 12) sc *= (w - 12) / gfx_text_width(&a->big, s, sc);
            text(a, s, k_ctl_x[c], y - 2, sc, 1, WHITE);
        }
    }
    text(a, a->capturing ? "Press a key or pad button (Esc: cancel)"
                         : "Enter/A: rebind   Del/X: clear   R/Y: reset all   B/Esc: back",
         UI_W / 2, 600, 0.26f, 1, C(255, 255, 255, 0.9f));
}

static char g_bot_hud[256];

// Progress bar and percentage as in the original.
static void draw_hud(App *a)
{
    Game *gm = &a->game;
    Gfx *g = &a->g;
    const float u = a->lh / VIEW_H;                    // pixels per game unit
    const float pct = fminf(fmaxf(gm->percent, 0.0f), 100.0f);
    const float w = 210.0f * u, h = 16.0f * u, x = a->lw / 2 - w / 2, y = 0.0f;
    if (a->groove.tex && a->stripes.tex) {
        const float tile = 32.0f * u, left = x + 2.0f * u, fill = (w - 4.0f * u) * pct / 100.0f;
        GdColorF c = {gm->c1[0], gm->c1[1], gm->c1[2], 1};
        for (float at = 0; at < fill; at += tile) {
            const float part = fminf(tile, fill - at);
            gfx_image_rect(g, &a->stripes, (SDL_FRect){0, 0, a->stripes.w * part / tile, (float)a->stripes.h},
                           (SDL_FRect){left + at, y + h * 0.25f, part, h * 0.5f}, c);
        }
        gfx_image_rect(g, &a->groove, (SDL_FRect){0, 0, (float)a->groove.w, (float)a->groove.h},
                       (SDL_FRect){x, y, w, h}, WHITE);
    } else {
        gfx_fill(g, (SDL_FRect){x, y + 8, w, 14}, C(0, 0, 0, 0.5f));
        gfx_fill(g, (SDL_FRect){x + 2, y + 10, (w - 4) * pct / 100.0f, 10}, C(90, 255, 70, 1));
    }
    char s[16];
    snprintf(s, sizeof s, "%d%%", (int)pct);
    text(a, s, x + w + 5.0f * u, y - 2.0f * u, 0.6f, 0, WHITE);
}

static void draw_play(App *a)
{
    Game *gm = &a->game;
    Gfx *g = &a->g;
    float scale = a->lh / VIEW_H;
    float vw = a->lw / scale;
    game_draw(gm, g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);

    char s[128];
    // "Attempt N" stands in the level, in the middle of the first screen
    const float at_x = pt_width(a) * 0.5f - gm->cam_x;
    if (at_x > -150.0f && !gm->finished) {
        snprintf(s, sizeof s, "Attempt %d", gm->attempts);
        label(a, &a->big, s, at_x, 243.0f - (gm->cam_bottom - -90.0f), 1.0f, 1, WHITE, 0);
    }
    draw_hud(a);
#ifdef __SWITCH__
    button(a, "GJ_pauseBtn_001.png", UI_W - 50, 50, 0.7f, 0);   // the original shows it on touch devices
#endif
    if (g_bot_hud[0]) text(a, g_bot_hud, 20, 672, 0.32f, 0, C(255, 255, 255, 0.9f));

    if (gm->finished) draw_complete(a);
    else if (a->paused) draw_pause(a);
}

// ------------------------------------------------------------ input

static void save_config(App *a) { config_save(&a->cfg, a->cfg_path); }

static void press_play(App *a)
{
    if (a->nlevels > 0 && start_level(a, a->sel) != 0) fprintf(stderr, "level load failed\n");
}

static void leave_level(App *a)
{
    if (a->game_loaded) a->levels[a->sel].best = a->game.best_percent;
    a->scene = SC_SELECT;
}

static void restart(App *a)
{
    game_reset(&a->game);
    a->game.attempts++;
    a->paused = 0;
}

static void garage_step(App *a, int dir)
{
    Config *c = &a->cfg;
    switch (a->garage_row) {
    case 0: {
        int i = list_index(a->cube_icons, a->n_cube, c->icon_cube) + dir;
        i = (i + a->n_cube) % a->n_cube;
        c->icon_cube = a->cube_icons[i];
        break;
    }
    case 1: {
        int i = list_index(a->ship_icons, a->n_ship, c->icon_ship) + dir;
        i = (i + a->n_ship) % a->n_ship;
        c->icon_ship = a->ship_icons[i];
        break;
    }
    case 2: c->col1 = (c->col1 + dir + palette_size()) % palette_size(); break;
    case 3: c->col2 = (c->col2 + dir + palette_size()) % palette_size(); break;
    }
}

static void on_confirm(App *a)
{
    switch (a->scene) {
    case SC_TITLE: a->scene = SC_SELECT; break;
    case SC_SELECT: press_play(a); break;
    case SC_PLAY:
        if (a->game.finished) restart(a);
        else if (a->paused) a->paused = 0;
        break;
    case SC_CONTROLS: a->capturing = 1; break;
    case SC_GARAGE: break;
    }
}

static void on_back(App *a)
{
    switch (a->scene) {
    case SC_TITLE: a->quit = 1; break;
    case SC_SELECT: a->scene = SC_TITLE; break;
    case SC_GARAGE:
    case SC_CONTROLS:
        save_config(a);
        a->scene = SC_TITLE;
        break;
    case SC_PLAY:
        if (a->game.finished) leave_level(a);
        else if (a->paused) leave_level(a);
        else a->paused = 1;
        break;
    }
}

static void do_left_right(App *a, int dir)
{
    switch (a->scene) {
    case SC_SELECT:
        if (a->nlevels > 0) a->sel = (a->sel + dir + a->nlevels) % a->nlevels;   // the list wraps around
        break;
    case SC_GARAGE: garage_step(a, dir); break;
    case SC_CONTROLS: a->ctl_col = (a->ctl_col + dir + 4) % 4; break;
    default: break;
    }
}

static void do_up_down(App *a, int dir)
{
    if (a->scene == SC_GARAGE) a->garage_row = (a->garage_row + dir + 4) % 4;
    else if (a->scene == SC_CONTROLS) a->ctl_row = (a->ctl_row + dir + ACT_COUNT) % ACT_COUNT;
}

static void action_down(App *a, int act)
{
    int playing = a->scene == SC_PLAY && !a->paused && !a->game.finished;
    switch (act) {
    case ACT_JUMP:
        if (playing) game_set_hold(&a->game, 1);
        else on_confirm(a);
        break;
    case ACT_PAUSE:
        if (a->scene == SC_PLAY && !a->game.finished) a->paused = !a->paused;
        break;
    case ACT_RESTART:
        if (a->scene == SC_PLAY) restart(a);
        else if (a->scene == SC_CONTROLS) { config_reset_bindings(&a->cfg); save_config(a); }
        break;
    case ACT_BACK: on_back(a); break;
    case ACT_CONFIRM: on_confirm(a); break;
    case ACT_LEFT: do_left_right(a, -1); break;
    case ACT_RIGHT: do_left_right(a, 1); break;
    default: break;
    }
}

static void action_up(App *a, int act)
{
    if (act == ACT_JUMP && a->scene == SC_PLAY) game_set_hold(&a->game, 0);
}

static void to_ui(App *a, int px, int py, float *ux, float *uy)
{
    int w, h;
    SDL_GetWindowSize(a->g.win, &w, &h);
    *ux = px * UI_W / (float)w;
    *uy = py * a->lh / (float)h;
}

static void do_left_right(App *a, int dir);

static void on_click(App *a, float x, float y, int right)
{
    int hit = H_NONE;
    for (int i = a->nhot - 1; i >= 0 && !hit; i--)
        if (inside(a->hot[i].r, x, y)) hit = a->hot[i].id;
    switch (a->scene) {
    case SC_TITLE:
        if (hit == H_PLAY) a->scene = SC_SELECT;
        else if (hit == H_GARAGE) a->scene = SC_GARAGE;
        else if (hit == H_SETTINGS) a->scene = SC_CONTROLS;
        else if (hit == H_QUIT) a->quit = 1;
        break;
    case SC_SELECT:
        if (hit == H_BACK) a->scene = SC_TITLE;
        else if (hit == H_PREV) do_left_right(a, -1);
        else if (hit == H_NEXT) do_left_right(a, 1);
        else if (hit == H_CARD) press_play(a);
        break;
    case SC_GARAGE: {
        if (x < 200 && y < 130) { on_back(a); break; }
        for (int r = 0; r < 4; r++)
            if (fabsf(y - k_garage_row_y[r]) < 50 && x < 700) {
                a->garage_row = r;
                if (r < 2) {
                    if (fabsf(x - 400) < 50) garage_step(a, -1);
                    else if (fabsf(x - 640) < 50) garage_step(a, 1);
                }
            }
        for (int i = 0; i < palette_size(); i++) {
            float px = PAL_X + (i % PAL_COLS) * PAL_CELL, py = PAL_Y + (i / PAL_COLS) * PAL_CELL;
            if (x >= px && x < px + PAL_CELL && y >= py && y < py + PAL_CELL) {
                if (right) a->cfg.col2 = i; else a->cfg.col1 = i;
            }
        }
        break;
    }
    case SC_CONTROLS: {
        if (x < 200 && y < 130) { on_back(a); break; }
        for (int r = 0; r < ACT_COUNT; r++)
            for (int c = 0; c < 4; c++)
                if (fabsf(x - k_ctl_x[c]) < 70 && fabsf(y - (CTL_Y0 + r * CTL_ROW + 8)) < 26) {
                    a->ctl_row = r; a->ctl_col = c; a->capturing = 1;
                }
        break;
    }
    case SC_PLAY:
        if (a->game.finished || a->paused) {
            if (hit == H_RESTART) restart(a);
            else if (hit == H_MENU) leave_level(a);
            else if (hit == H_RESUME) a->paused = 0;
        }
#ifdef __SWITCH__
        else if (x > UI_W - 100 && y < 100) a->paused = 1;
#endif
        else {
            game_set_hold(&a->game, 1);
        }
        break;
    }
}

// Stores a captured key / pad button into the selected binding.
static void bind_capture(App *a, int is_joy, int code)
{
    Binding *b = &a->cfg.bind[a->ctl_row];
    int col = a->ctl_col;
    if (is_joy) { if (col >= 2) b->joy[col - 2] = code; else b->joy[0] = code; }
    else { if (col < 2) b->key[col] = code; else b->key[0] = code; }
    a->capturing = 0;
    save_config(a);
}

static void handle_event(App *a, const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT: a->quit = 1; break;
    case SDL_KEYDOWN: {
        if (e->key.repeat) break;
        int sym = e->key.keysym.sym;
        if (a->capturing) {
            if (sym == SDLK_ESCAPE) a->capturing = 0;
            else bind_capture(a, 0, sym);
            break;
        }
        // non-rebindable shortcuts
        if (a->scene == SC_TITLE && sym == SDLK_g) { a->scene = SC_GARAGE; break; }
        if (a->scene == SC_TITLE && sym == SDLK_o) { a->scene = SC_CONTROLS; break; }
        if (a->scene == SC_GARAGE || a->scene == SC_CONTROLS) {
            if (sym == SDLK_UP) { do_up_down(a, -1); break; }
            if (sym == SDLK_DOWN) { do_up_down(a, 1); break; }
        }
        if (a->scene == SC_CONTROLS && sym == SDLK_DELETE) {
            Binding *b = &a->cfg.bind[a->ctl_row];
            if (a->ctl_col < 2) b->key[a->ctl_col] = 0; else b->joy[a->ctl_col - 2] = -1;
            save_config(a);
            break;
        }
        int act = config_action_for_key(&a->cfg, sym);
        // in the menus the cursor keys always navigate, even if rebound elsewhere
        if (act < 0 && (sym == SDLK_LEFT)) act = ACT_LEFT;
        if (act < 0 && (sym == SDLK_RIGHT)) act = ACT_RIGHT;
        if (act < 0 && sym == SDLK_ESCAPE) act = ACT_BACK;
        if (act >= 0) action_down(a, act);
        break;
    }
    case SDL_KEYUP: {
        int act = config_action_for_key(&a->cfg, e->key.keysym.sym);
        if (act >= 0) action_up(a, act);
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        float x, y;
        to_ui(a, e->button.x, e->button.y, &x, &y);
        on_click(a, x, y, e->button.button == SDL_BUTTON_RIGHT);
        break;
    }
    case SDL_MOUSEBUTTONUP:
        if (a->scene == SC_PLAY) game_set_hold(&a->game, 0);
        break;
    case SDL_JOYBUTTONDOWN: {
        int b = e->jbutton.button;
        if (a->capturing) { bind_capture(a, 1, b); break; }
        if (a->scene == SC_TITLE && b == 2) { a->scene = SC_GARAGE; break; }
        if (a->scene == SC_TITLE && b == 3) { a->scene = SC_CONTROLS; break; }
        if (a->scene == SC_CONTROLS && b == 3) { config_reset_bindings(&a->cfg); save_config(a); break; }
        if (a->scene == SC_CONTROLS && b == 2) {
            Binding *bd = &a->cfg.bind[a->ctl_row];
            if (a->ctl_col < 2) bd->key[a->ctl_col] = 0; else bd->joy[a->ctl_col - 2] = -1;
            save_config(a);
            break;
        }
        if ((a->scene == SC_GARAGE || a->scene == SC_CONTROLS) && (b == 13 || b == 15)) {
            do_up_down(a, b == 13 ? -1 : 1);
            break;
        }
        int act = config_action_for_joy(&a->cfg, b);
        if (act >= 0) action_down(a, act);
        break;
    }
    case SDL_JOYBUTTONUP: {
        int act = config_action_for_joy(&a->cfg, e->jbutton.button);
        if (act >= 0) action_up(a, act);
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------ bot run (development aid)
//
// Plays levels with the look-ahead bot. Where a level cannot be passed (features the engine does
// not simulate, e.g. moving objects), the stretch is skipped with noclip after a few identical
// deaths so that every part of every level is still visited and tested.

#define BOT_MAX_SKIPS 64
#define BOT_MAX_CP 220
#define BOT_MAX_FAILS 80
#define BOT_MAX_DEATHS 1500
#define BOT_CP_SPACING 300.0f

typedef struct {
    Bot bot;
    int deaths, attempts_cap;
    float front_x;                        // furthest place where it failed, and how often
    int fails;
    float skip_from[BOT_MAX_SKIPS], skip_to[BOT_MAX_SKIPS];
    int nskips;
    unsigned modes;                       // bit per player mode seen
    int saw_dual, saw_mirror, saw_mini, saw_flip;
    int tick, nofound;
    float max_x;
    GameCheckpoint cps[BOT_MAX_CP];
    int ncp;
    float next_cp_x;
    char label[128];
} BotRun;

#define MODE_COUNT 8
static const char *const k_mode_names[MODE_COUNT] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};

static void botrun_end(App *a, BotRun *r)
{
    a->game.practice = NULL;
    a->game.noclip = 0;
    for (int i = 0; i < r->ncp; i++) game_checkpoint_free(&r->cps[i]);
    r->ncp = 0;
}

static void botrun_begin(BotRun *r)
{
    memset(r, 0, sizeof *r);
    bot_init(&r->bot);
}

static void botrun_summary(App *a, BotRun *r, int idx, const char *why)
{
    char m[128] = "";
    for (int i = 0; i < MODE_COUNT; i++)
        if (r->modes & (1u << i)) { strcat(m, m[0] ? "," : ""); strcat(m, k_mode_names[i]); }
    printf("level %2d %-24s %-9s deaths=%3d skips=%2d modes=[%s]%s%s%s%s\n", idx + 1,
           a->levels[idx].name, why, r->deaths, r->nskips, m, r->saw_dual ? " dual" : "",
           r->saw_mirror ? " mirror" : "", r->saw_mini ? " mini" : "", r->saw_flip ? " gravity" : "");
    for (int i = 0; i < r->nskips; i++) printf("    skipped x=%.0f..%.0f\n", r->skip_from[i], r->skip_to[i]);
    fflush(stdout);
}

// Progress along the level. Turned gameplay and teleports make x useless, the distance covered is not.
static float progress(const Game *gm) { return gm->has_jumps ? gm->travel : gm->p.x; }
static float cp_progress(const Game *gm, const GameCheckpoint *c) { return gm->has_jumps ? c->s.travel : c->s.p.x; }

// A failure (death, or no surviving plan) sends the bot back to an earlier checkpoint, where it
// tries other decisions; how far back varies from attempt to attempt. Only when that fails many
// times at the same place is the stretch skipped.
static void botrun_retry(App *a, BotRun *r, float x)
{
    if (x > r->front_x + 300.0f) { r->front_x = x; r->fails = 0; }
    r->fails++;
    if (r->fails > BOT_MAX_FAILS && r->nskips < BOT_MAX_SKIPS) {
        r->skip_from[r->nskips] = r->front_x - 300;
        r->skip_to[r->nskips] = r->front_x + 700;
        r->nskips++;
        r->fails = 1;
    }
    // each round of 13 attempts reaches further back (wrong turns can show up much later)
    const float back = 150.0f + 230.0f * (float)((r->fails - 1) % 13) + 900.0f * (float)((r->fails - 1) / 13);
    int k = 0;
    for (int i = 0; i < r->ncp; i++) if (cp_progress(&a->game, &r->cps[i]) <= r->front_x - back) k = i;
    if (r->ncp) { a->game.practice = &r->cps[k]; r->ncp = k + 1; r->next_cp_x = cp_progress(&a->game, &r->cps[k]) + BOT_CP_SPACING; }
    bot_init(&r->bot);
    r->bot.explore = r->fails > 1 ? (unsigned)r->fails : 0;
    r->bot.rng = (unsigned)(r->fails * 2654435761u) ^ (unsigned)r->front_x;
    r->nofound = 0;
}

// Advances the bot-controlled game by one 60 Hz frame. Returns 1 when the level is over.
static int botrun_frame(App *a, BotRun *r, int idx)
{
    Game *gm = &a->game;
    if (gm->finished) { botrun_summary(a, r, idx, "COMPLETE"); botrun_end(a, r); return 1; }
    int noclip = 0;
    for (int i = 0; i < r->nskips; i++)
        if (progress(gm) >= r->skip_from[i] && progress(gm) <= r->skip_to[i]) noclip = 1;
    gm->noclip = noclip;
    gm->bot_strict = !noclip;
    const int stop = getenv("BOT_STOP") != NULL;   // debugging: end at the first failure
    if (!gm->p.dead) {
        if ((r->tick++ & 1) == 0) game_set_hold(gm, bot_decide(&r->bot, gm));
        if (r->bot.explore && progress(gm) > r->front_x + 400.0f) r->bot.explore = 0;   // got past it
        // no survivable plan for 4 s of game time: treat it like a death
        if (!r->bot.found && !gm->noclip) r->nofound++; else if (r->bot.found) r->nofound = 0;
        if (r->nofound >= 240) {
            if (stop || getenv("BOT_LOG"))
                printf("  L%d no plan at x=%.0f y=%.0f mode=%d grav=%d\n", idx + 1, gm->p.x, gm->p.y, gm->p.mode, gm->p.grav);
            if (stop) { botrun_summary(a, r, idx, "STOPPED"); botrun_end(a, r); return 1; }
            botrun_retry(a, r, progress(gm));
            gm->p.dead = 1;
            gm->p.dead_t = 1.0f;
        }
        if (gm->noclip) {   // keep the icon in a sane place while ignoring collisions
            Player *pl = &gm->p;
            if (pl->y > 420.0f || (pl->grav < 0 && pl->mode == M_CUBE)) {
                pl->y = pl->mini ? 9.0f : 15.0f; pl->grav = 1; pl->yv = 0;
            }
        }
        if (r->bot.found && !gm->p.dead && progress(gm) >= r->next_cp_x && r->ncp < BOT_MAX_CP && !gm->noclip) {
            game_checkpoint_save(gm, &r->cps[r->ncp++]);
            r->next_cp_x = progress(gm) + BOT_CP_SPACING;
        }
    }
    int was_dead = gm->p.dead;
    game_update(gm, 1.0f / 60.0f);
    {
        const char *t0 = getenv("GDTX0"), *t1 = getenv("GDTX1");
        if (t0 && t1 && gm->p.x >= (float)atof(t0) && gm->p.x <= (float)atof(t1))
            printf("    x=%.1f y=%.1f yv=%.2f g=%d hold=%d og=%d rise=%d mode=%d found=%d depth=%d dual=%d y2=%.1f g2=%d m2=%d fwd=%d,%d up=%d,%d ch=%d free=%d\n",
                   gm->p.x, gm->p.y, gm->p.yv, gm->p.grav, gm->p.hold, gm->p.on_ground, gm->p.rising, gm->p.mode,
                   r->bot.found, r->bot.depth_best, gm->dual, gm->p2.y, gm->p2.grav, gm->p2.mode,
                   gm->frame.fx, gm->frame.fy, gm->frame.ux, gm->frame.uy, gm->channel, gm->free);
    }
    if (getenv("GDCAM") && ((int)gm->p.x % 60) < 3) printf("CAM x=%.0f y=%.0f cam=%.0f ctr=%.0f mode=%d dual=%d mini=%d grav=%d\n", gm->p.x, gm->p.y, gm->cam_bottom, gm->ctr, gm->p.mode, gm->dual, gm->p.mini, gm->p.grav);
    r->modes |= 1u << gm->p.mode;
    if (gm->dual) r->saw_dual = 1;
    if (gm->mirror) r->saw_mirror = 1;
    if (gm->p.mini) r->saw_mini = 1;
    if (gm->p.grav < 0) r->saw_flip = 1;
    if (gm->p.x > r->max_x) r->max_x = gm->p.x;
    if (!was_dead && gm->p.dead) {
        r->deaths++;
        if (stop || getenv("BOT_LOG"))
            printf("  L%d death %d at x=%.0f y=%.0f g=%d mini=%d dual=%d mode=%s (%s)\n", idx + 1, r->deaths, gm->p.x,
                   gm->p.y, gm->p.grav, gm->p.mini, gm->dual, k_mode_names[gm->p.mode], gm->death_info);
        botrun_retry(a, r, progress(gm));
        if (r->deaths > BOT_MAX_DEATHS) { botrun_summary(a, r, idx, "GAVE UP"); botrun_end(a, r); return 1; }
        if (stop) { botrun_summary(a, r, idx, "STOPPED"); botrun_end(a, r); return 1; }
    }
    return 0;
}

// Fast, headless: plays one level to the end and prints a summary.
static int save_shot(App *a, const char *path);
static void render(App *a);

static int selftest(App *a, int idx, float seconds)
{
    if (start_level(a, idx) != 0) { puts("load failed"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 4096);
    if (getenv("BOT_LOG")) setvbuf(stdout, NULL, _IONBF, 0);   // keep the log if the run is stopped
    static BotRun r;
    botrun_begin(&r);
    // development: BOT_LOAD=<file> starts from a saved state; BOT_SAVE=<x>:<file> writes one when the
    // bot passes x (BOT_SAVE_EXIT=1 stops there)
    const char *load = getenv("BOT_LOAD"), *save = getenv("BOT_SAVE");
    if (load) {
        if (game_state_io(&a->game, load, 0)) { puts("cannot load the saved state"); return 1; }
        game_checkpoint_save(&a->game, &r.cps[r.ncp++]);
        r.next_cp_x = progress(&a->game) + BOT_CP_SPACING;
        r.front_x = progress(&a->game);
    }
    float save_x = save ? (float)atof(save) : 0;
    const char *save_path = save ? strchr(save, ':') : NULL;
    // BOT_SAVE_EVERY=<prefix> keeps a state every 1500 units of progress (<prefix><n>.bin)
    const char *every = getenv("BOT_SAVE_EVERY");
    int saved_n = load ? (int)(progress(&a->game) / 1500.0f) : 0;
    // BOT_SHOT=<prefix> saves a picture every 600 units of progress (<prefix><n>.png)
    const char *shots = getenv("BOT_SHOT");
    int shot_n = 0;
    long frames = 0;
    while (frames < (long)(seconds * 60)) {
        if (botrun_frame(a, &r, idx)) return 0;
        frames++;
        if (shots && !a->game.p.dead && (int)(progress(&a->game) / 600.0f) > shot_n) {
            char path[512];
            shot_n = (int)(progress(&a->game) / 600.0f);
            snprintf(path, sizeof path, "%s%02d.png", shots, shot_n);
            render(a);
            save_shot(a, path);
        }
        if (every && !a->game.p.dead && (int)(progress(&a->game) / 1500.0f) > saved_n) {
            char path[512];
            saved_n = (int)(progress(&a->game) / 1500.0f);
            snprintf(path, sizeof path, "%s%d.bin", every, saved_n);
            if (!game_state_io(&a->game, path, 1))
                printf("checkpoint %d at x=%.0f (deaths %d, skips %d)\n", saved_n, a->game.p.x, r.deaths, r.nskips);
        }
        if (save_path && !a->game.p.dead && a->game.p.x >= save_x) {
            if (game_state_io(&a->game, save_path + 1, 1)) puts("cannot save the state");
            else printf("state saved at x=%.0f (deaths %d, skips %d)\n", a->game.p.x, r.deaths, r.nskips);
            save_path = NULL;
            if (getenv("BOT_SAVE_EXIT")) break;
        }
    }
    botrun_summary(a, &r, idx, "TIMEOUT");
    botrun_end(a, &r);
    return 0;
}

// Renders N frames of the current scene and prints the average frame time (development aid).
static void bench(App *a, int frames)
{
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < frames; i++) {
        if (a->scene == SC_PLAY) game_update(&a->game, 1.0f / 60.0f);
        a->t += 1.0f / 60.0f;
        gfx_begin_frame(&a->g);
        if (a->scene == SC_PLAY) draw_play(a);
        gfx_present(&a->g);
    }
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    printf("bench: %d frames, %.2f ms/frame (%.0f fps)\n", frames, ms / frames, 1000.0 / (ms / frames));
}

// ------------------------------------------------------------ screenshots (development aid)

static int save_shot(App *a, const char *path)
{
    int w, h;
    unsigned char *px = gfx_read_pixels(&a->g, &w, &h);
    if (!px) return -1;
    for (int i = 0; i < w * h; i++) px[i * 4 + 3] = 255;
    int ok = stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
    return ok ? 0 : -1;
}

static void render(App *a)
{
    gfx_begin_frame(&a->g);
    a->nhot = 0;
    switch (a->scene) {
    case SC_TITLE: draw_title(a); break;
    case SC_SELECT: draw_select(a); break;
    case SC_PLAY: draw_play(a); break;
    case SC_GARAGE: draw_garage(a); break;
    case SC_CONTROLS: draw_controls(a); break;
    }
    if (a->scene != a->shown) {
        a->shown = a->scene;
        a->shown_since = a->t;
    }
    const float dark = 1.0f - (a->t - a->shown_since) / 0.25f;
    if (dark > 0) gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(0, 0, 0, fminf(dark, 1.0f)));
    gfx_present(&a->g);
}

// ------------------------------------------------------------ level tour (visual comparison aid)

#ifdef GD_ALIGN
// Alignment against recordings of the original game (GD_TOURREF=<folder with *_pNNNN.jpg>):
// the tour then looks for the camera position whose picture matches the recorded one best.
// Needs -DGD_ALIGN -DSTBI_ONLY_JPEG.
#define REF_W 320
#define REF_H 200

// horizontal brightness steps of the picture scaled to REF_W x REF_H (independent of colour tints)
static void ref_edges(const unsigned char *px, int w, int h, float *out)
{
    static float g[REF_H][REF_W];
    for (int y = 0; y < REF_H; y++)
        for (int x = 0; x < REF_W; x++) {
            int sum = 0, n = 0;
            for (int yy = y * h / REF_H; yy < (y + 1) * h / REF_H; yy++)
                for (int xx = x * w / REF_W; xx < (x + 1) * w / REF_W; xx++, n++) {
                    const unsigned char *p = px + (yy * w + xx) * 4;
                    sum += p[0] + 2 * p[1] + p[2];
                }
            g[y][x] = n ? sum / (4.0f * n) : 0;
        }
    for (int y = 0; y < REF_H; y++)
        for (int x = 0; x < REF_W; x++) {
            const float d = x > 0 && x < REF_W - 1 ? g[y][x + 1] - g[y][x - 1] : 0;
            // only hard edges: the soft ones of the background move at their own speed
            out[y * REF_W + x] = fabsf(d) < 22.0f ? 0.0f : fminf(fmaxf(d, -70.0f), 70.0f);
        }
}

// best normalised correlation over vertical shifts of up to +-24 rows (96 screen px)
static float ref_score(const float *a, const float *b, int *shift)
{
    float best = -2;
    for (int dy = -24; dy <= 24; dy++) {
        double ab = 0, aa = 0, bb = 0;
        for (int y = 36; y < 176; y++)
            for (int x = 1; x < REF_W - 1; x++) {
                const float p = a[y * REF_W + x], q = b[(y + dy) * REF_W + x];
                ab += p * q; aa += p * p; bb += q * q;
            }
        const float v = (float)(ab / sqrt(aa * bb + 1e-9)) - 0.0004f * abs(dy);
        if (v > best) { best = v; *shift = dy; }
    }
    return best;
}

static int ref_load(const char *dir, int mark, float *edges)
{
    char suffix[24], path[900] = "";
    snprintf(suffix, sizeof suffix, "_p%04d.jpg", mark);
    DIR *d = opendir(dir);
    if (!d) return -1;
    for (struct dirent *e; (e = readdir(d));) {
        const size_t n = strlen(e->d_name);
        if (e->d_name[0] == 'f' && n > 10 && !strcmp(e->d_name + n - 10, suffix)) {
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
    int w, h, c;
    unsigned char *px = path[0] ? stbi_load(path, &w, &h, &c, 4) : NULL;
    if (!px) return -1;
    ref_edges(px, w, h, edges);
    stbi_image_free(px);
    return 0;
}
#endif

//
// Runs a level without input and without deaths (like the original game with noclip) and saves a
// picture each time the player reaches one of the requested progress values (per mille).
static int tour(App *a, int idx, const char *marks, const char *dir, float extra_len)
{
    if (start_level(a, idx) != 0) { puts("load failed"); return 1; }
    Game *gm = &a->game;
    const float len = gm->end_x - 380.0f + extra_len;   // progress bar of the original -> position
    const float scale = (float)a->g.h / VIEW_H, vw = (float)a->g.w / scale;
    static const float tc1[3] = {0, 255, 255}, tc2[3] = {0, 0, 255};   // colours used in the recordings
    game_set_skin(gm, &a->g, a->cfg.icon_cube, a->cfg.icon_ship, tc1, tc2);
    game_reset(gm);
    for (const char *s = marks; s && *s; ) {
        const int mark = atoi(s);
        float target = len * (float)mark / 1000.0f + 240.0f;
#ifdef GD_ALIGN
        static float want[REF_W * REF_H], have[REF_W * REF_H];
        const char *refdir = getenv("GD_TOURREF");
        const int aligned = refdir && !ref_load(refdir, mark, want);
        float best = -2, best_x = 0;   // spots: 100 units before and after the expected one
        int best_dy = 0;
        char best_path[700];
        snprintf(best_path, sizeof best_path, "%s/p%04d.png", dir, mark);
        const float from = target - 100.0f;
        if (aligned) target += 100.0f;
#endif
        for (int frame = 0; gm->p.x < target && !gm->finished && frame < 60 * 600; frame++) {
            gm->noclip = 1;
            Player *pl = &gm->p;
            if (pl->y > 1100.0f || (pl->y > 420.0f && pl->grav < 0 && pl->mode == M_CUBE)) {
                pl->y = 15.0f; pl->grav = 1; pl->yv = 0;
            }
            float dt = 1.0f / 60.0f;
            int shown = frame % 2 == 0 || gm->p.x >= target - 60.0f;
#ifdef GD_ALIGN
            if (aligned && gm->p.x >= from) { dt = 1.0f / 240.0f; shown = 1; }   // finer steps near the spot
#endif
            game_update(gm, dt);
            // the camera is advanced while drawing too, so draw (without showing) every frame
            if (shown) {
                gfx_begin_frame(&a->g);
                game_draw(gm, &a->g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);
#ifdef GD_ALIGN
                if (aligned && gm->p.x >= from) {
                    int w, h, dy = 0;
                    if (getenv("GD_TOURHUD")) draw_hud(a);
                    unsigned char *px = gfx_read_pixels(&a->g, &w, &h);
                    if (px) {
                        ref_edges(px, w, h, have);
                        free(px);
                        const float v = ref_score(have, want, &dy);
                        if (v > best) {
                            best = v; best_x = gm->p.x; best_dy = dy;
                            save_shot(a, best_path);
                        }
                    }
                }
#endif
            }
        }
#ifdef GD_ALIGN
        if (aligned && best > -2) {
            // dx: distance from the expected spot; dy: screen px the recording sits lower than the port
            printf("p%04d x=%.0f dx=%.0f dy=%d score=%.3f mode=%d\n", mark, best_x, best_x - (target - 100.0f),
                   best_dy * 4, best, gm->p.mode);
        } else
#endif
        {
            gfx_begin_frame(&a->g);
            game_draw(gm, &a->g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);
            if (getenv("GD_TOURHUD")) draw_hud(a);   // laid out for 1280x720, close enough at 1280x800
            char path[700];
            snprintf(path, sizeof path, "%s/p%04d.png", dir, mark);
            if (save_shot(a, path)) { printf("cannot write %s\n", path); return 1; }
            printf("p%04d x=%.0f y=%.0f mode=%d\n", mark, gm->p.x, gm->p.y, gm->p.mode);
        }
        gfx_present(&a->g);
        const char *c = strchr(s, ',');
        s = c ? c + 1 : NULL;
    }
    return 0;
}

int main(int argc, char **argv)
{
    App *a = calloc(1, sizeof *a);
    const char *shot = NULL, *scene = "title";
    int level = 0, fullscreen = 0, bench_frames = 0;
    float tp_x = -1, tp_y = 15;
    int mode = -1, selftest_level = -1, rttest_level = -1;
    float selftest_secs = 120;
    const char *bot_spec = NULL;
    int shot_mirror = 0, shot_mini = 0, shot_flip = 0, shot_dual = 0;
    int bot_speed = 1;
    const char *tour_marks = NULL, *tour_dir = ".";
    int tall = 0;     // 16:10 screenshots, to compare with recordings of the original
    float tour_len = 0.0f;
    snprintf(a->resdir, sizeof a->resdir, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/Resources");
#else
             "C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources");
#endif
    snprintf(a->custom_dir, sizeof a->custom_dir, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/levels");
#else
             "gd-levels");
#endif
    snprintf(a->cfg_path, sizeof a->cfg_path, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/config.ini");
#else
             "gd-config.ini");
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--res") && i + 1 < argc) snprintf(a->resdir, sizeof a->resdir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--levels") && i + 1 < argc) snprintf(a->custom_dir, sizeof a->custom_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) snprintf(a->cfg_path, sizeof a->cfg_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--x") && i + 1 < argc) tp_x = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--y") && i + 1 < argc) tp_y = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftest_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rttest") && i + 1 < argc) rttest_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) selftest_secs = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) bot_spec = argv[++i];
        else if (!strcmp(argv[i], "--botspeed") && i + 1 < argc) bot_speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--mirror")) shot_mirror = 1;
        else if (!strcmp(argv[i], "--mini")) shot_mini = 1;
        else if (!strcmp(argv[i], "--flip")) shot_flip = 1;
        else if (!strcmp(argv[i], "--dual")) shot_dual = 1;
        else if (!strcmp(argv[i], "--tour") && i + 1 < argc) tour_marks = argv[++i];
        else if (!strcmp(argv[i], "--tall")) tall = 1;
        else if (!strcmp(argv[i], "--tourdir") && i + 1 < argc) tour_dir = argv[++i];
        else if (!strcmp(argv[i], "--tourlen") && i + 1 < argc) tour_len = (float)atof(argv[++i]);
    }
#ifdef __SWITCH__
    // Log to the SD card so a failed start can be diagnosed.
    freopen("sdmc:/switch/gd/log.txt", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "gd-switch2 start, res=%s\n", a->resdir);
#endif
    // the tour renders in 16:10 like the recordings of the original game
    a->lw = UI_W;
    a->lh = tour_marks || tall ? 800.0f : UI_H;
    int rc_init = gfx_init(&a->g, a->resdir, (int)a->lw, (int)a->lh, fullscreen);
    if (rc_init != 0) {
        fprintf(stderr, "init failed rc=%d (resources: %s)\n", rc_init, a->resdir);
#ifdef __SWITCH__
        // Show the error on screen instead of a black screen.
        char sdl_err[256];
        snprintf(sdl_err, sizeof sdl_err, "%s", SDL_GetError());
        gfx_quit(&a->g);   // releases the screen so that the console can use it
        consoleInit(NULL);
        printf("Geometry Dash (fan port): start failed\n\nerror %d: %s\n", rc_init,
               rc_init == -4   ? "cannot load the sprite sheets"
               : rc_init == -5 ? "cannot create textures (out of memory?)"
                               : "cannot set up graphics");
        if (rc_init == -4) printf("resources: %s\n", a->resdir);
        else printf("SDL: %s\n", sdl_err);
        printf("\nDetails: sdmc:/switch/gd/log.txt\n\nPress + to exit.\n");
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState ep;
        padInitializeDefault(&ep);
        while (appletMainLoop()) {
            padUpdate(&ep);
            if (padGetButtonsDown(&ep) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
#endif
        return 1;
    }
    fprintf(stderr, "gfx ok (%dx%d), %d sheets\n", a->g.w, a->g.h, a->g.atlas.nsheets);
    gfx_font_load(&a->g, "bigFont", &a->big);
    gfx_load_image(&a->g, "GJ_gradientBG-hd.png", &a->gradient);
    gfx_font_load(&a->g, "goldFont", &a->gold);
    gfx_load_image(&a->g, "square02_001-hd.png", &a->dark);
    gfx_load_image(&a->g, "GJ_progressBar_001-hd.png", &a->bar);
    gfx_load_image(&a->g, "game_bg_01_001-hd.png", &a->title_bg);
    gfx_load_image(&a->g, "smallDot-hd.png", &a->dot);
    gfx_load_image(&a->g, "slidergroove-hd.png", &a->slider);
    gfx_load_image(&a->g, "sliderthumb-hd.png", &a->thumb);
    gfx_load_image(&a->g, "groundSquare_01_001-hd.png", &a->ground_menu);
    gfx_load_image(&a->g, "slidergroove2-hd.png", &a->groove);
    gfx_load_image(&a->g, "sliderBar2-hd.png", &a->stripes);
    scan_levels(a);
    config_load(&a->cfg, a->cfg_path);
    a->cube_icons = scan_icons(a, "player_%02d_001.png", 400, &a->n_cube);
    a->ship_icons = scan_icons(a, "ship_%02d_001.png", 200, &a->n_ship);
    if (!a->n_cube) { a->cube_icons[0] = 1; a->n_cube = 1; }
    if (!a->n_ship) { a->ship_icons[0] = 1; a->n_ship = 1; }
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#else
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    SDL_ShowCursor(SDL_ENABLE);
#endif

    if (tour_marks) {
        int rc = tour(a, level, tour_marks, tour_dir, tour_len);
        gfx_quit(&a->g);
        return rc;
    }
    if (selftest_level >= 0) {
        int rc = selftest(a, selftest_level, selftest_secs);
        gfx_quit(&a->g);
        return rc;
    }
    if (shot || bench_frames) {
        a->sel = level;
        if (!strcmp(scene, "select")) a->scene = SC_SELECT;
        else if (!strcmp(scene, "garage")) { a->scene = SC_GARAGE; a->garage_row = 2; }
        else if (!strcmp(scene, "controls")) a->scene = SC_CONTROLS;
        else if (!strcmp(scene, "play") || !strcmp(scene, "complete") || !strcmp(scene, "pause")) {
            if (start_level(a, level) != 0) { fprintf(stderr, "cannot start level\n"); return 1; }
            if (mode >= 0) a->game.p.mode = mode;
            if (shot_mirror) { a->game.mirror = 1; a->game.mir_x = a->game.mirror ? 1280.0f / (720.0f / 320.0f) - 190.0f : 190.0f; }
            if (shot_mini) a->game.p.mini = 1;
            if (shot_flip) a->game.p.grav = -1;
            if (shot_dual) { a->game.dual = 1; a->game.p2 = a->game.p; a->game.p2.grav = -a->game.p.grav; a->game.p2.y = 200 - a->game.p.y; }
            if (mode >= 0 && mode != 0 && mode != 5) a->game.has_ceil = 1;
            if (tp_x >= 0) {
                a->game.p.x = tp_x;
                a->game.p.y = tp_y;
                a->game.prev_x = tp_x;
                a->game.prev_y = tp_y;
                a->game.cam_x = tp_x - 190;
                a->game.time = 5;
                a->game.percent = tp_x / (a->game.end_x - 380) * 100.0f;
            }
            if (!strcmp(scene, "complete")) a->game.finished = 1;
            if (!strcmp(scene, "pause")) a->paused = 1;
        }
        if (bench_frames) {
            bench(a, bench_frames);
            gfx_quit(&a->g);
            return 0;
        }
        for (int i = 0; i < 4; i++) { a->t += 0.5f; render(a); }
        int rc = save_shot(a, shot);
        gfx_quit(&a->g);
        return rc;
    }

    Uint64 rt_start = 0;
    if (rttest_level >= 0) {
        if (start_level(a, rttest_level) != 0) return 1;
        rt_start = SDL_GetPerformanceCounter();
    }
    // --bot "all" | "1,4,7-9": watch the look-ahead bot play levels in the window
    int bot_list[64], bot_n = 0, bot_pos = -1, bot_next = 0;
    static BotRun brun;
    if (bot_spec) {
        const char *s = bot_spec;
        while (*s && bot_n < 64) {
            if (!strncmp(s, "all", 3)) { for (int i = 0; i < a->nlevels && i < 22 && bot_n < 64; i++) bot_list[bot_n++] = i; s += 3; }
            else if (*s >= '0' && *s <= '9') {
                int lo = atoi(s); while (*s >= '0' && *s <= '9') s++;
                int hi = lo;
                if (*s == '-') { s++; hi = atoi(s); while (*s >= '0' && *s <= '9') s++; }
                for (int v = lo; v <= hi && bot_n < 64; v++) if (v >= 1 && v <= a->nlevels) bot_list[bot_n++] = v - 1;
            } else s++;
        }
        printf("bot: %d level(s)\n", bot_n);
    }
    Uint64 last = SDL_GetPerformanceCounter();
    float dt_smooth = 1.0f / 60.0f;
    while (!a->quit) {
        SDL_Event e;
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
        // libnx pad -> SDL joystick button numbers (same layout SDL uses on the Switch).
        static const u64 pad_map[16] = {
            HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
            HidNpadButton_StickL, HidNpadButton_StickR, HidNpadButton_L, HidNpadButton_R,
            HidNpadButton_ZL, HidNpadButton_ZR, HidNpadButton_Plus, HidNpadButton_Minus,
            HidNpadButton_Left, HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down};
        padUpdate(&pad);
        u64 pdown = padGetButtonsDown(&pad), pup = padGetButtonsUp(&pad);
        for (int b = 0; b < 16; b++) {
            if (!((pdown | pup) & pad_map[b])) continue;
            SDL_Event je;
            memset(&je, 0, sizeof je);
            je.type = (pdown & pad_map[b]) ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
            je.jbutton.button = (Uint8)b;
            handle_event(a, &je);
        }
#endif
        while (SDL_PollEvent(&e)) handle_event(a, &e);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        // Frame times jitter a little; smoothing them keeps the motion even.
        if (dt > 0.25f) dt = 0.25f;
        // Use the real elapsed time. (An earlier version smoothed it, which made the game run
        // about 15 % too fast: after every long frame the filter decayed slowly and added time.)
        dt_smooth = dt;
        a->t += dt_smooth;
        a->dbg_sum_dt += dt;
        a->dbg_sum_ds += dt_smooth;
        a->dbg_frames++;        if (a->press > 0) a->press = fmaxf(0, a->press - dt_smooth * 4);
        if (bot_spec) {
            if (bot_pos < 0 || bot_next) {
                bot_next = 0;
                if (++bot_pos >= bot_n) break;
                if (start_level(a, bot_list[bot_pos]) != 0) { printf("level %d: load failed\n", bot_list[bot_pos] + 1); continue; }
                botrun_begin(&brun);
                snprintf(g_bot_hud, sizeof g_bot_hud, "BOT  level %d  %s", bot_list[bot_pos] + 1, a->levels[bot_list[bot_pos]].name);
            }
            int over = 0;
            for (int k = 0; k < bot_speed && !over; k++) over = botrun_frame(a, &brun, bot_list[bot_pos]);
            if (over) { bot_next = 1; }
            else {
                static char m[64];
                m[0] = 0;
                for (int i = 0; i < MODE_COUNT; i++) if (brun.modes & (1u << i)) { if (m[0]) strcat(m, ","); strcat(m, k_mode_names[i]); }
                snprintf(g_bot_hud, sizeof g_bot_hud, "BOT  level %d  %s   deaths %d  skips %d  [%s]", bot_list[bot_pos] + 1,
                         a->levels[bot_list[bot_pos]].name, brun.deaths, brun.nskips, m);
            }
            render(a);
            {   // development aid: GDSHOTS="x1,x2,..." saves a screenshot when the player passes each x
                static const char *sp;
                static int init;
                if (!init) { sp = getenv("GDSHOTS"); init = 1; }
                if (sp && *sp) {
                    float nx = (float)atof(sp);
                    if (a->game.p.x >= nx) {
                        char fn[64];
                        snprintf(fn, sizeof fn, "shot_%d.png", (int)nx);
                        save_shot(a, fn);
                        const char *c = strchr(sp, ',');
                        sp = c ? c + 1 : "";
                    }
                }
            }
            continue;
        }
        if (a->scene == SC_PLAY && !a->paused) game_update(&a->game, dt_smooth);
        render(a);
        if (rttest_level >= 0 && a->game.p.dead) {
            double wall = (double)(SDL_GetPerformanceCounter() - rt_start) / (double)SDL_GetPerformanceFrequency();
            printf("rttest: died at x=%.1f after %.3f s wall clock (game time %.3f s) -> %.1f units/s\n", a->game.p.x, wall, a->game.time, a->game.p.x / wall);
            printf("rttest: frames=%d sum(dt)=%.3f s sum(dt_smooth)=%.3f s\n", a->dbg_frames, a->dbg_sum_dt, a->dbg_sum_ds);
            break;
        }
    }
    save_config(a);
    if (a->game_loaded) game_free(&a->game);
    gfx_quit(&a->g);
    return 0;
}
