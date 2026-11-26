// SPDX-License-Identifier: GPL-3.0-or-later
// State of the application, shared by app.c (levels, play, development aids) and menus.c
// (screens and input).
#ifndef GD_APP_H
#define GD_APP_H

#include <SDL.h>

#include "config.h"
#include "game.h"
#include "gfx.h"
#include "saves.h"

#define UI_W 1280.0f
#define UI_H 720.0f
#define VIEW_H 320.0f

typedef enum { SC_TITLE, SC_SELECT, SC_PLAY, SC_GARAGE, SC_CONTROLS, SC_ONLINE, SC_LEVEL } Scene;

typedef struct {
    char path[600];
    const char *data;   // level string held in memory (saved levels), else NULL
    char name[96];
    int diff;           // difficulty face index (0 = N/A)
    int stars;
    int official;
    GdLevelInfo info;   // custom levels: creator, length ...
    float best;
} LevelEntry;

typedef struct { float x, y, w, h; } Rect;       // centre and size
typedef struct { Rect r; int id, arg; } Hot;     // clickable area of the screen shown last

enum {
    H_NONE, H_PLAY, H_GARAGE, H_ONLINE, H_SETTINGS, H_QUIT, H_BACK, H_PREV, H_NEXT, H_CARD, H_PAGEDOT,
    H_RESUME, H_RESTART, H_MENU,
    H_TAB, H_ICON, H_TURN, H_DOT, H_COLORS, H_CLOSE, H_WHICH, H_SWATCH,
    H_ROW, H_START, H_BIND, H_RESET
};

#define MAX_HOT 256

typedef struct {
    Gfx g;
    GfxFont big, gold;
    GfxImage gradient, dark, bar, title_bg, bg, ground, ground2, ground_menu, groove, stripes, dot;
    GfxImage brown, grey, green_btn, grey_btn, white;
    float lw, lh;       // drawing size (1280x720; the level tour renders 16:10)
    Hot hot[MAX_HOT];
    int nhot;
    int focus_id, focus_arg, show_focus;   // the button chosen with the keys / the controller
    Scene shown;        // screen of the last picture, and when it appeared
    float shown_since;

    LevelEntry *levels; // the official levels first, then the custom ones
    int nlevels, nofficial;
    int sel;            // level select: official level shown
    int online_sel;     // online list: custom level chosen
    float scroll;       // online list: distance scrolled in points
    int cur;            // level being played
    GdSavedLevel *saved;
    int nsaved;

    Scene scene;
    Game game;
    int game_loaded;
    int paused;
    float t;            // seconds since start (menu animation)
    int quit;
    char resdir[512];
    char custom_dir[512];
    char save_path[600];
    char cfg_path[512];

    Config cfg;
    int *icons[IC_COUNT], nicons[IC_COUNT];   // icon numbers that exist, found when first needed
    int tab, page;      // garage
    int colors;         // garage: the colour page is open
    int which;          // colour page: 0 first colour, 1 second
    Anim pose[2];       // standing robot and spider
    int pose_loaded;
    int capturing;      // key bindings: waiting for a key / pad button
    int ctl_row, ctl_col;
    double dbg_sum_dt, dbg_sum_ds;  // development: sum of real vs smoothed frame times
    int dbg_frames;
} App;

static inline GdColorF C(float r, float g, float b, float a) { return (GdColorF){r, g, b, a}; }
static const GdColorF WHITE = {255, 255, 255, 1};

// ---- app.c
int start_level(App *a, int idx);
void restart(App *a);
void leave_level(App *a);
void apply_skin(App *a);

// ---- menus.c
void ui_load(App *a);                    // images and fonts of the menus
void ui_draw(App *a);                    // every screen but the running level
void ui_draw_pause(App *a);
void ui_draw_complete(App *a);
void ui_go(App *a, Scene s);
void ui_open_garage(App *a, int tab, int colors);
void ui_event(App *a, const SDL_Event *e);
// Text centred vertically at (x,y) in points (320 from the bottom to the top of the screen).
void label(App *a, const GfxFont *f, const char *s, float x, float y, float scale, int align,
           GdColorF c, float max_w);
float pt_width(const App *a);

#endif
