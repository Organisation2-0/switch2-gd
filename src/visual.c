// SPDX-License-Identifier: GPL-3.0-or-later
// What only changes the picture or the sound: shader effects, middleground, particle objects,
// objects fixed to the screen and sound triggers.
#include <stdio.h>

#include "game_int.h"

#define PI 3.14159265f

static float prop(const Obj *t, int key, float def) { return trg_prop(t, key, def); }

// ---------------------------------------------------------------- loading

static const char *particle_text(const Obj *o) { return o->id == 2065 && o->data ? o->data->text : NULL; }

void visual_load(Game *gm)
{
    int n = 0;
    for (int i = 0; i < gm->n; i++) n += particle_text(&gm->obj[i]) != NULL;
    if (!n) return;
    gm->emitters = calloc((size_t)n, sizeof *gm->emitters);
    if (!gm->emitters) return;
    for (int i = 0; i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        if (!particle_text(o)) continue;
        Emitter *e = &gm->emitters[gm->nemitters];
        e->obj = i;
        const char *p = particle_text(o);
        for (int k = 0; k < PARTICLE_FIELDS && *p; k++) {   // "value a value a ..."
            e->v[k] = (float)atof(p);
            while (*p && *p != 'a') p++;
            if (*p) p++;
        }
        o->value = (float)(++gm->nemitters);
    }
}

void visual_free(Game *gm)
{
    free(gm->emitters);
    gm->emitters = NULL;
    gm->nemitters = 0;
}

void visual_reset(Game *gm)
{
    gm->nburst = 0;
    gm->nsnd = 0;
    for (int i = 0; i < gm->n; i++) gm->obj[i].on_screen = 0;
    Camera *v = &gm->view;
    v->mg_id = gm->mg_id0;
    v->mg_mx = 0.3f;
    v->mg_my = 0.5f;
    v->bg_mx = 0.1f;
}

// What the values of the effects stand for (Shader.v); the property a trigger takes each from is
// named in shader_trigger.
enum { WV_STRENGTH, WV_SPEED, WV_WIDTH, WV_THICK, WV_IN, WV_OUT, WV_OFFSET, WV_INNER, WV_OUTER, WV_REACH, WV_X, WV_Y };
enum { GL_STRENGTH, GL_SPEED, GL_HEIGHT, GL_SHIFT, GL_COLX, GL_COLY };
enum { CG_SPEED, CG_STRENGTH, CG_RGB, CG_THICK, CG_LINES, CG_SEGMENT };
enum { LN_STRENGTH, LN_SIZE, LN_FADE, LN_X, LN_Y };
enum { RB_SIZE, RB_FADE, RB_X, RB_Y };
enum { BU_VALUE, BU_RADIUS, BU_X, BU_Y };
enum { PI_X, PI_Y, PI_RADIUS, PI_MOD, PI_OFFX, PI_OFFY };
enum { MB_X, MB_Y, MB_FADE };
// pixelate, chromatic, split: 0 across, 1 up; invert: 0 amount, 1..3 colour; colour: 0..2 factors,
// 3..5 added; grey, sepia, hue: 0

void visual_step(Camera *v, float dt)
{
    if (v->mg_y.t < v->mg_y.dur) v->mg_y.t += dt;
    for (int i = 0; i < SH_COUNT; i++) {
        Shader *s = &v->shader[i];
        if (!s->on) continue;
        for (int k = 0; k < GD_SHADER_VALUES; k++)
            if (s->v[k].t < s->v[k].dur) s->v[k].t += dt;
        if (i == SH_SHOCKWAVE || i == SH_SHOCKLINE) s->clock += dt * trg_tween(&s->v[WV_SPEED]);
        if (i == SH_CHROMAGLITCH) s->clock = fmodf(s->clock + dt * trg_tween(&s->v[CG_SPEED]), 1000.0f);
    }
}

// ---------------------------------------------------------------- triggers

static void sound(Game *gm, const Sound *s)
{
    if (!gm->sim && gm->nsnd < GD_MAX_SOUNDS) gm->snd[gm->nsnd++] = *s;
}

static void sound_trigger(Game *gm, const Obj *t, int group)
{
    Sound s;
    memset(&s, 0, sizeof s);
    s.volume = prop(t, 406, 1.0f);
    s.speed = prop(t, 404, 0);
    s.start = prop(t, 408, 0) / 1000.0f;
    s.end = prop(t, 410, 0) / 1000.0f;
    s.fade_in = prop(t, 409, 0) / 1000.0f;
    s.fade_out = prop(t, 411, 0) / 1000.0f;
    s.loop = prop(t, 413, 0) != 0;
    s.uid = (int)prop(t, 416, 0);
    s.group = (int)prop(t, 455, 0);
    s.stop = prop(t, 417, 0) != 0;
    s.set_volume = prop(t, 418, 0) != 0;
    if (t->tid == 3602) {
        s.type = SND_SFX;
        s.id = (int)prop(t, 392, 0);
    } else if (t->tid == 1934) {
        s.type = SND_SONG;
        s.id = (int)prop(t, 392, 0);
        s.group = (int)prop(t, 432, 0);   // song channel, 0: the level's music
    } else {
        s.type = t->tid == 3603 ? SND_SFX_EDIT : SND_SONG_EDIT;
        s.fade_in = t->tdur;              // the change takes the trigger's time
        if (t->tid == 3605) s.group = (int)prop(t, 432, 0);
        else if (!s.uid && !s.group) s.group = group;
    }
    sound(gm, &s);
}

static void set_now(Shader *s, int i, float to) { s->v[i] = (Tween){to, to, 0, 0, 0, 0}; }

// A level stores every value that is not 0: a trigger without one sets that value to 0, which is how
// effects are faded out again. (Key 531 only concerns the editor's preview.)
static void shader_trigger(Game *gm, const Obj *t, int group)
{
    Camera *v = &gm->view;
    const int id = t->tid;
    if (id == 2904) {   // setup: the layers the effects reach; switch everything off
        const int bottom = (int)prop(t, 196, 0), top = (int)prop(t, 197, 0);
        if (top > 0 && top < 32) {
            v->shader_top = top;
            v->shader_bottom = bottom > 0 && bottom < 32 ? bottom : 0;
        }
        if (prop(t, 192, 0) != 0) memset(v->shader, 0, sizeof v->shader);
        return;
    }
    const int k = id == 2905 ? SH_SHOCKWAVE : id == 2907 ? SH_SHOCKLINE : id == 2909 ? SH_GLITCH
                : id == 2910 ? SH_CHROMATIC : id == 2911 ? SH_CHROMAGLITCH : id == 2912 ? SH_PIXELATE
                : id == 2913 ? SH_LENS : id == 2914 ? SH_RADIAL : id == 2915 ? SH_MOTION : id == 2916 ? SH_BULGE
                : id == 2917 ? SH_PINCH : id == 2919 ? SH_GREY : id == 2920 ? SH_SEPIA : id == 2921 ? SH_INVERT
                : id == 2922 ? SH_HUE : id == 2923 ? SH_COLOUR : id == 2924 ? SH_SPLIT : -1;
    if (k < 0) return;
    Shader *s = &v->shader[k];
    if (!s->on) {
        memset(s, 0, sizeof *s);
        if (k == SH_COLOUR) for (int i = 0; i < 3; i++) set_now(s, i, 1);
        if (k == SH_INVERT) for (int i = 1; i < 4; i++) set_now(s, i, 1);
    }
    s->on = 1;
    const int use_x = prop(t, 188, 0) != 0, use_y = prop(t, 190, 0) != 0;
    const unsigned char relative = prop(t, 514, 0) != 0 ? SF_RELATIVE : 0;
    int centred = 0;   // the effect sits on an object or an icon
#define GLIDE(i, to) trg_tween_to(&s->v[i], to, t)
    switch (k) {
    case SH_GREY:
        GLIDE(0, prop(t, 176, 0));
        s->flags = (unsigned char)((use_x ? SF_DUAL : 0) | (use_y ? SF_TINT : 0));   // by luminance, tinted
        s->channel = use_y ? (int)prop(t, 51, 0) : 0;
        break;
    case SH_SEPIA: case SH_HUE: GLIDE(0, prop(t, 176, 0)); break;
    case SH_INVERT:
        GLIDE(0, prop(t, 176, 0));
        GLIDE(1, use_x ? prop(t, 179, 0) : 1);
        GLIDE(2, use_x ? prop(t, 180, 0) : 1);
        GLIDE(3, use_x ? prop(t, 189, 0) : 1);
        s->flags = prop(t, 194, 0) != 0 ? SF_HARD : 0;   // shares are limited to 1
        break;
    case SH_COLOUR: {   // factors (red, green, blue), then amounts added
        static const int key[6] = {176, 191, 175, 179, 180, 189};
        for (int i = 0; i < 6; i++) GLIDE(i, prop(t, key[i], 0));
        break;
    }
    case SH_CHROMATIC: case SH_PIXELATE: case SH_SPLIT:   // across and up, each only if chosen
        if (use_x) GLIDE(0, prop(t, 180, 0));
        if (use_y) GLIDE(1, prop(t, 189, 0));
        s->flags = (unsigned char)(relative | (prop(t, 515, 0) != 0 ? SF_HARD : 0));
        break;
    case SH_MOTION:
        if (use_x) GLIDE(MB_X, prop(t, 180, 0));
        if (use_y) GLIDE(MB_Y, prop(t, 189, 0));
        GLIDE(MB_FADE, prop(t, 181, 0));
        s->flags = (unsigned char)(relative | (prop(t, 194, 0) != 0 ? SF_DUAL : 0));
        s->channel = prop(t, 51, 0) != 0;   // bound to an object: follows its movement, left out
        break;
    case SH_LENS:
        GLIDE(LN_STRENGTH, prop(t, 176, 0));
        GLIDE(LN_SIZE, prop(t, 179, 0));
        GLIDE(LN_FADE, prop(t, 181, 0));
        GLIDE(LN_X, prop(t, 290, 0));
        GLIDE(LN_Y, prop(t, 291, 0));
        s->channel = (int)prop(t, 71, 0);
        s->flags = relative;
        centred = 1;
        break;
    case SH_RADIAL:
        GLIDE(RB_SIZE, prop(t, 179, 0));
        GLIDE(RB_FADE, prop(t, 181, 0));
        GLIDE(RB_X, prop(t, 290, 0));
        GLIDE(RB_Y, prop(t, 291, 0));
        s->flags = 0;
        centred = use_x;
        break;
    case SH_BULGE:
        GLIDE(BU_VALUE, prop(t, 176, 0));
        GLIDE(BU_RADIUS, prop(t, 180, 0));
        GLIDE(BU_X, prop(t, 290, 0));
        GLIDE(BU_Y, prop(t, 291, 0));
        s->flags = relative;
        centred = use_x;
        break;
    case SH_PINCH:
        if (use_y) GLIDE(PI_X, prop(t, 180, 0));
        if (prop(t, 194, 0) != 0) GLIDE(PI_Y, prop(t, 189, 0));
        GLIDE(PI_RADIUS, prop(t, 512, 0));
        GLIDE(PI_MOD, prop(t, 179, 0));
        GLIDE(PI_OFFX, prop(t, 290, 0));
        GLIDE(PI_OFFY, prop(t, 291, 0));
        s->flags = relative;
        centred = use_x;
        break;
    case SH_SHOCKWAVE: case SH_SHOCKLINE: {
        // without "animate" the new wave starts over at once with its own settings
        const int animate = prop(t, 513, 0) != 0;
        static const int key[12] = {176, 175, 179, 180, 181, 182, 177, 183, 191, 512, 290, 291};
        for (int i = 0; i < 12; i++) {
            if (animate) GLIDE(i, prop(t, key[i], 0));
            else set_now(s, i, prop(t, key[i], 0));
        }
        if (!animate) s->clock = 0;
        s->flags = (unsigned char)(relative | (prop(t, 184, 0) != 0 ? SF_INVERT : 0));
        if (k == SH_SHOCKLINE)
            s->flags |= (unsigned char)((prop(t, 186, 0) != 0 ? SF_UPRIGHT : 0) | (prop(t, 185, 0) != 0 ? SF_FLIP : 0) |
                                        (prop(t, 187, 0) != 0 ? SF_DUAL : 0));
        if (!use_y) s->flags |= SF_FIXED;   // stays where its object was
        centred = use_x;
        break;
    }
    case SH_GLITCH: {
        static const int key[6] = {176, 175, 179, 191, 181, 182};
        GLIDE(GL_STRENGTH, prop(t, 176, 0));
        for (int i = 1; i < 6; i++) set_now(s, i, prop(t, key[i], 0));
        s->flags = relative;
        break;
    }
    case SH_CHROMAGLITCH:
        if (prop(t, 192, 0) != 0) { memset(s, 0, sizeof *s); return; }
        GLIDE(CG_SPEED, prop(t, 175, 0));
        GLIDE(CG_STRENGTH, prop(t, 176, 0));
        GLIDE(CG_RGB, prop(t, 180, 0));
        GLIDE(CG_THICK, prop(t, 179, 0));
        GLIDE(CG_LINES, prop(t, 191, 0));
        GLIDE(CG_SEGMENT, 1.0f / fmaxf(prop(t, 189, 0), 0.1f));
        s->flags = relative;
        break;
    default: break;
    }
#undef GLIDE
    s->center = centred ? group : 0;
    s->player = (unsigned char)(!centred ? 0 : prop(t, 138, 0) != 0 ? 1 : prop(t, 200, 0) != 0 ? 2 : 0);
    const Obj *at = (s->flags & SF_FIXED) && s->center ? trg_first(gm, s->center) : NULL;
    if (at) { s->x = at->x; s->y = at->y; }
    else s->flags &= (unsigned char)~SF_FIXED;
}

static int emitter_of(const Game *gm, int group)
{
    const Obj *o = trg_first(gm, group);
    return o && o->id == 2065 ? (int)o->value - 1 : -1;
}

int visual_fire(Game *gm, Obj *t, int group)
{
    const int id = t->tid;
    Camera *v = &gm->view;
    if (id >= 2904 && id <= 2924) {
        if (!gm->sim) shader_trigger(gm, t, group);
        return 1;
    }
    if (id == 3602 || id == 3603 || id == 3605 || id == 1934) { sound_trigger(gm, t, group); return 1; }
    switch (id) {
    case 2999:   // middleground: its height
        if (!gm->sim) trg_tween_to(&v->mg_y, t->tdy, t);
        return 1;
    case 3031:
        if (!gm->sim) v->mg_id = (int)prop(t, 533, 0);
        return 1;
    case 3612:
        if (!gm->sim) { v->mg_mx = prop(t, 143, v->mg_mx); v->mg_my = prop(t, 144, v->mg_my); }
        return 1;
    case 3606:
        if (!gm->sim) v->bg_mx = prop(t, 143, v->bg_mx) * 0.1f;
        return 1;
    case 3608: {   // spawn particle: the particle object of one group at the place of another
        const int e = emitter_of(gm, group);
        const Obj *at = trg_first(gm, trg_group(gm, t->tcenter));
        if (gm->sim || e < 0 || !at) return 1;
        const int slot = gm->nburst < GD_MAX_BURSTS ? gm->nburst++ : 0;
        const float spread = prop(t, 555, 0), turn = prop(t, 553, 0);
        gm->bursts[slot] = (Burst){e, at->x + prop(t, 547, 0) + prop(t, 549, 0) * (2 * gm_random() - 1),
                                   at->y + prop(t, 548, 0) + prop(t, 550, 0) * (2 * gm_random() - 1),
                                   (prop(t, 551, 0) != 0 ? at->rot : 0) + prop(t, 552, 0) + turn * (2 * gm_random() - 1),
                                   prop(t, 554, 1.0f) + spread * (2 * gm_random() - 1), gm->anim_t};
        return 1;
    }
    case 3613: {   // UI: the objects of a group stay on the screen, placed relative to a reference object
        if (gm->sim || group <= 0 || group >= GD_GROUPS || !trg_first(gm, group)) return 1;
        int at = 0;
        while (at < v->nui && v->ui[at].group != group) at++;
        if (at >= (int)(sizeof v->ui / sizeof v->ui[0])) return 1;
        v->ui[at].group = (short)group;
        v->ui[at].ref = (short)trg_group(gm, t->tcenter);
        const int h = (int)prop(t, 385, 0), w = (int)prop(t, 386, 0);
        v->ui[at].ax = h == 3 ? 0.0f : h == 4 ? 1.0f : 0.5f;
        v->ui[at].ay = w == 7 ? 1.0f : w == 8 ? 0.0f : 0.5f;
        if (at == v->nui) v->nui++;
        for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) gm->obj[gm->grp_items[k]].on_screen = 1;
        return 1;
    }
    default: return 0;
    }
}

// ---------------------------------------------------------------- middleground

void visual_middleground(Game *gm, Gfx *g, float ox, float oy, float w, float h, float margin, float scale)
{
    if (!gm->mg || !gm->mg->tex || gm->view.mg_id <= 0 || (gm->logic.options & OPT_HIDE_MG)) return;
    const Camera *v = &gm->view;
    const float unit = scale / 2.0f;   // the pictures have two pixels per unit
    const float tile = gm->mg->w * unit;
    // at the start it stands on the ground line; it follows the camera more slowly than the level
    const float up = trg_tween(&v->mg_y) + 90.0f - (gm->cam_bottom + 90.0f) * v->mg_my;
    const float base = oy + h - up * scale;
    float off = fmodf(gm->cam_x * v->mg_mx * scale, tile);
    if (off < 0) off += tile;
    const GfxImage *layer[2] = {gm->mg, gm->mg2};
    for (int i = 0; i < 2; i++) {
        if (!layer[i] || !layer[i]->tex) continue;
        GdColorF c = gm_channel(gm, i ? 1014 : 1013);
        const float high = layer[i]->h * unit;
        if (c.a <= 0.003f) continue;
        for (float x = -off - ceilf(margin / tile) * tile; x < w + margin; x += tile) {
            gfx_image_rect(g, layer[i], (SDL_FRect){0, 0, (float)layer[i]->w, (float)layer[i]->h},
                           (SDL_FRect){ox + x, base - high, tile, high}, c);
        }
    }
    if (base < oy + h + margin) {   // below it its darkest part goes on
        GdColorF c = gm_channel(gm, 1013);
        const GfxImage *m = gm->mg;
        gfx_image_rect(g, m, (SDL_FRect){0, (float)m->h - 2, (float)m->w, 1},
                       (SDL_FRect){ox - margin, base - 1, w + 2 * margin, oy + h + margin - base + 1}, c);
    }
}

// ---------------------------------------------------------------- particle objects

static float hash(unsigned a, unsigned b)
{
    unsigned x = a * 2654435761u ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13;
    return (float)(x & 0xffffff) / 16777216.0f;
}

// a value with its random spread, the same for one particle every time it is drawn
static float spread(const float *v, int field, unsigned seed, unsigned n)
{
    return v[field] + v[field + 1] * (2 * hash(seed + (unsigned)field * 7919u, n) - 1);
}

// The particles of one emitter around a screen place. since: seconds the emitter has run;
// for_ever: it never stops. The particles follow from their number, so nothing has to be stored.
static void particles(Gfx *g, const float *v, unsigned seed, float since, int for_ever, float x, float y, float s,
                      float rot, float size_factor, GdColorF tint, int tinted)
{
    const float max = fminf(v[0], 200.0f), life = fmaxf(v[2], 0.01f);
    if (max < 1 || since < 0) return;
    const float rate = v[4] > 0 ? v[4] : max / life;
    const float duration = v[1];
    const float longest = life + fabsf(v[3]);
    float last = since, first = since - longest;
    if (!for_ever && duration >= 0) last = fminf(since, duration);
    if (first < 0) first = 0;
    char name[40];
    snprintf(name, sizeof name, "particle_%02d_001.png", (int)v[57] > 0 ? (int)v[57] : 1);
    const GdFrame *f = gfx_find(g, name);
    if (!f) f = gfx_find(g, "particle_00_001.png");
    const int from = (int)ceilf(first * rate), to = (int)floorf(last * rate);
    int drawn = 0;
    for (int n = to; n >= from && drawn < (int)max; n--) {
        const unsigned un = (unsigned)n;
        const float age = since - n / rate;
        const float mine = fmaxf(spread(v, 2, seed, un), 0.01f);
        if (age < 0 || age > mine) continue;
        drawn++;
        const float k = age / mine;
        const float angle = (spread(v, 5, seed, un) - rot) * PI / 180.0f;
        float px, py;
        if (v[51] > 0.5f) {   // radius mode: circling in or out
            const float start = spread(v, 45, seed, un);
            const float end = v[47] < 0 ? start : spread(v, 47, seed, un);
            const float a = angle + spread(v, 49, seed, un) * PI / 180.0f * age, r = start + (end - start) * k;
            px = -cosf(a) * r;
            py = -sinf(a) * r;
        } else {
            const float speed = spread(v, 7, seed, un);
            const float radial = spread(v, 13, seed, un), side = spread(v, 15, seed, un);
            const float c = cosf(angle), sn = sinf(angle), half = 0.5f * age * age;
            px = c * speed * age + (v[11] + radial * c - side * sn) * half;
            py = sn * speed * age + (v[12] + radial * sn + side * c) * half;
        }
        px += v[9] * (2 * hash(seed + 101, un) - 1);
        py += v[10] * (2 * hash(seed + 102, un) - 1);
        const float a0 = spread(v, 17, seed, un), a1 = v[29] < 0 || v[64] > 0.5f ? a0 : spread(v, 29, seed, un);
        const float size = fmaxf(a0 + (a1 - a0) * k, 0) * size_factor;
        GdColorF c;
        float ch[4];
        for (int i = 0; i < 4; i++) {
            const float from_c = spread(v, 21 + i * 2, seed, un), to_c = spread(v, 33 + i * 2, seed, un);
            ch[i] = fminf(fmaxf(from_c + (to_c - from_c) * k, 0), 1);
        }
        c = (GdColorF){ch[0] * 255, ch[1] * 255, ch[2] * 255, ch[3]};
        if (tinted) { c.r = tint.r; c.g = tint.g; c.b = tint.b; }
        c.a *= tint.a;
        if (v[41] > 0 && age < v[41]) c.a *= age / v[41];
        if (v[43] > 0 && mine - age < v[43]) c.a *= (mine - age) / v[43];
        const float turn = spread(v, 19, seed, un) + (spread(v, 31, seed, un) - spread(v, 19, seed, un)) * k +
                           (v[55] > 0.5f ? -angle * 180.0f / PI : 0);
        const float sx = x + px * s, sy = y - py * s;
        if (f) {
            const float wide = (float)(f->srcw > 0 ? f->srcw : f->w);
            const float factor = size * s / (wide > 0 ? wide : 1);
            gfx_frame(g, f, sx, sy, turn, factor, factor, c, v[53] > 0.5f);
        } else {
            const SDL_FRect r = {sx - size * s * 0.5f, sy - size * s * 0.5f, size * s, size * s};
            if (v[53] > 0.5f) gfx_glow_fill(g, r, c); else gfx_fill(g, r, c);
        }
    }
}

void visual_particles(Game *gm, Gfx *g, int front, float ox, float oy, float vw, float vh, float scale)
{
    if (g->no_particles || !gm->nemitters) return;
    for (int i = 0; i < gm->nemitters; i++) {
        const Emitter *e = &gm->emitters[i];
        const Obj *o = &gm->obj[e->obj];
        if (o->off || o->on_screen || (o->zlayer >= 5) != front) continue;
        if (o->x < gm->cam_x - 200 || o->x > gm->cam_x + vw + 200) continue;
        if (prop(o, 123, 0) != 0) continue;   // waits for a trigger
        float alpha = 1;
        for (int k = 0; k < o->ngrp; k++) alpha *= gm->galpha[o->grp[k]];
        GdColorF tint = {255, 255, 255, alpha};
        const int tinted = prop(o, 146, 0) != 0;
        if (tinted) { tint = gm_channel(gm, o->ch_main); tint.a *= alpha; }
        particles(g, e->v, (unsigned)e->obj * 31u + 7u, gm->anim_t + 20.0f, 1,
                  ox + (o->x - gm->cam_x) * scale, oy + (vh - (o->y - gm->cam_bottom)) * scale, scale, o->rot,
                  fabsf(o->sx), tint, tinted);
    }
    if (!front) return;
    for (int i = 0; i < gm->nburst; i++) {
        const Burst *b = &gm->bursts[i];
        const Emitter *e = &gm->emitters[b->emitter];
        const float since = gm->anim_t - b->start;
        const GdColorF white = {255, 255, 255, 1};
        const int endless = e->v[1] < 0;
        if (!endless && since > e->v[1] + e->v[2] + fabsf(e->v[3]) + 0.1f) {
            gm->bursts[i--] = gm->bursts[--gm->nburst];
            continue;
        }
        particles(g, e->v, (unsigned)(b->start * 1000.0f) + (unsigned)i, since, 0,
                  ox + (b->x - gm->cam_x) * scale, oy + (vh - (b->y - gm->cam_bottom)) * scale, scale, b->rot,
                  b->scale, white, 0);
    }
}

// ---------------------------------------------------------------- objects on the screen

void visual_screen_objects(Game *gm, Gfx *g, float ox, float oy, float vw, float vh, float scale)
{
    const Camera *v = &gm->view;
    const float s = scale / g->atlas.px_per_unit;
    for (int u = 0; u < v->nui; u++) {
        const int group = v->ui[u].group;
        const Obj *ref = trg_first(gm, v->ui[u].ref);
        if (!ref) ref = trg_first(gm, group);
        if (!ref) continue;
        for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
            const Obj *o = &gm->obj[gm->grp_items[k]];
            if (o->type == OT_TRIGGER || o->off || o->nodraw) continue;
            gm_draw_object(gm, g, o, ox + (vw * v->ui[u].ax + (o->x - ref->x)) * scale,
                           oy + (vh * (1 - v->ui[u].ay) - (o->y - ref->y)) * scale, s);
        }
    }
}

// ---------------------------------------------------------------- shader effects

// Where an effect sits in the picture (fractions from the lower left corner).
static void shader_center(const Game *gm, const Shader *s, float offx, float offy, float ox, float oy,
                          float sw, float sh, float *cx, float *cy)
{
    *cx = 0.5f + 0.5f * offx;
    *cy = 0.5f + 0.5f * offy;
    const Obj *at = trg_first(gm, s->center);
    if (!at && !s->player) return;
    const Player *p = s->player == 2 ? &gm->p2 : &gm->p;
    float x, y;
    if (s->player) game_screen_pos(gm, p->x, p->y, &x, &y);
    else if (s->flags & SF_FIXED) game_screen_pos(gm, s->x, s->y, &x, &y);
    else game_screen_pos(gm, at->x, at->y, &x, &y);
    *cx = (x - ox) / sw;
    *cy = 1.0f - (y - oy) / sh;
}

static float clamped(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }

// The sizes and strengths follow the original (Geometry Dash 2.2081): its screen is 320 units
// high, and most of the distances of the effects count in widths of the picture.
void visual_shaders(Game *gm, Gfx *g, float ox, float oy, float vw, float vh, float scale, int layered)
{
    const Camera *v = &gm->view;
    GfxEffects fx;
    memset(&fx, 0, sizeof fx);
    const float sw = vw * scale, sh = vh * scale;   // the picture in drawing coordinates
    const float aspect = sh > 0 ? sw / sh : 1.7778f, wide = 320.0f * aspect;
    const float camera = fmaxf(trg_tween(&v->zoom), 0.05f);
    fx.v[0][0] = gm->anim_t;
    fx.v[0][1] = aspect;
    for (int k = 0; k < SH_COUNT; k++) {
        const Shader *s = &v->shader[k];
        if (!s->on) continue;
        float a[GD_SHADER_VALUES];
        for (int i = 0; i < GD_SHADER_VALUES; i++) a[i] = trg_tween(&s->v[i]);
        const float z = s->flags & SF_RELATIVE ? camera : 1.0f;
        float cx, cy;
        switch (k) {
        case SH_SPLIT:   // that many more pictures beside each other
            if (fabsf(a[0]) > 0.001f || fabsf(a[1]) > 0.001f) {
                fx.v[1][0] = 1 + a[0]; fx.v[1][1] = 1 + a[1]; fx.any = 1;
            }
            break;
        case SH_BULGE: {
            const float radius = a[BU_RADIUS] * z;
            if (a[BU_VALUE] <= 0.001f || radius <= 0.001f) break;
            shader_center(gm, s, a[BU_X], a[BU_Y], ox, oy, sw, sh, &cx, &cy);
            const float reach = 0.7071f * z, bend = PI / reach * 0.45f * fminf(a[BU_VALUE], 1.1f);
            fx.v[2][0] = cx; fx.v[2][1] = cy; fx.v[2][2] = radius; fx.v[2][3] = bend;
            fx.v[3][0] = reach / tanf(reach * bend);
            fx.any = 1;
            break;
        }
        case SH_PINCH: {
            const float radius = a[PI_RADIUS] * z;
            if ((fabsf(a[PI_X]) <= 0.001f && fabsf(a[PI_Y]) <= 0.001f) || radius <= 0.001f) break;
            shader_center(gm, s, a[PI_OFFX], a[PI_OFFY], ox, oy, sw, sh, &cx, &cy);
            const float mod = fmaxf(a[PI_MOD], 0.001f);
            fx.v[3][1] = radius; fx.v[4][0] = cx; fx.v[4][1] = cy;
            for (int i = 0; i < 2; i++) {
                const float value = fabsf(a[PI_X + i]) > 0.001f ? a[PI_X + i] : 0.001f;
                fx.v[4][2 + i] = 2 * value;
                fx.v[5][i] = mod / atanf(2 * value * mod);
            }
            fx.any = 1;
            break;
        }
        case SH_SHOCKWAVE: case SH_SHOCKLINE: {
            if (a[WV_STRENGTH] <= 0.0005f) break;
            const float thick = fmaxf(a[WV_THICK], 0.001f) * 0.2f, in = a[WV_IN] * thick, out = a[WV_OUT] * thick;
            const float freq = 22.0f / fmaxf(a[WV_WIDTH], 0.001f);
            const int turned = (s->flags & SF_INVERT) != 0;
            float t = s->clock + a[WV_OFFSET] * 0.1f;
            if (k == SH_SHOCKLINE && turned) t = (in + thick + out) * (s->flags & SF_DUAL ? 0.5f : 1.0f) - t;
            // the ring runs outwards ever faster; a line (and a wave turned inwards) steadily
            const float front = (k == SH_SHOCKWAVE && !turned ? t * t : t) - in;
            // where it rises from nothing, is full, begins to fall and has gone, from the front back
            const float edge[4] = {front + in + 0.001f, front, front - thick + 0.001f, front - thick - out};
            const float fade = a[WV_REACH] != 0 ? 1.0f / (a[WV_REACH] * 0.5f * z) : 0;
            shader_center(gm, s, a[WV_X], a[WV_Y], ox, oy, sw, sh, &cx, &cy);
            if (k == SH_SHOCKWAVE) {
                fx.v[6][0] = cx; fx.v[6][1] = cy; fx.v[6][2] = freq * t; fx.v[6][3] = freq / z;
                fx.v[7][0] = a[WV_STRENGTH] * 0.1f * z; fx.v[7][1] = fade;
                fx.v[7][2] = a[WV_OUTER] * z; fx.v[7][3] = a[WV_INNER] * 0.5f * z;
                for (int i = 0; i < 4; i++) fx.v[8][i] = edge[i] * z;
                fx.v[9][0] = turned ? 1.0f : 0;
            } else {
                const int upright = (s->flags & SF_UPRIGHT) != 0;
                fx.v[10][0] = upright ? cy : cx; fx.v[10][1] = (float)upright;
                fx.v[10][2] = s->flags & SF_FLIP ? 1.0f : 0; fx.v[10][3] = s->flags & SF_DUAL ? 1.0f : 0;
                fx.v[11][0] = freq * t; fx.v[11][1] = freq / z;
                fx.v[11][2] = a[WV_STRENGTH] * 0.01f * z; fx.v[11][3] = fade;
                for (int i = 0; i < 4; i++) fx.v[12][i] = edge[i] * z;
            }
            fx.any = 1;
            break;
        }
        case SH_GLITCH: {   // one slice of the picture jumps aside, one of its colours further
            const float strong = a[GL_STRENGTH];
            if (strong <= 0.001f) break;
            const unsigned seed = (unsigned)(gm->anim_t * fmaxf(a[GL_SPEED], 0.001f) * 20.0f);
            const float bottom = hash(seed, 1), pick = hash(seed, 6);
            fx.v[13][0] = bottom;
            fx.v[13][1] = bottom + hash(seed, 2) * strong * a[GL_HEIGHT] * z;
            fx.v[13][2] = (2 * hash(seed, 3) - 1) * strong * a[GL_SHIFT] * 0.02f * z;
            fx.v[13][3] = pick < 0.33f ? 1.0f : pick < 0.66f ? 2.0f : 3.0f;
            fx.v[14][0] = (2 * hash(seed, 4) - 1) * strong * a[GL_COLX] * 0.02f * z;
            fx.v[14][1] = (2 * hash(seed, 5) - 1) * strong * a[GL_COLY] * 0.02f * z;
            fx.any = 1;
            break;
        }
        case SH_PIXELATE:   // blocks of that many units
            if (a[0] <= 1.001f && a[1] <= 1.001f) break;
            fx.v[14][2] = fmaxf(a[0] * z, 1.0f) / wide;
            fx.v[14][3] = fmaxf(a[1] * z, 1.0f) / 320.0f;
            fx.v[15][0] = s->flags & SF_HARD ? 1.0f : 0;
            fx.any = 1;
            break;
        case SH_CHROMATIC:   // red and blue drawn apart by that many units
            if (fabsf(a[0]) > 0.001f || fabsf(a[1]) > 0.001f) {
                fx.v[15][2] = a[0] * z / wide; fx.v[15][3] = a[1] * z / 320.0f; fx.any = 1;
            }
            break;
        case SH_CHROMAGLITCH: {
            const float rgb = a[CG_RGB], strong = a[CG_STRENGTH], lines = a[CG_LINES];
            if (fabsf(rgb) <= 0.001f && fabsf(strong) <= 0.001f && fabsf(lines) <= 0.001f) break;
            const float cells = a[CG_SEGMENT] / (z * aspect);
            fx.v[16][0] = fabsf(rgb) > 0.001f ? rgb * 0.01f * z : 0.0001f;
            fx.v[16][1] = strong * 0.02f * z;
            fx.v[16][2] = s->clock;
            fx.v[16][3] = 50.0f * cells;
            fx.v[17][0] = fabsf(lines) > 0.001f ? fmaxf(1.0f - a[CG_THICK] * 0.2f * z, 0.001f) : 0;
            fx.v[17][1] = lines * 0.05f;
            fx.v[17][2] = 300.0f * cells;
            fx.any = 1;
            break;
        }
        case SH_RADIAL:
            if (fabsf(a[RB_SIZE]) <= 0.001f) break;
            shader_center(gm, s, a[RB_X], a[RB_Y], ox, oy, sw, sh, &cx, &cy);
            fx.v[18][0] = cx; fx.v[18][1] = cy; fx.v[18][2] = a[RB_SIZE] / 9.0f * 0.2f;
            fx.v[18][3] = clamped(a[RB_FADE] * 0.2f, 0, 0.2f);
            fx.any = 1;
            break;
        case SH_MOTION: {
            if ((fabsf(a[MB_X]) <= 0.001f && fabsf(a[MB_Y]) <= 0.001f) || s->channel) break;
            const float limit = s->flags & SF_DUAL ? 0.2f : 0.1f;
            fx.v[19][0] = a[MB_X] * 0.1f * z; fx.v[19][1] = a[MB_Y] * 0.1f * aspect * z;
            fx.v[19][2] = clamped(a[MB_FADE] * limit, -limit, limit);
            fx.v[19][3] = s->flags & SF_DUAL ? 1.0f : 0;
            fx.any = 1;
            break;
        }
        case SH_GREY: {
            if (a[0] <= 0.001f) break;
            GdColorF c = {255, 255, 255, 1};
            if ((s->flags & SF_TINT) && s->channel > 0) c = gm_channel(gm, s->channel);
            fx.v[20][0] = a[0]; fx.v[20][1] = s->flags & SF_DUAL ? 1.0f : 0;
            fx.v[21][0] = c.r / 255.0f; fx.v[21][1] = c.g / 255.0f; fx.v[21][2] = c.b / 255.0f;
            fx.any = 1;
            break;
        }
        case SH_SEPIA: if (a[0] > 0.001f) { fx.v[20][2] = a[0]; fx.any = 1; } break;
        case SH_INVERT:
            if (a[0] <= 0.001f) break;
            for (int i = 0; i < 3; i++) {
                const float share = a[1 + i] * a[0];
                fx.v[22][i] = s->flags & SF_HARD ? fminf(share, 1.0f) : share;
            }
            fx.any = 1;
            break;
        case SH_HUE:
            if (fabsf(a[0]) <= 0.01f) break;
            fx.v[23][0] = cosf(a[0] * PI / 180.0f); fx.v[23][1] = sinf(a[0] * PI / 180.0f); fx.v[23][2] = 1;
            fx.any = 1;
            break;
        case SH_COLOUR:
            for (int i = 0; i < 3; i++) {
                fx.v[24][i] = a[i] - 1;
                fx.v[25][i] = a[3 + i];
                if (fabsf(a[i] - 1) > 0.001f || fabsf(a[3 + i]) > 0.001f) fx.any = 1;
            }
            break;
        case SH_LENS: {   // everything outside a circle takes the tint (black without a channel)
            if (a[LN_STRENGTH] <= 0.001f) break;
            GdColorF c = {0, 0, 0, 1};
            if (s->channel > 0) c = gm_channel(gm, s->channel);
            shader_center(gm, s, a[LN_X], a[LN_Y], ox, oy, sw, sh, &cx, &cy);
            const float radius = a[LN_SIZE] * z;
            fx.v[26][0] = cx; fx.v[26][1] = cy;
            fx.v[26][2] = fmaxf(radius - fmaxf(a[LN_FADE], 0), 0); fx.v[26][3] = fmaxf(radius, 0);
            fx.v[27][0] = c.r / 255.0f; fx.v[27][1] = c.g / 255.0f; fx.v[27][2] = c.b / 255.0f;
            fx.v[27][3] = a[LN_STRENGTH];
            fx.v[15][1] = s->channel > 0 && s->channel < CH_COUNT && gm->ch[s->channel].additive ? 1.0f : 0;
            fx.any = 1;
            break;
        }
        default: break;
        }
    }
    if (layered) gfx_layer_end(g, &fx);
    else gfx_effects(g, &fx);
}
