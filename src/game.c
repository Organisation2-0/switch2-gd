// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "objects.h"
#include "objchan.h"
#include "objz.h"
#include "phystab.h"

#define STEP (1.0f / 240.0f)

static const float k_speeds[5] = {251.16f, 311.58f, 387.42f, 468.0f, 576.0f};

// ---------------------------------------------------------------- header parsing

static void set_channel(Game *gm, int id, float r, float g, float b, int blend)
{
    if (id < 0 || id >= CH_COUNT) return;
    Channel *c = &gm->ch[id];
    c->r = r; c->g = g; c->b = b; c->a = 1.0f;
    c->additive = blend;
    c->lbg = 0;
    c->fading = 0;
}

// "1_r_2_g_3_b_..._6_id_..." style key/value list, separated by '_'
static void parse_kv_underscore(const char *s, int *keys, float *vals, int max, int *n)
{
    *n = 0;
    char buf[256];
    snprintf(buf, sizeof buf, "%s", s);
    char *save = NULL;
    char *tok = strtok_r(buf, "_", &save);
    int have = 0, key = 0;
    while (tok && *n < max) {
        if (!have) { key = atoi(tok); have = 1; }
        else { keys[*n] = key; vals[*n] = (float)atof(tok); (*n)++; have = 0; }
        tok = strtok_r(NULL, "_", &save);
    }
}

// "h a s a v a sChecked a vChecked" (GD HSV string)
static void parse_hsv(Channel *c, const char *s)
{
    float v[5] = {0, 1, 1, 0, 0};
    int n = 0;
    while (*s && n < 5) {
        v[n++] = (float)atof(s);
        while (*s && *s != 'a') s++;
        if (*s == 'a') s++;
    }
    c->hsv_on = 1;
    c->hs = v[0]; c->ss = v[1]; c->vs = v[2]; c->sadd = (int)v[3]; c->vadd = (int)v[4];
}

static float kv_get(const int *keys, const float *vals, int n, int key, float def)
{
    for (int i = 0; i < n; i++) if (keys[i] == key) return vals[i];
    return def;
}

static void parse_header(Game *gm, const char *h)
{
    // defaults
    for (int i = 0; i < CH_COUNT; i++) { gm->ch[i].r = gm->ch[i].g = gm->ch[i].b = 255; gm->ch[i].a = 1; }
    set_channel(gm, CH_BG, 40, 125, 255, 0);
    set_channel(gm, CH_GROUND, 0, 102, 255, 0);
    set_channel(gm, CH_LINE, 255, 255, 255, 1);
    set_channel(gm, CH_3DL, 255, 255, 255, 0);
    set_channel(gm, CH_OBJ, 255, 255, 255, 0);
    set_channel(gm, CH_P1, 125, 255, 0, 1);   // objects in player colours are blended additively
    set_channel(gm, CH_P2, 0, 255, 255, 1);
    set_channel(gm, CH_BLACK, 0, 0, 0, 0);
    set_channel(gm, CH_WHITE, 255, 255, 255, 0);
    gm->ch[1007].lbg = 1;          // "light BG", derived from the background colour
    gm->ch[1007].additive = 1;
    gm->bg_id = 1;
    gm->ground_id = 1;

    // The header is "key,value,key,value,...".
    // values may be empty ("kA14,,kA6,..."), so the commas are split one by one
    char *copy = strdup(h);
    char *key = copy;
    while (key) {
        char *val = strchr(key, ',');
        if (!val) break;
        *val++ = 0;
        char *rest = strchr(val, ',');
        if (rest) *rest++ = 0;
        int keys[24]; float vals[24]; int n;
        if (!strcmp(key, "kS38")) {
            // list of channels separated by '|'
            char *l2 = strdup(val), *s2 = NULL;
            for (char *e = strtok_r(l2, "|", &s2); e; e = strtok_r(NULL, "|", &s2)) {
                parse_kv_underscore(e, keys, vals, 24, &n);
                int id = (int)kv_get(keys, vals, n, 6, -1);
                if (id < 0 || id == 1007) continue;   // light BG is always derived, whatever the header lists
                set_channel(gm, id, kv_get(keys, vals, n, 1, 255), kv_get(keys, vals, n, 2, 255),
                            kv_get(keys, vals, n, 3, 255), (int)kv_get(keys, vals, n, 5, 0));
                gm->ch[id].a = kv_get(keys, vals, n, 7, 1);
                gm->ch[id].player = (int)kv_get(keys, vals, n, 4, -1);
                if (gm->ch[id].player < 1 || gm->ch[id].player > 2) gm->ch[id].player = 0;
                gm->ch[id].copy = (int)kv_get(keys, vals, n, 9, 0);
                gm->ch[id].copy_op = (int)kv_get(keys, vals, n, 10, 0);
                if (gm->ch[id].copy > 0) {
                    const char *h = strstr(e, "_11_");
                    if (h) parse_hsv(&gm->ch[id], h + 4);
                }
                if (gm->ch[id].a <= 0 && n > 0 && id != CH_LINE) gm->ch[id].a = 1;
            }
            free(l2);
        } else if (key[0] == 'k' && key[1] == 'S' && strlen(key) == 4) {
            int ks = atoi(key + 2);
            static const int old_map[] = {29, CH_BG, 30, CH_GROUND, 31, CH_LINE, 32, CH_OBJ, 33, CH_3DL,
                                          34, 1, 35, 2, 36, 3, 37, 4};
            for (unsigned i = 0; i < sizeof old_map / sizeof old_map[0]; i += 2)
                if (ks == old_map[i]) {
                    parse_kv_underscore(val, keys, vals, 24, &n);
                    set_channel(gm, old_map[i + 1], kv_get(keys, vals, n, 1, 255),
                                kv_get(keys, vals, n, 2, 255), kv_get(keys, vals, n, 3, 255),
                                (int)kv_get(keys, vals, n, 5, 0));
                }
        } else if (!strcmp(key, "kA6")) {
            gm->bg_id = atoi(val) > 0 ? atoi(val) : 1;
        } else if (!strcmp(key, "kA7")) {
            gm->ground_id = atoi(val) > 0 ? atoi(val) : 1;
        } else if (!strcmp(key, "kA4")) {
            gm->start_speed = atoi(val);
        } else if (!strcmp(key, "kA2")) {
            gm->start_mode = atoi(val);
        }
        key = rest;
    }
    free(copy);
}

// ---------------------------------------------------------------- classification

static int starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }

static int trigger_channel(int id)
{
    switch (id) {
    case 29: return CH_BG;
    case 30: return CH_GROUND;
    case 104: return CH_LINE;
    case 105: return CH_OBJ;
    case 744: return CH_3DL;
    case 221: return 1;
    case 717: return 2;
    case 718: return 3;
    case 743: return 4;
    case 899: return -2; // target in key 23
    default: return -1;
    }
}

static int portal_kind(int id)
{
    switch (id) {
    case 10: return P_GRAV_DOWN;
    case 11: return P_GRAV_UP;
    case 12: return P_CUBE;
    case 13: return P_SHIP;
    case 45: return P_MIRROR_ON;
    case 46: return P_MIRROR_OFF;
    case 47: return P_BALL;
    case 99: return P_SIZE_NORMAL;
    case 101: return P_SIZE_MINI;
    case 111: return P_UFO;
    case 286: return P_DUAL_ON;
    case 287: return P_DUAL_OFF;
    case 660: return P_WAVE;
    default: return P_OTHER;
    }
}

// Replaces the "_001.png" suffix, e.g. suffix "_2_001.png".
static const GdFrame *sibling(Gfx *g, const char *frame, const char *from, const char *to)
{
    char name[128];
    size_t n = strlen(frame), fl = strlen(from);
    if (n < fl || strcmp(frame + n - fl, from)) return NULL;
    snprintf(name, sizeof name, "%.*s%s", (int)(n - fl), frame, to);
    return gfx_find(g, name);
}

static void classify_visual(Game *gm, Gfx *g, Obj *o, const GdObject *src)
{
    const char *fr = gd_object_frame(src->id);
    o->type = OT_HIDDEN;
    if (trigger_channel(src->id) != -1) {
        o->type = OT_TRIGGER;
        o->tr = gd_obj_prop(src, 7, 255);
        o->tg = gd_obj_prop(src, 8, 255);
        o->tb = gd_obj_prop(src, 9, 255);
        o->tdur = gd_obj_prop(src, 10, 0);
        o->tblend = (int)gd_obj_prop(src, 17, 0);
        o->topacity = gd_obj_prop(src, 35, 1);
        int c = trigger_channel(src->id);
        o->ttarget = c == -2 ? (int)gd_obj_prop(src, 23, 0) : c;
        return;
    }
    if (!fr || starts(fr, "edit_") || starts(fr, "GJ_")) return;
    o->main = gfx_find(g, fr);
    if (!o->main) return;
    const float ppu = g->atlas.px_per_unit;
    float w = o->main->srcw / ppu, h = o->main->srch / ppu;
    o->detail = sibling(g, fr, "_001.png", "_2_001.png");
    if (!o->detail) o->detail = sibling(g, fr, "_001.png", "_color_001.png");
    o->extra = sibling(g, fr, "_001.png", "_extra_001.png");
    o->spin = 0;
    if (starts(fr, "sawblade_01")) o->spin = -200.0f;
    else if (starts(fr, "sawblade_02")) o->spin = -240.0f;
    else if (starts(fr, "sawblade_03")) o->spin = -300.0f;
    else if (starts(fr, "spinBlade") || starts(fr, "blade_")) o->spin = -360.0f;
    o->type = OT_DECO;
    o->hw = w * 0.5f; o->hh = h * 0.5f; o->hx = o->hy = 0;

    if (starts(fr, "portal_")) {
        o->type = OT_PORTAL;
        o->sub = portal_kind(src->id);
        o->back = sibling(g, fr, "_front_001.png", "_back_001.png");
        if (strlen(fr) > 9) {
            char nm[80];
            memcpy(nm, fr, 9); nm[9] = 0;
            strcat(nm, "_extra_001.png");
            o->fx1 = gfx_find(g, nm);
            nm[9] = 0; strcat(nm, "_extra_2_001.png");
            o->fx2 = gfx_find(g, nm);
        }
        o->hw = 15; o->hh = h * 0.5f;
    } else if (starts(fr, "boost_")) {
        o->type = OT_SPEED;
        o->sub = src->id == 200 ? 0 : src->id == 201 ? 1 : src->id == 202 ? 2 : src->id == 203 ? 3 : 4;
        o->hw = 15; o->hh = 18;
    } else if (src->id == 35 || src->id == 67 || src->id == 140 || src->id == 1332) {
        o->type = OT_PAD;
        o->sub = src->id == 35 ? K_YELLOW : src->id == 67 ? K_BLUE : K_PINK;
        o->hw = 15; o->hh = 6; o->hy = -9;
    } else if (src->id == 36 || src->id == 84 || src->id == 141 || src->id == 1022) {
        o->type = OT_ORB;
        o->sub = src->id == 36 ? K_YELLOW : src->id == 84 ? K_BLUE : src->id == 141 ? K_PINK : K_GREEN;
        o->hw = 18; o->hh = 18;
    } else if (starts(fr, "spike_") || starts(fr, "invis_spike")) {
        o->type = OT_HAZARD;
        o->hw = w * 0.14f; o->hh = h * 0.3f; o->hy = -(h * 0.5f - o->hh) + 2;
    } else if (starts(fr, "pit_")) {
        o->type = OT_HAZARD;
        o->hw = w * 0.36f; o->hh = h * 0.30f; o->hy = (h * 0.5f - o->hh) - 1.0f;
    } else if (starts(fr, "saw") || starts(fr, "monster")) {
        o->type = OT_HAZARD;
        o->hw = w * 0.3f; o->hh = h * 0.3f;
    } else if ((starts(fr, "square_") && fr[7] >= '0' && fr[7] <= '9') || starts(fr, "plank_") ||
               starts(fr, "invis_square")) {
        o->type = OT_SOLID;
        if (starts(fr, "plank_")) { o->hh = h * 0.5f; }
        if (starts(fr, "invis_square")) o->main = NULL, o->detail = NULL;
    }
}

// Type and hitbox from the OpenGD tables (phystab.c). Objects the table does not know about,
// or marks as decoration, never collide.
static void apply_phys(Obj *o, int id)
{
    if (o->type == OT_TRIGGER) return;
    const GdPhys *ph = gd_phys_find(id);
    int t = ph ? ph->type : -1;
    ObjType ot = OT_DECO;
    int sub = 0;
    switch (t) {
    case 0: ot = OT_SOLID; break;
    case 25:   // slopes; the sloped spike pits kill
        ot = id == 366 || id == 367 ? OT_HAZARD : OT_SOLID;
        o->slope = 1;
        break;
    case 21: ot = OT_SOLID; o->breakable = 1; break;   // bricks: stand on them, or smash through
    case 2: ot = OT_HAZARD; break;
    case 3: ot = OT_PORTAL; sub = P_GRAV_UP; break;
    case 4: ot = OT_PORTAL; sub = P_GRAV_DOWN; break;
    case 5: ot = OT_PORTAL; sub = P_SHIP; break;
    case 6: ot = OT_PORTAL; sub = P_CUBE; break;
    case 17: ot = OT_PORTAL; sub = P_SIZE_NORMAL; break;
    case 18: ot = OT_PORTAL; sub = P_SIZE_MINI; break;
    case 14: ot = OT_PORTAL; sub = P_MIRROR_ON; break;
    case 15: ot = OT_PORTAL; sub = P_MIRROR_OFF; break;
    case 16: ot = OT_PORTAL; sub = P_BALL; break;
    case 19: ot = OT_PORTAL; sub = P_UFO; break;
    case 23: ot = OT_PORTAL; sub = P_DUAL_ON; break;
    case 24: ot = OT_PORTAL; sub = P_DUAL_OFF; break;
    case 26: ot = OT_PORTAL; sub = P_WAVE; break;
    case 27: ot = OT_PORTAL; sub = P_ROBOT; break;
    case 33: ot = OT_PORTAL; sub = P_SPIDER; break;
    case 28: ot = OT_PORTAL; sub = P_OTHER; break;
    case 8: ot = OT_PAD; sub = K_YELLOW; break;
    case 9: ot = OT_PAD; sub = K_PINK; break;
    case 10: ot = OT_PAD; sub = K_BLUE; break;
    case 34: ot = OT_PAD; sub = K_RED; break;
    case 11: ot = OT_ORB; sub = K_YELLOW; break;
    case 12: ot = OT_ORB; sub = K_PINK; break;
    case 13: ot = OT_ORB; sub = K_BLUE; break;
    case 29: ot = OT_ORB; sub = K_GREEN; break;
    case 35: ot = OT_ORB; sub = K_RED; break;
    case 20:
        ot = OT_SPEED;
        sub = id == 200 ? 0 : id == 201 ? 1 : id == 202 ? 2 : id == 203 ? 3 : 4;
        break;
    default: break;
    }
    o->type = ot;
    o->sub = sub;
    o->rad = 0;
    o->hw = o->hh = o->hx = o->hy = 0;
    // objects newer than the table: swing portal, special orbs and pads, force blocks
    if (id == 1859) {   // H block (invisible helper)
        o->type = OT_HBLOCK;
        o->main = o->glow = NULL;
        o->hw = 15.0f * fabsf(o->sx);
        o->hh = 15.0f * fabsf(o->sy);
        return;
    }
    if (id == 1933) {
        o->type = OT_PORTAL;
        o->sub = P_SWING;
        o->hw = 17.0f;
        o->hh = 43.0f;
        return;
    }
    if (id == 1594 || id == 3004 || id == 3027) {
        o->type = OT_ORB;
        o->sub = id == 1594 ? K_TOGGLE : id == 3004 ? K_SPIDER : K_TELEPORT;
        o->hw = o->hh = 18.0f * fabsf(o->sx);
        return;
    }
    if (id == 3005) {
        o->type = OT_PAD;
        o->sub = K_SPIDER;
        o->hw = 13.0f;
        o->hh = 6.0f;
        return;
    }
    if (id == 2069 || id == 3645) {   // force block (square) / force circle
        o->type = OT_FORCE;
        o->hw = 15.0f * fabsf(o->sx);
        o->hh = 15.0f * fabsf(o->sy);
        return;
    }
    // black orb, dash orbs, teleport portals
    if (id == 1330 || id == 1704 || id == 1751) {
        o->type = OT_ORB;
        o->sub = id == 1330 ? K_BLACK : id == 1704 ? K_DASH : K_DASH_FLIP;
        o->hw = o->hh = 18.0f;
        return;
    }
    if (id == 747 || id == 2902) {
        o->type = OT_PORTAL;
        o->sub = P_TELEPORT;
        o->hw = 6.0f;      // fires only close to its centre (the exit may sit right behind spikes)
        o->hh = 45.0f;
        return;
    }
    if (!ph || ot == OT_DECO) return;

    // Hitbox rectangle relative to the object centre, then flip -> rotate (clockwise) -> AABB.
    float rx[2] = {ph->x, ph->x + ph->w}, ry[2] = {ph->y, ph->y + ph->h};
    float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < 4; i++) {
        float px = rx[i & 1] * o->sx, py = ry[i >> 1] * o->sy;
        float wx = px * c + py * s, wy = -px * s + py * c;
        if (wx < minx) minx = wx;
        if (wx > maxx) maxx = wx;
        if (wy < miny) miny = wy;
        if (wy > maxy) maxy = wy;
    }
    o->hw = (maxx - minx) * 0.5f;
    o->hh = (maxy - miny) * 0.5f;
    o->hx = (maxx + minx) * 0.5f;
    o->hy = (maxy + miny) * 0.5f;
    if (ot == OT_HAZARD) o->rad = ph->radius;
    if (o->slope) {
        // the unrotated shape has its right angle bottom right; find where flip + rotation put it
        const float px = (ph->x + ph->w) * o->sx, py = ph->y * o->sy;
        const float wx = px * c + py * s, wy = -px * s + py * c;
        o->slope = (unsigned char)(1 | (wx > o->hx ? 2 : 0) | (wy > o->hy ? 4 : 0));
    }
}

static void classify(Game *gm, Gfx *g, Obj *o, const GdObject *src)
{
    classify_visual(gm, g, o, src);
    const int id = src->id;
    // triggers of newer versions that the engine acts on
    const int newer = id == 2900 || id == 1935 || id == 3600 || id == 1917 || id == 2066 || id == 1347 || id == 1914 ||
                      id == 1611 || id == 1811 || id == 1817 || id == 1912 || id == 2068 || id == 3607 || id == 1595 || id == 1815 || id == 3022;
    const int enter = (id >= 22 && id <= 28) || (id >= 55 && id <= 59) || id == 1915 || newer;
    if (o->type != OT_TRIGGER && (enter || id == 901 || id == 1007 || id == 1049 || id == 1268 || id == 1346 || id == 1006 ||
                                  id == 1616)) {
        o->type = OT_TRIGGER;
        o->main = o->detail = o->back = o->extra = NULL;
    }
    if (o->type == OT_TRIGGER) {
        o->tid = id;
        o->tgroup = (int)gd_obj_prop(src, 51, 0);
        o->tdx = gd_obj_prop(src, 28, 0);
        o->tdy = gd_obj_prop(src, 29, 0);
        o->tdur = id == 901 || id == 1007 ? gd_obj_prop(src, 10, 0) : o->tdur;
        o->teasing = (int)gd_obj_prop(src, 30, 0);
        o->trate = gd_obj_prop(src, 85, 2);
        o->tact = (int)gd_obj_prop(src, 56, 0);
        o->tcopy = (int)gd_obj_prop(src, 50, 0);
        o->tdelay = gd_obj_prop(src, 63, 0);
        o->trot = gd_obj_prop(src, 68, 0) + 360.0f * gd_obj_prop(src, 69, 0);
        o->tcenter = (int)gd_obj_prop(src, 71, 0);
        o->tlockx = gd_obj_prop(src, 58, 0) != 0;
        o->tlocky = gd_obj_prop(src, 59, 0) != 0;
        if (id == 1006) {
            o->ttarget = (int)gd_obj_prop(src, 51, 0);
            o->pgroup = gd_obj_prop(src, 52, 0) != 0;
            o->pin = gd_obj_prop(src, 45, 0);
            o->phold = gd_obj_prop(src, 46, 0);
            o->pout = gd_obj_prop(src, 47, 0);
            o->pcopy = gd_obj_prop(src, 48, 0) != 0;
            o->phue = gd_obj_prop(src, 49, 0);
            o->ponly = gd_obj_prop(src, 65, 0) != 0 ? 1 : gd_obj_prop(src, 66, 0) != 0 ? 2 : 0;
            o->tr = gd_obj_prop(src, 7, 255);
            o->tg = gd_obj_prop(src, 8, 255);
            o->tb = gd_obj_prop(src, 9, 255);
            o->tcopy = (int)gd_obj_prop(src, 50, 0);
        }
        if (id == 1346 || id == 1616) o->tdur = gd_obj_prop(src, 10, 0);
        o->touch = gd_obj_prop(src, 11, 0) != 0;
        o->tground = id == 29 && gd_obj_prop(src, 14, 0) != 0;   // old BG trigger with "tint ground"
        o->tplayer = gd_obj_prop(src, 15, 0) != 0 ? 1 : gd_obj_prop(src, 16, 0) != 0 ? 2 : 0;
        if (id == 1007) o->topacity = gd_obj_prop(src, 35, 1);
        o->spawn_only = gd_obj_prop(src, 62, 0) != 0;
        if (id == 1914) o->tact = gd_obj_prop(src, 110, 0) != 0;   // static camera: "exit static"
        if (id == 1611 || id == 1811 || id == 1817) {   // count, instant count, pickup
            o->ttarget = (int)gd_obj_prop(src, 80, 0);              // item
            o->tcopy = (int)gd_obj_prop(src, 77, 0);                // target value / amount
            o->tcenter = (int)gd_obj_prop(src, 88, 0);              // comparison: equal, larger, smaller
        }
        if (id == 1815) {   // collision: two block ids, or the icon and one block id
            o->ttarget = (int)gd_obj_prop(src, 80, 0);
            o->tcopy = (int)gd_obj_prop(src, 95, 0);
            o->tplayer = gd_obj_prop(src, 138, 0) != 0 || gd_obj_prop(src, 200, 0) != 0;
            o->tlockx = gd_obj_prop(src, 93, 0) != 0;               // fire when they separate
        }
        if (id == 1595) {   // touch: act on the player's clicks
            o->tlockx = gd_obj_prop(src, 81, 0) != 0;               // only while held
            o->tcenter = (int)gd_obj_prop(src, 82, 0);              // 0 toggle, 1 on, 2 off
        }
    }
    if (id >= 15 && id <= 17) {
        char bn[32];
        snprintf(bn, sizeof bn, "rod_ball_%02d_001.png", id - 14);
        o->ball = gfx_find(g, bn);
    }
    // key 96: the object was placed with its glow switched off
    if (o->main && o->type != OT_TRIGGER && o->type != OT_ORB && o->type != OT_PAD && gd_obj_prop(src, 96, 0) == 0)
        o->glow = sibling(g, gd_object_frame(id), "_001.png", "_glow_001.png");
    int hidden_no_sprite = o->type == OT_HIDDEN && !o->main;
    apply_phys(o, src->id);
    if (o->type == OT_PORTAL && o->sub == P_TELEPORT) {
        o->tdy = gd_obj_prop(src, 54, 0);
        o->tgroup = (int)gd_obj_prop(src, 51, 0);   // 2.2: exit marked by a group
        o->tgrav = (unsigned char)gd_obj_prop(src, 354, 0);
        o->tforce = gd_obj_prop(src, 345, 0) != 0 ? gd_obj_prop(src, 346, 0) : 0;
    }
    if (o->type == OT_ORB && (o->sub == K_TOGGLE || o->sub == K_TELEPORT)) {
        o->tgroup = (int)gd_obj_prop(src, 51, 0);
        o->tact = (int)gd_obj_prop(src, 56, 0);
    }
    if (o->type == OT_FORCE) o->value = gd_obj_prop(src, 149, 1.0f);
    if (o->type == OT_PORTAL) o->freemode = gd_obj_prop(src, 111, 0) != 0;
    if (o->type == OT_SOLID) o->passable = gd_obj_prop(src, 134, 0) != 0;
    if (o->type == OT_TRIGGER && id == 3022) {
        o->tgrav = (unsigned char)gd_obj_prop(src, 354, 0);
        o->tforce = gd_obj_prop(src, 345, 0) != 0 ? gd_obj_prop(src, 346, 0) : 0;
    }
    if (o->type == OT_TRIGGER) {
        o->fxmod = gd_obj_prop(src, 72, 1.0f);
        o->fymod = gd_obj_prop(src, 73, 1.0f);
        o->channel = (short)gd_obj_prop(src, 170, 0);
        o->to_channel = (short)gd_obj_prop(src, 173, 0);
        o->rground = (signed char)gd_obj_prop(src, 166, 0);
        o->rmove = (signed char)gd_obj_prop(src, 167, 0);
        o->value = id == 1935 ? gd_obj_prop(src, 120, 1.0f) : id == 2066 ? gd_obj_prop(src, 148, 1.0f)
                 : id == 1912 ? gd_obj_prop(src, 10, 50.0f) : 0;   // 1912: random chance in percent
    }
    // keep objects without a sprite only if they collide (invisible blocks, hitbox-only objects)
    if (hidden_no_sprite && o->type == OT_DECO) o->type = OT_HIDDEN;
    if (id == 1816) {   // collision block: invisible, watched by collision triggers (and moved by move triggers)
        o->type = OT_SENSOR;
        o->main = o->glow = NULL;
        o->value = gd_obj_prop(src, 80, 0);
    }
}

// ---------------------------------------------------------------- loading

static Gfx *g_sort_gfx; // unused, kept simple
static const Obj *g_sort_obj;

static int cmp_z(const void *a, const void *b)
{
    const Obj *x = &g_sort_obj[*(const int *)a], *y = &g_sort_obj[*(const int *)b];
    if (x->zlayer != y->zlayer) return x->zlayer - y->zlayer;
    if (x->zorder != y->zorder) return x->zorder - y->zorder;
    return *(const int *)a - *(const int *)b;
}

static int cmp_x(const void *a, const void *b)
{
    float d = g_sort_obj[*(const int *)a].x - g_sort_obj[*(const int *)b].x;
    return d < 0 ? -1 : d > 0 ? 1 : *(const int *)a - *(const int *)b;
}

// Stores a per-object colour shift (identical ones are shared); returns its 1-based index.
static int add_hsv(Game *gm, const float v[5])
{
    for (int i = gm->nhsv - 1; i >= 0 && i >= gm->nhsv - 64; i--)
        if (!memcmp(gm->hsv[i], v, sizeof(float) * 5)) return i + 1;
    if (gm->nhsv >= 30000) return 0;
    if ((gm->nhsv & 255) == 0) {
        void *p = realloc(gm->hsv, (size_t)(gm->nhsv + 256) * sizeof *gm->hsv);
        if (!p) return 0;
        gm->hsv = p;
    }
    memcpy(gm->hsv[gm->nhsv], v, sizeof(float) * 5);
    return ++gm->nhsv;
}

// Triggers and helpers of newer versions only have editor icons: they are never drawn.
static int editor_only(int id)
{
    static const short r[][2] = {{1520,1520},{1585,1585},{1595,1595},{1611,1620},{1755,1755},{1811,1819},{1829,1829},{1912,1917},
        {1931,1935},{2015,2016},{2062,2068},{2899,2905},{2907,2907},{2909,2925},{2999,2999},{3006,3033},{3092,3092},{3600,3600},
        {3602,3620},{3640,3643},{3660,3662}};
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++)
        if (id >= r[i][0] && id <= r[i][1]) return 1;
    return 0;
}

int game_load(Game *gm, Gfx *g, const char *path)
{
    memset(gm, 0, sizeof *gm);
    GdLevel lvl;
    if (gd_level_load(path, &lvl)) return -1;
    snprintf(gm->name, sizeof gm->name, "%s", lvl.name);
    gm->path = path;
    parse_header(gm, lvl.header);
    memcpy(gm->ch0, gm->ch, sizeof gm->ch);
    gm->ch0_valid = 1;

    gm->obj = calloc(lvl.count ? lvl.count : 1, sizeof(Obj));
    gm->by_z = malloc(sizeof(int) * (lvl.count + 1));
    gm->by_x = malloc(sizeof(int) * (lvl.count + 1));
    gm->kidsets = calloc(1100, sizeof(KidSet));
    float maxx = 0;
    for (size_t i = 0; i < lvl.count; i++) {
        const GdObject *s = &lvl.objects[i];
        Obj *o = &gm->obj[gm->n];
        memset(o, 0, sizeof *o);   // the slot may hold a skipped object (its groups, flags, ...)
        o->id = s->id;
        o->x = s->x; o->y = s->y; o->rot = s->rot;
        const float kx = s->scale_x != 0 ? fabsf(s->scale_x) : s->scale, ky = s->scale_y != 0 ? fabsf(s->scale_y) : s->scale;
        o->sx = s->flipx ? -kx : kx;
        o->sy = s->flipy ? -ky : ky;
        if (s->has_hsv1) o->hsv1 = (short)add_hsv(gm, s->hsv1);
        if (s->has_hsv2) o->hsv2 = (short)add_hsv(gm, s->hsv2);
        {   // draw order: stored in the level only when it differs from the object default
            const GdObjZ *oz = gd_objz_find(s->id);
            o->zlayer = (int)gd_obj_prop(s, 24, 0);
            o->zorder = (int)gd_obj_prop(s, 25, 0);
            if (o->zlayer == 0) o->zlayer = oz && oz->layer ? oz->layer : 5;
            if (o->zorder == 0 && oz) o->zorder = oz->order;
        }
        {
            // default colour channels come from the object table (OpenGD); keys 21/22 override them
            const GdObjChan *oc = gd_objchan_find(s->id);
            int def_main = CH_OBJ, def_detail = 0;
            if (oc) {
                if (oc->kind == 2) def_main = CH_BLACK;
                else if (oc->primary > 0) def_main = oc->primary;
                if (oc->secondary > 0) def_detail = oc->secondary;
            }
            o->ch_main = (int)gd_obj_prop(s, 21, (float)def_main);
            o->ch_detail = (int)gd_obj_prop(s, 22, (float)(def_detail > 0 ? def_detail : o->ch_main));
        }
        if (o->ch_main <= 0) o->ch_main = CH_OBJ;
        if (o->ch_detail <= 0) o->ch_detail = o->ch_main;
        for (int k = 0; k < s->ngroups && k < GD_MAX_GROUPS; k++)
            if (s->groups[k] > 0 && s->groups[k] < GD_GROUPS) o->grp[o->ngrp++] = (short)s->groups[k];
        o->kidset = -1;
        classify(gm, g, o, s);
        {   // levels made before 2.0 store the colour as "legacy channel" (key 19) instead of keys 21/22
            static const short legacy[9] = {0, CH_P1, CH_P2, 1, 2, 1007, 3, 4, CH_3DL};
            const int old = (int)gd_obj_prop(s, 19, 0);
            if (old > 0 && old < 9 && gd_obj_prop(s, 21, 0) == 0 && gd_obj_prop(s, 22, 0) == 0) {
                o->ch_detail = legacy[old];
                const GdObjChan *oc = gd_objchan_find(s->id);
                // objects with a base and a detail sprite only recolour the detail
                if (!(oc && oc->kind == 0 && oc->secondary > 0) && !o->detail) o->ch_main = legacy[old];
            }
        }
        if (o->type != OT_TRIGGER && s->id >= 0 && s->id < 5000) {
            int cnt;
            const GdChild *ch = gd_children_find(s->id, &cnt);
            if (cnt > 0 && cnt <= 12) {
                if (gm->kid_of_id[s->id] == 0) {
                    KidSet *ks = &gm->kidsets[gm->nkidsets];
                    ks->c = ch; ks->n = cnt;
                    for (int k = 0; k < cnt; k++) ks->f[k] = gfx_find(g, ch[k].tex);
                    gm->kid_of_id[s->id] = (short)(++gm->nkidsets);
                }
                o->kidset = gm->kid_of_id[s->id] - 1;
                o->detail = NULL;
                o->extra = NULL;
                // newer slope objects: the table names an outline sprite as the main one, the
                // visible art are the children; textured ones are cut to the triangle
                const char *fr = gd_object_frame(s->id);
                if (fr && (!strcmp(fr, "blockOutline_14_001.png") || !strcmp(fr, "blockOutline_15_001.png"))) {
                    o->clipw = fr[14] == '5' ? 60 : 30;
                    const GdObjChan *oc = gd_objchan_find(s->id);
                    // detail art keeps its own default colour when the level only recolours the base
                    if (oc && oc->primary > 0 && oc->primary < 1000 && gd_obj_prop(s, 22, 0) == 0)
                        o->ch_detail = oc->primary;
                    o->main = NULL;
                    o->glow = NULL;
                    if (o->type == OT_HIDDEN) o->type = OT_DECO;
                }
            }
        }
        if (o->type == OT_HIDDEN && !o->main) continue;
        o->nodraw = o->type != OT_TRIGGER && (gd_obj_prop(s, 135, 0) != 0 || (o->type != OT_PORTAL && o->type != OT_ORB &&
                    o->type != OT_PAD && editor_only(s->id)));
        if (s->nlist > 1 && (s->id == 2068 || s->id == 3607)) {
            gm->lists = realloc(gm->lists, (size_t)(gm->nlists + 1) * sizeof *gm->lists);
            gm->lists[gm->nlists].obj = gm->n;
            gm->lists[gm->nlists].n = s->nlist;
            memcpy(gm->lists[gm->nlists].list, s->list, sizeof s->list);
            gm->nlists++;
        }
        if (o->x > maxx) maxx = o->x;
        gm->n++;
    }
    gd_level_free(&lvl);

    // groups (CSR lists), objects that move triggers can move, static broadphase order
    gm->grp_start = calloc(GD_GROUPS + 2, sizeof(int));
    for (int i = 0; i < gm->n; i++)
        for (int k = 0; k < gm->obj[i].ngrp; k++) {
            int gid = gm->obj[i].grp[k];
            if (gid > 0 && gid < GD_GROUPS) gm->grp_start[gid + 1]++;
        }
    for (int gid = 0; gid <= GD_GROUPS; gid++) gm->grp_start[gid + 1] += gm->grp_start[gid];
    gm->grp_items = malloc(sizeof(int) * (size_t)(gm->grp_start[GD_GROUPS] + 1));
    int *fill = calloc(GD_GROUPS + 1, sizeof(int));
    for (int i = 0; i < gm->n; i++)
        for (int k = 0; k < gm->obj[i].ngrp; k++) {
            int gid = gm->obj[i].grp[k];
            if (gid > 0 && gid < GD_GROUPS) gm->grp_items[gm->grp_start[gid] + fill[gid]++] = i;
        }
    free(fill);
    unsigned char *moving = calloc(GD_GROUPS + 1, 1);
    for (int i = 0; i < gm->n; i++)
        if (gm->obj[i].type == OT_TRIGGER && (gm->obj[i].tid == 901 || gm->obj[i].tid == 1346) && gm->obj[i].tgroup > 0 && gm->obj[i].tgroup < GD_GROUPS)
            moving[gm->obj[i].tgroup] = 1;
    gm->movers = malloc(sizeof(int) * (size_t)(gm->n + 1));
    gm->nx = 0;
    gm->nmov = 0;
    for (int i = 0; i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        o->x0 = o->x; o->y0 = o->y; o->rot0 = o->rot;
        for (int k = 0; k < o->ngrp; k++) if (o->grp[k] > 0 && o->grp[k] < GD_GROUPS && moving[o->grp[k]]) o->mover = 1;
        gm->by_z[i] = i;
        if (o->type == OT_TRIGGER && o->tid == 3600) gm->has_end = 1;
        if (o->type == OT_TRIGGER && o->tid == 3022) gm->has_jumps = 1;
        if (o->type == OT_SENSOR) {
            if (!(gm->ncblocks & 63)) gm->cblocks = realloc(gm->cblocks, (size_t)(gm->ncblocks + 64) * sizeof *gm->cblocks);
            gm->cblocks[gm->ncblocks++] = i;
        }
        if (o->type == OT_TRIGGER && o->tid == 2900) {
            if (!(gm->nturns & 63)) gm->turns = realloc(gm->turns, (size_t)(gm->nturns + 64) * sizeof *gm->turns);
            gm->turns[gm->nturns++] = i;
        }
        if ((o->type == OT_TRIGGER && (o->tid == 2900 || o->tid == 1917)) ||
            ((o->type == OT_PORTAL || o->type == OT_ORB) && o->tgroup > 0 &&
             (o->sub == (o->type == OT_PORTAL ? P_TELEPORT : K_TELEPORT)))) gm->has_jumps = 1;
        // movers are collected separately (the look-ahead saves and restores them)
        const int inert = o->type == OT_DECO || o->type == OT_HIDDEN;
        if (inert) continue;   // by_x and movers only hold what the simulation needs (drawing uses by_z)
        if (!o->mover) gm->by_x[gm->nx++] = i;
        else gm->movers[gm->nmov++] = i;
    }
    free(moving);
    g_sort_obj = gm->obj;
    qsort(gm->by_z, (size_t)gm->n, sizeof(int), cmp_z);
    qsort(gm->by_x, (size_t)gm->nx, sizeof(int), cmp_x);
    gm->end_x = maxx + 380;
    (void)g_sort_gfx;
    game_reset(gm);
    gm->attempts = 1;
    return 0;
}

void game_set_skin(Game *gm, Gfx *g, int cube, int ship, const float c1[3], const float c2[3])
{
    char n[64];
    snprintf(n, sizeof n, "player_%02d_001.png", cube);   gm->fr_cube  = gfx_find(g, n);
    snprintf(n, sizeof n, "player_%02d_2_001.png", cube); gm->fr_cube2 = gfx_find(g, n);
    snprintf(n, sizeof n, "ship_%02d_001.png", ship);     gm->fr_ship  = gfx_find(g, n);
    snprintf(n, sizeof n, "ship_%02d_2_001.png", ship);   gm->fr_ship2 = gfx_find(g, n);
    snprintf(n, sizeof n, "player_ball_%02d_001.png", cube);   gm->fr_ball  = gfx_find(g, n);
    snprintf(n, sizeof n, "player_ball_%02d_2_001.png", cube); gm->fr_ball2 = gfx_find(g, n);
    snprintf(n, sizeof n, "bird_%02d_001.png", ship);   gm->fr_ufo  = gfx_find(g, n);
    snprintf(n, sizeof n, "bird_%02d_2_001.png", ship); gm->fr_ufo2 = gfx_find(g, n);
    snprintf(n, sizeof n, "bird_%02d_3_001.png", ship); gm->fr_ufo3 = gfx_find(g, n);
    snprintf(n, sizeof n, "dart_%02d_001.png", cube);   gm->fr_dart  = gfx_find(g, n);
    snprintf(n, sizeof n, "dart_%02d_2_001.png", cube); gm->fr_dart2 = gfx_find(g, n);
    gm->fr_swing = gfx_find(g, "swing_01_001.png");
    gm->fr_swing2 = gfx_find(g, "swing_01_2_001.png");
    for (int k = 0; k < 3; k++) { gm->c1[k] = c1[k]; gm->c2[k] = c2[k]; }
    gm->skin_set = 1;
    if (gm->ch0_valid) {
        gm->ch[CH_P1].r = c1[0]; gm->ch[CH_P1].g = c1[1]; gm->ch[CH_P1].b = c1[2];
        gm->ch[CH_P2].r = c2[0]; gm->ch[CH_P2].g = c2[1]; gm->ch[CH_P2].b = c2[2];
    }
}

void game_free(Game *gm)
{
    free(gm->obj);
    free(gm->by_z);
    free(gm->by_x);
    free(gm->kidsets);
    free(gm->movers);
    free(gm->grp_start);
    free(gm->grp_items);
    free(gm->hsv);
    free(gm->undo);
    free(gm->turns);
    free(gm->cblocks);
    free(gm->lists);
    memset(gm, 0, sizeof *gm);
}

void game_reset(Game *gm)
{
    gm->npt = 0; gm->dead_fx = 0; gm->emit_t = 0;
    gm->enter_fx = 0;
    gm->npu = gm->naf = 0;
    Player *p = &gm->p;
    memset(p, 0, sizeof *p);
    memset(&gm->p2, 0, sizeof gm->p2);
    p->x = 0; p->y = 15; p->grav = 1;
    p->gscale = 1.0f;
    gm->frame = gm->next_frame = (Frame){1, 0, 0, 1};
    gm->travel = 0;
    gm->timewarp = 1.0f;
    gm->channel = 0;
    memset(&gm->logic, 0, sizeof gm->logic);
    gm->free = 0;
    // kA4: 0 = normal, 1 = slow, 2 = fast, 3 = faster, 4 = fastest
    static const int map[5] = {1, 0, 2, 3, 4};
    int si = gm->start_speed;
    p->speed = k_speeds[map[(si >= 0 && si < 5) ? si : 0]];
    p->mode = gm->start_mode == 1 ? M_SHIP : M_CUBE;
    p->last_ground_y = 15;
    p->hold = p->queued = gm->input;
    gm->dual = 0;
    gm->mirror = 0;
    gm->mir_x = 190;
    gm->ctr = 240;
    gm->ground_y = 0;
    gm->has_ceil = p->mode != M_CUBE;
    gm->ceil_y = 301;
    for (int i = 0; i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        o->used = o->used2 = 0;
        o->tdone = 0;
        o->off = 0;
        o->x = o->x0; o->y = o->y0;
        if (o->spin == 0) o->rot = o->rot0;
    }
    for (int i = 0; i < GD_GROUPS; i++) gm->galpha[i] = 1.0f;
    gm->nmv = gm->nsp = 0;
    gm->time = 0;
    gm->finished = 0;
    gm->cam_x = -190;
    gm->cam_bottom = p->mode == M_CUBE ? -90.0f : gm->ctr - 250.0f;
    gm->cam_target = gm->cam_bottom + 90.0f;
    if (gm->ch0_valid) memcpy(gm->ch, gm->ch0, sizeof gm->ch);
    gm->acc = 0;
    gm->prev_x = p->x; gm->prev_y = p->y; gm->prev_rot = 0;
    if (gm->skin_set) {
        gm->ch[CH_P1].r = gm->c1[0]; gm->ch[CH_P1].g = gm->c1[1]; gm->ch[CH_P1].b = gm->c1[2];
        gm->ch[CH_P2].r = gm->c2[0]; gm->ch[CH_P2].g = gm->c2[1]; gm->ch[CH_P2].b = gm->c2[2];
    }
}

// ---------------------------------------------------------------- simulation

static void die_why(Game *gm, const char *why, const Obj *o)
{
    if (gm->p.dead) return;
    if (gm->noclip) return;
    snprintf(gm->death_info, sizeof gm->death_info, "%s id=%d at (%.0f,%.0f)", why, o ? o->id : 0, o ? o->x : 0.0f, o ? o->y : 0.0f);
    gm->p.dead = 1;
    gm->p.dead_t = 0;
}

#define die(gm) die_why(gm, "?", NULL)

// ---------------------------------------------------------------- particles (visual only)

static unsigned fx_rng = 2463534242u;
static float frand(void)
{
    fx_rng = fx_rng * 1664525u + 1013904223u;
    return (float)(fx_rng >> 8) / 16777216.0f;
}

static void emit(Game *gm, float x, float y, float vx, float vy, float life, float size, float grow,
                 float r, float g, float b, float a)
{
    if (gm->sim) return;
    if (gm->npt >= GD_MAX_PART) return;
    Particle *p = &gm->pt[gm->npt++];
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->life = p->max = life; p->size = size; p->grow = grow;
    p->r = r; p->g = g; p->b = b; p->a = a;
}

static void emit_burst(Game *gm, float x, float y, int n, float speed, float life, float size,
                       float r, float g, float b)
{
    for (int i = 0; i < n; i++) {
        float ang = frand() * 6.2831853f, sp = speed * (0.3f + 0.7f * frand());
        emit(gm, x, y, cosf(ang) * sp, sinf(ang) * sp, life * (0.6f + 0.4f * frand()), size * (0.6f + 0.6f * frand()),
             -size * 0.8f / life, r, g, b, 1.0f);
    }
}

static void kind_rgb(int kind, float *r, float *g, float *b)
{
    switch (kind) {
    case K_YELLOW: *r = 255; *g = 220; *b = 40; break;
    case K_BLUE: *r = 60; *g = 170; *b = 255; break;
    case K_PINK: *r = 255; *g = 90; *b = 200; break;
    case K_GREEN: *r = 80; *g = 255; *b = 80; break;
    default: *r = 255; *g = 60; *b = 60; break;
    }
}

static void portal_rgb(int sub, float *r, float *g, float *b)
{
    switch (sub) {
    case P_GRAV_DOWN: case P_MIRROR_OFF: *r = 60; *g = 170; *b = 255; break;
    case P_GRAV_UP: case P_MIRROR_ON: *r = 255; *g = 230; *b = 60; break;
    case P_CUBE: case P_SIZE_NORMAL: *r = 80; *g = 255; *b = 100; break;
    case P_SHIP: case P_SIZE_MINI: *r = 255; *g = 90; *b = 200; break;
    case P_BALL: case P_UFO: case P_DUAL_ON: *r = 255; *g = 150; *b = 30; break;
    case P_WAVE: case P_DUAL_OFF: *r = 40; *g = 200; *b = 255; break;
    default: *r = 255; *g = 255; *b = 255; break;
    }
}

static void death_fx(Game *gm)
{
    if (gm->sim || gm->dead_fx) return;
    gm->dead_fx = 1;
    float x = gm->p.x, y = gm->p.y;
    emit(gm, x, y, 0, 0, 0.35f, 30.0f, 520.0f, gm->c1[0], gm->c1[1], gm->c1[2], 0.9f);   // shock ring (soft disc)
    emit_burst(gm, x, y, 26, 260.0f, 0.7f, 12.0f, gm->c1[0], gm->c1[1], gm->c1[2]);
    emit_burst(gm, x, y, 14, 180.0f, 0.6f, 10.0f, gm->c2[0], gm->c2[1], gm->c2[2]);
}

// Idle emitters of the player icon (ship flame, ground dust, wave sparks), run once per step.
static void player_fx(Game *gm, const Player *p, int was_ground)
{
    if (gm->sim || p->dead) return;
    const float k = p->mini ? 0.6f : 1.0f, gsign = (float)p->grav;
    if (p->on_ground && !was_ground && (p->mode == M_CUBE || p->mode == M_ROBOT || p->mode == M_BALL || p->mode == M_SPIDER)) {
        float fy = p->y - 14.0f * k * gsign;   // landing puff
        for (int i = 0; i < 6; i++)
            emit(gm, p->x - 6 + frand() * 12, fy, (frand() - 0.7f) * 90.0f, gsign * (15.0f + frand() * 40.0f), 0.4f,
                 7.0f * k, -9.0f, 255, 255, 255, 0.55f);
    }
    gm->emit_t += STEP;
    if (gm->emit_t < 0.012f) return;
    gm->emit_t = 0;
    float a = p->rot * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    if (p->mode == M_SHIP || p->mode == M_UFO) {
        float dx = (p->mode == M_SHIP ? -17.0f : -8.0f) * k, dy = (p->mode == M_SHIP ? -3.0f : -11.0f) * k * gsign;
        float wx = dx * c + dy * s, wy = -dx * s + dy * c;
        if (p->mode == M_SHIP && (p->hold || 1))
            emit(gm, p->x + wx, p->y + wy, -70.0f - frand() * 50.0f, (frand() - 0.5f) * 50.0f, 0.28f, 11.0f * k, -30.0f,
                 255, 150 + frand() * 60.0f, 30, 0.9f);
        else if (p->mode == M_UFO && p->hold)
            emit(gm, p->x + wx, p->y + wy, -30.0f * frand(), -gsign * (40.0f + frand() * 30.0f), 0.3f, 7.0f * k, -15.0f, 255, 255, 255, 0.7f);
    } else if (p->mode == M_WAVE) {
        emit(gm, p->x, p->y, -40.0f * frand(), (frand() - 0.5f) * 60.0f, 0.3f, 8.0f * k, -20.0f, gm->c1[0], gm->c1[1], gm->c1[2], 0.8f);
    } else if (p->on_ground && p->mode != M_ROBOT) {   // faint dust trail while sliding along the floor
        emit(gm, p->x - 12 * k, p->y - 14.0f * k * gsign, -50.0f - frand() * 30.0f, gsign * (5.0f + frand() * 25.0f), 0.35f,
             5.0f * k, -8.0f, 255, 255, 255, 0.35f);
    }
}

static void particles_step(Game *gm, float dt)
{
    for (int i = 0; i < gm->npt;) {
        Particle *p = &gm->pt[i];
        p->life -= dt;
        if (p->life <= 0) { *p = gm->pt[--gm->npt]; continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->size += p->grow * dt;
        if (p->size < 1.0f) p->size = 1.0f;
        i++;
    }
}

void game_set_hold(Game *gm, int down)
{
    down = down != 0;
    if (down && !gm->input) {
        gm->p.hold = gm->p2.hold = 1;
        gm->p.queued = gm->p2.queued = 1;
        gm->p.just_held = gm->p2.just_held = 1;
    } else if (!down) {
        gm->p.hold = gm->p2.hold = 0;
        gm->p.queued = gm->p2.queued = 0;
        gm->p.just_held = gm->p2.just_held = 0;
    }
    gm->input = down;
}

static void start_fade(Game *gm, const Obj *t)
{
    if (t->ttarget < 0 || t->ttarget >= CH_COUNT) return;
    Channel *c = &gm->ch[t->ttarget];
    c->player = t->tplayer;
    c->lbg = 0;
    c->hsv_on = 0;
    c->copy = t->tcopy > 0 && t->tcopy < CH_COUNT && t->tcopy != t->ttarget ? t->tcopy : 0;
    c->copy_op = 0;
    c->fr = c->r; c->fg = c->g; c->fb = c->b; c->fa = c->a;
    c->tr = t->tr; c->tg = t->tg; c->tb = t->tb;
    c->ta = t->topacity;   // key 35, 1 when absent
    c->additive = t->tblend;
    c->t = 0;
    c->dur = t->tdur;
    c->fading = 1;
    if (c->dur <= 0.001f) {
        c->r = c->tr; c->g = c->tg; c->b = c->tb; c->a = c->ta;
        c->fading = 0;
    }
}

static int first_index(const Game *gm, float x)
{
    int lo = 0, hi = gm->nx;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (gm->obj[gm->by_x[mid]].x < x) lo = mid + 1; else hi = mid;
    }
    return lo;
}

// ---------------------------------------------------------------- triggers: move, toggle, alpha, spawn

static float ease_apply(int type, float t, float rate)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    if (rate < 0.1f) rate = 2.0f;
    switch (type) {
    case 0: return t;
    case 1: // ease in out
        return t < 0.5f ? 0.5f * powf(2.0f * t, rate) : 1.0f - 0.5f * powf(2.0f * (1.0f - t), rate);
    case 2: return powf(t, rate);                       // ease in
    case 3: return 1.0f - powf(1.0f - t, rate);         // ease out
    case 10: return t == 0 ? 0 : powf(2.0f, 10.0f * (t - 1.0f));      // exponential in
    case 11: return t == 1 ? 1 : 1.0f - powf(2.0f, -10.0f * t);      // exponential out
    case 12: return t < 0.5f ? 0.5f * powf(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * powf(2.0f, -20.0f * t + 10.0f);
    case 13: return 1.0f - cosf(t * 1.5707963f);                      // sine in
    case 14: return sinf(t * 1.5707963f);                             // sine out
    case 15: return -0.5f * (cosf(3.1415926f * t) - 1.0f);            // sine in out
    case 16: return t * t * (2.70158f * t - 1.70158f);                // back in
    case 17: { float u = t - 1.0f; return 1.0f + u * u * (2.70158f * u + 1.70158f); }
    default: return t * t * (3.0f - 2.0f * t);                        // smoothstep for the rest
    }
}

// Call before the look-ahead changes an object, so that game_rewind can put it back.
static void touch(Game *gm, const Obj *o)
{
    if (!gm->sim) return;
    if (gm->nundo == gm->capundo) {
        const int cap = gm->capundo ? gm->capundo * 2 : 4096;
        UndoEntry *u = realloc(gm->undo, (size_t)cap * sizeof *u);
        if (!u) return;
        gm->undo = u;
        gm->capundo = cap;
    }
    gm->undo[gm->nundo++] = (UndoEntry){(int)(o - gm->obj), o->x, o->y, o->rot, (unsigned char)o->used,
                                        (unsigned char)o->used2, (unsigned char)o->tdone, o->off};
}

static void group_apply_move(Game *gm, int group, float dx, float dy)
{
    if (group <= 0 || group >= GD_GROUPS) return;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (gm->sim && (o->type == OT_DECO || o->type == OT_HIDDEN)) continue;
        touch(gm, o);
        o->x += dx;
        o->y += dy;
    }
}

// Rotates the objects of a group (clockwise degrees) around the first object of the centre group.
static void group_apply_rotate(Game *gm, int group, int center, float deg)
{
    if (group <= 0 || group >= GD_GROUPS) return;
    if (center <= 0 || center >= GD_GROUPS || gm->grp_start[center] >= gm->grp_start[center + 1]) center = group;
    if (gm->grp_start[center] >= gm->grp_start[center + 1]) return;
    const Obj *pivot = &gm->obj[gm->grp_items[gm->grp_start[center]]];
    const float cx = pivot->x, cy = pivot->y;
    const float r = deg * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (gm->sim && (o->type == OT_DECO || o->type == OT_HIDDEN)) continue;
        touch(gm, o);
        const float dx = o->x - cx, dy = o->y - cy;
        o->x = cx + dx * c + dy * s;
        o->y = cy - dx * s + dy * c;
        o->rot += deg;
    }
}

static void fire_trigger(Game *gm, Obj *t, int depth);
static void activate_group(Game *gm, int group, int on, int depth);
static int teleport_to_group(Game *gm, Player *p, int group);

// Switches the objects of a group on (their triggers fire) or off.
static void activate_group(Game *gm, int group, int on, int depth)
{
    if (group <= 0 || group >= GD_GROUPS) return;
    for (int k = gm->grp_start[group]; k < gm->grp_start[group + 1]; k++) {
        Obj *o = &gm->obj[gm->grp_items[k]];
        if (!gm->sim || (o->type != OT_DECO && o->type != OT_HIDDEN)) { touch(gm, o); o->off = on ? 0 : 1; }
        if (on && o->type == OT_TRIGGER && o->spawn_only && depth < 4) fire_trigger(gm, o, depth + 1);
    }
}

static Frame frame_from(int move, int ground, Frame cur)
{
    static const signed char dir[5][2] = {{0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0}};
    Frame f = cur;
    if (move >= 1 && move <= 4) { f.fx = dir[move][0]; f.fy = dir[move][1]; }
    if (ground >= 1 && ground <= 4) { f.ux = (signed char)-dir[ground][0]; f.uy = (signed char)-dir[ground][1]; }
    if (f.fx * f.ux + f.fy * f.uy != 0) { f.ux = (signed char)-f.fy; f.uy = f.fx; }   // keep them at a right angle
    return f;
}

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

static unsigned logic_random(Game *gm)   // part of the state, so the look-ahead sees the same dice
{
    unsigned x = gm->logic.rng ? gm->logic.rng : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return gm->logic.rng = x;
}

static void watch_trigger(Game *gm, Obj *t)
{
    const int oi = (int)(t - gm->obj);
    Logic *l = &gm->logic;
    for (int i = 0; i < l->nwatch; i++)
        if (l->watch[i].obj == oi) return;
    if (l->nwatch >= GD_MAX_WATCH) return;
    int state = 0;
    if (t->tid == 1595) state = gm->input;
    else if (t->tid == 1611) { const int *v = item_slot(gm, t->ttarget); state = v && *v == t->tcopy; }
    l->watch[l->nwatch].obj = oi;
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

static int collision_now(const Game *gm, const Obj *t)
{
    if (t->tplayer) {   // icon against a block id (either field may hold it)
        const int id = t->tcopy > 0 ? t->tcopy : t->ttarget;
        const float h = gm->p.mini ? 9.0f : 15.0f;
        for (int i = 0; i < gm->ncblocks; i++) {
            const Obj *b = &gm->obj[gm->cblocks[i]];
            if (!b->off && (int)b->value == id && fabsf(gm->p.x - b->x) < h + 15.0f * fabsf(b->sx) &&
                fabsf(gm->p.y - b->y) < h + 15.0f * fabsf(b->sy)) return 1;
        }
        return 0;
    }
    for (int i = 0; i < gm->ncblocks; i++) {
        const Obj *a = &gm->obj[gm->cblocks[i]];
        if (!a->off && (int)a->value == t->ttarget && blocks_touch(gm, a, t->tcopy)) return 1;
    }
    return 0;
}

static void logic_step(Game *gm)
{
    Logic *l = &gm->logic;
    for (int i = 0; i < l->nwatch; i++) {
        const Obj *t = &gm->obj[l->watch[i].obj];
        if (t->off) continue;
        int now = 0;
        if (t->tid == 1611) {
            const int *v = item_slot(gm, t->ttarget);
            now = v && *v == t->tcopy;
            if (now && !l->watch[i].state) activate_group(gm, t->tgroup, t->tact, 0);
        } else if (t->tid == 1815) {
            now = collision_now(gm, t);
            if (t->tlockx ? !now && l->watch[i].state : now && !l->watch[i].state) activate_group(gm, t->tgroup, t->tact, 0);
        } else {   // 1595 touch
            now = gm->input;
            const int pressed = now && !l->watch[i].state, released = !now && l->watch[i].state;
            if (pressed || (released && t->tlockx)) {
                int on = t->tcenter == 1 ? 1 : t->tcenter == 2 ? 0 : -1;
                if (on < 0) {   // toggle: look at the first object of the group
                    const int g = t->tgroup;
                    on = g > 0 && g < GD_GROUPS && gm->grp_start[g] < gm->grp_start[g + 1] &&
                         gm->obj[gm->grp_items[gm->grp_start[g]]].off;
                }
                activate_group(gm, t->tgroup, on, 0);
            }
        }
        l->watch[i].state = now;
    }
}

static void fire_trigger(Game *gm, Obj *t, int depth)
{
    switch (t->tid) {
    case 3022: { // teleport trigger: move the icon to a group, with the options of the teleport portal
        Player *p = &gm->p;
        if (!teleport_to_group(gm, p, t->tgroup)) break;
        if (t->tforce != 0) p->yv = t->tforce;
        if (t->tgrav) p->grav = (t->tgrav == 3 ? p->grav > 0 : t->tgrav == 2) ? -1 : 1;
        p->rising = p->yv * p->grav > 0;
        break;
    }
    case 1611: case 1815: case 1595: // keep watching from now on
        watch_trigger(gm, t);
        break;
    case 1817: { // pickup: change an item counter
        int *v = item_slot(gm, t->ttarget);
        if (v) *v += t->tcopy;
        break;
    }
    case 1811: { // instant count: compare once
        const int *v = item_slot(gm, t->ttarget);
        if (v && (t->tcenter == 1 ? *v > t->tcopy : t->tcenter == 2 ? *v < t->tcopy : *v == t->tcopy) && depth < 4)
            activate_group(gm, t->tgroup, t->tact, depth + 1);
        break;
    }
    case 1912: // random: one of two groups
        if (depth < 4) activate_group(gm, (float)(logic_random(gm) % 10000) < t->value * 100.0f ? t->tgroup : t->tcenter, 1, depth + 1);
        break;
    case 2068: case 3607: { // weighted random / sequence over a list of groups
        const int oi = (int)(t - gm->obj);
        for (int i = 0; i < gm->nlists; i++) {
            if (gm->lists[i].obj != oi) continue;
            const short *v = gm->lists[i].list;
            const int n = gm->lists[i].n & ~1;
            int total = 0, pick = -1;
            for (int k = 1; k < n; k += 2) total += v[k] > 0 ? v[k] : 0;
            if (total <= 0) break;
            int at;
            if (t->tid == 2068) at = (int)(logic_random(gm) % (unsigned)total);
            else { touch(gm, t); at = t->used % total; t->used = (at + 1) % total; }
            for (int k = 0; k < n && pick < 0; k += 2) {
                at -= v[k + 1] > 0 ? v[k + 1] : 0;
                if (at < 0) pick = v[k];
            }
            if (depth < 4) activate_group(gm, pick, 1, depth + 1);
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
        break;
    case 1935: // time warp
        if (!gm->sim) gm->timewarp = fminf(fmaxf(t->value, 0.1f), 2.0f);
        break;
    case 2066: // gravity factor
        if (t->value > 0.01f) gm->p.gscale = gm->p2.gscale = t->value;
        break;
    case 1914: // static camera: the camera is not simulated, but the flying borders that follow it go away
        if (gm->has_jumps && !t->tact) gm->free = 1;
        break;
    case 3600: // end of the level
        gm->finished = 1;
        break;
    case 1347: // follow: a group moves along with the first object of another group
        if (gm->nmv < GD_MAX_MOVES && t->tcenter > 0 && t->tcenter < GD_GROUPS &&
            gm->grp_start[t->tcenter] < gm->grp_start[t->tcenter + 1]) {
            Move *m = &gm->mv[gm->nmv++];
            memset(m, 0, sizeof *m);
            const Obj *lead = &gm->obj[gm->grp_items[gm->grp_start[t->tcenter]]];
            m->group = t->tgroup; m->follow = t->tcenter; m->dur = t->tdur > 0 ? t->tdur : 0.001f;
            m->dx = t->fxmod; m->dy = t->fymod; m->lx = lead->x; m->ly = lead->y;
        }
        break;
    case 901: // move
        if (t->tdur <= 0.001f) { group_apply_move(gm, t->tgroup, t->tdx, t->tdy); break; }
        if (gm->nmv < GD_MAX_MOVES) {
            Move *m = &gm->mv[gm->nmv++];
            memset(m, 0, sizeof *m);
            m->group = t->tgroup; m->dx = t->tdx; m->dy = t->tdy; m->dur = t->tdur;
            m->easing = t->teasing; m->rate = t->trate;
            m->lockx = t->tlockx; m->locky = t->tlocky;
            m->lx = gm->p.x; m->ly = gm->p.y;
            if (m->lockx) m->dx = 0;
            if (m->locky) m->dy = 0;
        }
        break;
    case 1346: // rotate a group around the object of the centre group
        if (t->tdur <= 0.001f) { group_apply_rotate(gm, t->tgroup, t->tcenter, t->trot); break; }
        if (gm->nmv < GD_MAX_MOVES) {
            Move *m = &gm->mv[gm->nmv++];
            memset(m, 0, sizeof *m);
            m->group = t->tgroup; m->center = t->tcenter > 0 ? t->tcenter : t->tgroup; m->rot = t->trot;
            m->dur = t->tdur; m->easing = t->teasing; m->rate = t->trate;
        }
        break;
    case 1616: // stop: cancel the running commands of a group
        for (int i = 0; i < gm->nmv;)
            if (gm->mv[i].group == t->tgroup) gm->mv[i] = gm->mv[--gm->nmv]; else i++;
        break;
    case 1049: // toggle
        if (t->tgroup > 0 && t->tgroup < GD_GROUPS)
            for (int k = gm->grp_start[t->tgroup]; k < gm->grp_start[t->tgroup + 1]; k++)
            {
                Obj *o = &gm->obj[gm->grp_items[k]];
                if (!gm->sim || (o->type != OT_DECO && o->type != OT_HIDDEN)) { touch(gm, o); o->off = t->tact ? 0 : 1; }
            }
        break;
    case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 55: case 56: case 57: case 58: case 59: case 1915:
        if (!gm->sim) gm->enter_fx = t->tid;   // visual only
        break;
    case 1007: // alpha (visual only), fading over the trigger's duration
        if (gm->sim || t->tgroup <= 0 || t->tgroup >= GD_GROUPS) break;
        for (int i = 0; i < gm->naf;)
            if (gm->af[i].group == t->tgroup) gm->af[i] = gm->af[--gm->naf]; else i++;
        if (t->tdur > 0.001f && gm->naf < GD_MAX_PULSES)
            gm->af[gm->naf++] = (AlphaFade){t->tgroup, gm->galpha[t->tgroup], t->topacity, t->tdur, 0};
        else
            gm->galpha[t->tgroup] = t->topacity;
        break;
    case 1006: // pulse (visual only)
        if (gm->sim || gm->npu >= GD_MAX_PULSES) break;
        gm->pu[gm->npu++] = (Pulse){t->ttarget, t->pgroup, t->pcopy ? (t->tcopy > 0 ? t->tcopy : -1) : 0, t->ponly,
                                    t->tr, t->tg, t->tb, t->phue, t->pin, t->phold, t->pout, 0};
        break;
    case 1268: // spawn: fire the triggers of a group, optionally after a delay
        if (depth < 4 && t->tgroup > 0 && t->tgroup < GD_GROUPS)
            for (int k = gm->grp_start[t->tgroup]; k < gm->grp_start[t->tgroup + 1]; k++) {
                int oi = gm->grp_items[k];
                if (gm->obj[oi].type != OT_TRIGGER) continue;
                if (t->tdelay > 0.001f) {
                    if (gm->nsp < GD_MAX_SPAWNS) { gm->sp[gm->nsp].obj = oi; gm->sp[gm->nsp].left = t->tdelay; gm->nsp++; }
                } else {
                    fire_trigger(gm, &gm->obj[oi], depth + 1);
                }
            }
        break;
    default: // colour triggers
        if (!gm->sim) {
            start_fade(gm, t);
            if (t->tground) {
                Obj g = *t;
                g.ttarget = CH_GROUND;
                start_fade(gm, &g);
            }
        }
        break;
    }
}

// Advances running move commands and delayed spawns by one simulation step.
static void triggers_step(Game *gm)
{
    for (int i = 0; i < gm->nmv;) {
        Move *m = &gm->mv[i];
        m->t += STEP;
        float f = ease_apply(m->easing, m->t >= m->dur ? 1.0f : m->t / m->dur, m->rate);
        float d = f - m->done;
        if (m->follow) {
            const Obj *lead = &gm->obj[gm->grp_items[gm->grp_start[m->follow]]];
            const float mx = (lead->x - m->lx) * m->dx, my = (lead->y - m->ly) * m->dy;
            m->lx = lead->x; m->ly = lead->y;
            if (mx != 0 || my != 0) group_apply_move(gm, m->group, mx, my);
        } else if (m->center) {
            if (d != 0) group_apply_rotate(gm, m->group, m->center, m->rot * d);
        } else {
            float mx = m->dx * d, my = m->dy * d;
            if (m->lockx) { mx += gm->p.x - m->lx; m->lx = gm->p.x; }
            if (m->locky) { my += gm->p.y - m->ly; m->ly = gm->p.y; }
            if (mx != 0 || my != 0) group_apply_move(gm, m->group, mx, my);
        }
        m->done = f;
        if (m->t >= m->dur) gm->mv[i] = gm->mv[--gm->nmv]; else i++;
    }
    for (int i = 0; i < gm->nsp;) {
        gm->sp[i].left -= STEP;
        if (gm->sp[i].left <= 0) {
            int oi = gm->sp[i].obj;
            gm->sp[i] = gm->sp[--gm->nsp];
            fire_trigger(gm, &gm->obj[oi], 1);
        } else i++;
    }
}

// ---------------------------------------------------------------- physics
//
// Ported from OpenGD (https://github.com/Open-GD/OpenGD, GPL-3.0): PlayerObject::update /
// updateJump / collidedWithObject / ringJump and PlayLayer::checkCollisions, extended with the
// wave, robot, spider, dual and mirror behaviour that OpenGD does not have.
// One simulation step is a quarter of a 60 Hz frame (OpenGD runs 4 sub-steps per frame), so
// STEP = 1/240 s corresponds to dt = 0.25 "frames". Positions are in level units with the
// ground line at y = 0 (OpenGD's y minus 90); the player position is the centre of the icon.

#define FRAMES_PER_STEP 0.25
#define GRAVITY 0.958199
#define JUMP_HEIGHT 11.180032

static int is_flipped(const Player *p) { return p->grav < 0; }
static double flip_mod(const Player *p) { return p->grav < 0 ? -1.0 : 1.0; }

static int falling(const Player *p)
{
    return is_flipped(p) ? p->yv > GRAVITY : p->yv < GRAVITY;
}

// Cube and robot walk on the level ground; every other mode (and anything in dual mode) lives
// between a floor and a ceiling that follow the camera centre.
static int is_restricted(const Game *gm, const Player *p)
{
    return gm->dual || (!gm->free && p->mode != M_CUBE && p->mode != M_ROBOT);
}

static void region(const Game *gm, const Player *p, float *fl, float *ce)
{
    float f, c;
    // The floor follows the (smoothed) camera like OpenGD's bottom ground, the ceiling follows
    // the camera centre directly. cam_target is the physics copy of the camera (OpenGD coordinates).
    if (p->mode == M_BALL || p->mode == M_SPIDER) { f = gm->cam_target - 35.0f; c = gm->ctr + 16.0f; }
    else { f = gm->cam_target - 65.0f; c = gm->ctr + 46.0f; }
    if (p->mini) { f -= 6.0f; c += 6.0f; }
    *fl = f; *ce = c;
}

static void run_rotate_action(Player *p)
{
    p->snap = 0;
    p->rot_left = 180.0f;
    p->rot_dir = (float)flip_mod(p);
    p->rot_rate = 180.0f / (0.41f * (p->mini ? 0.8f : 1.0f));
}

static void stop_rotation(Player *p)
{
    p->rot_left = 0;
    p->snap = 1;
}

static void flip_gravity(Player *p, int flipped)
{
    if (is_flipped(p) != flipped) {
        p->grav = flipped ? -1 : 1;
        p->yv /= 2.0;
    }
}

static void hit_ground(Player *p)
{
    p->yv = 0;
    p->queued = 0;
    p->on_ground = 1;
    p->last_ground_y = p->y;
    if (p->rot_left > 0) stop_rotation(p);
}

static void set_mode(Player *p, int mode)
{
    if (p->mode == mode) return;
    p->rot_left = 0;
    p->snap = 0;
    p->trail_n = 0;
    if (mode == M_SHIP) {
        p->rot = 0;
        p->yv /= 2.0;
        p->on_ground = 0;
        run_rotate_action(p);
    } else if (mode == M_UFO) {
        p->rot = 0;
        p->on_ground = 0;
    } else if (mode == M_WAVE) {
        p->rot = 0;
        p->yv = 0;
    } else {
        p->on_ground = 0;
        if (mode != M_BALL) p->rot = 0;
    }
    p->mode = mode;
}

static void propel(Player *p, double force)
{
    p->rising = 1;
    p->on_ground = 0;
    p->yv = flip_mod(p) * 16.0 * force * (p->mini ? 0.8 : 1.0);
    if (p->mode == M_BALL || p->mode == M_SPIDER) p->yv *= 0.6;
    if (p->mode == M_CUBE || p->mode == M_SHIP || p->mode == M_ROBOT) run_rotate_action(p);
}

// PlayerObject::ringJump
static void ring_jump(Player *p, const Obj *o, int *used)
{
    if (!(p->touch_ring && p->queued && p->hold)) return;
    *used = 1;
    p->rising = 1;
    p->queued = 0;
    p->on_ground = 0;
    double v = JUMP_HEIGHT;
    const int m = p->mode;
    if (o->sub == K_DASH || o->sub == K_DASH_FLIP) {   // fly straight along the orb while held
        if (o->sub == K_DASH_FLIP) flip_gravity(p, !is_flipped(p));
        float a = fmodf(o->rot, 360.0f);
        if (a > 180.0f) a -= 360.0f;
        if (a < -180.0f) a += 360.0f;
        if (a > 70.0f) a = 70.0f;
        if (a < -70.0f) a = -70.0f;
        p->dashing = 1;
        p->dash_slope = -tanf(a * 3.14159265f / 180.0f);
        p->yv = 0;
        p->touch_ring = 0;
        return;
    }
    if (o->sub == K_BLACK) {   // slams the icon in the direction of gravity
        p->yv = -flip_mod(p) * 15.0 * (p->mini ? 0.8 : 1.0);
        p->rising = 0;
        p->touch_ring = 0;
        return;
    }
    switch (o->sub) {
    case K_RED:
        if (m == M_SHIP) { if (p->mini) v *= 1.4; }
        else if (m == M_UFO) v *= p->mini ? 1.36 : 1.02;
        else if (m == M_SPIDER || m == M_BALL) v *= 1.34;
        else if (m == M_ROBOT) v *= 1.28;
        else v *= 1.38;
        break;
    case K_PINK:
        v *= m == M_SHIP ? 0.37 : m == M_UFO ? 0.42 : m == M_BALL ? 0.77 : 0.72;
        break;
    case K_BLUE:
        v *= 0.8;
        break;
    case K_GREEN:
        if (m == M_SHIP) v *= 0.7;
        flip_gravity(p, !is_flipped(p));
        break;
    default:
        if (m == M_ROBOT) v *= 0.9;
        break;
    }
    v *= flip_mod(p);
    v *= p->mini ? 0.8 : 1.0;
    p->yv = v;
    if (m == M_CUBE || m == M_SHIP || m == M_ROBOT) run_rotate_action(p);
    if (o->sub == K_BLUE) flip_gravity(p, !is_flipped(p)); // gravity ring flips after the jump
    if (m == M_BALL || m == M_SPIDER) { p->hold = 0; p->yv *= 0.7; }
    p->touch_ring = 0;
}

// PlayerObject::updateJump. dt is in OpenGD frame units, already * 0.9.
static void update_jump(Player *p, double dt)
{
    const double g = GRAVITY * (p->gscale > 0 ? p->gscale : 1.0f);
    const double flip = flip_mod(p);
    double size = p->mini ? 0.8 : 1.0;
    const int m = p->mode;

    if (m == M_SWING) {   // flies; every click turns gravity around
        if (p->hold && p->just_held) {
            p->just_held = 0;
            flip_gravity(p, !is_flipped(p));
        }
        const double limit = p->mini ? 9.0 : 8.0, f = flip_mod(p);
        p->yv -= g * dt * f * 0.65 / size;
        if (p->yv > limit) p->yv = limit;
        if (p->yv < -limit) p->yv = -limit;
        return;
    }

    if (m == M_SHIP || m == M_UFO) {
        if (p->mini) size = 0.85;
        double upper = 8.0 / size, lower = -6.4 / size;
        if (m == M_SHIP) {
            double accel = 0.8;
            if (p->hold) accel = -1.0;
            if (!p->hold && !falling(p)) accel = 1.2;
            double boost = 0.4;
            if (p->hold && falling(p)) boost = 0.5;
            p->yv -= g * dt * flip * accel * boost / size;
        } else {
            if (p->hold && p->just_held) {
                p->just_held = 0;
                double nv = flip * (p->mini ? 8.0 : 7.0) * size;
                if (!is_flipped(p) || nv < p->yv) p->yv = nv;
            }
            double gm_ = falling(p) ? 0.8 : 1.2;
            p->yv -= g * dt * flip * gm_ * 0.5 / size;
        }
        if (!is_flipped(p)) {
            if (p->yv <= lower) p->yv = lower;
        } else {
            if (p->yv <= -upper) p->yv = -upper;
            upper = 6.4 / size;
        }
        if (p->yv >= upper) p->yv = upper;
        return;
    }

    // cube, ball, robot, spider
    const double gmul = (m == M_BALL || m == M_SPIDER) ? 0.6 : 1.0;
    if (p->hold && p->on_ground) {
        p->rising = 1;
        p->on_ground = 0;
        p->yv = flip * JUMP_HEIGHT * size;
        if (m == M_BALL || m == M_SPIDER) {
            flip_gravity(p, !is_flipped(p));
            p->hold = 0;
            p->yv *= (m == M_SPIDER) ? 1.6 : 0.6;
        } else {
            if (!p->touch_ring) p->queued = 0;
            if (m == M_ROBOT) { p->yv *= 0.75; p->boost_t = 14.0f; }
            run_rotate_action(p);
        }
        return;
    }
    if (p->rising) {
        double gg = g;
        if (m == M_ROBOT && p->hold && p->boost_t > 0) { gg *= 0.25; p->boost_t -= (float)dt; }
        p->yv -= gg * dt * flip * gmul;
        if (falling(p)) {
            p->rising = 0;
            p->on_ground = 0;
        }
        return;
    }
    if (!is_flipped(p)) {
        if (p->yv < -g * 2.0) p->on_ground = 0;
    } else {
        if (p->yv > g * 2.0) p->on_ground = 0;
    }
    p->yv -= g * dt * flip * gmul;
    p->yv = is_flipped(p) ? fmin(p->yv, 15.0) : fmax(p->yv, -15.0);
    if (!is_flipped(p)) { if (p->yv >= g * 2.0) return; }
    else { if (p->yv <= g * 2.0) return; }
    if ((m == M_CUBE || m == M_ROBOT) && p->rot_left <= 0) run_rotate_action(p);
}

// Object rectangle in level coordinates (rotation and flip are baked into hx/hy/hw/hh).
static void obj_rect(const Obj *o, float *x0, float *x1, float *y0, float *y1)
{
    *x0 = o->x + o->hx - o->hw; *x1 = o->x + o->hx + o->hw;
    *y0 = o->y + o->hy - o->hh; *y1 = o->y + o->hy + o->hh;
}

static int rect_hit(float ax0, float ax1, float ay0, float ay1, float bx0, float bx1, float by0, float by1)
{
    return !(ax1 < bx0 || bx1 < ax0 || ay1 < by0 || by1 < ay0);
}

static int circle_hit(float rx0, float rx1, float ry0, float ry1, float cx, float cy, float r)
{
    float nx = cx < rx0 ? rx0 : cx > rx1 ? rx1 : cx;
    float ny = cy < ry0 ? ry0 : cy > ry1 ? ry1 : cy;
    float dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy <= r * r;
}

// PlayerObject::collidedWithObject. OpenGD keeps object rectangles shifted by +15 in both axes
// (so that "set position to MaxY" lands the icon centre on top of the block); MaxY/MinY
// reproduce that. For the mini icon the "landing window" is kept equal to the big icon's
// (it may sink 5 units into a block) by moving the reference points by the 6 unit rest offset.
static void collided(Game *gm, Player *p, const Obj *o, Obj *orig)
{
    float bx0, bx1, by0, by1;
    obj_rect(o, &bx0, &bx1, &by0, &by1);
    const float cy = p->y;
    const float oh = p->mini ? 9.0f : 15.0f;
    const float fm = (float)flip_mod(p);
    const int ship = p->mode == M_SHIP || p->mode == M_UFO || p->mode == M_SWING;
    const float rest = p->mini ? 6.0f : 0.0f;
    const float tol = p->mode == M_SHIP ? 9.0f : 5.0f;
    float topP = cy + (tol + rest) * fm;
    float botP = cy - (tol + rest) * fm;
    const float MaxY = by1 + 15.0f, MinY = by0 + 15.0f;
    const float MaxYP = cy + 15.0f + oh, MinYP = cy + 15.0f - oh;
    const float mid = (MinY + MaxY) * 0.5f;
    float t = topP, b = botP;

    const float iy0 = cy - 3.75f, iy1 = cy + 3.75f;
    const int inner_vert = !(iy1 < by0 || by1 < iy0);
    float rx0, rx1, ry0, ry1;
    obj_rect(o, &rx0, &rx1, &ry0, &ry1);
    const int inner_real = rect_hit(p->x - 3.75f, p->x + 3.75f, iy0, iy1, rx0, rx1, ry0, ry1);
    if (o->passable) {   // one-way: catches an icon that is coming down onto it, nothing else
        const float depth = fm > 0 ? ry1 - (cy - oh) : (cy + oh) - ry0;
        if (depth >= 0 && depth <= 12.0f && p->yv * fm <= 0) {
            p->y = fm > 0 ? ry1 + oh : ry0 - oh;
            hit_ground(p);
        }
        return;
    }
    if (gm->head_safe && !ship) {   // bumped the head inside an H block: stay below the ceiling
        const float depth = fm > 0 ? cy + oh - ry0 : ry1 - (cy - oh);
        if ((cy - (ry0 + ry1) * 0.5f) * fm < 0 && depth <= 14.0f) {
            p->y = fm > 0 ? ry0 - oh : ry1 + oh;
            if (p->yv * fm > 0) p->yv = 0;
            return;
        }
    }

    if (is_flipped(p)) {
        if (!ship) goto top_collision;
        t = botP; b = topP;
    }
    if (b >= MaxY || t >= MaxY) {
        if (p->yv < 0.0) {
            if (inner_vert) goto death;
            if (MaxYP >= mid) {
                p->y = MaxY - rest;
                hit_ground(p);
            }
        }
    }
    if (!is_flipped(p)) {
        if (!ship) goto death;
        t = botP; b = topP;
    }
top_collision:
    if (b <= MinY || t <= MinY) {
        if (p->yv > 0.0) {
            if (inner_vert) goto death;
            if (MinYP <= mid) {
                p->y = MinY - (p->mini ? 24.0f : 30.0f);
                hit_ground(p);
            }
        }
    }
death:
    if (!inner_real) return;
    if (o->breakable) { touch(gm, orig); orig->off = 1; }   // the brick shatters (restored on the next attempt)
    else die_why(gm, "inner", o);
}

// Triangles: the icon slides along the long side; running into one of the short sides kills.
// Ceiling slopes are handled by mirroring everything vertically.
static void slope_collide(Game *gm, Player *p, const Obj *o)
{
    float x0, x1, y0, y1;
    obj_rect(o, &x0, &x1, &y0, &y1);
    const float half = p->mini ? 9.0f : 15.0f;
    const int ceiling = o->slope & 4, high_right = o->slope & 2;
    const float m = ceiling ? -1.0f : 1.0f;       // mirrored frame: the solid part is below
    const float py = p->y * m, lo = ceiling ? -y1 : y0, hi = ceiling ? -y0 : y1;
    const float w = x1 - x0, h = hi - lo;
    if (w <= 0 || h <= 0 || p->x + half <= x0 || p->x - half >= x1) return;
    const float len = sqrtf(w * w + h * h);
    const float lift = half * len / w;            // centre height above the surface (resting flat on it)
    float t = (p->x - x0) / w;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const float surface = lo + (high_right ? t : 1.0f - t) * h;
    float rest = surface + lift;
    if (rest > hi + half) rest = hi + half;       // the top end continues as flat ground
    if (py >= rest) return;                       // above the slope
    const int towards = p->grav * (ceiling ? -1 : 1) > 0; // gravity pulls onto this surface
    const int flies = p->mode == M_SHIP || p->mode == M_UFO || p->mode == M_SWING;
    const float step_rise = p->speed * STEP * h / w;
    const float tol = (p->mini ? 8.0f : 11.0f) + step_rise;
    // how deep the centre line of the icon is inside the triangle itself
    const int inside = p->x >= x0 - 3.0f && p->x <= x1 + 3.0f && py >= lo - half;
    if (p->mode == M_WAVE) {
        if (p->x + 4.0f > x0 && p->x - 4.0f < x1 && py - 4.0f < surface && py + 4.0f > lo) die_why(gm, "wave-slope", o);
        return;
    }
    if ((towards || flies) && rest - py <= tol && inside) {
        const double up = p->yv * m * (ceiling ? 1.0 : 1.0);
        // a ramp that climbs clearly faster than the rising icon catches it again (launch ramps)
        const double ramp_up = p->speed * STEP * h / w / (FRAMES_PER_STEP * 0.9);
        const int caught = towards && high_right && !flies && up < ramp_up * 0.75 && rest - py > 4.0f;
        if (up > 0.5 && !caught && (flies ? !(high_right && towards) : p->rising)) return;   // leaving the surface
        p->y = rest * m;
        if (towards) hit_ground(p);
        else p->yv = 0;
        // going uphill: keep the upward speed, so that the icon takes off at the top end
        if (towards && high_right && t < 1.0f)
            p->yv = m * (double)(p->speed * STEP * h / w) / (FRAMES_PER_STEP * 0.9);
        return;
    }
    // inner box against the triangle: hit a short side, or came from the wrong direction
    if (p->x + 3.75f > x0 && p->x - 3.75f < x1 && py + 3.75f > lo && py - 3.75f < surface) die_why(gm, "slope", o);
}

static void set_ctr(Game *gm, int mode, float obj_y)
{
    float oy = obj_y + 90.0f; // OpenGD coordinates
    if (mode == M_SHIP || mode == M_UFO || mode == M_WAVE || mode == M_SWING)
        gm->ctr = oy < 270.0f ? 240.0f : floorf(oy / 30.0f) * 30.0f;
    else if (mode == M_BALL || mode == M_SPIDER)
        gm->ctr = oy < 240.0f ? 210.0f : floorf(oy / 30.0f) * 30.0f;
    // with teleports the play area cannot glide after the camera: it moves at once
    if (gm->has_jumps) gm->cam_target = fmaxf(0.0f, gm->ctr - 160.0f);
}

static int rotated(const Game *gm) { return !(gm->frame.fx == 1 && gm->frame.uy == 1); }

static void to_local(Frame f, float wx, float wy, float *lx, float *ly)
{
    *lx = f.fx * wx + f.fy * wy;
    *ly = f.ux * wx + f.uy * wy;
}

static void to_world(Frame f, float lx, float ly, float *wx, float *wy)
{
    *wx = f.fx * lx + f.ux * ly;
    *wy = f.fy * lx + f.uy * ly;
}

// A copy of an object as seen from the rotated movement frame (position, hitbox, slope shape).
static void local_obj(Frame f, const Obj *o, Obj *out)
{
    *out = *o;
    to_local(f, o->x, o->y, &out->x, &out->y);
    to_local(f, o->hx, o->hy, &out->hx, &out->hy);
    if (f.fx == 0) { out->hw = o->hh; out->hh = o->hw; }
    if (o->slope) {
        float cx, cy;
        to_local(f, (o->slope & 2) ? 1.0f : -1.0f, (o->slope & 4) ? 1.0f : -1.0f, &cx, &cy);
        out->slope = (unsigned char)(1 | (cx > 0 ? 2 : 0) | (cy > 0 ? 4 : 0));
    }
}

// Moves the player to the first object of a group (2.2 teleport portals and orbs).
static int teleport_to_group(Game *gm, Player *p, int group)
{
    if (group <= 0 || group >= GD_GROUPS || gm->grp_start[group] >= gm->grp_start[group + 1]) return 0;
    const Obj *t = &gm->obj[gm->grp_items[gm->grp_start[group]]];
    if (gm->local) to_local(gm->frame, t->x, t->y, &p->x, &p->y);
    else { p->x = t->x; p->y = t->y; }
    p->on_ground = 0;
    return 1;
}

static void apply_portal(Game *gm, Player *p, const Obj *o)
{
    const int is1 = p == &gm->p;
    int mode = -1;
    switch (o->sub) {
    case P_GRAV_DOWN: flip_gravity(p, 0); break;
    case P_GRAV_UP: flip_gravity(p, 1); break;
    case P_CUBE: mode = M_CUBE; break;
    case P_SHIP: mode = M_SHIP; break;
    case P_BALL: mode = M_BALL; break;
    case P_UFO: mode = M_UFO; break;
    case P_WAVE: mode = M_WAVE; break;
    case P_ROBOT: mode = M_ROBOT; break;
    case P_SPIDER: mode = M_SPIDER; break;
    case P_SIZE_NORMAL: p->mini = 0; break;
    case P_SIZE_MINI: p->mini = 1; break;
    case P_MIRROR_ON: if (is1) gm->mirror = 1; break;
    case P_MIRROR_OFF: if (is1) gm->mirror = 0; break;
    case P_DUAL_ON:
        if (is1 && !gm->dual) {
            gm->dual = 1;
            gm->p2 = gm->p;
            gm->p2.y = 2.0f * o->y - gm->p.y;
            gm->p2.grav = -gm->p.grav;
            gm->p2.yv = -gm->p.yv;
            gm->p2.trail_n = 0;
            set_ctr(gm, gm->p.mode == M_CUBE || gm->p.mode == M_ROBOT ? M_SHIP : gm->p.mode, o->y);
        }
        break;
    case P_DUAL_OFF:
        if (is1) gm->dual = 0;
        break;
    case P_TELEPORT:
        if (!teleport_to_group(gm, p, o->tgroup)) p->y = o->y + o->tdy;   // exit height relative to the entry
        if (is1 && gm->has_jumps && !rotated(gm)) set_ctr(gm, p->mode, p->y);
        if (o->tforce != 0) p->yv = o->tforce;
        if (o->tgrav) {   // only the direction of gravity changes, the icon keeps its momentum
            p->grav = (o->tgrav == 3 ? !is_flipped(p) : o->tgrav == 2) ? -1 : 1;
            p->rising = p->yv * p->grav > 0;
            p->on_ground = 0;
        }
        p->rising = p->yv * p->grav > 0;
        break;
    case P_SWING: mode = M_SWING; break;
    default: break;
    }
    if (mode >= 0) {
        if (is1) gm->free = o->freemode;
        set_mode(p, mode);
        // in dual mode the play area stays where the dual portal put it
        if (is1 && !gm->dual) set_ctr(gm, mode, o->y);
    }
    {
        float r, g, b;
        portal_rgb(o->sub, &r, &g, &b);
        emit_burst(gm, o->x, o->y, 14, 140.0f, 0.5f, 9.0f, r, g, b);
    }
}

static void trail_push(Player *p)
{
    p->trail[p->trail_i][0] = p->x;
    p->trail[p->trail_i][1] = p->y;
    p->trail_i = (p->trail_i + 1) & 63;
    if (p->trail_n < 64) p->trail_n++;
}

static void spider_teleport(Game *gm, Player *p);

// PlayLayer::checkCollisions for one player. In a rotated movement frame the player is given in that
// frame, and the objects are converted into it.
static void check_collisions(Game *gm, Player *p)
{
    const int rot = rotated(gm);
    float wpx = p->x, wpy = p->y;
    if (rot) to_world(gm->frame, p->x, p->y, &wpx, &wpy);
    const int is1 = p == &gm->p;
    const int wave = p->mode == M_WAVE;
    const float oh = p->mini ? 9.0f : 15.0f;
    const float bh = wave ? (p->mini ? 3.0f : 5.0f) : oh;   // body box used against blocks/hazards
    float floor_y = p->mini ? 9.0f : 15.0f, ceil_y = 0;

    if (gm->has_jumps && (wpy < -200.0f || wpy > 4000.0f || wpx < -300.0f)) { die_why(gm, "outside", NULL); return; }
    // rotate triggers of the active channel fire when passed in the current movement direction
    if (is1) {
        for (int i = 0; i < gm->nturns; i++) {
            Obj *t = &gm->obj[gm->turns[i]];
            if (t->tdone || t->off || t->spawn_only || t->channel != gm->channel) continue;
            float tx = t->x, ty = t->y;
            if (rot) to_local(gm->frame, t->x, t->y, &tx, &ty);
            if (p->x < tx || p->x > tx + 400.0f) continue;
            touch(gm, t);
            t->tdone = 1;
            fire_trigger(gm, t, 0);
            break;
        }
    }
    if (rot) {
        // no level floor or play area borders while the gameplay is turned
    } else if (is_restricted(gm, p)) {
        region(gm, p, &floor_y, &ceil_y);
        if (p->y < floor_y) {
            p->y = floor_y;
            if (!is_flipped(p) || wave) hit_ground(p);
            p->yv = 0;
        }
        if (p->y > ceil_y) {
            p->y = ceil_y;
            if (is_flipped(p) || wave) hit_ground(p);
            p->yv = 0;
        }
    } else {
        if (p->y < floor_y) {
            if (is_flipped(p) && !gm->free) { die_why(gm, "floor", NULL); return; }
            if (is_flipped(p)) p->yv = 0;
            p->y = floor_y;
            hit_ground(p);
        } else if (p->y > (gm->has_jumps ? 4000.0f : 1200.0f)) {
            die_why(gm, "ceiling", NULL);
            return;
        }
    }

    int cand[1536], nc = 0;
    for (int i = first_index(gm, wpx - 90); i < gm->nx && nc < 1400; i++) {
        const Obj *o = &gm->obj[gm->by_x[i]];
        if (o->x > wpx + 90) break;
        if (rot && fabsf(o->y - wpy) > 200.0f) continue;
        cand[nc++] = gm->by_x[i];
    }
    for (int m = 0; m < gm->nmov && nc < 1530; m++) {
        const Obj *o = &gm->obj[gm->movers[m]];
        if (fabsf(o->x - wpx) <= 90.0f && fabsf(o->y - wpy) <= (rot ? 200.0f : 400.0f)) cand[nc++] = gm->movers[m];
    }
    // test bot: flying past a form portal means it took a wrong route
    if (gm->bot_strict && is1 && !rot) {
        for (int ci = 0; ci < nc; ci++) {
            const Obj *o = &gm->obj[cand[ci]];
            if (o->off || o->type != OT_PORTAL || o->used || p->x < o->x + 55.0f || p->x > o->x + 80.0f) continue;
            // turned levels run through the same columns on several lanes: only nearby portals count
            if (gm->has_jumps && fabsf(p->y - o->y) > (gm->channel ? 700.0f : 400.0f)) continue;
            const int want = o->sub == P_CUBE ? M_CUBE : o->sub == P_SHIP ? M_SHIP : o->sub == P_BALL ? M_BALL
                           : o->sub == P_UFO ? M_UFO : o->sub == P_WAVE ? M_WAVE : o->sub == P_ROBOT ? M_ROBOT
                           : o->sub == P_SPIDER ? M_SPIDER : o->sub == P_SWING ? M_SWING : -1;
            if (want >= 0 && want != p->mode) { die_why(gm, "missed portal", o); return; }
        }
    }
    gm->head_safe = 0;
    for (int ci = 0; ci < nc; ci++) {
        const Obj *o = &gm->obj[cand[ci]];
        Obj turned;
        if (o->type != OT_HBLOCK || o->off) continue;
        if (rot) { local_obj(gm->frame, o, &turned); o = &turned; }
        if (fabsf(p->x - o->x) < oh + o->hw && fabsf(p->y - o->y) < oh + o->hh) gm->head_safe = 1;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int ci = 0; ci < nc; ci++) {
            Obj *r = &gm->obj[cand[ci]], turned;   // r holds the state, o the geometry
            if (r->off) continue;
            const Obj *o = r;
            if (rot) { local_obj(gm->frame, r, &turned); o = &turned; }
            if (o->type == OT_TRIGGER) {
                // normal triggers fire when passed, touch triggers only when the icon runs into them
                const int reached = o->touch ? fabsf(p->x - o->x) <= oh + 15.0f && fabsf(p->y - o->y) <= oh + 15.0f
                                             : p->x >= o->x;
                if (is1 && pass == 0 && !r->tdone && !r->spawn_only && reached && r->tid != 2900 &&
                    (r->touch || r->channel == gm->channel)) {
                    touch(gm, r);
                    r->tdone = 1;
                    fire_trigger(gm, r, 0);
                }
                continue;
            }
            if (o->type == OT_DECO || o->type == OT_HIDDEN || o->type == OT_SENSOR || o->type == OT_HBLOCK) continue;
            if (o->hw <= 0 || o->hh <= 0) { if (o->rad <= 0) continue; }
            if ((o->type == OT_HAZARD) != (pass == 1)) continue;

            float x0, x1, y0, y1;
            obj_rect(o, &x0, &x1, &y0, &y1);
            if (pass == 1) {
                if (o->slope) {   // sloped spike pit: only the filled half of the rectangle
                    const float hx = p->x < x0 ? x0 : p->x > x1 ? x1 : p->x;
                    float t = x1 > x0 ? (hx - x0) / (x1 - x0) : 0;
                    if (!(o->slope & 2)) t = 1.0f - t;
                    const float fill = t * (y1 - y0) - 6.0f;
                    const int in = p->x + bh * 0.5f > x0 && p->x - bh * 0.5f < x1 &&
                                   ((o->slope & 4) ? p->y + bh * 0.5f > y1 - fill && p->y - bh * 0.5f < y1
                                                   : p->y - bh * 0.5f < y0 + fill && p->y + bh * 0.5f > y0);
                    if (in) { die_why(gm, "hazard", o); return; }
                    continue;
                }
                int hit = o->rad > 0 ? circle_hit(p->x - bh, p->x + bh, p->y - bh, p->y + bh, o->x, o->y, o->rad)
                                     : rect_hit(p->x - bh, p->x + bh, p->y - bh, p->y + bh, x0, x1, y0, y1);
                if (hit) { die_why(gm, "hazard", o); return; }
                continue;
            }
            int *used = is1 ? &r->used : &r->used2;
            if (o->type == OT_SOLID) {
                if (o->slope) {
                    if (rect_hit(p->x - oh - 1.0f, p->x + oh + 1.0f, p->y - oh * 2.0f, p->y + oh * 2.0f, x0, x1, y0, y1))
                        slope_collide(gm, p, o);
                    if (gm->p.dead) return;
                    continue;
                }
                if (!rect_hit(p->x - bh, p->x + bh, p->y - bh, p->y + bh, x0, x1, y0, y1)) continue;
                if (wave) { die_why(gm, "wave-block", o); return; }
                collided(gm, p, o, r);
                if (gm->p.dead) return;
                continue;
            }
            if (!rect_hit(p->x - oh, p->x + oh, p->y - oh, p->y + oh, x0, x1, y0, y1)) continue;
            if (*used) continue;
            if (o->type != OT_FORCE) touch(gm, r);
            if (o->type == OT_FORCE) {   // pushes along its arrow while touched
                const float a = r->rot * 3.14159265f / 180.0f;
                float lx, ly;
                to_local(gm->frame, sinf(a), cosf(a), &lx, &ly);
                p->yv += ly * r->value * GRAVITY * FRAMES_PER_STEP * 0.9;
                if (ly * p->grav > 0.1f) p->on_ground = 0;
                continue;
            }
            switch (o->type) {
            case OT_PORTAL:
                *used = 1;
                apply_portal(gm, p, o);
                break;
            case OT_SPEED: p->speed = k_speeds[o->sub]; break;
            case OT_PAD:
                if (o->sub == K_SPIDER) {
                    *used = 1;
                    spider_teleport(gm, p);
                    break;
                }
                if (wave) {
                    if (o->sub == K_BLUE) { *used = 1; flip_gravity(p, !is_flipped(p)); }
                    break;
                }
                if (o->sub == K_BLUE) {
                    if (p->touch_pad) break;
                    *used = 1;
                    { float r, g, b; kind_rgb(o->sub, &r, &g, &b); emit_burst(gm, o->x, o->y - 6, 10, 130.0f, 0.4f, 8.0f, r, g, b); }
                    propel(p, 0.8);
                    p->touch_pad = 1;
                    flip_gravity(p, !is_flipped(p));
                } else {
                    *used = 1;
                    { float r, g, b; kind_rgb(o->sub, &r, &g, &b); emit_burst(gm, o->x, o->y - 6, 10, 130.0f, 0.4f, 8.0f, r, g, b); }
                    propel(p, o->sub == K_YELLOW ? 1.0 : o->sub == K_PINK ? 0.65 : 1.25);
                    p->touch_pad = 1;
                }
                break;
            case OT_ORB:
                if (o->sub == K_TOGGLE || o->sub == K_SPIDER || o->sub == K_TELEPORT) {
                    if (!(p->queued && p->hold)) break;
                    *used = 1;
                    p->queued = 0;
                    if (o->sub == K_TOGGLE) activate_group(gm, o->tgroup, o->tact, 0);
                    else if (o->sub == K_TELEPORT) teleport_to_group(gm, p, o->tgroup);
                    else spider_teleport(gm, p);
                    break;
                }
                if (wave) {
                    if ((o->sub == K_BLUE || o->sub == K_GREEN) && p->queued && p->hold) {
                        *used = 1; p->queued = 0; flip_gravity(p, !is_flipped(p));
                    }
                    break;
                }
                p->touch_ring = 1;
                {
                    int was_used = *used;
                    ring_jump(p, o, used);
                    if (!was_used && *used) {
                        float r, g, b;
                        kind_rgb(o->sub, &r, &g, &b);
                        emit(gm, o->x, o->y, 0, 0, 0.3f, 14.0f, 140.0f, r, g, b, 0.9f);
                        emit_burst(gm, o->x, o->y, 12, 150.0f, 0.45f, 8.0f, r, g, b);
                    }
                }
                break;
            default: break;
            }
        }
    }
    if (p->mode == M_SHIP) p->queued = 0;
}

// Spider: a click teleports the icon to the opposite surface (nearest block face or the level
// floor/ceiling in the new gravity direction).
static void spider_teleport(Game *gm, Player *p)
{
    const float oh = p->mini ? 9.0f : 15.0f;
    float fl, ce;
    region(gm, p, &fl, &ce);
    const int rot = rotated(gm);
    float wpx = p->x, wpy = p->y;
    const int open = rot || !is_restricted(gm, p);   // no play area: only blocks (and the ground) catch the icon
    if (open) {
        if (rot) to_world(gm->frame, p->x, p->y, &wpx, &wpy);
        fl = rot ? p->y - 600.0f : oh;
        ce = p->y + 600.0f;
    }
    const int up = is_flipped(p); // after the flip gravity points up when it is currently down...
    const int going_up = !up;     // ...so the icon travels in the direction of the NEW gravity
    float target = going_up ? ce : fl;
    float best = target;
    int cand[1536], nc = 0;
    const float reach = rot ? 620.0f : 60.0f;
    for (int i = first_index(gm, wpx - reach); i < gm->nx && nc < 1400; i++) {
        if (gm->obj[gm->by_x[i]].x > wpx + reach) break;
        cand[nc++] = gm->by_x[i];
    }
    for (int m = 0; m < gm->nmov && nc < 1530; m++)
        if (fabsf(gm->obj[gm->movers[m]].x - wpx) <= reach) cand[nc++] = gm->movers[m];
    for (int ci = 0; ci < nc; ci++) {
        Obj turned;
        const Obj *o = &gm->obj[cand[ci]];
        if (o->off || o->type != OT_SOLID || o->hw <= 0) continue;
        if (rot) { local_obj(gm->frame, o, &turned); o = &turned; }
        float x0, x1, y0, y1;
        obj_rect(o, &x0, &x1, &y0, &y1);
        if (x1 <= p->x - oh + 0.5f || x0 >= p->x + oh - 0.5f) continue;
        if (going_up) {
            if (y0 >= p->y + oh - 1.0f && y0 - oh < best) best = y0 - oh;
        } else {
            if (y1 <= p->y - oh + 1.0f && y1 + oh > best) best = y1 + oh;
        }
    }
    if (open && best == target && (rot || going_up)) return;   // nothing to land on
    // a hazard between here and the landing surface catches the icon on its way
    for (int ci = 0; ci < nc; ci++) {
        Obj turned;
        const Obj *o = &gm->obj[cand[ci]];
        if (o->off || o->type != OT_HAZARD) continue;
        if (rot) { local_obj(gm->frame, o, &turned); o = &turned; }
        float x0, x1, y0, y1;
        if (o->rad > 0) { x0 = o->x - o->rad; x1 = o->x + o->rad; y0 = o->y - o->rad; y1 = o->y + o->rad; }
        else if (o->hw > 0) obj_rect(o, &x0, &x1, &y0, &y1);
        else continue;
        if (x1 <= p->x - oh * 0.5f || x0 >= p->x + oh * 0.5f) continue;
        if (going_up ? (y0 >= p->y + oh && y0 < best) : (y1 <= p->y - oh && y1 > best)) {
            die_why(gm, "spider", o);
            return;
        }
    }
    p->y = best;
    p->grav = going_up ? -1 : 1;
    p->yv = 0;
    p->on_ground = 1;
    p->rising = 0;
    p->last_ground_y = p->y;
}

static void step_player(Game *gm, Player *p)
{
    // running rotation action / snap back to a multiple of 90 degrees
    if (p->mode == M_BALL) {
        p->rot += 720.0f * STEP * (float)flip_mod(p);
    } else if (p->rot_left > 0) {
        float s = p->rot_rate * STEP;
        if (s > p->rot_left) s = p->rot_left;
        p->rot += p->rot_dir * s;
        p->rot_left -= s;
    } else if (p->snap && (p->mode == M_CUBE || p->mode == M_ROBOT)) {
        float target = floorf(p->rot / 90.0f + 0.5f) * 90.0f;
        p->rot += (target - p->rot) * fminf(1.0f, STEP / 0.075f);
        if (fabsf(target - p->rot) < 0.05f) { p->rot = target; p->snap = 0; }
    }

    // physics runs in the movement frame; positions are stored in level coordinates
    const int rot = rotated(gm);
    if (rot) {
        const float wx = p->x, wy = p->y;
        to_local(gm->frame, wx, wy, &p->x, &p->y);
    }
    gm->local = rot;

    // PlayerObject::update
    const double dt = FRAMES_PER_STEP;
    const double dt_slow = dt * 0.9;
    const float prev_y = p->y;
    if (p->mode == M_WAVE) {
        float dir = (p->hold ? 1.0f : -1.0f) * (float)flip_mod(p);
        float sp = p->speed * STEP * (p->mini ? 2.0f : 1.0f);
        p->y += dir * sp;
        p->x += p->speed * STEP;
        p->yv = dir * 0.5; // sign only (pads/orbs ignore it)
    } else if (p->dashing && p->hold) {
        p->y += p->dash_slope * p->speed * STEP;
        p->x += p->speed * STEP;
        p->yv = 0;
        p->on_ground = 0;
    } else {
        p->dashing = 0;
        if (p->mode == M_SPIDER && p->hold && p->on_ground) { spider_teleport(gm, p); p->hold = 0; }
        else update_jump(p, dt_slow);
        p->y += (float)(dt_slow * p->yv);
        p->x += p->speed * STEP;
    }
    p->vy = (float)(p->yv * 54.0);
    p->touch_ring = 0; // re-armed by the overlap test below
    p->touch_pad = 0;

    const int grav_before = p->grav;
    check_collisions(gm, p);
    // rotated and free sections have no floor: an icon that only falls for 1.5 s has left the level
    if (gm->has_jumps && p->mode != M_SHIP && p->mode != M_SWING && p->mode != M_UFO && p->mode != M_WAVE &&
        !p->on_ground && fabs(p->yv) >= 14.9) {
        if (++p->fall > 360 && !gm->noclip) die_why(gm, "outside", NULL);
    } else p->fall = 0;
    const float travelled = p->y - prev_y, lx = p->x, ly = p->y;
    if (rot) to_world(gm->frame, lx, ly, &p->x, &p->y);
    gm->local = 0;
    // dual mode: while both icons have the same form, their gravities stay opposite
    if (gm->dual && p->grav != grav_before) {
        Player *other = p == &gm->p ? &gm->p2 : &gm->p;
        if (other->mode == p->mode) flip_gravity(other, p->grav > 0);
    }

    if (p->mode == M_SHIP || p->mode == M_SWING) {
        float dy = travelled, dx = p->speed * STEP;
        if (dx * dx + dy * dy > 0) {
            float ang = -atan2f(dy, dx) * 180.0f / 3.14159265f;
            if (p->mini) ang *= 1.2f;
            p->rot += (ang - p->rot) * 0.04f;
        }
    } else if (p->mode == M_WAVE) {
        float dy = travelled, dx = p->speed * STEP;
        float ang = -atan2f(dy, dx) * 180.0f / 3.14159265f;
        if (fabsf(dy) < 1e-4f) ang = 0;
        p->rot = ang;
        static int tick;
        if ((++tick & 1) == 0 || p->trail_n == 0) trail_push(p);
    } else if (p->mode == M_UFO || p->mode == M_SPIDER) {
        p->rot = 0;
    }
}

static void step(Game *gm, float d)
{
    (void)d;
    if (gm->p.dead) return;
    gm->time += STEP;
    gm->travel += gm->p.speed * STEP;
    if (is_restricted(gm, &gm->p)) {   // physics copy of the camera lerp (0.1 per 60 Hz frame)
        float tgt = fmaxf(0.0f, gm->ctr - 160.0f);
        if (tgt > 820.0f && !gm->has_jumps) tgt = 820.0f;
        gm->cam_target += (tgt - gm->cam_target) * (1.0f - powf(0.9f, STEP * 60.0f));
    } else {
        gm->cam_target = gm->cam_bottom + 90.0f;
    }
    triggers_step(gm);
    if (gm->logic.nwatch) logic_step(gm);
    {
        int g1 = gm->p.on_ground, g2 = gm->p2.on_ground;
        step_player(gm, &gm->p);
        player_fx(gm, &gm->p, g1);
        if (gm->dual && !gm->p.dead) { step_player(gm, &gm->p2); player_fx(gm, &gm->p2, g2); }
    }
    if (memcmp(&gm->frame, &gm->next_frame, sizeof(Frame))) {   // turn the gameplay
        const Frame old = gm->frame;
        // moving sideways with the ground above is plain flipped gravity (later gravity portals undo it)
        int grav = 1;
        if (gm->next_frame.uy < 0) { gm->next_frame.uy = 1; grav = -1; }
        gm->frame = gm->next_frame;
        Player *pl[2] = {&gm->p, &gm->p2};
        for (int i = 0; i < 2; i++) {
            // the icon keeps its momentum: express the old velocity along the new "up" axis
            // (yv is in units per 60 Hz frame * 0.9, i.e. 54 units per second)
            const float fwd = pl[i]->speed / 54.0f, up = (float)pl[i]->yv;
            const float vx = old.fx * fwd + old.ux * up, vy = old.fy * fwd + old.uy * up;
            pl[i]->yv = gm->frame.ux * vx + gm->frame.uy * vy;
            pl[i]->grav = grav;
            pl[i]->on_ground = 0;
            pl[i]->rising = pl[i]->yv * grav > 0;
            pl[i]->dashing = 0;
            pl[i]->trail_n = 0;
        }
    }
}

void game_update(Game *gm, float dt)
{
    if (dt > 0.1f) dt = 0.1f;
    gm->anim_t += dt;
    particles_step(gm, dt);
    // pulses and alpha fades
    for (int i = 0; i < gm->npu;) {
        Pulse *pl = &gm->pu[i];
        pl->t += dt;
        if (pl->t >= pl->in + pl->hold + pl->out) *pl = gm->pu[--gm->npu]; else i++;
    }
    for (int i = 0; i < gm->naf;) {
        AlphaFade *f = &gm->af[i];
        f->t += dt;
        const float k = f->t >= f->dur ? 1.0f : f->t / f->dur;
        gm->galpha[f->group] = f->from + (f->to - f->from) * k;
        if (k >= 1.0f) *f = gm->af[--gm->naf]; else i++;
    }
    // colour channel fades run on real time
    for (int i = 0; i < CH_COUNT; i++) {
        Channel *c = &gm->ch[i];
        if (!c->fading) continue;
        c->t += dt;
        float k = c->t >= c->dur ? 1.0f : c->t / c->dur;
        c->r = c->fr + (c->tr - c->fr) * k;
        c->g = c->fg + (c->tg - c->fg) * k;
        c->b = c->fb + (c->tb - c->fb) * k;
        c->a = c->fa + (c->ta - c->fa) * k;
        if (k >= 1.0f) c->fading = 0;
    }

    if (gm->p.dead) {
        gm->p.dead_t += dt;
        death_fx(gm);
        if (gm->p.dead_t > 1.0f) { // restart attempt
            int att = gm->attempts + 1;
            float best = gm->best_percent;
            if (gm->practice) { const GameCheckpoint *cp = gm->practice; game_checkpoint_restore(gm, cp); gm->dead_fx = 0; gm->npt = 0; gm->p.dead = 0; gm->p.dead_t = 0; gm->p.hold = gm->p.queued = gm->input; }
            else game_reset(gm);
            gm->attempts = att;
            gm->best_percent = best;
        }
        return;
    }
    if (gm->finished) return;

    gm->acc += dt * (gm->timewarp > 0 ? gm->timewarp : 1.0f);
    for (int guard = 0; gm->acc >= STEP && guard < 96; guard++) {
        gm->prev_x = gm->p.x; gm->prev_y = gm->p.y; gm->prev_rot = gm->p.rot;
        step(gm, STEP);
        gm->acc -= STEP;
        if (gm->p.dead || gm->finished) break;
        if (!gm->has_end && gm->p.x > gm->end_x) { gm->finished = 1; break; }
    }
    if (gm->acc > STEP * 96) gm->acc = 0;
    gm->percent = gm->end_x > 0 ? fminf(100.0f, gm->p.x / (gm->end_x - 380) * 100.0f) : 0;
    if (gm->percent > gm->best_percent) gm->best_percent = gm->percent;

    // camera (OpenGD PlayLayer::updateCamera, in OpenGD coordinates: camy = cam_bottom + 90)
    {
        Player *p = &gm->p;
        const float winh = 320.0f;
        float camy = gm->cam_bottom + 90.0f, tgt = camy;
        float py = p->y + 90.0f;
        if (rotated(gm) && gm->frame.fx == 0) {   // vertical gameplay: keep the icon in the middle
            tgt = py - winh * 0.5f;
        } else if (is_restricted(gm, p)) {
            tgt = -winh * 0.5f + gm->ctr;
            if (tgt < 0) tgt = 0;
        } else {
            float unk2 = 90.0f, unk3 = 120.0f;
            if (is_flipped(p)) { unk2 = 120.0f; unk3 = 90.0f; }
            if (py <= winh + camy - unk2) {
                if (py < unk3 + camy) tgt = py - unk3;
            } else {
                tgt = py - winh + unk2;
            }
            if (!is_flipped(p) && fabsf(p->last_ground_y - 15.0f) < 0.01f && py <= tgt + winh - unk2) tgt = 0;
        }
        if (tgt < 0) tgt = 0;
        if (!gm->has_jumps && tgt > 1140.0f - winh) tgt = 1140.0f - winh;
        float k = 1.0f - powf(0.9f, dt * 60.0f);       // iLerp(a, b, 0.1, dt / 60)
        camy += (tgt - camy) * k;
        if (gm->has_jumps) {   // teleports and turns are faster than the glide: the icon stays on screen
            if (py - camy > winh - 50.0f) camy = py - (winh - 50.0f);
            if (py - camy < 50.0f) camy = py - 50.0f;
        }
        gm->cam_bottom = camy - 90.0f;

        // ground line and ceiling (they move to the play area of the current mode)
        float gt = 0, ct = 0;
        int has = 0;
        if (is_restricted(gm, p)) {
            float f, c;
            Player ref = *p; ref.mini = 0;
            region(gm, &ref, &f, &c);
            gt = f - 15.0f; ct = c + 15.0f; has = 1;
        }
        float kg = fminf(1.0f, dt * 12.0f);
        gm->ground_y += (gt - gm->ground_y) * kg;
        if (has) {
            if (!gm->has_ceil) gm->ceil_y = ct + 200.0f;
            gm->ceil_y += (ct - gm->ceil_y) * kg;
        }
        gm->has_ceil = has;

        // mirror: the player slides to the other side of the screen
        float mt = gm->mirror ? 1.0f : 0.0f;
        (void)mt;
    }
}

// ---------------------------------------------------------------- drawing

static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v)
{
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    *v = mx;
    *s = mx > 0 ? d / mx : 0;
    float hh = 0;
    if (d > 0) {
        if (mx == r) hh = fmodf((g - b) / d, 6.0f);
        else if (mx == g) hh = (b - r) / d + 2.0f;
        else hh = (r - g) / d + 4.0f;
        hh *= 60.0f;
        if (hh < 0) hh += 360.0f;
    }
    *h = hh;
}

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    float c = v * s, hp = fmodf(h, 360.0f) / 60.0f;
    if (hp < 0) hp += 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f)), m = v - c;
    float rr = 0, gg = 0, bb = 0;
    if (hp < 1) { rr = c; gg = x; } else if (hp < 2) { rr = x; gg = c; }
    else if (hp < 3) { gg = c; bb = x; } else if (hp < 4) { gg = x; bb = c; }
    else if (hp < 5) { rr = x; bb = c; } else { rr = c; bb = x; }
    *r = rr + m; *g = gg + m; *b = bb + m;
}

static GdColorF chan_color_d(const Game *gm, int id, float *additive, int depth)
{
    if (id < 0 || id >= CH_COUNT) id = CH_OBJ;
    const Channel *c = &gm->ch[id];
    *additive = (float)c->additive;
    GdColorF o = {c->r, c->g, c->b, c->a};
    if (c->lbg) {   // a paler background, fading towards player colour 1 as the background gets dark
        const Channel *bg = &gm->ch[CH_BG], *p1 = &gm->ch[CH_P1];
        float h, sat, val, r, gg, b;
        rgb_to_hsv(bg->r / 255.0f, bg->g / 255.0f, bg->b / 255.0f, &h, &sat, &val);
        hsv_to_rgb(h, fmaxf(sat - 0.2f, 0.0f), val, &r, &gg, &b);
        o.r = r * 255.0f * val + p1->r * (1.0f - val);
        o.g = gg * 255.0f * val + p1->g * (1.0f - val);
        o.b = b * 255.0f * val + p1->b * (1.0f - val);
        return o;
    }
    if (depth < 6 && id != CH_P1 && id != CH_P2) {
        int src = c->player == 1 ? CH_P1 : c->player == 2 ? CH_P2 : c->copy;
        if (src > 0 && src != id) {
            float dummy;
            GdColorF s = chan_color_d(gm, src, &dummy, depth + 1);
            if (c->hsv_on && c->copy > 0 && c->player == 0) {
                float h, sat, val;
                rgb_to_hsv(s.r / 255.0f, s.g / 255.0f, s.b / 255.0f, &h, &sat, &val);
                h += c->hs;
                sat = c->sadd ? sat + c->ss : sat * c->ss;
                val = c->vadd ? val + c->vs : val * c->vs;
                sat = fminf(fmaxf(sat, 0), 1);
                val = fminf(fmaxf(val, 0), 1);
                hsv_to_rgb(h, sat, val, &s.r, &s.g, &s.b);
                s.r *= 255.0f; s.g *= 255.0f; s.b *= 255.0f;
            }
            o.r = s.r; o.g = s.g; o.b = s.b;
            if (c->copy_op) o.a = s.a;
        }
    }
    return o;
}

static float pulse_level(const Pulse *p)
{
    if (p->t < p->in) return p->t / p->in;
    if (p->t < p->in + p->hold) return 1.0f;
    return p->out > 0 ? fmaxf(0.0f, 1.0f - (p->t - p->in - p->hold) / p->out) : 0.0f;
}

// Blends a running pulse over a colour. A pulse either brings its own colour or a hue-shifted
// copy of another channel (or of the colour it flashes).
static void pulse_blend(const Game *gm, const Pulse *p, GdColorF *c)
{
    float r = p->r, g = p->g, b = p->b;
    if (p->copy) {
        if (p->copy > 0) {
            float add;
            const GdColorF s = chan_color_d(gm, p->copy, &add, 1);
            r = s.r; g = s.g; b = s.b;
        } else {
            r = c->r; g = c->g; b = c->b;
        }
        float h, sat, val;
        rgb_to_hsv(r / 255.0f, g / 255.0f, b / 255.0f, &h, &sat, &val);
        hsv_to_rgb(h + p->hue, sat, val, &r, &g, &b);
        r *= 255.0f; g *= 255.0f; b *= 255.0f;
    }
    const float k = pulse_level(p);
    c->r += (r - c->r) * k;
    c->g += (g - c->g) * k;
    c->b += (b - c->b) * k;
}

// the colour shift stored with the object itself
static void obj_hsv(const Game *gm, int idx, GdColorF *c)
{
    if (idx <= 0 || idx > gm->nhsv) return;
    const float *v = gm->hsv[idx - 1];
    float h, s, val, r, g, b;
    rgb_to_hsv(c->r / 255.0f, c->g / 255.0f, c->b / 255.0f, &h, &s, &val);
    s = v[3] > 0 ? s + v[1] : s * v[1];
    val = v[4] > 0 ? val + v[2] : val * v[2];
    hsv_to_rgb(h + v[0], fminf(fmaxf(s, 0.0f), 1.0f), fminf(fmaxf(val, 0.0f), 1.0f), &r, &g, &b);
    c->r = r * 255.0f; c->g = g * 255.0f; c->b = b * 255.0f;
}

// pulses aimed at one of the object's groups; part: 1 = main sprite, 2 = detail sprite
static void group_pulses(const Game *gm, const Obj *o, GdColorF *c, int part)
{
    for (int i = 0; i < gm->npu; i++) {
        const Pulse *p = &gm->pu[i];
        if (!p->group || (p->only && p->only != part)) continue;
        for (int k = 0; k < o->ngrp; k++)
            if (o->grp[k] == p->target) { pulse_blend(gm, p, c); break; }
    }
}

static GdColorF chan_color(const Game *gm, int id, float *additive)
{
    GdColorF c = chan_color_d(gm, id, additive, 0);
    for (int i = 0; i < gm->npu; i++)
        if (!gm->pu[i].group && gm->pu[i].target == id) pulse_blend(gm, &gm->pu[i], &c);
    return c;
}

static void draw_kid(Game *gm, Gfx *g, const Obj *o, const GdChild *k, const GdFrame *f, float cx, float cy, float s,
                     const GdFrame *mainf, float oa)
{
    if (!f) return;
    const float ppu = g->atlas.px_per_unit, scale = s * ppu;
    float pw = mainf ? mainf->srcw / ppu : o->clipw ? (float)o->clipw : 30.0f, ph = mainf ? mainf->srch / ppu : 30.0f;
    float dx = (k->x - pw * 0.5f) * (o->sx < 0 ? -1.0f : 1.0f) * fabsf(o->sx);
    float dy = (k->y - ph * 0.5f) * (o->sy < 0 ? -1.0f : 1.0f) * fabsf(o->sy);
    float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    float wx = dx * c + dy * sn, wy = -dx * sn + dy * c;
    float add;
    int ch = k->kind == 2 ? CH_BLACK : k->kind == 1 ? o->ch_detail : o->ch_main;
    GdColorF col = chan_color(gm, ch, &add);
    if (k->kind != 2) obj_hsv(gm, k->kind == 1 ? o->hsv2 : o->hsv1, &col);
    col.r *= k->r / 255.0f; col.g *= k->g / 255.0f; col.b *= k->b / 255.0f;
    col.a *= oa;
    gfx_frame(g, f, cx + wx * scale, cy - wy * scale, o->rot + k->rot,
              o->sx * k->sx * (k->flipx ? -1.0f : 1.0f) * s, o->sy * k->sy * (k->flipy ? -1.0f : 1.0f) * s, col, (int)add);
}

static void draw_portal_fx(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, float oa);

// Restricts the following sprites to the object's slope triangle (screen px, unit = px per unit).
// The unrotated shape has its right angle at the bottom right.
static void cut_to_slope(Gfx *g, const Obj *o, float cx, float cy, float unit)
{
    const float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    const float hw = o->clipw * 0.5f, hh = 15.0f;
    const float lx[3] = {-hw, hw, hw}, ly[3] = {-hh, hh, -hh};   // both ends of the long side, right angle
    float x[3], y[3];
    for (int i = 0; i < 3; i++) {
        const float px = lx[i] * o->sx, py = ly[i] * o->sy;
        x[i] = cx + (px * c + py * sn) * unit;
        y[i] = cy - (-px * sn + py * c) * unit;
    }
    g->cut_a = -(y[1] - y[0]);
    g->cut_b = x[1] - x[0];
    g->cut_c = -(g->cut_a * x[0] + g->cut_b * y[0]);
    if (g->cut_a * x[2] + g->cut_b * y[2] + g->cut_c < 0) {
        g->cut_a = -g->cut_a; g->cut_b = -g->cut_b; g->cut_c = -g->cut_c;
    }
    g->cut = 1;
}

static void draw_glow(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s)
{
    float add, oa = 1.0f;
    for (int k = 0; k < o->ngrp; k++) oa *= gm->galpha[o->grp[k]];
    GdColorF col = chan_color(gm, o->ch_main, &add);
    if (o->ch_main == CH_BLACK) return;
    col.a *= oa;
    gfx_frame(g, o->glow, cx, cy, o->rot, o->sx * s, o->sy * s, col, 1);
}

static void draw_obj_layer(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, int pass)
{
    float add;
    float oa = 1.0f;
    Obj spun;
    if (o->spin != 0) {   // rotating saw blades
        spun = *o;
        spun.rot += o->spin * gm->anim_t;
        o = &spun;
    }
    for (int k = 0; k < o->ngrp; k++) oa *= gm->galpha[o->grp[k]];
    if (oa <= 0.003f) return;
    GdColorF col = chan_color(gm, o->ch_main, &add);
    if (o->hsv1) obj_hsv(gm, o->hsv1, &col);
    if (gm->npu) group_pulses(gm, o, &col, 1);
    col.a *= oa;
    if (o->type == OT_PORTAL) {
        if (pass == 0 && o->back) gfx_frame(g, o->back, cx, cy, o->rot, o->sx * s, o->sy * s, col, 0);
        if (pass == 1 && o->main) gfx_frame(g, o->main, cx, cy, o->rot, o->sx * s, o->sy * s, col, 0);
        if (pass == 1) draw_portal_fx(gm, g, o, cx, cy, s, oa);
        return;
    }
    if (o->kidset >= 0) {
        const KidSet *ks = &gm->kidsets[o->kidset];
        for (int k = 0; k < ks->n; k++) if (ks->c[k].z < 0) draw_kid(gm, g, o, &ks->c[k], ks->f[k], cx, cy, s, o->main, oa);
        if (o->main) gfx_frame(g, o->main, cx, cy, o->rot, o->sx * s, o->sy * s, col, (int)add);
        for (int k = 0; k < ks->n; k++) {
            if (ks->c[k].z < 0) continue;
            // textured slopes: everything but the outline is cut to the triangle
            if (o->clipw && o->type == OT_DECO && strncmp(ks->c[k].tex, "blockOutline", 12))
                cut_to_slope(g, o, cx, cy, s * g->atlas.px_per_unit);
            draw_kid(gm, g, o, &ks->c[k], ks->f[k], cx, cy, s, o->main, oa);
            g->cut = 0;
        }
        return;
    }
    if (o->main) gfx_frame(g, o->main, cx, cy, o->rot, o->sx * s, o->sy * s, col, (int)add);
    if (o->ball && o->main) {   // the original pulses it with the music; here it breathes gently
        const float ppu = g->atlas.px_per_unit, up = (o->main->srch / ppu * 0.5f + 10.0f) * o->sy * s * ppu;
        const float r = o->rot * 3.14159265f / 180.0f;
        const float size = 0.42f + 0.08f * sinf(gm->anim_t * 7.0f);
        float badd;
        GdColorF bc = chan_color(gm, CH_P1, &badd);
        bc.a = oa;
        gfx_frame(g, o->ball, cx + sinf(r) * up, cy - cosf(r) * up, o->rot, size * s, size * s, bc, 1);
    }
    if (o->main && (o->type == OT_ORB || o->type == OT_PAD)) {   // idle pulse: an expanding, fading copy
        const int orb = o->type == OT_ORB;
        for (int k = 0; k < (orb ? 2 : 1); k++) {
            float ph = fmodf(gm->anim_t * (orb ? 1.25f : 1.0f) + o->x * 0.0137f + k * 0.5f, 1.0f);
            float grow = orb ? 0.8f + 0.4f * ph : 1.0f + 0.25f * ph;
            GdColorF pc = col;
            pc.a *= (orb ? 0.3f : 0.5f) * (1.0f - ph);
            gfx_frame(g, o->main, cx, cy, o->rot, o->sx * s * grow, o->sy * s * (orb ? grow : 1.0f + 0.9f * ph), pc, 1);
        }
    }
    if (o->detail) {
        GdColorF dc = chan_color(gm, o->ch_detail, &add);
        if (o->hsv2) obj_hsv(gm, o->hsv2, &dc);
        if (gm->npu) group_pulses(gm, o, &dc, 2);
        dc.a *= oa;
        gfx_frame(g, o->detail, cx, cy, o->rot, o->sx * s, o->sy * s, dc, (int)add);
    }
}

// Animated portal decoration: swirl sprites that pulse and slide, plus dots drawn into the portal.
static void draw_portal_fx(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, float oa)
{
    float pr, pg, pb;
    portal_rgb(o->sub, &pr, &pg, &pb);
    GdColorF c = {pr, pg, pb, oa};
    const float t = gm->anim_t, ppu = g->atlas.px_per_unit, w2s = s * ppu;
    const float flip = o->sx < 0 ? -1.0f : 1.0f;
    if (o->fx1) {
        float ph = fmodf(t * 0.9f + o->y * 0.01f, 1.0f);
        GdColorF a = c;
        a.a *= sinf(ph * 3.14159265f);
        gfx_frame(g, o->fx1, cx, cy, o->rot, o->sx * s * (1.15f - 0.3f * ph), o->sy * s, a, 1);
    }
    if (o->fx2) {
        float ph = fmodf(t * 0.9f + 0.5f + o->y * 0.01f, 1.0f);
        GdColorF a = c;
        a.a *= sinf(ph * 3.14159265f);
        gfx_frame(g, o->fx2, cx + flip * (ph - 0.5f) * 10.0f * w2s, cy, o->rot, o->sx * s * (0.9f + 0.2f * ph), o->sy * s, a, 1);
    }
    // dots drifting into the portal
    const float hh = o->hh > 8 ? o->hh : 30.0f;
    for (int i = 0; i < 6; i++) {
        float ph = fmodf(t * 0.8f + i / 6.0f + o->x * 0.0071f, 1.0f);
        float ang = i * 2.399f + o->x * 0.37f;
        float r = 1.0f - ph;
        float dx = cosf(ang) * 34.0f * r * flip, dy = sinf(ang) * hh * r;
        float sz = 5.0f * (0.5f + 0.5f * r) * w2s;
        GdColorF d = c;
        d.a *= sinf(ph * 3.14159265f);
        gfx_fill(g, (SDL_FRect){cx + dx * w2s - sz / 2, cy - dy * w2s - sz / 2, sz, sz}, d);
    }
}

static void draw_icon(Game *gm, Gfx *g, const Player *p, float px, float py, float rot, float s,
                      float scale, int swap)
{
    float add;
    GdColorF c1 = chan_color(gm, swap ? CH_P2 : CH_P1, &add), c2 = chan_color(gm, swap ? CH_P1 : CH_P2, &add);
    c1.a = c2.a = 1;
    const float mini = p->mini ? 0.6f : 1.0f;
    const float k = mini * s;
    const float flip = (float)p->grav;
    const GdFrame *cube = gm->fr_cube ? gm->fr_cube : gfx_find(g, "player_01_001.png");
    const GdFrame *cube2 = gm->fr_cube2 ? gm->fr_cube2 : gfx_find(g, "player_01_2_001.png");
    const float up = 5.0f * scale * mini * flip; // the little cube sits 5 units above the vehicle centre
    rot += atan2f((float)gm->frame.ux, (float)gm->frame.uy) * 180.0f / 3.14159265f;   // turned gameplay
    switch (p->mode) {
    case M_SWING: {
        const GdFrame *a1 = gm->fr_swing ? gm->fr_swing : gm->fr_ship, *a2 = gm->fr_swing ? gm->fr_swing2 : gm->fr_ship2;
        if (a2) gfx_frame(g, a2, px, py, rot, k, k * flip, c2, 0);
        if (a1) gfx_frame(g, a1, px, py, rot, k, k * flip, c1, 0);
        break;
    }
    case M_SHIP: {
        const GdFrame *sh = gm->fr_ship ? gm->fr_ship : gfx_find(g, "ship_01_001.png");
        const GdFrame *sh2 = gm->fr_ship2 ? gm->fr_ship2 : gfx_find(g, "ship_01_2_001.png");
        float ry = flip;
        // the cube follows the ship tilt: offset along the ship local "up" direction
        float a = rot * 3.14159265f / 180.0f;
        float ox = sinf(a) * up, oy = -cosf(a) * up;
        if (cube) gfx_frame(g, cube, px + ox, py + oy, rot, k * 0.55f, k * 0.55f * ry, c1, 0);
        if (cube2) gfx_frame(g, cube2, px + ox, py + oy, rot, k * 0.55f, k * 0.55f * ry, c2, 0);
        if (sh2) gfx_frame(g, sh2, px, py, rot, k, k * ry, c2, 0);
        if (sh) gfx_frame(g, sh, px, py, rot, k, k * ry, c1, 0);
        break;
    }
    case M_UFO: {
        float ry = flip;
        const GdFrame *b1 = gm->fr_ufo ? gm->fr_ufo : gfx_find(g, "bird_01_001.png");
        const GdFrame *b2 = gm->fr_ufo2 ? gm->fr_ufo2 : gfx_find(g, "bird_01_2_001.png");
        const GdFrame *b3 = gm->fr_ufo3 ? gm->fr_ufo3 : gfx_find(g, "bird_01_3_001.png");
        float cy = py - 4.0f * scale * mini * flip;
        if (cube) gfx_frame(g, cube, px, cy, 0, k * 0.55f, k * 0.55f * ry, c1, 0);
        if (cube2) gfx_frame(g, cube2, px, cy, 0, k * 0.55f, k * 0.55f * ry, c2, 0);
        if (b3) gfx_frame(g, b3, px, py, 0, k, k * ry, (GdColorF){255, 255, 255, 1}, 0);
        if (b2) gfx_frame(g, b2, px, py, 0, k, k * ry, c2, 0);
        if (b1) gfx_frame(g, b1, px, py, 0, k, k * ry, c1, 0);
        break;
    }
    case M_BALL: {
        const GdFrame *b1 = gm->fr_ball ? gm->fr_ball : gfx_find(g, "player_ball_01_001.png");
        const GdFrame *b2 = gm->fr_ball2 ? gm->fr_ball2 : gfx_find(g, "player_ball_01_2_001.png");
        if (b2) gfx_frame(g, b2, px, py, rot, k, k, c2, 0);
        if (b1) gfx_frame(g, b1, px, py, rot, k, k, c1, 0);
        break;
    }
    case M_WAVE: {
        const GdFrame *d1 = gm->fr_dart ? gm->fr_dart : gfx_find(g, "dart_01_001.png");
        const GdFrame *d2 = gm->fr_dart2 ? gm->fr_dart2 : gfx_find(g, "dart_01_2_001.png");
        if (d2) gfx_frame(g, d2, px, py, rot, k, k, c2, 0);
        if (d1) gfx_frame(g, d1, px, py, rot, k, k, c1, 0);
        break;
    }
    default:
        if (cube2) gfx_frame(g, cube2, px, py, rot, k, k, c2, 0);
        if (cube) gfx_frame(g, cube, px, py, rot, k, k, c1, 0);
        break;
    }
}

// screen mapping helpers for the trail (set by game_draw)
static float g_ox, g_oy, g_vh, g_scale;
static float trail_sx(const Game *gm, float x) { return g_ox + (x - gm->cam_x) * g_scale; }
static float trail_sy(const Game *gm, float y) { return g_oy + (g_vh - (y - gm->cam_bottom)) * g_scale; }

static void draw_trail(Game *gm, Gfx *g, const Player *p, float scale, int swap)
{
    if (p->mode != M_WAVE || p->trail_n < 2) return;
    float add;
    GdColorF c = chan_color(gm, swap ? CH_P2 : CH_P1, &add);
    float w = (p->mini ? 3.0f : 5.0f) * scale;
    for (int i = 1; i < p->trail_n; i++) {
        int a = (p->trail_i - i - 1) & 63, b = (p->trail_i - i) & 63;
        float x0 = trail_sx(gm, p->trail[a][0]), y0 = trail_sy(gm, p->trail[a][1]);
        float x1 = trail_sx(gm, p->trail[b][0]), y1 = trail_sy(gm, p->trail[b][1]);
        float len = hypotf(x1 - x0, y1 - y0);
        int n = (int)(len / (w * 0.5f)) + 1;
        c.a = fmaxf(0.0f, 1.0f - (float)i / (float)p->trail_n);
        for (int j = 0; j < n; j++) {
            float t = (float)j / (float)n;
            gfx_fill(g, (SDL_FRect){x0 + (x1 - x0) * t - w * 0.5f, y0 + (y1 - y0) * t - w * 0.5f, w, w}, c);
        }
    }
}

// Enter / exit transition at the screen edges (enter effect triggers). Returns 0..1 visibility and
// adjusts position (screen px) and scale of a temporary copy of the object.
static float edge_effect(const Game *gm, Obj *o, float vw, float scale, float *cx, float *cy)
{
    if (gm->enter_fx == 1915) return 1.0f;
    const float x = o->x - gm->cam_x;
    float t = fminf(x, vw - x) / 70.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.0f) t = 0.0f;
    const float off = (1.0f - t) * 100.0f * scale;
    switch (gm->enter_fx) {
    case 23: *cy += off; break;
    case 24: *cy -= off; break;
    case 25: *cx -= off; break;
    case 26: *cx += off; break;
    case 27: o->sx *= t; o->sy *= t; break;
    case 28: o->sx *= 1.0f + 0.75f * (1.0f - t); o->sy *= 1.0f + 0.75f * (1.0f - t); break;
    default: break;
    }
    return t;
}

static void draw_obj_fx(Game *gm, Gfx *g, const Obj *o, float vw, float scale, float cx, float cy, float s, int pass)
{
    if (o->type == OT_PORTAL) { draw_obj_layer(gm, g, o, cx, cy, s, pass); return; }
    Obj e = *o;
    g->fade = edge_effect(gm, &e, vw, scale, &cx, &cy);
    draw_obj_layer(gm, g, &e, cx, cy, s, pass);
    g->fade = 1;
}

void game_draw(Game *gm, Gfx *g, float vw, float vh, float scale, float ox, float oy,
               const GfxImage *bg, const GfxImage *ground)
{
    const float ppu = g->atlas.px_per_unit;
    const float s = scale / ppu; // screen px per sheet px
    const float W = vw * scale, H = vh * scale;
    float al = gm->acc / STEP;
    if (al > 1) al = 1;
    if (gm->p.dead || gm->finished) al = 1;
    float ix = gm->prev_x + (gm->p.x - gm->prev_x) * al;
    float iy = gm->prev_y + (gm->p.y - gm->prev_y) * al;
    float irot = gm->prev_rot + (gm->p.rot - gm->prev_rot) * al;
    // mirror portal: the icon slides to the other side of the screen
    const float lead = vw * 0.5f - 75.0f;     // icon position of the original for this aspect ratio
    float target = gm->mirror != (gm->frame.fx < 0) ? vw - lead : lead;
    if (gm->frame.fx == 0) target = vw * 0.5f;
    if (gm->time < 0.02f) gm->mir_x = target;
    gm->mir_x += (target - gm->mir_x) * 0.06f;
    gm->cam_x = ix - (gm->mirror ? vw - gm->mir_x : gm->mir_x);
    g_ox = ox; g_oy = oy; g_vh = vh; g_scale = scale;
    SDL_Rect clip = {(int)ox, (int)oy, (int)W, (int)H};
    gfx_set_clip(g, &clip);
    g->mirror = gm->mirror;
    g->mir_w = ox * 2 + W;

    float add;
    GdColorF bgc = chan_color(gm, CH_BG, &add);
    bgc.a = 1;
    gfx_fill(g, (SDL_FRect){ox, oy, W, H}, bgc);
    if (bg && bg->tex) {
        float tile = vh * 1.709f * scale;         // as in the original: much taller than the view, bottom aligned
        float step = tile;
        float off = fmodf(gm->cam_x * 0.1f * scale, step);
        if (off < 0) off += step;
        for (float x = -off; x < W; x += step) {
            gfx_image_rect(g, bg, (SDL_FRect){0, 0, (float)bg->w, (float)bg->h},
                           (SDL_FRect){ox + x, oy + H - tile, step, tile}, bgc);
        }
    }

    // world -> screen
    #define SX(x) (ox + ((x) - gm->cam_x) * scale)
    #define SY(y) (oy + (vh - ((y) - gm->cam_bottom)) * scale)

    // glow of all visible objects first, so that it only shows around them
    float view_l = gm->cam_x - 120, view_r = gm->cam_x + vw + 120;
    for (int k = 0; k < gm->n; k++) {
        const Obj *o = &gm->obj[gm->by_z[k]];
        if (!o->glow || o->off || o->nodraw || o->x < view_l || o->x > view_r) continue;
        Obj e = *o;
        float ex = SX(o->x), ey = SY(o->y);
        g->fade = edge_effect(gm, &e, vw, scale, &ex, &ey);
        draw_glow(gm, g, &e, ex, ey, s);
        g->fade = 1;
    }

    // pass 0: back layers
    for (int k = 0; k < gm->n; k++) {
        const Obj *o = &gm->obj[gm->by_z[k]];
        if (o->type == OT_TRIGGER || o->off || o->nodraw || o->x < view_l || o->x > view_r) continue;
        if (o->zlayer >= 5 && o->type != OT_PORTAL) continue;
        draw_obj_fx(gm, g, o, vw, scale, SX(o->x), SY(o->y), s, 0);
    }

    // player
    if (!gm->p.dead) {
        draw_trail(gm, g, &gm->p, scale, 0);
        if (gm->dual) draw_trail(gm, g, &gm->p2, scale, 1);
        if (gm->dual) draw_icon(gm, g, &gm->p2, SX(gm->p2.x), SY(gm->p2.y), gm->p2.rot, s, scale, 1);
        draw_icon(gm, g, &gm->p, SX(ix), SY(iy), irot, s, scale, 0);
    } else {
        // simple burst of squares
        float t = gm->p.dead_t;
        GdColorF c = chan_color(gm, CH_P1, &add);
        c.a = fmaxf(0, 1.0f - t * 1.4f);
        for (int i = 0; i < 14; i++) {
            float ang = i * 6.2831853f / 14.0f;
            float dist = 180.0f * t * (0.4f + 0.6f * ((i * 37) % 10) / 10.0f);
            float sz = (8 + (i % 4) * 3) * scale * (1.0f - t * 0.6f);
            gfx_fill(g, (SDL_FRect){SX(gm->p.x) + cosf(ang) * dist * scale * 0.5f - sz / 2,
                                    SY(gm->p.y) + sinf(ang) * dist * scale * 0.5f - sz / 2, sz, sz}, c);
        }
    }

    // particles (additive soft discs)
    {
        const GdFrame *pf = gfx_find(g, "particle_02_001.png"), *pring = gfx_find(g, "particle_07_001.png");
        for (int i = 0; i < gm->npt; i++) {
            const Particle *p = &gm->pt[i];
            float k = p->life / p->max;
            GdColorF pc = {p->r, p->g, p->b, p->a * k};
            float sz = p->size * scale;
            const GdFrame *use = p->grow > 100.0f && pring ? pring : pf;   // fast-growing = shock ring
            if (use) {
                float f = sz / (float)(use->srcw > 0 ? use->srcw : use->w);
                gfx_frame(g, use, SX(p->x), SY(p->y), 0, f, f, pc, 1);
            } else {
                gfx_fill(g, (SDL_FRect){SX(p->x) - sz / 2, SY(p->y) - sz / 2, sz, sz}, pc);
            }
        }
    }

    // pass 1: front layers and portal fronts
    for (int k = 0; k < gm->n; k++) {
        const Obj *o = &gm->obj[gm->by_z[k]];
        if (o->type == OT_TRIGGER || o->off || o->nodraw || o->x < view_l || o->x > view_r) continue;
        if (o->type == OT_PORTAL) draw_obj_layer(gm, g, o, SX(o->x), SY(o->y), s, 1);
        else if (o->zlayer >= 5) draw_obj_fx(gm, g, o, vw, scale, SX(o->x), SY(o->y), s, 0);
    }
    // ground and ceiling cover whatever reaches into them (fixed tile size: it must not stretch
    // with the camera height)
    GdColorF gc = chan_color(gm, CH_GROUND, &add);
    gc.a = 1;
    float tile = (ground && ground->tex) ? ground->w / ppu * scale : 128.0f * scale;
    float gy = SY(gm->ground_y);
    if (gy < oy + H) {
        float off = fmodf(gm->cam_x * scale, tile);
        if (off < 0) off += tile;
        if (ground && ground->tex) {
            for (float y = gy; y < oy + H; y += tile)
                for (float x = -off; x < W; x += tile)
                    gfx_image_rect(g, ground, (SDL_FRect){0, 0, (float)ground->w, (float)ground->h},
                                   (SDL_FRect){ox + x, y, tile, tile}, gc);
        } else {
            gfx_fill(g, (SDL_FRect){ox, gy, W, oy + H - gy}, gc);
        }
        const GdFrame *line = gfx_find(g, "floorLine_01_001.png");
        if (line) {
            GdColorF lc = chan_color(gm, CH_LINE, &add);
            gfx_frame(g, line, ox + W * 0.5f, gy, 0, W / (float)line->w, s, lc, 1);
        }
    }
    if (gm->has_ceil) {
        float cy = SY(gm->ceil_y);
        if (cy > oy) {
            float off = fmodf(gm->cam_x * scale, tile);
            if (off < 0) off += tile;
            if (ground && ground->tex) {
                for (float y = cy; y > oy - tile; y -= tile)
                    for (float x = -off; x < W; x += tile)
                        gfx_image_rect(g, ground, (SDL_FRect){0, (float)ground->h, (float)ground->w, -(float)ground->h},
                                       (SDL_FRect){ox + x, y - tile, tile, tile}, gc);
            } else {
                gfx_fill(g, (SDL_FRect){ox, oy, W, cy - oy}, gc);
            }
            const GdFrame *line = gfx_find(g, "floorLine_01_001.png");
            if (line) {
                GdColorF lc = chan_color(gm, CH_LINE, &add);
                gfx_frame(g, line, ox + W * 0.5f, cy, 0, W / (float)line->w, -s, lc, 1);
            }
        }
    }

    #undef SX
    #undef SY
    g->mirror = 0;
    gfx_set_clip(g, NULL);
}

// ---------------------------------------------------------------- look-ahead support (test bot)

void game_snap_free(GameSnap *s)
{
    free(s->flags);
    free(s->mpos);
    free(s->mflags);
    memset(s, 0, sizeof *s);
}

// the part of the state that is not stored per object
static void core_save(const Game *gm, GameSnap *s)
{
    s->p = gm->p; s->p2 = gm->p2;
    s->dual = gm->dual; s->mirror = gm->mirror; s->input = gm->input;
    s->ctr = gm->ctr; s->cam_f = gm->cam_target; s->acc = gm->acc; s->time = gm->time;
    s->prev_x = gm->prev_x; s->prev_y = gm->prev_y; s->prev_rot = gm->prev_rot;
    s->frame = gm->frame; s->travel = gm->travel; s->timewarp = gm->timewarp; s->finished = gm->finished;
    s->channel = gm->channel;
    s->logic = gm->logic;
    s->free = gm->free;
    s->nmv = gm->nmv;
    memcpy(s->mv, gm->mv, sizeof(Move) * (size_t)gm->nmv);
    s->nsp = gm->nsp;
    memcpy(s->sp, gm->sp, sizeof(PendingSpawn) * (size_t)gm->nsp);
}

static void core_restore(Game *gm, const GameSnap *s)
{
    gm->p = s->p; gm->p2 = s->p2;
    gm->dual = s->dual; gm->mirror = s->mirror; gm->input = s->input;
    gm->ctr = s->ctr; gm->cam_target = s->cam_f; gm->acc = s->acc; gm->time = s->time;
    gm->prev_x = s->prev_x; gm->prev_y = s->prev_y; gm->prev_rot = s->prev_rot;
    gm->frame = gm->next_frame = s->frame; gm->travel = s->travel; gm->timewarp = s->timewarp;
    gm->finished = s->finished;
    gm->channel = s->channel;
    gm->logic = s->logic;
    gm->free = s->free;
    gm->nmv = s->nmv;
    memcpy(gm->mv, s->mv, sizeof(Move) * (size_t)s->nmv);
    gm->nsp = s->nsp;
    memcpy(gm->sp, s->sp, sizeof(PendingSpawn) * (size_t)s->nsp);
}

void game_mark(const Game *gm, GameSnap *s)
{
    core_save(gm, s);
    s->mark = gm->nundo;
}

void game_rewind(Game *gm, const GameSnap *s)
{
    core_restore(gm, s);
    while (gm->nundo > s->mark) {
        const UndoEntry *u = &gm->undo[--gm->nundo];
        Obj *o = &gm->obj[u->obj];
        o->x = u->x; o->y = u->y; o->rot = u->rot;
        o->used = u->used; o->used2 = u->used2; o->tdone = u->tdone; o->off = u->off;
    }
}

void game_snap_save(const Game *gm, GameSnap *s, float x0, float x1)
{
    core_save(gm, s);
    int lo = first_index(gm, x0), hi = first_index(gm, x1);
    s->lo = lo;
    s->n = hi - lo;
    if (s->n > s->cap) {
        free(s->flags);
        s->cap = s->n + 64;
        s->flags = malloc((size_t)s->cap * 4);
    }
    for (int i = 0; i < s->n; i++) {
        const Obj *o = &gm->obj[gm->by_x[lo + i]];
        s->flags[i * 4] = (unsigned char)o->used;
        s->flags[i * 4 + 1] = (unsigned char)o->used2;
        s->flags[i * 4 + 2] = (unsigned char)o->tdone;
        s->flags[i * 4 + 3] = o->off;
    }
    s->nm = gm->nmov;
    if (s->nm > s->mcap) {
        free(s->mpos); free(s->mflags);
        s->mcap = s->nm + 64;
        s->mpos = malloc((size_t)s->mcap * 3 * sizeof(float));
        s->mflags = malloc((size_t)s->mcap * 4);
    }
    for (int i = 0; i < s->nm; i++) {
        const Obj *o = &gm->obj[gm->movers[i]];
        s->mpos[i * 3] = o->x; s->mpos[i * 3 + 1] = o->y; s->mpos[i * 3 + 2] = o->rot;
        s->mflags[i * 4] = (unsigned char)o->used;
        s->mflags[i * 4 + 1] = (unsigned char)o->used2;
        s->mflags[i * 4 + 2] = (unsigned char)o->tdone;
        s->mflags[i * 4 + 3] = o->off;
    }
}

void game_snap_restore(Game *gm, const GameSnap *s)
{
    core_restore(gm, s);
    gm->nundo = 0;
    for (int i = 0; i < s->n; i++) {
        Obj *o = &gm->obj[gm->by_x[s->lo + i]];
        o->used = s->flags[i * 4];
        o->used2 = s->flags[i * 4 + 1];
        o->tdone = s->flags[i * 4 + 2];
        o->off = s->flags[i * 4 + 3];
    }
    for (int i = 0; i < s->nm; i++) {
        Obj *o = &gm->obj[gm->movers[i]];
        o->x = s->mpos[i * 3]; o->y = s->mpos[i * 3 + 1];
        if (o->spin == 0) o->rot = s->mpos[i * 3 + 2];
        o->used = s->mflags[i * 4];
        o->used2 = s->mflags[i * 4 + 1];
        o->tdone = s->mflags[i * 4 + 2];
        o->off = s->mflags[i * 4 + 3];
    }
}

int game_sim(Game *gm, int hold, int frames)
{
    gm->sim = 1;
    game_set_hold(gm, hold);
    int rc = 0;
    for (int f = 0; f < frames && !rc; f++) {
        for (int k = 0; k < 4; k++) {
            step(gm, STEP);
            if (gm->p.dead) { rc = 1; break; }
            if (gm->finished || (!gm->has_end && gm->p.x > gm->end_x)) { rc = 2; break; }
        }
    }
    gm->sim = 0;
    return rc;
}

void game_checkpoint_free(GameCheckpoint *c)
{
    game_snap_free(&c->s);
    free(c->tdone);
    free(c->ch);
    free(c->galpha);
    memset(c, 0, sizeof *c);
}

void game_checkpoint_save(const Game *gm, GameCheckpoint *c)
{
    game_snap_save(gm, &c->s, -1e9f, 1e9f);
    if (!c->tdone) c->tdone = malloc((size_t)gm->n + 1);
    if (!c->ch) c->ch = malloc(sizeof gm->ch);
    if (!c->galpha) c->galpha = malloc(sizeof gm->galpha);
    memcpy(c->galpha, gm->galpha, sizeof gm->galpha);
    for (int i = 0; i < gm->n; i++) c->tdone[i] = (unsigned char)gm->obj[i].tdone;
    memcpy(c->ch, gm->ch, sizeof gm->ch);
    c->cam_bottom = gm->cam_bottom; c->ground_y = gm->ground_y; c->ceil_y = gm->ceil_y;
    c->mir_x = gm->mir_x; c->cam_x = gm->cam_x; c->percent = gm->percent; c->has_ceil = gm->has_ceil;
    c->valid = 1;
}

void game_checkpoint_restore(Game *gm, const GameCheckpoint *c)
{
    game_snap_restore(gm, &c->s);
    for (int i = 0; i < gm->n; i++) gm->obj[i].tdone = c->tdone[i];
    memcpy(gm->ch, c->ch, sizeof gm->ch);
    memcpy(gm->galpha, c->galpha, sizeof gm->galpha);
    gm->cam_bottom = c->cam_bottom; gm->ground_y = c->ground_y; gm->ceil_y = c->ceil_y;
    gm->mir_x = c->mir_x; gm->cam_x = c->cam_x; gm->percent = c->percent; gm->has_ceil = c->has_ceil;
    gm->finished = 0;
}

// Writes (save != 0) or reads the complete changing state of a loaded level, so that the test bot
// can be restarted just before a difficult place. Only valid for the same build and level.
int game_state_io(Game *gm, const char *path, int save)
{
    FILE *f = fopen(path, save ? "wb" : "rb");
    if (!f) return -1;
    int ok = 1;
#define IO(v) (ok = ok && (save ? fwrite(&(v), sizeof(v), 1, f) : fread(&(v), sizeof(v), 1, f)) == 1)
    int n = gm->n;
    IO(n);
    ok = ok && n == gm->n;
    IO(gm->p); IO(gm->p2); IO(gm->dual); IO(gm->mirror); IO(gm->input); IO(gm->ctr); IO(gm->cam_target);
    IO(gm->acc); IO(gm->time); IO(gm->prev_x); IO(gm->prev_y); IO(gm->prev_rot);
    IO(gm->cam_bottom); IO(gm->ground_y); IO(gm->ceil_y); IO(gm->mir_x); IO(gm->cam_x); IO(gm->percent);
    IO(gm->has_ceil); IO(gm->enter_fx); IO(gm->ch); IO(gm->galpha); IO(gm->mv); IO(gm->nmv); IO(gm->sp);
    IO(gm->nsp); IO(gm->pu); IO(gm->npu); IO(gm->af); IO(gm->naf);
    IO(gm->frame); IO(gm->next_frame); IO(gm->travel); IO(gm->timewarp); IO(gm->channel); IO(gm->free); IO(gm->logic);
    for (int i = 0; ok && i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        IO(o->x); IO(o->y); IO(o->rot); IO(o->used); IO(o->used2); IO(o->tdone); IO(o->off);
    }
#undef IO
    fclose(f);
    return ok ? 0 : -1;
}
