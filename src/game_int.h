// SPDX-License-Identifier: GPL-3.0-or-later
// What the parts of the engine share among each other: game.c (loading, physics, drawing),
// triggers.c (commands and logic) and motion.c (keyframes, areas, followers).
#ifndef GD_GAME_INT_H
#define GD_GAME_INT_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"

#define STEP (1.0f / 240.0f)

// ---- game.c
void gm_obj_scale(Obj *o, float kx, float ky);       // factors on size and hitbox
int gm_teleport(Game *gm, Player *p, int group);     // to the object of a group; 0 if it has none
void gm_add_gradient_state(Game *gm, int obj);       // a gradient trigger was fired
float gm_random(void);                               // 0..1, for looks only

// ---- triggers.c
float trg_prop(const Obj *o, int key, float def);    // a property of a trigger or special block
float trg_ease(int type, float t, float rate);       // the easing curves of the game, t in 0..1
void trg_touch(Game *gm, const Obj *o);              // before the look-ahead changes an object
const Obj *trg_first(const Game *gm, int group);     // the object standing for a group, or NULL
void trg_move_group(Game *gm, int group, float dx, float dy);
void trg_rotate_group(Game *gm, int group, int center, float deg);
void trg_scale_group(Game *gm, int group, int center, float kx, float ky);
int trg_group(const Game *gm, int group);            // a group number after the spawn remaps
void trg_activate(Game *gm, int group, int on, int depth);   // switch a group on or off
void trg_spawn(Game *gm, int group, int depth);      // carry out the triggers of a group
void trg_fire(Game *gm, Obj *t, int depth);
void trg_step(Game *gm);                             // one simulation step of all running commands
void trg_event(Game *gm, int event);                 // something happened that event triggers watch
void trg_death(Game *gm);                            // the icon has just crashed
void trg_channel_fade(Game *gm, const Obj *t);       // colour triggers
void trg_attempt(Game *gm);                          // a new attempt begins (after the reset)
void trg_keep(Game *gm);                             // before the reset: remember persistent items

float trg_tween(const Tween *w);
void trg_tween_to(Tween *w, float to, const Obj *t);   // glides there with the trigger's timing
void trg_camera_step(Camera *v, float dt);
float trg_static_pos(const Game *gm, int axis);

void gm_draw_object(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s);   // at a screen place

// ---- visual.c
void visual_load(Game *gm);                          // reads the particle settings of the objects
void visual_free(Game *gm);
void visual_reset(Game *gm);
int visual_fire(Game *gm, Obj *t, int group);        // 1 if the trigger was one of visual.c's
void visual_step(Camera *v, float dt);
void visual_middleground(Game *gm, Gfx *g, float ox, float oy, float w, float h, float margin, float scale);
// Particle objects (front: those in front of the icon) in the view that game_draw describes.
void visual_particles(Game *gm, Gfx *g, int front, float ox, float oy, float vw, float vh, float scale);
void visual_screen_objects(Game *gm, Gfx *g, float ox, float oy, float vw, float vh, float scale);
// Runs the shader effects; layered: only over what was drawn since gfx_layer_begin.
void visual_shaders(Game *gm, Gfx *g, float ox, float oy, float vw, float vh, float scale, int layered);

// ---- motion.c
void motion_load(Game *gm);                          // after the objects of a level are there
void motion_free(Game *gm);
void motion_reset(Game *gm);
// 1 if the trigger is one of motion.c's (then it has been carried out)
int motion_fire(Game *gm, Obj *t, int group);
void motion_move(Game *gm, Move *m);                 // MV_KEYFRAME and MV_ADVANCED
// For a picture: works out what areas and enter effects make of the objects between left and right.
void motion_areas(Game *gm, float left, float right);
// Applies that to a copy of object i; returns its opacity (1: unchanged) and sets gm->tinted.
float motion_apply(Game *gm, int i, Obj *o);
void motion_tint(const Game *gm, GdColorF *c);       // the colour change of gm->tinted
GdColorF gm_channel(const Game *gm, int id);         // the present colour of a channel (game.c)

// The icon's two boxes, both centred on it and never turned. The outer one (the size of the icon)
// meets spikes, portals, pads and orbs and finds the blocks it may land on; only the small inner one
// makes running into a block deadly. Sizes of the original: 30 (spider 27, wave 10), mini 60 %.
static inline float gd_body_half(const Player *p)
{
    const float h = p->mode == M_SPIDER ? 13.5f : p->mode == M_WAVE ? 5.0f : 15.0f;
    return p->mini ? h * 0.6f : h;
}
// 30 % of the full size, for the mini icon too
static inline float gd_inner_half(const Player *p) { return p->mode == M_SPIDER ? 4.05f : p->mode == M_WAVE ? 1.5f : 4.5f; }
// distance of the icon's centre from the level floor it rests on (the wave stays further away)
static inline float gd_floor_half(const Player *p)
{
    const float h = p->mode == M_SPIDER ? 13.5f : p->mode == M_WAVE ? 10.0f : 15.0f;
    return p->mini ? h * 0.6f : h;
}

#endif
