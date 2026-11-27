// SPDX-License-Identifier: GPL-3.0-or-later
// Game state: runtime objects, colour channels/triggers and player physics.
#ifndef GD_GAME_H
#define GD_GAME_H

#include "gfx.h"
#include "level.h"
#include "objchild.h"
#include "anim.h"
#include "config.h"

// animations loaded with every level
enum { AN_BEAST1, AN_BEAST2, AN_BEAST3, AN_BEAST4, AN_BEAST5, AN_ROBOT_RUN, AN_ROBOT_JUMP, AN_ROBOT_FALL,
       AN_SPIDER_RUN, AN_SPIDER_FALL, AN_BITE, AN_COUNT };

typedef enum { OT_DECO, OT_SOLID, OT_HAZARD, OT_PORTAL, OT_PAD, OT_ORB, OT_SPEED, OT_TRIGGER, OT_HIDDEN, OT_FORCE, OT_SENSOR, OT_HBLOCK, OT_COIN } ObjType;

// Portal kinds (Obj.sub for OT_PORTAL)
enum { P_GRAV_DOWN = 1, P_GRAV_UP, P_CUBE, P_SHIP, P_BALL, P_UFO, P_WAVE, P_SIZE_NORMAL, P_SIZE_MINI,
       P_MIRROR_ON, P_MIRROR_OFF, P_DUAL_ON, P_DUAL_OFF, P_ROBOT, P_SPIDER, P_OTHER, P_TELEPORT, P_SWING,
       P_GRAV_TOGGLE };
// Player modes
enum { M_CUBE = 0, M_SHIP, M_BALL, M_UFO, M_WAVE, M_ROBOT, M_SPIDER, M_SWING };
// Invisible helper blocks (Obj.sub for OT_HBLOCK): H forgives hitting the head, F turns gravity
// around then, D lets the wave slide on blocks, S ends a dash.
enum { SB_HEAD, SB_FLIP, SB_DART, SB_STOPDASH };
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
#define CH_GROUND2 1009
#define CH_BLACK 1010
#define CH_WHITE 1011
#define CH_LIGHTER 1012

typedef struct {
    float r, g, b, a;    // current value (0..255 rgb)
    int additive;
    // fade state
    float fr, fg, fb, fa, tr, tg, tb, ta, t, dur;
    int fading;
    int copy, copy_op, player;       // copy another channel (+ its opacity) / follow player colour 1 or 2
    int next_player;                 // player colour the running fade ends in
    int hsv_on, sadd, vadd;
    int lbg;                         // default "light background" channel: mix of background and colour 1
    float hs, ss, vs;                // hue shift (degrees), saturation and value factor/offset
} Channel;

// The properties of an object as the level stores them (see GdObject).
typedef struct {
    int nprops;
    char *text;                    // key 442, 430 or 145
    GdProp prop[];
} TData;

// What a special block does when the icon touches it (Obj.sub for OT_SENSOR; 0: a collision block)
enum { SN_BLOCK, SN_STATE, SN_TOGGLE, SN_NOBUFFER };

// An area or enter trigger at work: how strongly it changes an object depends on the object's
// distance from a centre object (area) or from the screen edge (enter). Looks only.
enum { AREA_MOVE, AREA_ROTATE, AREA_SCALE, AREA_FADE, AREA_TINT };
typedef struct {
    int src;                       // its trigger
    short group, center, effect, channel, colour;
    unsigned char kind, enter;     // enter: 1 an enter effect (channel), 0 an area (group)
    unsigned char dir, xy, relative, inwards, ease_out, side;
    float length, offset, front, back, dead, rate, rate2;
    float dist, angle, mx, my, rot, sx, sy, to_alpha, from_alpha, tint;
    short ease, ease2;
} AreaFx;
#define GD_MAX_AREAS 32
struct GdTrack;                    // a keyframe animation (motion.c)
struct GdAreaObj;                  // what the areas make of one object (motion.c)

typedef struct {
    int id;
    float x, y, rot, sx, sy;       // world position, rotation, signed scale (flip)
    ObjType type;
    int sub;                       // portal/pad/orb kind or speed index
    const GdFrame *main, *detail, *back, *extra;
    int ch_main, ch_detail;
    int zlayer, zorder;
    float hw, hh, hx, hy;          // hitbox half size and centre offset (world units, rotation/flip baked in)
    float bw, bh, tilt;            // a box turned by an odd angle: its own half size and the angle (radians,
                                   // counterclockwise; 0: the box is hw x hh and lies straight)
    float rad;                     // >0: circular hazard hitbox radius (centred on x,y)
    int used, used2;               // already triggered by player 1 / player 2
    // trigger data
    float tr, tg, tb, tdur, topacity;
    int tblend, ttarget, tdone;
    // groups, moving objects and the extra trigger types (move, toggle, alpha, spawn)
    short grp[GD_MAX_GROUPS];
    short hsv1, hsv2;              // 1 + index into Game.hsv: own colour shift of main / detail (0 = none)
    short copy_hsv;                // the same for the colour a trigger copies from another channel
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
    unsigned char invisible;       // "invisible" block / spike: only shows away from the icon
    unsigned char notouch;         // placed with "no touch": collides with nothing
    unsigned char fill;            // shaded filler square: never blended additively
    unsigned char faint;           // black decoration that only shades what is behind it
    unsigned char beat;            // sized by the music: 1 = decoration ball, 2 = orb
    unsigned char solid_fill;      // block with an opaque black filling: no transition at the screen edges
    unsigned char two_colour;      // has a base and a detail colour by default
    unsigned char lighter;         // its detail colour is the "lighter" shade
    unsigned char no_fade, no_enter;   // set by the level: no fading / no movement at the screen edges
    unsigned char tgrav;           // 2.2 teleport: 1 = normal gravity, 2 = flipped, 3 = toggle at the exit
    float tforce;                  // 2.2 teleport: upward speed the icon leaves the exit with (static force)
    const GdFrame *glow;           // soft light drawn behind the object
    const GdFrame *ball;           // pulsing ball above a rod
    unsigned char breakable;       // solid that breaks instead of killing
    unsigned char clipw;           // textured slope decoration: width of the triangle its sprites are cut to
    unsigned char slope;           // triangle: 0 = none, else 1 | 2 (right angle on the right) | 4 (ceiling)
    const GdFrame *fx1, *fx2;      // portal swirl sprites
    // 2.2 effects: animated sprites in two layers
    unsigned char effect;          // number in Game.effect + 1, 0 = none
    unsigned char fx_trigger;      // only plays (once) when an animate trigger starts it
    int fx_run;                    // attempt in which it was started ...
    float fx_at, fx_speed;         // ... and when; speed factor of the frames
    float kx, ky;                  // factors scale triggers have applied to size and hitbox (1 = none)
    TData *data;                   // every property, kept for triggers and special objects (else NULL)
    unsigned char enter_ch;        // enter effect channel
    unsigned char on_screen;       // fixed to the screen by a UI trigger: not drawn in the level
} Obj;

// A particle object (2065) or its settings, from the string of key 145.
#define PARTICLE_FIELDS 72
typedef struct { int obj; float v[PARTICLE_FIELDS]; } Emitter;
#define GD_MAX_BURSTS 24
typedef struct { int emitter; float x, y, rot, scale, start; } Burst;   // a spawn particle trigger

// Sounds the level asks for; the application plays them.
enum { SND_SFX, SND_SFX_EDIT, SND_SONG, SND_SONG_EDIT };
typedef struct {
    unsigned char type, loop, stop, set_volume;
    int id, uid, group;                       // sound or song number; unique id and sound group
    float volume, speed, start, end, fade_in, fade_out;   // times in seconds
} Sound;
#define GD_MAX_SOUNDS 32

// A value of a 2.2 trigger that glides from one setting to the next.
typedef struct { float from, to, t, dur, rate; int ease; } Tween;
enum { SH_SHOCKWAVE, SH_SHOCKLINE, SH_GLITCH, SH_CHROMATIC, SH_CHROMAGLITCH, SH_PIXELATE, SH_LENS, SH_RADIAL,
       SH_MOTION, SH_BULGE, SH_PINCH, SH_GREY, SH_SEPIA, SH_INVERT, SH_HUE, SH_COLOUR, SH_SPLIT, SH_COUNT };
#define GD_SHADER_VALUES 12
// One shader effect: its gliding settings (what each stands for: visual.c) and what does not glide.
typedef struct {
    Tween v[GD_SHADER_VALUES];
    float clock;                  // waves and line glitches: their own time, running at their speed
    float x, y;                   // centre taken from an object that is not followed
    int center, channel;          // group at the effect's centre, colour channel of its tint
    unsigned char on, player;     // centred on icon 1 or 2 (0: not)
    unsigned char flags;          // SF_ bits
} Shader;
enum { SF_RELATIVE = 1, SF_INVERT = 2, SF_UPRIGHT = 4, SF_FLIP = 8, SF_DUAL = 16, SF_TINT = 32, SF_HARD = 64,
       SF_FIXED = 128 };
// What the camera triggers (and those changing how the picture looks) have set.
typedef struct {
    Tween zoom, offx, offy, turn;   // zoom factor, offset in units, rotation in degrees clockwise
    Tween shiftx, shifty;           // gameplay offset: moves the icon's place on the screen
    // static camera, per axis (0: x, 1: y): weight against the following camera, the glide from the
    // former standpoint to the object of the target group
    Tween hold[2], move[2];
    int target[2];
    float from[2];
    Tween bound[4];                 // weight of the edges left, right, up, down
    int edge[4];                    // group whose object bounds the view there, 0 = none
    int hidden;                     // the icon is not shown
    int bg_id, ground_id;           // background and ground the picture uses
    // middleground: which one, its height, how fast it and the background scroll
    int mg_id;
    Tween mg_y;
    float mg_mx, mg_my, bg_mx;
    Shader shader[SH_COUNT];        // shader effects, by SH_ number
    int shader_bottom, shader_top;  // layers they reach (setup trigger; numbers: see game_draw), top 0: all
    // groups fixed to the screen (UI trigger)
    struct { short group, ref; float ax, ay; } ui[4]; int nui;
} Camera;

#define GD_MAX_GRADIENTS 16        // gradient triggers shown at the same time (by their id)
typedef struct {                   // data of one gradient trigger
    int obj, id, layer, blend, ch1, ch2, off, vertex;
    int side[4];                   // groups of the objects marking up, down, left, right (0: screen edge)
    float rot;
} Gradient;

#define GD_GROUPS 10000
#define GD_MAX_PART 400
#define EFFECT_FRAMES 16
typedef struct {
    int id, n;   // object id, number of frames
    const GdFrame *main[EFFECT_FRAMES], *color[EFFECT_FRAMES];
} Effect;

typedef struct { float x, y, vx, vy, life, max, size, grow, r, g, b, a; int square; } Particle;
typedef struct { const GdChild *c; int n; const GdFrame *f[12]; } KidSet;
#define GD_MAX_MOVES 128
#define GD_MAX_SPAWNS 128
#define GD_REMAP_DEPTH 4
// the spawn triggers a command was started through: their group remaps hold for it
typedef struct { int spawner[GD_REMAP_DEPTH]; unsigned char n; } Chain;

// MV_PLAIN: move by (dx, dy), possibly locked to the icon or the camera; MV_ROTATE: rot degrees
// around the object of group center; MV_FOLLOW: with the object of group follow (dx, dy factors);
// MV_SCALE: dx, dy factors; MV_PLAYER_Y: dx speed, dy offset, rot limit; MV_TARGET: until the
// object of group center (or of the moved group) reaches that of group follow (or the icon);
// MV_KEYFRAME and MV_ADVANCED keep their state in the fields their code names.
enum { MV_PLAIN, MV_ROTATE, MV_FOLLOW, MV_SCALE, MV_PLAYER_Y, MV_TARGET, MV_KEYFRAME, MV_ADVANCED };
typedef struct {
    int group, easing, center, follow;
    int src;                       // object index of the trigger that started it
    float dx, dy, dur, t, rate, done, rot, lx, ly;   // lx, ly: last icon position (locked moves)
    float cx, cy;                  // last camera position (moves locked to it)
    float modx, mody;              // factors of the locked movement
    unsigned char kind, lockx, locky, camx, camy, paused, axis, player;
    Chain chain;
} Move;
typedef struct { int obj, src; float left; unsigned char paused; Chain chain; } PendingSpawn;
#define GD_MAX_ITEMS 64
#define GD_MAX_WATCH 64
#define GD_MAX_TIMERS 16
#define GD_MAX_LISTEN 32

// options switched by the options trigger
enum { OPT_HIDE_GROUND = 1, OPT_HIDE_MG = 2, OPT_HIDE_P1 = 4, OPT_HIDE_P2 = 8, OPT_FREE_DUAL = 16,
       OPT_HIDE_ATTEMPTS = 32, OPT_NO_DEATH_SOUND = 64 };

typedef struct {            // state of the 2.2 logic triggers
    short item[GD_MAX_ITEMS]; int value[GD_MAX_ITEMS]; int nitem;   // item counters
    // running count / collision / touch triggers
    struct { int obj, state; Chain chain; } watch[GD_MAX_WATCH]; int nwatch;
    unsigned rng;
    struct { short group, on; } death[8]; int ndeath;   // groups switched when the icon dies
    struct { short id, group; unsigned char on, check; float value, rate, stop; Chain chain; } timer[GD_MAX_TIMERS];
    int ntimer;
    struct { short item, group; unsigned char multi, done; float at; Chain chain; } at_time[GD_MAX_LISTEN];
    int nat;
    struct { unsigned mask[3]; short group; int src; Chain chain; } listen[GD_MAX_LISTEN]; int nlisten;
    unsigned options;
    float respawn;          // seconds from a crash to the next attempt, 0: the usual
} Logic;
typedef struct { int obj; float x, y, rot, kx, ky; unsigned char used, used2, tdone, off; } UndoEntry;
#define GD_GHOSTS 8
typedef struct { float x, y, rot, age; int mode, mini, grav; } Ghost;   // one image of the icon's ghost trail
#define GD_MAX_PULSES 96
typedef struct {   // pulse trigger: a colour flashed onto a channel or a group (visual only)
    int target, group, copy, only;      // only: 1 = main sprite, 2 = detail sprite, 0 = both
    float r, g, b, hue, in, hold, out, t;
} Pulse;
typedef struct { int group; float from, to, dur, t; } AlphaFade;

#define GD_MAX_COINS 3
// A coin on its way out of the level, after it was picked up (looks only; age 0: none).
typedef struct { float x, y, age, drift; unsigned char user, owned; } CoinFly;

typedef struct {
    float x, y, vy;  // vy: px/s, derived from yv (for display/bots only)
    double yv;       // OpenGD y velocity (units per 60 Hz frame, before the 0.9 slow factor)
    int mode;        // 0 cube, 1 ship
    int grav;        // +1 normal, -1 flipped
    int mini;
    float speed;     // units per second
    int on_ground, dead, hold;
    int rising, queued;   // OpenGD m_isRising, _queuedHold
    float prev_y;         // y before this step's movement (landing on blocks)
    int ceil_obj;         // 1 + block the icon bumped into with its upper side in the last step
    int fresh;            // the button went down since the last step
    float flip_grace, spider_grace;   // seconds left in which blocks forgive: after a gravity flip / spider jump
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
    float trail[64][2];   // wave trail and streak, ring buffer
    int trail_n, trail_i, trail_tick;
    // looks only: the streak behind the icon after orbs and pads and while dashing, and the flame,
    // the burst at the start and the flame flying off at the end of a dash (ages in seconds)
    int streak;
    float streak_idle;                      // since the streak got its last point
    float dash_age;                         // since the dash began, 0: none
    float boom_age, boom_x, boom_y;         // burst: age (0 none, < 0 about to start) and place
    float off_age, off_x, off_y;            // flame flying off
    float dash_angle, off_angle;            // direction on the screen, degrees clockwise
    int boom_flip;                          // the burst of a gravity dash orb
    float slope_t, slope_rot;     // resting on a slope: time left in which that holds, the icon's tilt
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
    const GfxImage *ground2;      // second layer of the ground texture (2.1 grounds), set by the app
    Anim *anim;                   // AN_COUNT entries (monsters, robot, spider)
    struct { int id, n; const GdFrame *main[16], *kid[16]; } loops[8]; int nloops;   // looping fires
    Effect effect[32]; int neffects;   // 2.2 effect animations used by this level
    int ball_type;                // look of the music balls on rods (picked per level load, 1..3)
    float end_x;
    int start_mode, start_speed;      // header: kA2 form, kA4 speed ...
    int start_mini, start_dual, start_flip, start_mirror, start_group;   // ... kA3, kA8, kA11, kA28, kA36
    float start_y;                    // height at which the icon appears
    int fix_radius;                   // header kA39: round hitboxes compare the distance of the centres
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
    Chain chain;                  // remaps of the command being carried out
    int *sensors, nsensors;       // state, toggle and stop-jump blocks
    AreaFx area[GD_MAX_AREAS]; int narea;
    struct GdTrack *tracks; int ntracks;
    struct GdAreaObj *areaobj; unsigned area_stamp;
    const struct GdAreaObj *tinted;   // the object being drawn, if an area changes its colour
    Emitter *emitters; int nemitters;
    Burst bursts[GD_MAX_BURSTS]; int nburst;
    Sound snd[GD_MAX_SOUNDS]; int nsnd;   // asked for since the application last looked
    int mg_id0, mg_used[3], nmg_used;     // middleground of the header and those triggers change to
    const GfxImage *mg, *mg2;             // its two layers, set by the application
    // items and timers the level keeps from one attempt to the next
    struct { short id; unsigned char timer; float value; } keep[16]; int nkeep;
    int local;                   // the icon is currently given in the turned movement frame
    int head_safe;                // the icon being checked is inside an H block: head bumps are harmless
    int head_flip, dart_safe;     // ... inside an F block / a D block
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
    int draw_under;               // drawing pass for the colour sprites that lie below their layer
    Ghost ghost[GD_GHOSTS]; int ghost_on, ghost_next; float ghost_t;
    float shake_left, shake_amp, shake_every;   // camera shake: seconds left, units, seconds per jolt
    int dead_fx;                  // death burst already spawned
    int enter_fx;                 // id of the active enter effect trigger (0 = fade)
    Pulse pu[GD_MAX_PULSES]; int npu;
    AlphaFade af[GD_MAX_PULSES]; int naf;
    float ground_y, ceil_y;       // drawn floor line / ceiling (world units); ceil_y < 0 + flag below
    int has_ceil;
    int mirror;                   // mirror portal active
    float mir_x;                  // player screen x (view units), animated
    float mir_t;                  // anim_t when it was last moved
    Camera view;                  // camera triggers
    short grad[GD_MAX_GRADIENTS]; // gradient shown per id: 1 + index into gradients, 0 = none
    Gradient *gradients; int ngradients;
    struct { float x, bottom, vh, scale, ox, oy, w, turn_cos, turn_sin; int mirror; } pic;   // the last picture
    int bg_id0, ground_id0;       // background and ground of the header
    int bg_used[8], ground_used[8], nbg_used, nground_used;   // those the level's triggers switch to
    float time, cam_x, cam_bottom, cam_target;
    float acc;                    // unsimulated time (fixed 240 Hz steps)
    float prev_x, prev_y, prev_rot; // previous tick, for smooth interpolation
    // skin (set by game_set_skin)
    int icon[IC_COUNT];
    float c1[3], c2[3];
    int skin_set;
    int attempts, finished;
    int run;              // counts the resets, so the song can start again with each attempt
    float song_offset;    // seconds into the song at which the level starts (header kA13)
    float pulse;          // how strongly the music beats, 0..1; below 0 without music
    float loud;           // average loudness of the music, 0..1; below 0 without music
    int jumps;                    // button presses of this attempt (end screen)
    float best_percent, percent;
    // the level's coins in their order, those the player owned before (bits, set by the application)
    // and the ones being picked up
    int coin_obj[GD_MAX_COINS], ncoins, coins_owned;
    CoinFly coin_fly[GD_MAX_COINS];
    char name[96];
    char death_info[96];   // what killed the player last (debug)
    const char *path;
} Game;

// data: packed level string that is already in memory (then path only names the level), or NULL.
int game_load(Game *gm, Gfx *g, const char *path, const char *data);
void game_free(Game *gm);
// Chooses the icons (player_NN / ship_NN) and the two player colours (0..255).
void game_set_skin(Game *gm, const int icon[IC_COUNT], const float c1[3], const float c2[3]);
void game_reset(Game *gm);
void game_set_hold(Game *gm, int down);
// Advances the simulation by dt seconds (internally in 1/240 s steps).
void game_update(Game *gm, float dt);
// Draws the world; (vw,vh) is the logical view size in world units (height 320).
void game_draw(Game *gm, Gfx *g, float vw, float vh, float scale, float ox, float oy,
               const GfxImage *bg, const GfxImage *ground);
// The coins picked up in this attempt, as bits (bit 0: the first one).
int game_coins(const Game *gm);
// Where a point of the level lies in the last picture.
void game_screen_pos(const Game *gm, float x, float y, float *sx, float *sy);

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
    float *mpos;              // movers: x, y, rotation, scale factors
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
    Camera view;
    short grad[GD_MAX_GRADIENTS];
    AreaFx area[GD_MAX_AREAS]; int narea;
} GameCheckpoint;
void game_checkpoint_free(GameCheckpoint *c);
void game_checkpoint_save(const Game *gm, GameCheckpoint *c);
void game_checkpoint_restore(Game *gm, const GameCheckpoint *c);
// Complete level state to / from a file (development aid for the test bot).
int game_state_io(Game *gm, const char *path, int save);

#endif
