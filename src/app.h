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
#include "songs.h"

#define UI_W 1280.0f
#define UI_H 720.0f
#define VIEW_H 320.0f

typedef enum {
    SC_TITLE, SC_SELECT, SC_PLAY, SC_GARAGE, SC_CONTROLS, SC_ONLINE, SC_LEVEL, SC_SETTINGS, SC_SONGS,
    SC_GRAPHICS
} Scene;

typedef struct {
    char path[600];
    const char *data;   // level string held in memory (saved levels), else NULL
    char name[96];
    int diff;           // difficulty face index (0 = N/A)
    int stars;
    int official;
    int track;          // official levels: number of the song
    float practice;     // best reached in practice mode
    GdLevelInfo info;   // custom levels: creator, length ...
    char song_name[SONG_NAME];   // .aiogdl: file name of the song inside, else empty
    float best;
} LevelEntry;
// Name under which the coins of a level are kept.
void level_coin_key(const LevelEntry *e, char *out, size_t cap);

typedef struct { float x, y, w, h; } Rect;       // centre and size
typedef struct { Rect r; int id, arg; } Hot;     // clickable area of the screen shown last

enum {
    H_NONE, H_PLAY, H_GARAGE, H_ONLINE, H_SETTINGS, H_QUIT, H_BACK, H_PREV, H_NEXT, H_CARD, H_PAGEDOT,
    H_RESUME, H_RESTART, H_MENU,
    H_TAB, H_ICON, H_TURN, H_DOT, H_COLORS, H_CLOSE, H_WHICH, H_SWATCH,
    H_ROW, H_START, H_BIND, H_RESET,
    H_KEYS, H_VOLUME, H_SONG, H_SONGROW,
    H_PRACTICE, H_CP_ADD, H_CP_DEL,
    H_GRAPHICS, H_OPTION
};

#define MAX_HOT 256
#define MAX_CHECKPOINTS 64

// a finger on the touch screen: what it does and where it started and is
typedef struct {
    int active, id, mode, slider;
    Scene scene;          // the screen it was put on
    int paused, colors;
    float x0, y0, x, y;
} Touch;
#define MAX_TOUCH 10
enum { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };

typedef struct {
    Gfx g;
    GfxFont big, gold;
    GfxImage gradient, dark, bar, title_bg, bg, ground, ground2, ground_menu, groove, stripes, dot;
    GfxImage brown, grey, green_btn, grey_btn, white, slider, thumb;
    GfxImage other_bg[8], other_ground[8], other_ground2[8];   // those the level's triggers switch to
    GfxImage mg[4][2];  // middleground (two layers): the level's, then those it switches to
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
    int fullscreen_arg;   // started with --fullscreen (holds until the menu changes it)
    int *icons[IC_COUNT], nicons[IC_COUNT];   // icon numbers that exist, found when first needed
    int tab, page;      // garage
    int colors;         // garage: the colour page is open
    int which;          // colour page: 0 first colour, 1 second
    Anim pose[2];       // standing robot and spider
    int pose_loaded;
    char music_dir[512];                      // the player's own songs
    char songs[SONGS_MAX_FILES][SONG_NAME];   // their file names, read when the song list opens
    int nsongs, song_sel;                     // song list: row chosen
    float song_scroll;
    char preview[720];                        // song list: file being tried out
    char music_key[900];                      // what the music player was last told to play
    int music_faded;
    float slider_x0[2], slider_x1[2];         // volume sliders of the screen shown, in points
    int dragging;                             // slider being dragged (1 music, 2 effects), else 0
    Touch touch[MAX_TOUCH];                   // fingers on the screen
    float intro;                              // seconds until the level chosen in a menu begins
    int hitch;                                // a level was just loaded: do not count that time
    int practice;                             // practice mode is on
    GameCheckpoint check[MAX_CHECKPOINTS], trial;   // its checkpoints; one not yet proven
    int ncheck;
    float since_check, pending;               // seconds since the last one / since the trial
    int was_dead;                             // practice mode: for the sound of each crash
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
void apply_graphics(App *a);   // after a change in the graphics menu
// How long the original takes from pressing play to the start of the level.
#define LEVEL_INTRO 1.15f
// A sound effect of the original, from the game's folder.
void play_effect(const App *a, int id);
// The file with the music of a level; 0 if it has none.
int level_song(const App *a, int idx, char *out, size_t cap);
// Name under which the song chosen for a custom level is kept.
void level_song_key(const LevelEntry *e, char *out, size_t cap);
// The choice that holds for a custom level: what the player picked, else the song of its file.
const char *level_choice(const LevelEntry *e);

// ---- practice.c
void practice_toggle(App *a);       // from the pause menu
void practice_place(App *a);
void practice_remove(App *a);
void practice_frame(App *a, float dt);
void practice_keep_best(App *a);    // stores the best value of the present mode with the level
void practice_restarted(App *a);    // a restart drops the checkpoints
void practice_draw(App *a);
void practice_free(App *a);         // leaves practice mode

// ---- menus.c
void ui_load(App *a);                    // images and fonts of the menus
void ui_draw(App *a);                    // every screen but the running level
void ui_draw_pause(App *a);
void ui_draw_complete(App *a);
void ui_draw_checkpoint_buttons(App *a);   // practice mode, while playing
void ui_go(App *a, Scene s);
void ui_open_garage(App *a, int tab, int colors);
void ui_open_songs(App *a);   // song list of the chosen custom level
void ui_event(App *a, const SDL_Event *e);
// A finger (id) is put down, moves or is lifted at (x,y) in drawing coordinates.
void ui_touch(App *a, int phase, int id, float x, float y);
// Text centred vertically at (x,y) in points (320 from the bottom to the top of the screen).
void label(App *a, const GfxFont *f, const char *s, float x, float y, float scale, int align,
           GdColorF c, float max_w);
float pt_width(const App *a);

#endif
