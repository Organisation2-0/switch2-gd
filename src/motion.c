// SPDX-License-Identifier: GPL-3.0-or-later
// The 2.2 triggers that animate: keyframes, advanced follow, area and enter effects. What the
// original does exactly is not documented; these follow the meaning of the settings.
#include "game_int.h"

#define PI 3.14159265f
#define KF_SPAWNS 64

typedef struct GdTrack {
    int first;             // the keyframe an animation trigger points at
    int n;
    int *key;              // object indices in playing order
} Track;

typedef struct GdAreaObj {
    unsigned stamp;
    float dx, dy, rot, sx, sy, alpha;
    float tint, r, g, b;
} AreaObj;

static float prop(const Game *gm, int i, int key, float def) { return trg_prop(&gm->obj[i], key, def); }

// ---------------------------------------------------------------- loading

typedef struct { int obj, anim, target, order; } Key;

static int by_animation(const void *a, const void *b)
{
    const Key *x = a, *y = b;
    if (x->anim != y->anim) return x->anim - y->anim;
    if (x->target != y->target) return x->target - y->target;
    return x->order != y->order ? x->order - y->order : x->obj - y->obj;
}

void motion_load(Game *gm)
{
    int n = 0, areas = 0;
    for (int i = 0; i < gm->n; i++) {
        const Obj *o = &gm->obj[i];
        n += o->id == 3032;
        areas += o->type == OT_TRIGGER && o->tid >= 3006 && o->tid <= 3021 && o->tid != 3016;
    }
    if (areas) gm->areaobj = calloc((size_t)gm->n, sizeof *gm->areaobj);
    if (!n) return;
    Key *keys = malloc((size_t)n * sizeof *keys);
    gm->tracks = calloc((size_t)n, sizeof *gm->tracks);
    if (!keys || !gm->tracks) { free(keys); return; }
    n = 0;
    for (int i = 0; i < gm->n; i++)
        if (gm->obj[i].id == 3032)
            keys[n++] = (Key){i, (int)prop(gm, i, 373, 0), (int)prop(gm, i, 51, 0), (int)prop(gm, i, 374, 0)};
    // keyframes with the same animation number and preview group form one animation, in their order
    qsort(keys, (size_t)n, sizeof *keys, by_animation);
    for (int i = 0, end; i < n; i = end) {
        for (end = i + 1; end < n && keys[end].anim == keys[i].anim && keys[end].target == keys[i].target; end++) {}
        Track *t = &gm->tracks[gm->ntracks];
        t->n = end - i;
        t->key = malloc((size_t)t->n * sizeof *t->key);
        if (!t->key) break;
        for (int k = 0; k < t->n; k++) t->key[k] = keys[i + k].obj;
        t->first = t->key[0];
        gm->ntracks++;
    }
    free(keys);
}

void motion_free(Game *gm)
{
    for (int i = 0; i < gm->ntracks; i++) free(gm->tracks[i].key);
    free(gm->tracks);
    free(gm->areaobj);
    gm->tracks = NULL;
    gm->areaobj = NULL;
    gm->ntracks = 0;
}

void motion_reset(Game *gm) { gm->narea = 0; }

// ---------------------------------------------------------------- keyframes

typedef struct { float x, y, rot, sx, sy; } Pose;

static Pose key_pose(const Game *gm, int obj)
{
    const Obj *o = &gm->obj[obj];
    return (Pose){o->x, o->y, o->rot, fabsf(o->sx), fabsf(o->sy)};
}

// seconds of the segment starting at keyframe s
static float segment_time(const Game *gm, const Track *t, int s, int segments, float time_mod)
{
    const int mode = (int)prop(gm, t->first, 379, 0);   // 0 each its own time, 1 even, 2 by distance
    float dur;
    if (mode == 0) dur = prop(gm, t->key[s], 10, 0.5f);
    else {
        const float whole = prop(gm, t->first, 10, 0.5f);   // the first keyframe's time for all of it
        if (mode == 1) dur = whole / segments;
        else {
            float all = 0, mine = 0;
            for (int i = 0; i < segments; i++) {
                const Obj *a = &gm->obj[t->key[i]], *b = &gm->obj[t->key[(i + 1) % t->n]];
                const float len = hypotf(b->x - a->x, b->y - a->y);
                all += len;
                if (i == s) mine = len;
            }
            dur = all > 0 ? whole * mine / all : whole / segments;
        }
    }
    return fmaxf(dur * time_mod, 0.001f);
}

static float spline(float a, float b, float c, float d, float t)   // between b and c (Catmull-Rom)
{
    return 0.5f * (2 * b + (c - a) * t + (2 * a - 5 * b + 4 * c - d) * t * t + (3 * b - a - 3 * c + d) * t * t * t);
}

static void keyframe_start(Game *gm, Obj *t, int group)
{
    const int anim_group = trg_group(gm, (int)trg_prop(t, 76, 0));
    if (anim_group <= 0 || anim_group >= GD_GROUPS) return;
    int track = -1;
    for (int k = gm->grp_start[anim_group]; track < 0 && k < gm->grp_start[anim_group + 1]; k++)
        for (int i = 0; i < gm->ntracks; i++)
            if (gm->tracks[i].first == gm->grp_items[k]) { track = i; break; }
    if (track < 0 || gm->tracks[track].n < 2) return;
    const Track *tr = &gm->tracks[track];
    const int loop = prop(gm, tr->first, 376, 0) != 0, segments = loop ? tr->n : tr->n - 1;
    const float time_mod = trg_prop(t, 520, 1.0f);
    float total = 0;
    for (int s = 0; s < segments; s++) total += segment_time(gm, tr, s, segments, time_mod);
    if (gm->nmv >= GD_MAX_MOVES) return;
    for (int i = 0; i < gm->nmv; i++)   // a group plays one animation at a time
        if (gm->mv[i].kind == MV_KEYFRAME && gm->mv[i].group == group) gm->mv[i--] = gm->mv[--gm->nmv];
    Move *m = &gm->mv[gm->nmv++];
    memset(m, 0, sizeof *m);
    m->kind = MV_KEYFRAME;
    m->group = group;
    m->center = trg_group(gm, t->tcenter);   // pivot of rotation and scale
    m->follow = track;
    m->src = (int)(t - gm->obj);
    m->dur = total;
    m->dx = m->dy = 1;                       // scale applied so far
    m->axis = 255;                           // keyframe reached so far (none)
    m->chain = gm->chain;
}

// MV_KEYFRAME: lx, ly offset applied so far, rot angle, dx, dy scale.
static void keyframe_move(Game *gm, Move *m)
{
    const Track *tr = &gm->tracks[m->follow];
    const Obj *t = &gm->obj[m->src];
    const int loop = prop(gm, tr->first, 376, 0) != 0, segments = loop ? tr->n : tr->n - 1;
    const float time_mod = trg_prop(t, 520, 1.0f);
    float left = fminf(m->t, m->dur), dur = 0;
    int s = 0;
    for (; s < segments; s++) {
        dur = segment_time(gm, tr, s, segments, time_mod);
        if (left <= dur || s == segments - 1) break;
        left -= dur;
    }
    const int a = tr->key[s], b = tr->key[(s + 1) % tr->n];
    if (m->axis != (unsigned char)s) {   // a new keyframe is reached: its spawn group
        m->axis = (unsigned char)s;
        const int spawn = (int)prop(gm, a, 71, 0);
        if (spawn > 0) {
            const Chain before = gm->chain;
            gm->chain = m->chain;
            trg_spawn(gm, trg_group(gm, spawn), 3);
            gm->chain = before;
        }
    }
    const float u = trg_ease((int)prop(gm, a, 30, 0), fminf(left / dur, 1.0f), prop(gm, a, 85, 2.0f));
    const Pose p0 = key_pose(gm, tr->first), pa = key_pose(gm, a), pb = key_pose(gm, b);
    float x = pa.x + (pb.x - pa.x) * u, y = pa.y + (pb.y - pa.y) * u;
    if (prop(gm, a, 378, 0) != 0) {   // curved path through the neighbours
        const int prev = s > 0 ? s - 1 : loop ? tr->n - 1 : 0;
        const int next = s + 2 < tr->n ? s + 2 : loop ? (s + 2) % tr->n : tr->n - 1;
        const Pose q = key_pose(gm, tr->key[prev]), r = key_pose(gm, tr->key[next]);
        x = spline(q.x, pa.x, pb.x, r.x, u);
        y = spline(q.y, pa.y, pb.y, r.y, u);
    }
    // the turn between two keyframes: the short way, or the way asked for, plus whole turns
    float turn = 0, at = 0;
    for (int i = 0; i <= s; i++) {
        const int from = tr->key[i], to = tr->key[(i + 1) % tr->n];
        float d = fmodf(gm->obj[to].rot - gm->obj[from].rot, 360.0f);
        if (d > 180) d -= 360;
        if (d < -180) d += 360;
        const int way = (int)prop(gm, from, 536, 0);
        if (way == 1 && d < 0) d += 360;
        if (way == 2 && d > 0) d -= 360;
        d += 360.0f * prop(gm, from, 537, 0) * (way == 2 ? -1.0f : 1.0f);
        if (i < s) turn += d; else at = turn + d * u;
    }
    const float ox = (x - p0.x) * trg_prop(t, 521, 1.0f), oy = (y - p0.y) * trg_prop(t, 545, 1.0f);
    const float rot = at * trg_prop(t, 522, 1.0f);
    const float sx = p0.sx > 0 ? (pa.sx + (pb.sx - pa.sx) * u) / p0.sx : 1;
    const float sy = p0.sy > 0 ? (pa.sy + (pb.sy - pa.sy) * u) / p0.sy : 1;
    const float kx = fmaxf(1 + (sx - 1) * trg_prop(t, 523, 1.0f), 0.02f);
    const float ky = fmaxf(1 + (sy - 1) * trg_prop(t, 546, 1.0f), 0.02f);
    if (ox != m->lx || oy != m->ly) trg_move_group(gm, m->group, ox - m->lx, oy - m->ly);
    if (rot != m->rot) trg_rotate_group(gm, m->group, m->center, rot - m->rot);
    if (fabsf(kx - m->dx) > 0.0001f || fabsf(ky - m->dy) > 0.0001f) trg_scale_group(gm, m->group, m->center, kx / m->dx, ky / m->dy);
    else { m->lx = ox; m->ly = oy; m->rot = rot; return; }
    m->lx = ox; m->ly = oy; m->rot = rot; m->dx = kx; m->dy = ky;
}

// ---------------------------------------------------------------- advanced follow

// MV_ADVANCED: dx, dy velocity in units per 60 Hz frame; follow: leading group (player: 1, 2).
static void advanced_start(Game *gm, Obj *t, int group)
{
    const int lead = trg_group(gm, t->tcenter);
    const int player = trg_prop(t, 138, 0) != 0 ? 1 : trg_prop(t, 200, 0) != 0 ? 2 : 0;
    if (!player && !trg_first(gm, lead)) return;
    for (int i = 0; i < gm->nmv; i++)
        if (gm->mv[i].kind == MV_ADVANCED && gm->mv[i].group == group) gm->mv[i--] = gm->mv[--gm->nmv];
    const Obj *c = trg_first(gm, group);
    if (!c || gm->nmv >= GD_MAX_MOVES) return;
    Move *m = &gm->mv[gm->nmv++];
    memset(m, 0, sizeof *m);
    m->kind = MV_ADVANCED;
    m->group = group;
    m->follow = lead;
    m->player = (unsigned char)player;
    m->src = (int)(t - gm->obj);
    m->dur = 1e9f;                       // until a stop trigger ends it
    m->t = -fmaxf(trg_prop(t, 292, 0), 0);
    m->chain = gm->chain;
    const float speed = trg_prop(t, 300, 0);
    if (speed != 0) {
        const float tx = player ? (player == 2 ? gm->p2.x : gm->p.x) : trg_first(gm, lead)->x;
        const float ty = player ? (player == 2 ? gm->p2.y : gm->p.y) : trg_first(gm, lead)->y;
        const float len = hypotf(tx - c->x, ty - c->y);
        float ang = len > 0.001f ? atan2f(tx - c->x, ty - c->y) : 0;
        ang += trg_prop(t, 563, 0) * PI / 180.0f;
        m->dx = sinf(ang) * speed;
        m->dy = cosf(ang) * speed;
    }
}

static void advanced_move(Game *gm, Move *m)
{
    const Obj *t = &gm->obj[m->src], *c = trg_first(gm, m->group), *lead = trg_first(gm, m->follow);
    const Player *p = m->player == 2 ? &gm->p2 : m->player == 1 ? &gm->p : NULL;
    if (!c || (!p && !lead)) return;
    const float f = STEP * 60.0f;   // all speeds are per 60 Hz frame
    float dx = (p ? p->x : lead->x) - c->x, dy = (p ? p->y : lead->y) - c->y;
    if (trg_prop(t, 306, 0)) dy = 0;
    if (trg_prop(t, 307, 0)) dx = 0;
    const float far = hypotf(dx, dy);
    const float range = trg_prop(t, 308, 0);
    if (range > 0 && far > range) dx = dy = 0;
    const float max = trg_prop(t, 298, 0), accel = trg_prop(t, 334, 0), steer = trg_prop(t, 316, 0);
    const float ux = far > 0.001f ? dx / far : 0, uy = far > 0.001f ? dy / far : 0;
    if (accel == 0 && steer == 0) {   // plain following: covers a share of the way each frame
        const float ease = fmaxf(trg_prop(t, 361, 10.0f), 1.0f);
        m->dx = dx / ease;
        m->dy = dy / ease;
    } else {
        m->dx += ux * accel * f;
        m->dy += uy * accel * f;
        if (steer != 0) {   // turns the velocity towards the target
            const float top = max > 0 ? max : 5.0f, share = fminf(steer * 0.01f * f, 1.0f);
            m->dx += (ux * top - m->dx) * share;
            m->dy += (uy * top - m->dy) * share;
        }
    }
    const float near = trg_prop(t, 359, 0);
    const float drag = near > 0 && far < near ? trg_prop(t, 561, 0) : trg_prop(t, 558, 0);
    if (drag > 0) {
        const float k = powf(fmaxf(1.0f - drag * 0.1f, 0.0f), f);
        m->dx *= k;
        m->dy *= k;
    }
    const float now = hypotf(m->dx, m->dy);
    if (max > 0 && now > max) { m->dx *= max / now; m->dy *= max / now; }
    if (m->dx != 0 || m->dy != 0) trg_move_group(gm, m->group, m->dx * f, m->dy * f);
}

void motion_move(Game *gm, Move *m)
{
    if (m->kind == MV_KEYFRAME) keyframe_move(gm, m);
    else advanced_move(gm, m);
}

// ---------------------------------------------------------------- area and enter triggers

// Takes over the settings a trigger carries; an edit trigger changes only those it names.
static void area_settings(AreaFx *a, const Obj *t)
{
#define SET(field, key) do { a->field = trg_prop(t, key, (float)a->field); } while (0)
    SET(length, 222); SET(offset, 220); SET(front, 263); SET(back, 264); SET(dead, 282);
    SET(rate, 243); SET(rate2, 249); SET(dist, 218); SET(angle, 231); SET(mx, 237); SET(my, 239);
    SET(rot, 270); SET(sx, 233); SET(sy, 235); SET(to_alpha, 275); SET(tint, 265);
    a->ease = (short)trg_prop(t, 242, a->ease);
    a->ease2 = (short)trg_prop(t, 248, a->ease2);
    a->colour = (short)trg_prop(t, 260, a->colour);
    a->dir = (unsigned char)trg_prop(t, 262, a->dir);
    a->xy = (unsigned char)trg_prop(t, 241, a->xy);
    a->relative = (unsigned char)trg_prop(t, 287, a->relative);
    a->inwards = (unsigned char)trg_prop(t, 276, a->inwards);
    a->ease_out = (unsigned char)trg_prop(t, 261, a->ease_out);
#undef SET
}

static void area_start(Game *gm, Obj *t, int group, int enter, int kind)
{
    AreaFx a;
    memset(&a, 0, sizeof a);
    a.src = (int)(t - gm->obj);
    a.kind = (unsigned char)kind;
    a.enter = (unsigned char)enter;
    a.group = (short)group;
    a.center = (short)trg_group(gm, t->tcenter);
    a.effect = (short)trg_prop(t, 225, 0);
    a.channel = (short)trg_prop(t, 344, 0);
    a.side = (unsigned char)trg_prop(t, 217, 0);
    a.length = 100; a.front = a.back = 1; a.rate = a.rate2 = 2; a.sx = a.sy = 1;
    a.from_alpha = 1; a.to_alpha = 0; a.tint = 1;
    area_settings(&a, t);
    int at = 0;
    while (at < gm->narea && !(gm->area[at].src == a.src)) at++;
    if (at >= GD_MAX_AREAS) return;
    gm->area[at] = a;
    if (at == gm->narea) gm->narea++;
}

int motion_fire(Game *gm, Obj *t, int group)
{
    const int id = t->tid;
    if (id == 3033) { keyframe_start(gm, t, group); return 1; }
    if (id == 3016) { advanced_start(gm, t, group); return 1; }
    if (id == 3660) {   // edit advanced follow: new speed for the followers of a group
        for (int i = 0; i < gm->nmv; i++) {
            Move *m = &gm->mv[i];
            if (m->kind != MV_ADVANCED || m->group != group) continue;
            m->dx *= trg_prop(t, 566, 1.0f);
            m->dy *= trg_prop(t, 568, 1.0f);
            const float speed = trg_prop(t, 300, 0);
            if (speed != 0) {
                const float len = hypotf(m->dx, m->dy);
                const float ang = (len > 0.001f ? atan2f(m->dx, m->dy) : 0) + trg_prop(t, 563, 0) * PI / 180.0f;
                m->dx = sinf(ang) * (len + speed);
                m->dy = cosf(ang) * (len + speed);
            }
        }
        return 1;
    }
    if (id == 3661) {   // re-target the followers of a group
        for (int i = 0; i < gm->nmv; i++) {
            Move *m = &gm->mv[i];
            if (m->kind != MV_ADVANCED || m->group != group) continue;
            m->player = (unsigned char)(trg_prop(t, 138, 0) != 0 ? 1 : trg_prop(t, 200, 0) != 0 ? 2 : 0);
            if (!m->player) m->follow = trg_group(gm, t->tcenter);
        }
        return 1;
    }
    if (id >= 3006 && id <= 3010) { if (!gm->sim) area_start(gm, t, group, 0, id - 3006); return 1; }
    if (id >= 3017 && id <= 3021) { if (!gm->sim) area_start(gm, t, 0, 1, id - 3017); return 1; }
    if (id >= 3011 && id <= 3015) {   // edit area: by effect id, else by the group
        const int by_effect = trg_prop(t, 255, 0) != 0 || trg_prop(t, 355, 0) != 0;
        for (int i = 0; !gm->sim && i < gm->narea; i++) {
            AreaFx *a = &gm->area[i];
            if (!a->enter && a->kind == id - 3011 && (by_effect ? a->effect == group : a->group == group)) area_settings(a, t);
        }
        return 1;
    }
    if (id == 3023 || id == 3024) {   // stop: enter channel, or area effect id
        const int which = id == 3023 ? (int)trg_prop(t, 344, 0) : group;
        for (int i = 0; !gm->sim && i < gm->narea; i++) {
            const AreaFx *a = &gm->area[i];
            if (id == 3023 ? a->enter && a->channel == which : !a->enter && (a->effect == which || a->group == which))
                gm->area[i--] = gm->area[--gm->narea];
        }
        return 1;
    }
    return 0;
}

// strength 0..1 of an effect at a distance
static float strength(const AreaFx *a, float d, int behind)
{
    const float length = a->length * (behind ? a->back : a->front);
    float u = length > 0.001f ? (d - a->offset) / length : 1;
    u = fminf(fmaxf(u, 0), 1);
    if (a->dead > 0 && a->dead < 1) u = fminf(fmaxf((u - a->dead) / (1 - a->dead), 0), 1);
    const int second = behind && a->ease_out;
    const float k = 1.0f - trg_ease(second ? a->ease2 : a->ease, u, second ? a->rate2 : a->rate);
    return a->inwards ? 1.0f - k : k;
}

static void add_effect_to(Game *gm, const AreaFx *a, int i, float k, float ax, float ay)
{
    if (k <= 0.0005f) return;
    AreaObj *f = &gm->areaobj[i];
    if (f->stamp != gm->area_stamp) *f = (AreaObj){gm->area_stamp, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0};
    switch (a->kind) {
    case AREA_MOVE:
        if (a->xy || (a->mx != 0 || a->my != 0)) { f->dx += a->mx * k; f->dy += a->my * k; }
        else if (a->relative) {   // away from the centre
            const float len = hypotf(ax, ay);
            if (len > 0.001f) { f->dx += ax / len * a->dist * k; f->dy += ay / len * a->dist * k; }
        } else {
            f->dx += sinf(a->angle * PI / 180.0f) * a->dist * k;
            f->dy += cosf(a->angle * PI / 180.0f) * a->dist * k;
        }
        break;
    case AREA_ROTATE: f->rot += a->rot * k; break;
    case AREA_SCALE: f->sx *= 1 + (a->sx - 1) * k; f->sy *= 1 + (a->sy - 1) * k; break;
    case AREA_FADE: f->alpha *= a->from_alpha + (a->to_alpha - a->from_alpha) * k; break;
    default: {
        const GdColorF c = gm_channel(gm, a->colour);
        const float share = fminf(fmaxf(a->tint * k, 0), 1);
        f->r += (c.r - f->r) * (f->tint > 0 ? share : 1);
        f->g += (c.g - f->g) * (f->tint > 0 ? share : 1);
        f->b += (c.b - f->b) * (f->tint > 0 ? share : 1);
        f->tint = f->tint + (1 - f->tint) * share;
        break;
    }
    }
}

void motion_areas(Game *gm, float left, float right)
{
    if (!gm->areaobj) return;
    gm->area_stamp++;
    if (!gm->narea) return;
    for (int n = 0; n < gm->narea; n++) {
        const AreaFx *a = &gm->area[n];
        if (gm->obj[a->src].off) continue;
        if (a->enter) {   // objects of the channel, by their distance from the screen edges
            for (int j = 0; j < gm->n; j++) {
                const Obj *o = &gm->obj[j];
                if (o->enter_ch != a->channel || o->type == OT_TRIGGER || o->x < left || o->x > right) continue;
                const float in = right - 120 - o->x, out = o->x - (left + 120);   // from the right / left edge
                const float d = a->side == 1 ? in : a->side == 2 ? out : fminf(in, out);
                add_effect_to(gm, a, j, strength(a, d, in > out), o->x < (left + right) * 0.5f ? -1.0f : 1.0f, 0);
            }
            continue;
        }
        const Obj *c = trg_first(gm, a->center);
        if (!c || a->group <= 0 || a->group >= GD_GROUPS) continue;
        for (int k = gm->grp_start[a->group]; k < gm->grp_start[a->group + 1]; k++) {
            const int j = gm->grp_items[k];
            const Obj *o = &gm->obj[j];
            if (o->x < left - 300 || o->x > right + 300) continue;
            const float dx = o->x - c->x, dy = o->y - c->y;
            const float d = a->dir == 1 ? fabsf(dx) : a->dir == 2 ? fabsf(dy) : hypotf(dx, dy);
            add_effect_to(gm, a, j, strength(a, d, (a->dir == 2 ? dy : dx) < 0), dx, dy);
        }
    }
}

float motion_apply(Game *gm, int i, Obj *o)
{
    gm->tinted = NULL;
    if (!gm->areaobj) return 1;
    const AreaObj *f = &gm->areaobj[i];
    if (f->stamp != gm->area_stamp) return 1;
    o->x += f->dx;
    o->y += f->dy;
    o->rot += f->rot;
    o->sx *= f->sx;
    o->sy *= f->sy;
    if (f->tint > 0) gm->tinted = f;
    return fminf(fmaxf(f->alpha, 0), 1);
}

void motion_tint(const Game *gm, GdColorF *c)
{
    const AreaObj *f = gm->tinted;
    if (!f) return;
    c->r += (f->r - c->r) * f->tint;
    c->g += (f->g - c->g) * f->tint;
    c->b += (f->b - c->b) * f->tint;
}
