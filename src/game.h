// SPDX-License-Identifier: GPL-3.0-or-later
// Game state: runtime objects, colour channels/triggers and player physics.
#ifndef GD_GAME_H
#define GD_GAME_H

#include "gfx.h"
#include "level.h"
#include "objchild.h"

typedef enum { OT_DECO, OT_SOLID, OT_HAZARD, OT_PORTAL, OT_PAD, OT_ORB, OT_SPEED, OT_TRIGGER, OT_HIDDEN } ObjType;

// Portal kinds (Obj.sub for OT_PORTAL)
enum { P_GRAV_DOWN = 1, P_GRAV_UP, P_CUBE, P_SHIP, P_BALL, P_UFO, P_WAVE, P_SIZE_NORMAL, P_SIZE_MINI,
       P_MIRROR_ON, P_MIRROR_OFF, P_DUAL_ON, P_DUAL_OFF, P_ROBOT, P_SPIDER, P_OTHER };
// Player modes
enum { M_CUBE = 0, M_SHIP, M_BALL, M_UFO, M_WAVE, M_ROBOT, M_SPIDER };
// Pad / orb kinds
enum { K_YELLOW = 1, K_BLUE, K_PINK, K_GREEN, K_RED };

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
    short grp[6];
    unsigned char ngrp, mover, off, spawn_only;
    float x0, y0;                  // initial position (movers are put back on every attempt)
    int tid;                       // trigger object id
    int tgroup, teasing, tact, tcopy; // target group, easing type, toggle: 1 = on, colour copy channel
    float tdx, tdy, trate, tdelay;
    int kidset;                    // index into Game.kidsets (child sprites), -1 = none
    float spin;                    // continuous rotation (degrees per second), saw blades
    const GdFrame *fx1, *fx2;      // portal swirl sprites
} Obj;

#define GD_GROUPS 1200
#define GD_MAX_PART 400
typedef struct { float x, y, vx, vy, life, max, size, grow, r, g, b, a; } Particle;
typedef struct { const GdChild *c; int n; const GdFrame *f[12]; } KidSet;
#define GD_MAX_MOVES 64
#define GD_MAX_SPAWNS 64
typedef struct { int group, easing; float dx, dy, dur, t, rate, done; } Move;
typedef struct { int obj; float left; } PendingSpawn;

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
    float rot, flame;
    float rot_left, rot_rate, rot_dir; // running 180 degree rotation action
    int snap;             // rotating back to a multiple of 90 degrees
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
    float galpha[GD_GROUPS];      // group opacity (alpha trigger)
    Move mv[GD_MAX_MOVES]; int nmv;
    PendingSpawn sp[GD_MAX_SPAWNS]; int nsp;
    int dual;
    int input;                    // physical button state (edge detection)
    int noclip, sim;              // test aids: ignore deaths / running a look-ahead rollout
    const void *practice;         // GameCheckpoint to respawn at instead of the level start (or NULL)
    float ctr;                    // camera centre for restricted modes (OpenGD m_fCameraYCenter)
    Particle pt[GD_MAX_PART]; int npt;   // visual-only particles (not part of snapshots)
    float anim_t, emit_t;         // render time for idle animations, particle emit timer
    int dead_fx;                  // death burst already spawned
    float ground_y, ceil_y;       // drawn floor line / ceiling (world units); ceil_y < 0 + flag below
    int has_ceil;
    int mirror;                   // mirror portal active
    float mir_x;                  // player screen x (view units), animated
    float time, cam_x, cam_bottom, cam_target;
    float acc;                    // unsimulated time (fixed 240 Hz steps)
    float prev_x, prev_y, prev_rot; // previous tick, for smooth interpolation
    // skin (set by game_set_skin)
    const GdFrame *fr_cube, *fr_cube2, *fr_ship, *fr_ship2;
    const GdFrame *fr_ball, *fr_ball2, *fr_ufo, *fr_ufo2, *fr_ufo3, *fr_dart, *fr_dart2;
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
    int lo, n;                // range of by_x indices whose flags are saved
    unsigned char *flags;     // 4 bytes per object: used, used2, tdone, off
    int cap;
    float *mpos;              // movers: x, y
    unsigned char *mflags;    // movers: used, used2, tdone, off
    int nm, mcap;
    Move mv[GD_MAX_MOVES]; int nmv;
    PendingSpawn sp[GD_MAX_SPAWNS]; int nsp;
} GameSnap;
void game_snap_free(GameSnap *s);
void game_snap_save(const Game *gm, GameSnap *s, float x0, float x1);
void game_snap_restore(Game *gm, const GameSnap *s);
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

#endif
