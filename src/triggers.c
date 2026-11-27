// SPDX-License-Identifier: GPL-3.0-or-later
// Triggers: what a level's commands do. Property numbers are those of the level format (several of
// the rules were worked out from level data and public key tables, not checked in the original).
#include <stdio.h>

#include "game_int.h"

float trg_prop(const Obj *o, int key, float def)
{
    if (o->data)
        for (int i = 0; i < o->data->nprops; i++)
            if (o->data->prop[i].key == key) return o->data->prop[i].val;
    return def;
}

static int prop_int(const Obj *o, int key, int def) { return (int)trg_prop(o, key, (float)def); }

static int index_of(const Game *gm, const Obj *o) { return (int)(o - gm->obj); }

// ---------------------------------------------------------------- easing

// bounce curve running out of 0 towards 1
static float bounce(float t)
{
    if (t < 1.0f / 2.75f) return 7.5625f * t * t;
    if (t < 2.0f / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
    if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
    t -= 2.625f / 2.75f;
    return 7.5625f * t * t + 0.984375f;
}

static float elastic_out(float t, float period)
{
    return powf(2.0f, -10.0f * t) * sinf((t - period / 4.0f) * 6.2831853f / period) + 1.0f;
}

// 0 none, 1-3 ease in-out / in / out (power `rate`), then in the order in-out, in, out:
// 4-6 elastic (period `rate`), 7-9 bounce, 10-12 exponential, 13-15 sine, 16-18 back
float trg_ease(int type, float t, float rate)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (rate < 0.1f) rate = 2.0f;
    const float pi = 3.14159265f, back = 1.70158f;
    switch (type) {
    case 0: return t;
    case 1: return t < 0.5f ? 0.5f * powf(2.0f * t, rate) : 1.0f - 0.5f * powf(2.0f * (1.0f - t), rate);
    case 2: return powf(t, rate);
    case 3: return powf(t, 1.0f / rate);   // the original's "ease out" takes the root
    case 4: {
        const float p = rate * 1.5f;
        return t < 0.5f ? 0.5f * (1.0f - elastic_out(1.0f - 2.0f * t, p)) : 0.5f + 0.5f * elastic_out(2.0f * t - 1.0f, p);
    }
    case 5: return 1.0f - elastic_out(1.0f - t, rate);
    case 6: return elastic_out(t, rate);
    case 7: return t < 0.5f ? 0.5f * (1.0f - bounce(1.0f - 2.0f * t)) : 0.5f + 0.5f * bounce(2.0f * t - 1.0f);
    case 8: return 1.0f - bounce(1.0f - t);
    case 9: return bounce(t);
    case 10: return t < 0.5f ? 0.5f * powf(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * powf(2.0f, -20.0f * t + 10.0f);
    case 11: return powf(2.0f, 10.0f * (t - 1.0f));
    case 12: return 1.0f - powf(2.0f, -10.0f * t);
    case 13: return -0.5f * (cosf(pi * t) - 1.0f);
    case 14: return 1.0f - cosf(t * pi * 0.5f);
    case 15: return sinf(t * pi * 0.5f);
    case 16: {
        const float s = back * 1.525f, u = 2.0f * t;
        return u < 1.0f ? 0.5f * u * u * ((s + 1.0f) * u - s) : 0.5f * ((u - 2.0f) * (u - 2.0f) * ((s + 1.0f) * (u - 2.0f) + s) + 2.0f);
    }
    case 17: return t * t * ((back + 1.0f) * t - back);
    case 18: { const float u = t - 1.0f; return 1.0f + u * u * ((back + 1.0f) * u + back); }
    default: return t;
    }
}

// ---------------------------------------------------------------- objects and groups

void trg_touch(Game *gm, const Obj *o)
{
    if (!gm->sim) return;
    if (gm->nundo == gm->capundo) {
        const int cap = gm->capundo ? gm->capundo * 2 : 4096;
        UndoEntry *u = realloc(gm->undo, (size_t)cap * sizeof *u);
        if (!u) return;
        gm->undo = u;
        gm->capundo = cap;
    }
    gm->undo[gm->nundo++] = (UndoEntry){index_of(gm, o), o->x, o->y, o->rot, o->kx, o->ky, (unsigned char)o->used,
                                        (unsigned char)o->used2, (unsigned char)o->tdone, o->off};
}

static int group_valid(const Game *gm, int group)
{
    return group > 0 && group < GD_GROUPS && gm->grp_start[group] < gm->grp_start[group + 1];
}

const Obj *trg_first(const Game *gm, int group)
{
    return group_valid(gm, group) ? &gm->obj[gm->grp_items[gm->grp_start[group]]] : NULL;
}

// pictures only: the look-ahead of the test bot leaves them alone
static int scenery(const Obj *o) { return o->type == OT_DECO || o->type == OT_HIDDEN || o->notouch; }

void trg_move_group(Game *gm, int group, float dx, float dy)
{
    if (!group_valid(gm, group)) return;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (gm->sim && scenery(o)) continue;
        trg_touch(gm, o);
        o->x += dx;
        o->y += dy;
    }
}

// Rotates the objects of a group (clockwise degrees) around the first object of the centre group.
void trg_rotate_group(Game *gm, int group, int center, float deg)
{
    if (!group_valid(gm, group)) return;
    if (!group_valid(gm, center)) center = group;
    const Obj *pivot = trg_first(gm, center);
    const float cx = pivot->x, cy = pivot->y;
    const float r = deg * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (gm->sim && scenery(o)) continue;
        trg_touch(gm, o);
        const float dx = o->x - cx, dy = o->y - cy;
        o->x = cx + dx * c + dy * s;
        o->y = cy - dx * s + dy * c;
        o->rot += deg;
    }
}

// Scales the objects of a group, their distances from the centre object too (if there is one).
void trg_scale_group(Game *gm, int group, int center, float kx, float ky)
{
    if (!group_valid(gm, group)) return;
    const Obj *pivot = trg_first(gm, center);
    const float cx = pivot ? pivot->x : 0, cy = pivot ? pivot->y : 0;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (gm->sim && scenery(o)) continue;
        trg_touch(gm, o);
        if (pivot) {
            o->x = cx + (o->x - cx) * kx;
            o->y = cy + (o->y - cy) * ky;
        }
        gm_obj_scale(o, kx, ky);
    }
}

// a factor on its way from 1 to k; never exactly 0, which could not be undone
static float scale_part(float k, float done)
{
    const float v = 1.0f + (k - 1.0f) * done;
    return fabsf(v) < 0.02f ? (v < 0 ? -0.02f : 0.02f) : v;
}

// ---------------------------------------------------------------- spawn chains

// The group a number stands for after the remaps of the spawn triggers in the running chain.
int trg_group(const Game *gm, int group)
{
    for (int i = gm->chain.n - 1; i >= 0; i--) {
        const TData *d = gm->obj[gm->chain.spawner[i]].data;
        if (!d || !d->text) continue;
        for (const char *p = d->text; *p;) {   // "from.to.from.to..."
            const int from = atoi(p);
            while (*p && *p != '.') p++;
            if (*p) p++;
            const int to = atoi(p);
            while (*p && *p != '.') p++;
            if (*p) p++;
            if (from == group) { group = to; break; }
        }
    }
    return group;
}

// Switches the objects of a group on (their triggers fire) or off.
void trg_activate(Game *gm, int group, int on, int depth)
{
    if (!group_valid(gm, group)) return;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (!gm->sim || !scenery(o)) { trg_touch(gm, o); o->off = on ? 0 : 1; }
        if (on && o->type == OT_TRIGGER && o->spawn_only && depth < 8) trg_fire(gm, o, depth + 1);
    }
}

// Carries out the triggers of a group (as a spawn trigger does).
void trg_spawn(Game *gm, int group, int depth)
{
    if (depth >= 8 || !group_valid(gm, group)) return;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (o->type == OT_TRIGGER && !o->off) trg_fire(gm, o, depth + 1);
    }
}

static unsigned logic_random(Game *gm)   // part of the state, so the look-ahead sees the same dice
{
    unsigned x = gm->logic.rng ? gm->logic.rng : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return gm->logic.rng = x;
}

static float random_01(Game *gm) { return (float)(logic_random(gm) % 10000) / 10000.0f; }

// The spawn trigger: its group after a delay, in the order of the level if it asks for that.
static void spawn_trigger(Game *gm, Obj *t, int group, int depth)
{
    if (depth >= 8 || !group_valid(gm, group)) return;
    Chain chain = gm->chain;
    if (t->data && t->data->text && chain.n < GD_REMAP_DEPTH) chain.spawner[chain.n++] = index_of(gm, t);
    float delay = t->tdelay;
    const float spread = trg_prop(t, 556, 0);
    if (spread != 0) delay += spread * (2.0f * random_01(gm) - 1.0f);
    const int ordered = prop_int(t, 441, 0) != 0;
    float first = 1e30f;
    for (int k = gm->grp_start[group]; ordered && k < gm->grp_start[group + 1]; k++) {
        const Obj *o = &gm->obj[gm->grp_items[k]];
        if (o->type == OT_TRIGGER && o->x0 < first) first = o->x0;
    }
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        const int oi = gm->grp_items[k];
        Obj *o = &gm->obj[oi];
        if (o->type != OT_TRIGGER) continue;
        // ordered: as far apart in time as an icon at normal speed would reach them
        const float wait = delay + (ordered ? (o->x0 - first) / 311.58f : 0);
        if (wait > 0.001f) {
            if (gm->nsp < GD_MAX_SPAWNS) gm->sp[gm->nsp++] = (PendingSpawn){oi, index_of(gm, t), wait, 0, chain};
        } else if (!o->off) {
            const Chain before = gm->chain;
            gm->chain = chain;
            trg_fire(gm, o, depth + 1);
            gm->chain = before;
        }
    }
}

// ---------------------------------------------------------------- camera

float trg_tween(const Tween *w)
{
    if (w->dur <= 0 || w->t >= w->dur) return w->to;
    return w->from + (w->to - w->from) * trg_ease(w->ease, w->t / w->dur, w->rate);
}

// Starts a glide from `from` to `to` with the duration and easing of a trigger.
static void tween_start(Tween *w, float from, float to, const Obj *t)
{
    *w = (Tween){from, to, 0, t->tdur, t->trate, t->teasing};
}

void trg_tween_to(Tween *w, float to, const Obj *t) { tween_start(w, trg_tween(w), to, t); }

static void tween_step(Tween *w, float dt)
{
    if (w->t < w->dur) w->t += dt;
}

void trg_camera_step(Camera *v, float dt)
{
    tween_step(&v->zoom, dt);
    tween_step(&v->offx, dt);
    tween_step(&v->offy, dt);
    tween_step(&v->shiftx, dt);
    tween_step(&v->shifty, dt);
    tween_step(&v->turn, dt);
    for (int i = 0; i < 2; i++) { tween_step(&v->hold[i], dt); tween_step(&v->move[i], dt); }
    for (int i = 0; i < 4; i++) tween_step(&v->bound[i], dt);
    visual_step(v, dt);
}

// Where a static camera stands on one axis (0: x, 1: y).
float trg_static_pos(const Game *gm, int axis)
{
    const Camera *v = &gm->view;
    const Obj *o = trg_first(gm, v->target[axis]);
    if (!o) return v->from[axis];
    return v->from[axis] + ((axis ? o->y : o->x) - v->from[axis]) * trg_tween(&v->move[axis]);
}

static void camera_static(Game *gm, const Obj *t, int group)
{
    Camera *v = &gm->view;
    for (int axis = 0; axis < 2; axis++) {
        if (t->ttarget == (axis ? 1 : 2)) continue;   // the trigger holds only the other axis
        if (t->tact) {                                // back to the camera that follows the icon
            trg_tween_to(&v->hold[axis], 0, t);
            continue;
        }
        if (!group_valid(gm, group)) continue;
        const int held = trg_tween(&v->hold[axis]) > 0.0001f;
        v->from[axis] = held ? trg_static_pos(gm, axis) : 0;
        v->target[axis] = group;
        if (held) tween_start(&v->move[axis], 0, 1, t);       // from the former standpoint
        else v->move[axis] = (Tween){1, 1, 0, 0, 0, 0};
        trg_tween_to(&v->hold[axis], 1, t);
    }
}

// ---------------------------------------------------------------- items and timers

static int *item_slot(Game *gm, int item)
{
    Logic *l = &gm->logic;
    for (int i = 0; i < l->nitem; i++)
        if (l->item[i] == item) return &l->value[i];
    if (l->nitem >= GD_MAX_ITEMS) return NULL;
    l->item[l->nitem] = (short)item;
    l->value[l->nitem] = 0;
    return &l->value[l->nitem++];
}

static int timer_slot(Game *gm, int id, int create)
{
    Logic *l = &gm->logic;
    for (int i = 0; i < l->ntimer; i++)
        if (l->timer[i].id == id) return i;
    if (!create || l->ntimer >= GD_MAX_TIMERS) return -1;
    memset(&l->timer[l->ntimer], 0, sizeof l->timer[0]);
    l->timer[l->ntimer].id = (short)id;
    l->timer[l->ntimer].rate = 1;
    return l->ntimer++;
}

// A value by its kind: 1 item, 2 timer, 3 points (none here), 4 level time, 5 attempts.
static float read_value(Game *gm, int kind, int id)
{
    if (kind == 2) {
        const int i = timer_slot(gm, id, 0);
        return i < 0 ? 0 : gm->logic.timer[i].value;
    }
    if (kind == 3) return 0;
    if (kind == 4) return gm->time;
    if (kind == 5) return (float)gm->attempts;
    const int *v = item_slot(gm, id);
    return v ? (float)*v : 0;
}

// operations: 1 add, 2 subtract, 3 multiply, 4 divide, else the new value
static float operate(int op, float a, float b)
{
    switch (op) {
    case 1: return a + b;
    case 2: return a - b;
    case 3: return a * b;
    case 4: return b != 0 ? a / b : 0;
    default: return b;
    }
}

// rounding (1 nearest, 2 down, 3 up), then sign (1 absolute, 2 negated absolute)
static float shape(float v, int rounding, int sign)
{
    if (rounding == 1) v = floorf(v + 0.5f);
    else if (rounding == 2) v = floorf(v);
    else if (rounding == 3) v = ceilf(v);
    if (sign == 1) v = fabsf(v);
    else if (sign == 2) v = -fabsf(v);
    return v;
}

static void item_edit(Game *gm, const Obj *t, int target)
{
    const int k1 = prop_int(t, 476, 0), k2 = prop_int(t, 477, 0), kt = prop_int(t, 478, 1);
    const float a = k1 ? read_value(gm, k1, prop_int(t, 80, 0)) : 0;
    const float b = k2 ? read_value(gm, k2, prop_int(t, 95, 0)) : 0;
    const float mod = trg_prop(t, 479, 1.0f);
    float v;
    if (k1 && k2) v = operate(prop_int(t, 482, 3), operate(prop_int(t, 481, 1), a, b), mod);
    else if (k1 || k2) v = operate(prop_int(t, 482, 3), k1 ? a : b, mod);
    else v = mod;
    v = shape(v, prop_int(t, 485, 0), prop_int(t, 578, 0));
    const int assign = prop_int(t, 480, 0);
    if (kt == 2) {
        const int i = timer_slot(gm, target, 1);
        if (i >= 0)
            gm->logic.timer[i].value = shape(assign ? operate(assign, gm->logic.timer[i].value, v) : v,
                                             prop_int(t, 486, 0), prop_int(t, 579, 0));
    } else {
        int *p = item_slot(gm, target);
        if (p) *p = (int)shape(assign ? operate(assign, (float)*p, v) : v, prop_int(t, 486, 0), prop_int(t, 579, 0));
    }
}

static int item_compare(Game *gm, const Obj *t)
{
    float side[2];
    for (int i = 0; i < 2; i++) {
        const int kind = prop_int(t, 476 + i, 0);
        const float mod = trg_prop(t, i ? 483 : 479, 1.0f);
        float v = kind ? operate(prop_int(t, 480 + i, 3), read_value(gm, kind, prop_int(t, i ? 95 : 80, 0)), mod) : mod;
        side[i] = shape(v, prop_int(t, 485 + i, 0), prop_int(t, 578 + i, 0));
    }
    const float tol = fabsf(trg_prop(t, 484, 0)) + 0.0001f, l = side[0], r = side[1];
    switch (prop_int(t, 482, 0)) {
    case 1: return l > r - tol + 0.0002f;
    case 2: return l >= r - tol;
    case 3: return l < r + tol - 0.0002f;
    case 4: return l <= r + tol;
    case 5: return fabsf(l - r) > tol;
    default: return fabsf(l - r) <= tol;
    }
}

static void timers_step(Game *gm)
{
    Logic *l = &gm->logic;
    for (int i = 0; i < l->ntimer; i++) {
        if (!l->timer[i].on) continue;
        const float before = l->timer[i].value;
        float now = before + l->timer[i].rate * STEP;
        int ended = 0;
        if (l->timer[i].check) {
            const float stop = l->timer[i].stop;
            if (l->timer[i].rate >= 0 ? before < stop && now >= stop : before > stop && now <= stop) {
                now = stop;
                ended = 1;
            }
        }
        l->timer[i].value = now;
        for (int k = 0; k < l->nat; k++) {   // time events
            if (l->at_time[k].item != l->timer[i].id || l->at_time[k].done) continue;
            const float at = l->at_time[k].at;
            if (!((before < at && now >= at) || (before > at && now <= at))) continue;
            if (!l->at_time[k].multi) l->at_time[k].done = 1;
            gm->chain = l->at_time[k].chain;
            trg_spawn(gm, l->at_time[k].group, 1);
            gm->chain.n = 0;
        }
        if (ended) {
            l->timer[i].on = 0;
            gm->chain = l->timer[i].chain;
            trg_spawn(gm, l->timer[i].group, 1);
            gm->chain.n = 0;
        }
    }
}

void trg_keep(Game *gm)
{
    for (int i = 0; i < gm->nkeep; i++) {
        if (gm->keep[i].timer) {
            const int k = timer_slot(gm, gm->keep[i].id, 0);
            if (k >= 0) gm->keep[i].value = gm->logic.timer[k].value;
        } else {
            for (int k = 0; k < gm->logic.nitem; k++)
                if (gm->logic.item[k] == gm->keep[i].id) gm->keep[i].value = (float)gm->logic.value[k];
        }
    }
}

void trg_attempt(Game *gm)
{
    for (int i = 0; i < gm->nkeep; i++) {
        if (gm->keep[i].timer) {
            const int k = timer_slot(gm, gm->keep[i].id, 1);
            if (k >= 0) gm->logic.timer[k].value = gm->keep[i].value;
        } else {
            int *v = item_slot(gm, gm->keep[i].id);
            if (v) *v = (int)gm->keep[i].value;
        }
    }
}

static void persistent(Game *gm, const Obj *t)
{
    const int id = prop_int(t, 80, 0), timer = prop_int(t, 494, 0) != 0;
    const int keep = prop_int(t, 491, 0) != 0, all = prop_int(t, 492, 0) != 0, reset = prop_int(t, 493, 0) != 0;
    if (reset) {
        for (int k = 0; k < gm->logic.nitem; k++)
            if (!timer && (all || gm->logic.item[k] == id)) gm->logic.value[k] = 0;
        for (int k = 0; k < gm->logic.ntimer; k++)
            if (timer && (all || gm->logic.timer[k].id == id)) gm->logic.timer[k].value = 0;
        for (int k = 0; k < gm->nkeep; k++)
            if (all || (gm->keep[k].id == id && gm->keep[k].timer == timer)) gm->keep[k].value = 0;
    }
    int at = 0;
    while (at < gm->nkeep && !(gm->keep[at].id == id && gm->keep[at].timer == timer)) at++;
    if (keep && at == gm->nkeep && gm->nkeep < (int)(sizeof gm->keep / sizeof gm->keep[0])) {
        gm->keep[at].id = (short)id;
        gm->keep[at].timer = (unsigned char)timer;
        gm->keep[at].value = 0;
        gm->nkeep++;
    }
}

// ---------------------------------------------------------------- watchers, collisions, events

static void watch_trigger(Game *gm, const Obj *t)
{
    const int oi = index_of(gm, t);
    Logic *l = &gm->logic;
    for (int i = 0; i < l->nwatch; i++)
        if (l->watch[i].obj == oi) return;
    if (l->nwatch >= GD_MAX_WATCH) return;
    int state = 0;
    if (t->tid == 1595) state = gm->input;
    else if (t->tid == 1611) { const int *v = item_slot(gm, t->ttarget); state = v && *v == t->tcopy; }
    l->watch[l->nwatch].obj = oi;
    l->watch[l->nwatch].chain = gm->chain;
    l->watch[l->nwatch++].state = state;
}

static int blocks_touch(const Game *gm, const Obj *a, int id_b)
{
    const float ahw = 15.0f * fabsf(a->sx), ahh = 15.0f * fabsf(a->sy);
    for (int i = 0; i < gm->ncblocks; i++) {
        const Obj *b = &gm->obj[gm->cblocks[i]];
        if (b == a || b->off || (int)b->value != id_b) continue;
        if (fabsf(a->x - b->x) < ahw + 15.0f * fabsf(b->sx) && fabsf(a->y - b->y) < ahh + 15.0f * fabsf(b->sy)) return 1;
    }
    return 0;
}

static int icon_touches(const Player *p, const Obj *b)
{
    const float h = gd_body_half(p);
    return fabsf(p->x - b->x) < h + 15.0f * fabsf(b->sx) && fabsf(p->y - b->y) < h + 15.0f * fabsf(b->sy);
}

static int collision_now(const Game *gm, const Obj *t)
{
    if (t->tplayer) {   // icon against a block id (either field may hold it)
        const int id = t->tcopy > 0 ? t->tcopy : t->ttarget;
        for (int i = 0; i < gm->ncblocks; i++) {
            const Obj *b = &gm->obj[gm->cblocks[i]];
            if (!b->off && (int)b->value == id && icon_touches(&gm->p, b)) return 1;
        }
        return 0;
    }
    for (int i = 0; i < gm->ncblocks; i++) {
        const Obj *a = &gm->obj[gm->cblocks[i]];
        if (!a->off && (int)a->value == t->ttarget && blocks_touch(gm, a, t->tcopy)) return 1;
    }
    return 0;
}

static void watchers_step(Game *gm)
{
    Logic *l = &gm->logic;
    for (int i = 0; i < l->nwatch; i++) {
        const Obj *t = &gm->obj[l->watch[i].obj];
        if (t->off) continue;
        int now = 0, done = 0;
        gm->chain = l->watch[i].chain;
        const int group = trg_group(gm, t->tgroup);
        if (t->tid == 1611) {
            const int *v = item_slot(gm, t->ttarget);
            now = v && *v == t->tcopy;
            if (now && !l->watch[i].state) {
                done = prop_int(t, 104, 0) == 0;   // without "multi activate" only once
                trg_activate(gm, group, t->tact, 0);
            }
        } else if (t->tid == 1815) {
            now = collision_now(gm, t);
            if (t->tlockx ? !now && l->watch[i].state : now && !l->watch[i].state) trg_activate(gm, group, t->tact, 0);
        } else {   // 1595 touch
            now = gm->input;
            const int pressed = now && !l->watch[i].state, released = !now && l->watch[i].state;
            if (pressed || (released && t->tlockx)) {
                int on = t->tcenter == 1 ? 1 : t->tcenter == 2 ? 0 : -1;
                if (on < 0) {   // toggle: look at the first object of the group
                    const Obj *o = trg_first(gm, group);
                    on = o && o->off;
                }
                trg_activate(gm, group, on, 0);
            }
        }
        gm->chain.n = 0;
        l->watch[i].state = now;
        if (done) l->watch[i--] = l->watch[--l->nwatch];
    }
}

// State, toggle and stop-jump blocks react to the icon.
static void sensors_step(Game *gm)
{
    for (int i = 0; i < gm->nsensors; i++) {
        Obj *o = &gm->obj[gm->sensors[i]];
        if (o->off) continue;
        const int touching = icon_touches(&gm->p, o) || (gm->dual && icon_touches(&gm->p2, o));
        if (o->sub == SN_NOBUFFER) {   // a click is not kept for a later orb (a new one still counts at once)
            if (touching && !gm->p.fresh) gm->p.queued = gm->p2.queued = 0;
            continue;
        }
        if (touching == (o->used != 0)) continue;
        trg_touch(gm, o);
        o->used = touching;
        if (o->sub == SN_STATE) trg_spawn(gm, prop_int(o, touching ? 51 : 71, 0), 1);
        else if (touching) {
            if (prop_int(o, 504, 0)) trg_spawn(gm, prop_int(o, 51, 0), 1);
            else trg_activate(gm, prop_int(o, 51, 0), prop_int(o, 56, 0) != 0, 0);
        }
    }
}

void trg_event(Game *gm, int event)
{
    Logic *l = &gm->logic;
    if (event <= 0 || event >= 96) return;
    for (int i = 0; i < l->nlisten; i++) {
        if (!(l->listen[i].mask[event >> 5] & (1u << (event & 31)))) continue;
        if (gm->obj[l->listen[i].src].off) continue;
        const Chain before = gm->chain;
        gm->chain = l->listen[i].chain;
        trg_spawn(gm, l->listen[i].group, 4);   // events can raise events: keep that short
        gm->chain = before;
    }
}

static void listen(Game *gm, const Obj *t, int group)
{
    Logic *l = &gm->logic;
    const int src = index_of(gm, t);
    int at = 0;
    while (at < l->nlisten && !(l->listen[at].src == src && l->listen[at].group == group)) at++;
    if (at >= GD_MAX_LISTEN || !t->data || !t->data->text) return;
    memset(&l->listen[at], 0, sizeof l->listen[at]);
    l->listen[at].src = src;
    l->listen[at].group = (short)group;
    l->listen[at].chain = gm->chain;
    for (const char *p = t->data->text; *p;) {   // "event.event..."
        const int e = atoi(p);
        if (e > 0 && e < 96) l->listen[at].mask[e >> 5] |= 1u << (e & 31);
        while (*p && *p != '.') p++;
        if (*p) p++;
    }
    if (at == l->nlisten) l->nlisten++;
}

void trg_death(Game *gm)
{
    for (int i = 0; i < gm->logic.ndeath; i++)
        trg_activate(gm, gm->logic.death[i].group, gm->logic.death[i].on, 0);
}

// ---------------------------------------------------------------- colours

void trg_channel_fade(Game *gm, const Obj *t)
{
    if (t->ttarget < 0 || t->ttarget >= CH_COUNT || t->ttarget == CH_BLACK || t->ttarget == CH_WHITE) return;
    Channel *c = &gm->ch[t->ttarget];
    const Channel *shown = &gm->ch[t->tplayer == 2 ? CH_P2 : CH_P1];
    if (c->player) {   // leaving a player colour: start from what was shown
        const Channel *now = &gm->ch[c->player == 2 ? CH_P2 : CH_P1];
        c->r = now->r; c->g = now->g; c->b = now->b;
    }
    c->player = 0;
    c->next_player = t->tplayer;
    c->lbg = 0;
    c->hsv_on = 0;
    c->copy = t->tcopy > 0 && t->tcopy < CH_COUNT && t->tcopy != t->ttarget ? t->tcopy : 0;
    c->copy_op = 0;
    if (c->copy && t->copy_hsv > 0 && t->copy_hsv <= gm->nhsv) {   // the copied colour is shifted
        const float *v = gm->hsv[t->copy_hsv - 1];
        c->hsv_on = 1;
        c->hs = v[0]; c->ss = v[1]; c->vs = v[2]; c->sadd = (int)v[3]; c->vadd = (int)v[4];
    }
    c->fr = c->r; c->fg = c->g; c->fb = c->b; c->fa = c->a;
    c->tr = t->tr; c->tg = t->tg; c->tb = t->tb;
    if (t->tplayer) { c->tr = shown->r; c->tg = shown->g; c->tb = shown->b; }
    c->ta = t->topacity;   // key 35, 1 when absent
    c->additive = t->tblend;
    c->t = 0;
    c->dur = t->tdur;
    c->fading = 1;
    if (c->dur <= 0.001f) {
        c->r = c->tr; c->g = c->tg; c->b = c->tb; c->a = c->ta;
        c->fading = 0;
        c->player = c->next_player;
    }
}

// ---------------------------------------------------------------- commands

static Frame frame_from(int move, int ground, Frame cur)
{
    static const signed char dir[5][2] = {{0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0}};
    Frame f = cur;
    if (move >= 1 && move <= 4) { f.fx = dir[move][0]; f.fy = dir[move][1]; }
    if (ground >= 1 && ground <= 4) { f.ux = (signed char)-dir[ground][0]; f.uy = (signed char)-dir[ground][1]; }
    if (f.fx * f.ux + f.fy * f.uy != 0) { f.ux = (signed char)-f.fy; f.uy = f.fx; }   // keep them at a right angle
    return f;
}

static Move *new_move(Game *gm, const Obj *t, int kind, int group)
{
    if (gm->nmv >= GD_MAX_MOVES) return NULL;
    Move *m = &gm->mv[gm->nmv++];
    memset(m, 0, sizeof *m);
    m->kind = (unsigned char)kind;
    m->group = group;
    m->src = index_of(gm, t);
    m->dur = t->tdur;
    m->easing = t->teasing;
    m->rate = t->trate;
    m->chain = gm->chain;
    return m;
}

static const Player *target_player(const Game *gm, const Obj *t)
{
    if (prop_int(t, 200, 0) && gm->dual) return &gm->p2;
    return prop_int(t, 138, 0) || prop_int(t, 200, 0) ? &gm->p : NULL;
}

static void move_trigger(Game *gm, Obj *t, int group)
{
    float dx = t->tdx, dy = t->tdy;
    const int to_target = prop_int(t, 100, 0) != 0, along = prop_int(t, 394, 0) != 0;
    if (to_target || along) {   // towards the object of a group (or the icon)
        const int center = trg_group(gm, prop_int(t, 395, 0)), goal = trg_group(gm, t->tcenter);
        const Obj *c = trg_first(gm, center > 0 ? center : group), *o = trg_first(gm, goal);
        const Player *p = target_player(gm, t);
        if (!c || (!o && !p)) return;
        const int axis = prop_int(t, 101, 0);   // 1: x only, 2: y only
        dx = (p ? p->x : o->x) - c->x;
        dy = (p ? p->y : o->y) - c->y;
        if (along) {   // a set distance in that direction
            const float len = hypotf(dx, dy), far = trg_prop(t, 396, 0);
            if (len < 0.001f) return;
            dx *= far / len;
            dy *= far / len;
        } else {
            if (axis == 2) dx = 0;
            if (axis == 1) dy = 0;
            if (prop_int(t, 397, 0) && t->tdur > 0.001f) {   // dynamic: keeps aiming at the target
                Move *m = new_move(gm, t, MV_TARGET, group);
                if (!m) return;
                m->center = center > 0 ? center : group;
                m->follow = goal;
                m->axis = (unsigned char)axis;
                m->player = p == &gm->p2 ? 2 : p ? 1 : 0;
                return;
            }
        }
    }
    const int camx = prop_int(t, 141, 0) != 0, camy = prop_int(t, 142, 0) != 0;
    if (t->tdur <= 0.001f && !t->tlockx && !t->tlocky && !camx && !camy) {
        trg_move_group(gm, group, dx, dy);
        return;
    }
    Move *m = new_move(gm, t, MV_PLAIN, group);
    if (!m) return;
    m->lockx = t->tlockx; m->locky = t->tlocky;
    m->camx = (unsigned char)camx; m->camy = (unsigned char)camy;
    m->modx = trg_prop(t, 143, 1.0f); m->mody = trg_prop(t, 144, 1.0f);
    m->dx = m->lockx || m->camx ? 0 : dx;
    m->dy = m->locky || m->camy ? 0 : dy;
    m->lx = gm->p.x; m->ly = gm->p.y;
    m->cx = gm->p.x; m->cy = gm->cam_bottom;
    if (m->dur <= 0.001f) m->dur = 0.001f;
}

// 1 if a stop trigger means this trigger: it is in the group, or has that control id.
static int meant(const Obj *source, int group, int by_id)
{
    if (by_id) return prop_int(source, 534, 0) == group;
    for (int k = 0; k < source->ngrp; k++)
        if (source->grp[k] == group) return 1;
    return 0;
}

// Stop, pause or resume: the commands of the triggers in a group (or with a control id).
static void stop_trigger(Game *gm, const Obj *t, int group)
{
    const int mode = prop_int(t, 580, 0), by_id = prop_int(t, 535, 0) != 0;
    for (int i = 0; i < gm->nmv;) {
        Move *m = &gm->mv[i];
        if (!meant(&gm->obj[m->src], group, by_id)) { i++; continue; }
        if (mode == 0) { gm->mv[i] = gm->mv[--gm->nmv]; continue; }
        m->paused = mode == 1;
        i++;
    }
    for (int i = 0; i < gm->nsp;) {   // delayed spawns: of the spawn trigger meant
        if (!meant(&gm->obj[gm->sp[i].src], group, by_id)) { i++; continue; }
        if (mode == 0) { gm->sp[i] = gm->sp[--gm->nsp]; continue; }
        gm->sp[i].paused = mode == 1;
        i++;
    }
}

static void set_option(Game *gm, const Obj *t, int key, unsigned flag)
{
    const int v = prop_int(t, key, 0);
    if (v > 0) gm->logic.options |= flag;
    else if (v < 0) gm->logic.options &= ~flag;
}

void trg_fire(Game *gm, Obj *t, int depth)
{
    if (depth > 8) return;
    const int group = trg_group(gm, t->tgroup);
    if (motion_fire(gm, t, group) || visual_fire(gm, t, group)) return;
    switch (t->tid) {
    case 3022: { // teleport trigger: move the icon to a group, with the options of the teleport portal
        Player *p = &gm->p;
        if (!gm_teleport(gm, p, group)) break;
        if (t->tforce != 0) p->yv = t->tforce;
        if (t->tgrav) p->grav = (t->tgrav == 3 ? p->grav > 0 : t->tgrav == 2) ? -1 : 1;
        p->rising = p->yv * p->grav > 0;
        trg_event(gm, 25);
        break;
    }
    case 1611: case 1815: case 1595: // keep watching from now on
        watch_trigger(gm, t);
        break;
    case 1817: { // pickup: change an item counter
        int *v = item_slot(gm, t->ttarget);
        if (!v) break;
        const float mod = trg_prop(t, 449, 1.0f);
        if (prop_int(t, 139, 0)) *v = t->tcopy;
        else if (t->tcenter == 1) *v = (int)(*v * mod);
        else if (t->tcenter == 2) *v = mod != 0 ? (int)(*v / mod) : 0;
        else *v += t->tcopy;
        break;
    }
    case 1811: { // instant count: compare once
        const int *v = item_slot(gm, t->ttarget);
        if (v && (t->tcenter == 1 ? *v > t->tcopy : t->tcenter == 2 ? *v < t->tcopy : *v == t->tcopy))
            trg_activate(gm, group, t->tact, depth + 1);
        break;
    }
    case 3619: item_edit(gm, t, trg_group(gm, t->tgroup)); break;
    case 3620: // item compare: one of two groups
        trg_spawn(gm, item_compare(gm, t) ? group : trg_group(gm, t->tcenter), depth + 1);
        break;
    case 3614: { // time: set a timer running
        const int id = prop_int(t, 80, 0), had = timer_slot(gm, id, 0) >= 0;
        const int i = timer_slot(gm, id, 1);
        if (i < 0) break;
        if (!(had && prop_int(t, 468, 0))) gm->logic.timer[i].value = trg_prop(t, 467, 0);
        gm->logic.timer[i].rate = trg_prop(t, 470, 1.0f);
        gm->logic.timer[i].check = prop_int(t, 474, 0) != 0;
        gm->logic.timer[i].stop = trg_prop(t, 473, 0);
        gm->logic.timer[i].group = (short)group;
        gm->logic.timer[i].chain = gm->chain;
        gm->logic.timer[i].on = prop_int(t, 471, 0) == 0;
        break;
    }
    case 3615: { // time event: a group when a timer passes a value
        Logic *l = &gm->logic;
        if (l->nat >= GD_MAX_LISTEN) break;
        l->at_time[l->nat].item = (short)prop_int(t, 80, 0);
        l->at_time[l->nat].group = (short)group;
        l->at_time[l->nat].at = trg_prop(t, 473, 0);
        l->at_time[l->nat].multi = prop_int(t, 475, 0) != 0;
        l->at_time[l->nat].done = 0;
        l->at_time[l->nat].chain = gm->chain;
        l->nat++;
        break;
    }
    case 3617: { // time control: stop or continue a timer
        const int i = timer_slot(gm, prop_int(t, 80, 0), 0);
        if (i >= 0) gm->logic.timer[i].on = prop_int(t, 472, 0) == 0;
        break;
    }
    case 3641: persistent(gm, t); break;
    case 3604: listen(gm, t, group); break;
    case 3609: // instant collision
        trg_spawn(gm, collision_now(gm, t) ? group : trg_group(gm, t->tcenter), depth + 1);
        break;
    case 3618: // reset: the triggers of a group may fire again
        for (int k = group_valid(gm, group) ? gm->grp_start[group] : 0; group_valid(gm, group) && k < gm->grp_start[group + 1]; k++) {
            Obj *o = &gm->obj[gm->grp_items[k]];
            trg_touch(gm, o);
            o->tdone = 0;
            o->used = o->used2 = 0;
            for (int i = 0; i < gm->logic.nwatch; i++)
                if (gm->logic.watch[i].obj == gm->grp_items[k]) gm->logic.watch[i--] = gm->logic.watch[--gm->logic.nwatch];
        }
        break;
    case 2899: { // options
        set_option(gm, t, 161, OPT_HIDE_GROUND);
        set_option(gm, t, 195, OPT_HIDE_MG);
        set_option(gm, t, 162, OPT_HIDE_P1);
        set_option(gm, t, 163, OPT_HIDE_P2);
        set_option(gm, t, 160, OPT_FREE_DUAL);
        set_option(gm, t, 532, OPT_HIDE_ATTEMPTS);
        set_option(gm, t, 576, OPT_NO_DEATH_SOUND);
        const int respawn = prop_int(t, 573, 0);
        if (respawn > 0) gm->logic.respawn = fmaxf(trg_prop(t, 574, 1.0f), 0.05f);
        else if (respawn < 0) gm->logic.respawn = 0;
        // Taking the controls away (keys 165 / 199) is left out on purpose: the physics of this port
        // are not exact enough for the stretches where the original steers the icon by itself (the
        // end of Dash cannot be survived without input here).
        break;
    }
    case 3600: // end of the level
        gm->finished = 1;
        break;
    case 1932: // player control: only "stop jump" has a meaning outside the platformer mode
        if (prop_int(t, 540, 0)) {
            if (prop_int(t, 138, 0) || !prop_int(t, 200, 0)) gm->p.hold = gm->p.queued = 0;
            if (prop_int(t, 200, 0)) gm->p2.hold = gm->p2.queued = 0;
        }
        break;
    case 1912: // random: one of two groups
        trg_activate(gm, random_01(gm) * 100.0f < t->value ? group : trg_group(gm, t->tcenter), 1, depth + 1);
        break;
    case 2068: case 3607: { // weighted random / sequence over a list of groups
        const int oi = index_of(gm, t);
        for (int i = 0; i < gm->nlists; i++) {
            if (gm->lists[i].obj != oi) continue;
            const short *v = gm->lists[i].list;
            const int n = gm->lists[i].n & ~1;
            int total = 0, pick = -1;
            for (int k = 1; k < n; k += 2) total += v[k] > 0 ? v[k] : 0;
            if (total <= 0) break;
            int at;
            if (t->tid == 2068) at = (int)(logic_random(gm) % (unsigned)total);
            else { trg_touch(gm, t); at = t->used % total; t->used = (at + 1) % total; }
            for (int k = 0; k < n && pick < 0; k += 2) {
                at -= v[k + 1] > 0 ? v[k + 1] : 0;
                if (at < 0) pick = v[k];
            }
            trg_activate(gm, trg_group(gm, pick), 1, depth + 1);
            break;
        }
        break;
    }
    case 2900: // rotate gameplay
        gm->next_frame = frame_from(t->rmove, t->rground, gm->next_frame);
        gm->channel = t->to_channel;
        break;
    case 1917: // reverse: run the other way
        gm->next_frame.fx = (signed char)-gm->next_frame.fx;
        gm->next_frame.fy = (signed char)-gm->next_frame.fy;
        trg_event(gm, 75);
        break;
    case 1935: // time warp
        if (!gm->sim) gm->timewarp = fminf(fmaxf(t->value, 0.1f), 2.0f);
        break;
    case 2066: // gravity factor
        if (t->value > 0.01f) gm->p.gscale = gm->p2.gscale = t->value;
        break;
    case 1914: // static camera; the flying borders that follow the camera go away
        if (gm->has_jumps && !t->tact) gm->free = 1;
        if (!gm->sim) camera_static(gm, t, trg_group(gm, t->tcenter));
        break;
    case 2925: // camera mode: free mode keeps flying forms out of a play area
        gm->free = prop_int(t, 111, 0) != 0;
        break;
    case 1913: // zoom
        if (!gm->sim) trg_tween_to(&gm->view.zoom, t->value, t);
        break;
    case 1916: // camera offset
        if (gm->sim) break;
        if (t->ttarget != 2) trg_tween_to(&gm->view.offx, t->tdx, t);
        if (t->ttarget != 1) trg_tween_to(&gm->view.offy, t->tdy, t);
        break;
    case 2901: { // gameplay offset: the icon stands elsewhere on the screen
        if (gm->sim) break;
        const int axis = prop_int(t, 101, 0);
        if (axis != 2) trg_tween_to(&gm->view.shiftx, t->tdx, t);
        if (axis != 1) trg_tween_to(&gm->view.shifty, t->tdy, t);
        break;
    }
    case 2015: // camera rotation
        if (!gm->sim) trg_tween_to(&gm->view.turn, t->trot, t);
        break;
    case 2062: { // camera edge: with a group the view stops at its object, without one the edge goes
        const int side = t->ttarget - 1;
        if (gm->sim || side < 0 || side > 3) break;
        const int set = group_valid(gm, group);
        if (set) gm->view.edge[side] = group;
        Obj glide = *t;
        if (glide.tdur <= 0) { glide.tdur = 0.5f; glide.teasing = 3; glide.trate = 2; }   // no jump
        trg_tween_to(&gm->view.bound[side], set ? 1.0f : 0.0f, &glide);
        break;
    }
    case 1612: case 1613: // hide / show the icon
        if (!gm->sim) gm->view.hidden = t->tid == 1612;
        break;
    case 3029: case 3030: // change the background / ground
        if (!gm->sim && t->ttarget > 0) {
            if (t->tid == 3029) gm->view.bg_id = t->ttarget;
            else gm->view.ground_id = t->ttarget;
        }
        break;
    case 2903: // gradient
        if (!gm->sim) gm_add_gradient_state(gm, index_of(gm, t));
        break;
    case 2067: // scale a group
        if (t->tdur <= 0.001f) {
            trg_scale_group(gm, group, trg_group(gm, t->tcenter), scale_part(t->tdx, 1), scale_part(t->tdy, 1));
            break;
        }
        {
            Move *m = new_move(gm, t, MV_SCALE, group);
            if (m) { m->center = trg_group(gm, t->tcenter); m->dx = t->tdx; m->dy = t->tdy; }
        }
        break;
    case 1814: { // a group follows the height of the icon
        Move *m = new_move(gm, t, MV_PLAYER_Y, group);
        if (!m) break;
        if (m->dur <= 0) m->dur = 0.001f;
        m->dx = t->value; m->dy = t->tdy; m->rot = t->fymod;
        m->t = -fmaxf(trg_prop(t, 91, 0), 0);   // starts after the delay
        break;
    }
    case 1812: // on death: remember the group for the next crash
        if (gm->logic.ndeath < (int)(sizeof gm->logic.death / sizeof gm->logic.death[0])) {
            gm->logic.death[gm->logic.ndeath].group = (short)group;
            gm->logic.death[gm->logic.ndeath++].on = (short)t->tact;
        }
        break;
    case 1347: { // follow: a group moves along with the first object of another group
        const int lead = trg_group(gm, t->tcenter);
        const Obj *o = trg_first(gm, lead);
        Move *m = o ? new_move(gm, t, MV_FOLLOW, group) : NULL;
        if (!m) break;
        if (m->dur <= 0) m->dur = 0.001f;
        m->follow = lead;
        m->dx = t->fxmod; m->dy = t->fymod; m->lx = o->x; m->ly = o->y;
        break;
    }
    case 901: move_trigger(gm, t, group); break;
    case 1346: { // rotate a group around the object of the centre group
        const int center = trg_group(gm, t->tcenter);
        if (t->tdur <= 0.001f) { trg_rotate_group(gm, group, center, t->trot); break; }
        Move *m = new_move(gm, t, MV_ROTATE, group);
        if (m) { m->center = center > 0 ? center : group; m->rot = t->trot; }
        break;
    }
    case 1616: stop_trigger(gm, t, group); break;
    case 1585:   // animate: starts the effects of a group that wait for it
        if (group_valid(gm, group))
            for (int i = gm->grp_start[group]; i < gm->grp_start[group + 1]; i++) {
                Obj *o = &gm->obj[gm->grp_items[i]];
                if (!o->effect || !o->fx_trigger) continue;
                o->fx_at = gm->anim_t;
                o->fx_run = gm->run;
            }
        break;
    case 1049: // toggle
        if (group_valid(gm, group))
            for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
                Obj *o = &gm->obj[gm->grp_items[k]];
                if (!gm->sim || !scenery(o)) { trg_touch(gm, o); o->off = t->tact ? 0 : 1; }
            }
        break;
    case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 55: case 56: case 57: case 58: case 59: case 1915:
        if (!gm->sim) gm->enter_fx = t->tid;   // visual only
        break;
    case 32: case 33:
        if (!gm->sim) gm->ghost_on = t->tid == 32;
        break;
    case 1520:
        if (!gm->sim) {
            gm->shake_left = t->tdur;
            gm->shake_amp = t->value;
            gm->shake_every = t->trate;
        }
        break;
    case 1007: // alpha (visual only), fading over the trigger's duration
        if (gm->sim || group <= 0 || group >= GD_GROUPS) break;
        for (int i = 0; i < gm->naf;)
            if (gm->af[i].group == group) gm->af[i] = gm->af[--gm->naf]; else i++;
        if (t->tdur > 0.001f && gm->naf < GD_MAX_PULSES)
            gm->af[gm->naf++] = (AlphaFade){group, gm->galpha[group], t->topacity, t->tdur, 0};
        else
            gm->galpha[group] = t->topacity;
        break;
    case 1006: // pulse (visual only)
        if (gm->sim || gm->npu >= GD_MAX_PULSES) break;
        gm->pu[gm->npu++] = (Pulse){t->pgroup ? trg_group(gm, t->ttarget) : t->ttarget, t->pgroup,
                                    t->pcopy ? (t->tcopy > 0 ? t->tcopy : -1) : 0, t->ponly,
                                    t->tr, t->tg, t->tb, t->phue, t->pin, t->phold, t->pout, 0};
        break;
    case 1268: spawn_trigger(gm, t, group, depth); break;
    case 29: case 30: case 104: case 105: case 744: case 900: case 915: case 221: case 717: case 718: case 743:
    case 899: // colour triggers
        if (!gm->sim) {
            trg_channel_fade(gm, t);
            if (t->tground) {
                Obj g = *t;
                g.ttarget = CH_GROUND;
                trg_channel_fade(gm, &g);
            }
        }
        break;
    default: break;
    }
}

// Advances running commands, delayed spawns, timers and watchers by one simulation step.
void trg_step(Game *gm)
{
    gm->chain.n = 0;
    for (int i = 0; i < gm->nmv;) {
        Move *m = &gm->mv[i];
        if (m->paused) { i++; continue; }
        m->t += STEP;
        if (m->t < 0) { i++; continue; }   // still waiting to start
        if (m->kind == MV_KEYFRAME || m->kind == MV_ADVANCED) {
            motion_move(gm, m);
            if (m->t >= m->dur) gm->mv[i] = gm->mv[--gm->nmv]; else i++;
            continue;
        }
        const float f = trg_ease(m->easing, m->t >= m->dur ? 1.0f : m->t / m->dur, m->rate);
        const float d = f - m->done;
        switch (m->kind) {
        case MV_SCALE: {
            const float kx = scale_part(m->dx, f) / scale_part(m->dx, m->done);
            const float ky = scale_part(m->dy, f) / scale_part(m->dy, m->done);
            if (kx != 1 || ky != 1) trg_scale_group(gm, m->group, m->center, kx, ky);
            break;
        }
        case MV_PLAYER_Y: {
            const Obj *lead = trg_first(gm, m->group);
            if (!lead) break;   // speed: the part of the way covered per 60 Hz frame
            float dy = (gm->p.y + m->dy - lead->y) * fminf(1.0f, fmaxf(m->dx, 0) * STEP * 60.0f);
            if (m->rot > 0) dy = fminf(fmaxf(dy, -m->rot * STEP * 60.0f), m->rot * STEP * 60.0f);
            if (dy != 0) trg_move_group(gm, m->group, 0, dy);
            break;
        }
        case MV_FOLLOW: {
            const Obj *lead = trg_first(gm, m->follow);
            if (!lead) break;
            const float mx = (lead->x - m->lx) * m->dx, my = (lead->y - m->ly) * m->dy;
            m->lx = lead->x; m->ly = lead->y;
            if (mx != 0 || my != 0) trg_move_group(gm, m->group, mx, my);
            break;
        }
        case MV_ROTATE:
            if (d != 0) trg_rotate_group(gm, m->group, m->center, m->rot * d);
            break;
        case MV_TARGET: {   // the share of the remaining way that this step's part of the curve stands for
            const Obj *c = trg_first(gm, m->center), *o = trg_first(gm, m->follow);
            const Player *p = m->player == 2 ? &gm->p2 : m->player ? &gm->p : NULL;
            if (!c || (!o && !p)) break;
            const float share = m->done < 0.9999f ? fminf(d / (1.0f - m->done), 1.0f) : 1.0f;
            const float mx = m->axis == 2 ? 0 : ((p ? p->x : o->x) - c->x) * share;
            const float my = m->axis == 1 ? 0 : ((p ? p->y : o->y) - c->y) * share;
            if (mx != 0 || my != 0) trg_move_group(gm, m->group, mx, my);
            break;
        }
        default: {
            float mx = m->dx * d, my = m->dy * d;
            if (m->lockx) { mx += (gm->p.x - m->lx) * m->modx; m->lx = gm->p.x; }
            if (m->locky) { my += (gm->p.y - m->ly) * m->mody; m->ly = gm->p.y; }
            // locked to the camera: along the way it keeps pace with the icon
            if (m->camx) { mx += (gm->p.x - m->cx) * m->modx; m->cx = gm->p.x; }
            if (m->camy) { my += (gm->cam_bottom - m->cy) * m->mody; m->cy = gm->cam_bottom; }
            if (mx != 0 || my != 0) trg_move_group(gm, m->group, mx, my);
            break;
        }
        }
        m->done = f;
        if (m->t >= m->dur) gm->mv[i] = gm->mv[--gm->nmv]; else i++;
    }
    for (int i = 0; i < gm->nsp;) {
        if (gm->sp[i].paused) { i++; continue; }
        gm->sp[i].left -= STEP;
        if (gm->sp[i].left <= 0) {
            const PendingSpawn due = gm->sp[i];
            gm->sp[i] = gm->sp[--gm->nsp];
            if (gm->obj[due.obj].off) continue;
            gm->chain = due.chain;
            trg_fire(gm, &gm->obj[due.obj], 1);
            gm->chain.n = 0;
        } else i++;
    }
    if (gm->logic.ntimer) timers_step(gm);
    if (gm->logic.nwatch) watchers_step(gm);
    if (gm->nsensors) sensors_step(gm);
}
