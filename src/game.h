// SPDX-License-Identifier: GPL-3.0-or-later
// Game state: runtime objects, colour channels/triggers and player physics.
#ifndef GD_GAME_H
#define GD_GAME_H

#include "gfx.h"
#include "level.h"
#include "objchild.h"

typedef enum { OT_DECO, OT_SOLID, OT_HAZARD, OT_PORTAL, OT_PAD, OT_ORB, OT_SPEED, OT_TRIGGER, OT_HIDDEN, OT_FORCE, OT_SENSOR, OT_HBLOCK } ObjType;

// Portal kinds (Obj.sub for OT_PORTAL)
enum { P_GRAV_DOWN = 1, P_GRAV_UP, P_CUBE, P_SHIP, P_BALL, P_UFO, P_WAVE, P_SIZE_NORMAL, P_SIZE_MINI,
       P_MIRROR_ON, P_MIRROR_OFF, P_DUAL_ON, P_DUAL_OFF, P_ROBOT, P_SPIDER, P_OTHER, P_TELEPORT, P_SWING };
// Player modes
enum { M_CUBE = 0, M_SHIP, M_BALL, M_UFO, M_WAVE, M_ROBOT, M_SPIDER, M_SWING };
// Movement frame (gameplay rotation / reverse): forward and up as axis-aligned unit vectors.
typedef struct { signed char fx, fy, ux, uy; } Frame;
// Pad / orb kinds
enum { K_YELLOW = 1, K_BLUE, K_PINK, K_GREEN, K_RED, K_BLACK, K_DASH, K_DASH_FLIP, K_TOGGLE, K_SPIDER, K_TELEPORT };

#define CH_COUNT 1100
#define CH_BG 1000
#define CH_GROUND 1001
#define CH_LINE 1002
#define CH_3DL 1003
#define CH_OBJ 1004
#define CH_P1 1005
#define CH_P2 1006
#define CH_BLACK 1010
#define CH_WHITE 1011

typedef struct {
    float r, g, b, a;    // current value (0..255 rgb)
    int additive;
    // fade state
    float fr, fg, fb, fa, tr, tg, tb, ta, t, dur;
    int fading;
    int copy, copy_op, player;       // copy another channel (+ its opacity) / follow player colour 1 or 2
    int hsv_on, sadd, vadd;
    int lbg;                         // default "light background" channel: mix of background and colour 1
    float hs, ss, vs;                // hue shift (degrees), saturation and value factor/offset
} Channel;

typedef struct {
    int id;
    float x, y, rot, sx, sy;       // world position, rotation, signed scale (flip)
    ObjType type;
    int sub;                       // portal/pad/orb kind or speed index
    const GdFrame *main, *detail, *back, *extra;
    int ch_main, ch_detail;
    int zlayer, zorder;
    float hw, hh, hx, hy;          // hitbox half size and centre offset (world units, rotation/flip baked in)
    float rad;                     // >0: circular hazard hitbox radius (centred on x,y)
    int used, used2;               // already triggered by player 1 / player 2
    // trigger data
    float tr, tg, tb, tdur, topacity;
    int tblend, ttarget, tdone;
    // groups, moving objects and the extra trigger types (move, toggle, alpha, spawn)
    short grp[GD_MAX_GROUPS];
    short hsv1, hsv2;              // 1 + index into Game.hsv: own colour shift of main / detail (0 = none)
    unsigned char ngrp, mover, off, spawn_only;
    float x0, y0, rot0;            // initial position and rotation (movers are put back on every attempt)
    int tid;                       // trigger object id
    int tgroup, teasing, tact, tcopy; // target group, easing type, toggle: 1 = on, colour copy channel
    float tdx, tdy, trate, tdelay;
    float trot;                    // rotate trigger: degrees
    int tcenter;                   // rotate trigger: group of the pivot object
    unsigned char tlockx, tlocky;  // move trigger: follow the player horizontally / vertically
    int kidset;                    // index into Game.kidsets (child sprites), -1 = none
    float spin;                    // continuous rotation (degrees per second), saw blades
    unsigned char touch, tplayer;  // trigger: fires on contact only / uses player colour 1 or 2
    unsigned char tground;         // background trigger that also colours the ground
    float pin, phold, pout, phue;  // pulse trigger timing and hue shift
    unsigned char pgroup, pcopy, ponly;
    signed char rmove, rground;    // rotate gameplay trigger: 1 up, 2 down, 3 left, 4 right
    float value;                   // time warp / gravity factor / force strength
    float fxmod, fymod;            // follow trigger factors
    short channel, to_channel;     // 2.2 trigger channels: own channel / channel a rotation switches to
    unsigned char freemode;        // 2.2 form portal: no play area borders
    unsigned char passable;        // 2.2 one-way block: only landing on it counts
    unsigned char nodraw;          // hidden by the level, or an editor-only helper
    unsigned char tgrav;           // 2.2 teleport: 1 = normal gravity, 2 = flipped, 3 = toggle at the exit
    float tforce;                  // 2.2 teleport: upward speed the icon leaves the exit with (static force)
    const GdFrame *glow;           // soft light drawn behind the object
    const GdFrame *ball;           // pulsing ball above a rod
    unsigned char breakable;       // solid that breaks instead of killing
    unsigned char clipw;           // textured slope decoration: width of the triangle its sprites are cut to
    unsigned char slope;           // triangle: 0 = none, else 1 | 2 (right angle on the right) | 4 (ceiling)
    const GdFrame *fx1, *fx2;      // portal swirl sprites
} Obj;

#define GD_GROUPS 10000
#define GD_MAX_PART 400
typedef struct { float x, y, vx, vy, life, max, size, grow, r, g, b, a; } Particle;
typedef struct { const GdChild *c; int n; const GdFrame *f[12]; } KidSet;
#define GD_MAX_MOVES 64
#define GD_MAX_SPAWNS 64
typedef struct {
    int group, easing, center;     // center > 0: pivot group of a rotation
    int follow;                    // follow trigger: leading group (dx, dy are then factors)
    float dx, dy, dur, t, rate, done, rot, lx, ly;   // lx, ly: last player position (locked moves)
    unsigned char lockx, locky;
} Move;
typedef struct { int obj; float left; } PendingSpawn;
#define GD_MAX_ITEMS 32
#define GD_MAX_WATCH 48
typedef struct {            // state of the 2.2 logic triggers
    short item[GD_MAX_ITEMS]; int value[GD_MAX_ITEMS]; int nitem;   // item counters
    struct { int obj, state; } watch[GD_MAX_WATCH]; int nwatch;     // running count / collision / touch triggers
    unsigned rng;
} Logic;
typedef struct { int obj; float x, y, rot; unsigned char used, used2, tdone, off; } UndoEntry;
#define GD_MAX_PULSES 96
typedef struct {   // pulse trigger: a colour flashed onto a channel or a group (visual only)
    int target, group, copy, only;      // only: 1 = main sprite, 2 = detail sprite, 0 = both
    float r, g, b, hue, in, hold, out, t;
} Pulse;
typedef struct { int group; float from, to, dur, t; } AlphaFade;

typedef struct {
    float x, y, vy;  // vy: px/s, derived from yv (for display/bots only)
    double yv;       // OpenGD y velocity (units per 60 Hz frame, before the 0.9 slow factor)
    int mode;        // 0 cube, 1 ship
    int grav;        // +1 normal, -1 flipped
    int mini;
    float speed;     // units per second
    int on_ground, dead, hold;
    int rising, queued;   // OpenGD m_isRising, _queuedHold
    int touch_ring, touch_pad;
    int dashing;             // holding a dash orb: straight flight with this slope
    float dash_slope;
    float gscale;            // gravity factor (2.2 gravity trigger)
    float rot, flame;
    float rot_left, rot_rate, rot_dir; // running 180 degree rotation action
    int snap;             // rotating back to a multiple of 90 degrees
    int fall;             // steps spent falling at top speed (lost outside a turned level)
    float dead_t;
    int just_held;        // UFO: click happened this step
    float last_ground_y;  // y of the last ground contact (camera logic)
    float boost_t;        // robot: remaining jump boost time
    float trail[64][2];   // wave trail ring buffer
    int trail_n, trail_i;
} Player;

typedef struct {
    Obj *obj;
    int n;
    int *by_z;       // draw order
    int *by_x;       // collision broadphase order (sorted by x)
    Channel ch[CH_COUNT];
    Channel ch0[CH_COUNT]; // header-time colours, restored on every attempt
    int ch0_valid;
    int bg_id, ground_id;
    float end_x;
    int start_mode, start_speed;
    Player p, p2;                 // p2 only exists while dual != 0
    KidSet *kidsets; int nkidsets;
    short kid_of_id[5000];
    int nx;                       // objects in by_x (static ones; movers are listed separately)
    int *movers, nmov;            // objects that move triggers can move
    int *grp_start, *grp_items;   // objects per group (CSR), grp_start has GD_GROUPS + 1 entries
    float (*hsv)[5]; int nhsv;    // distinct per-object colour shifts
    float galpha[GD_GROUPS];      // group opacity (alpha trigger)
    Move mv[GD_MAX_MOVES]; int nmv;
    PendingSpawn sp[GD_MAX_SPAWNS]; int nsp;
    int dual;
    Frame frame, next_frame;      // current movement frame; next_frame is applied after the step
    float travel;                 // distance covered along the way (progress even when x does not grow)
    float timewarp;               // game speed factor (time warp trigger)
    UndoEntry *undo; int nundo, capundo;   // object changes made by the look-ahead
    int has_end, has_jumps;       // level has an end trigger / rotation or teleports (x not monotonic)
    int channel;                  // active trigger channel
    Logic logic;
    int local;                    // the icon is currently given in the turned movement frame
    int head_safe;                // the icon being checked is inside an H block: head bumps are harmless
    int *cblocks, ncblocks;       // collision blocks
    struct { int obj; short list[16]; int n; } *lists; int nlists;   // random / sequence lists
    int free;                     // free mode: flying forms are not kept inside a play area
    int *turns, nturns;           // rotate gameplay triggers (they are checked wherever the icon is)
    int input;                    // physical button state (edge detection)
    int noclip, sim;              // test aids: ignore deaths / running a look-ahead rollout
    int bot_strict;               // test bot: missing a form portal counts as a death
    const void *practice;         // GameCheckpoint to respawn at instead of the level start (or NULL)
    float ctr;                    // camera centre for restricted modes (OpenGD m_fCameraYCenter)
    Particle pt[GD_MAX_PART]; int npt;   // visual-only particles (not part of snapshots)
    float anim_t, emit_t;         // render time for idle animations, particle emit timer
    int dead_fx;                  // death burst already spawned
    int enter_fx;                 // id of the active enter effect trigger (0 = fade)
    Pulse pu[GD_MAX_PULSES]; int npu;
    AlphaFade af[GD_MAX_PULSES]; int naf;
    float ground_y, ceil_y;       // drawn floor line / ceiling (world units); ceil_y < 0 + flag below
    int has_ceil;
    int mirror;                   // mirror portal active
    float mir_x;                  // player screen x (view units), animated
    float time, cam_x, cam_bottom, cam_target;
    float acc;                    // unsimulated time (fixed 240 Hz steps)
    float prev_x, prev_y, prev_rot; // previous tick, for smooth interpolation
    // skin (set by game_set_skin)
    const GdFrame *fr_cube, *fr_cube2, *fr_ship, *fr_ship2;
    const GdFrame *fr_ball, *fr_ball2, *fr_ufo, *fr_ufo2, *fr_ufo3, *fr_dart, *fr_dart2, *fr_swing, *fr_swing2;
    float c1[3], c2[3];
    int skin_set;
    int attempts, finished;
    float best_percent, percent;
    char name[96];
    char death_info[96];   // what killed the player last (debug)
    const char *path;
} Game;

int game_load(Game *gm, Gfx *g, const char *path);
void game_free(Game *gm);
// Chooses the icons (player_NN / ship_NN) and the two player colours (0..255).
void game_set_skin(Game *gm, Gfx *g, int cube, int ship, const float c1[3], const float c2[3]);
void game_reset(Game *gm);
void game_set_hold(Game *gm, int down);
// Advances the simulation by dt seconds (internally in 1/240 s steps).
void game_update(Game *gm, float dt);
// Draws the world; (vw,vh) is the logical view size in world units (height 320).
void game_draw(Game *gm, Gfx *g, float vw, float vh, float scale, float ox, float oy,
               const GfxImage *bg, const GfxImage *ground);

// ---- look-ahead support for the test bot
typedef struct {
    Player p, p2;
    int dual, mirror, input;
    float ctr, cam_f, acc, time, prev_x, prev_y, prev_rot;
    Frame frame;
    float travel, timewarp;
    int finished, channel, free;
    int lo, n;                // range of by_x indices whose flags are saved
    unsigned char *flags;     // 4 bytes per object: used, used2, tdone, off
    int cap;
    float *mpos;              // movers: x, y, rotation
    unsigned char *mflags;    // movers: used, used2, tdone, off
    int nm, mcap;
    Move mv[GD_MAX_MOVES]; int nmv;
    PendingSpawn sp[GD_MAX_SPAWNS]; int nsp;
    int mark;                 // length of the undo log (look-ahead snapshots)
    Logic logic;
} GameSnap;
void game_snap_free(GameSnap *s);
void game_snap_save(const Game *gm, GameSnap *s, float x0, float x1);
void game_snap_restore(Game *gm, const GameSnap *s);
// Cheap snapshot for the look-ahead: only valid while the following changes are made by game_sim;
// objects it changes are logged and put back by game_rewind. Snapshots rewind like a stack.
void game_mark(const Game *gm, GameSnap *s);
void game_rewind(Game *gm, const GameSnap *s);
// Runs `frames` 60 Hz frames with the button held (1) or not (0); returns 0 alive, 1 dead, 2 level end.
int game_sim(Game *gm, int hold, int frames);

// Practice-style checkpoint: complete restorable state (player, flags, colours, camera).
typedef struct {
    int valid;
    GameSnap s;
    unsigned char *tdone;
    Channel *ch;
    float *galpha;
    float cam_bottom, ground_y, ceil_y, mir_x, cam_x, percent;
    int has_ceil;
} GameCheckpoint;
void game_checkpoint_free(GameCheckpoint *c);
void game_checkpoint_save(const Game *gm, GameCheckpoint *c);
void game_checkpoint_restore(Game *gm, const GameCheckpoint *c);
// Complete level state to / from a file (development aid for the test bot).
int game_state_io(Game *gm, const char *path, int save);

#endif
