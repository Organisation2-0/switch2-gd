// SPDX-License-Identifier: GPL-3.0-or-later
#include "game_int.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "objects.h"
#include "objchan.h"
#include "objz.h"
#include "phystab.h"


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
    set_channel(gm, CH_GROUND2, 0, 102, 255, 0);
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
                // light BG is always derived and black / white are fixed, whatever the header lists
                if (id < 0 || id == 1007 || id == CH_BLACK || id == CH_WHITE) continue;
                set_channel(gm, id, kv_get(keys, vals, n, 1, 255), kv_get(keys, vals, n, 2, 255),
                            kv_get(keys, vals, n, 3, 255), (int)kv_get(keys, vals, n, 5, 0));
                gm->ch[id].a = kv_get(keys, vals, n, 7, 1);
                gm->ch[id].player = (int)kv_get(keys, vals, n, 4, -1);
                if (gm->ch[id].player < 1 || gm->ch[id].player > 2) gm->ch[id].player = 0;
                gm->ch[id].copy = (int)kv_get(keys, vals, n, 9, 0);
                gm->ch[id].copy_op = (int)kv_get(keys, vals, n, 17, 0);
                if (gm->ch[id].copy > 0) {
                    const char *h = strstr(e, "_10_");
                    if (h) parse_hsv(&gm->ch[id], h + 4);
                }
                if (gm->ch[id].a <= 0 && n > 0 && id != CH_LINE) gm->ch[id].a = 1;
            }
            free(l2);
        } else if (key[0] == 'k' && key[1] == 'S' && strlen(key) == 4) {
            int ks = atoi(key + 2);
            static const int old_map[] = {29, CH_BG, 30, CH_GROUND, 31, CH_LINE, 32, CH_OBJ, 33, 1,
                                          34, 2, 35, 3, 36, 4, 37, CH_3DL};
            for (unsigned i = 0; i < sizeof old_map / sizeof old_map[0]; i += 2)
                if (ks == old_map[i]) {
                    parse_kv_underscore(val, keys, vals, 24, &n);
                    set_channel(gm, old_map[i + 1], kv_get(keys, vals, n, 1, 255),
                                kv_get(keys, vals, n, 2, 255), kv_get(keys, vals, n, 3, 255),
                                (int)kv_get(keys, vals, n, 5, 0));
                    const int player = (int)kv_get(keys, vals, n, 4, 0);
                    gm->ch[old_map[i + 1]].player = player == 1 || player == 2 ? player : 0;
                }
        } else if (!strcmp(key, "kA6")) {
            gm->bg_id = atoi(val) > 0 ? atoi(val) : 1;
        } else if (!strcmp(key, "kA25")) {
            gm->mg_id0 = atoi(val);
        } else if (!strcmp(key, "kA7")) {
            gm->ground_id = atoi(val) > 0 ? atoi(val) : 1;
        } else if (!strcmp(key, "kA4")) {
            gm->start_speed = atoi(val);
        } else if (!strcmp(key, "kA2")) {
            gm->start_mode = atoi(val);
        } else if (!strcmp(key, "kA3")) {
            gm->start_mini = atoi(val) != 0;
        } else if (!strcmp(key, "kA8")) {
            gm->start_dual = atoi(val) != 0;
        } else if (!strcmp(key, "kA11")) {
            gm->start_flip = atoi(val) != 0;
        } else if (!strcmp(key, "kA28")) {
            gm->start_mirror = atoi(val) != 0;
        } else if (!strcmp(key, "kA36")) {
            gm->start_group = atoi(val);
        } else if (!strcmp(key, "kA39")) {
            gm->fix_radius = atoi(val) != 0;
        } else if (!strcmp(key, "kA13")) {
            gm->song_offset = (float)atof(val);
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
    case 900: return CH_GROUND2;
    case 915: return CH_LINE;
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

// Objects the original keeps turning (saw blades and the round decorations), degrees per second.
// One speed for all: blades and the wheels drawn over them have to stay in step.
static float spin_speed(const char *fr)
{
    static const char *const k[] = {
        "sawblade_", "spinBlade", "blade_", "darkblade_", "lightBlade_", "blackCogwheel_", "bladeTrap", "d_cogwheel_",
        "d_wheel_", "d_spikewheel_", "d_cartwheel_", "d_roundCloud_", "d_rotatingBall_", "d_geometric_", "d_ringSeg_",
        "d_spiral_", "d_rotatingLine_", "d_rotatingSquare_", "d_ringSpiral_"};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (starts(fr, k[i])) return -360.0f;
    return 0.0f;
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
        // a colour trigger without a channel (key 23) means channel 1
        o->ttarget = c == -2 ? (int)gd_obj_prop(src, 23, 1) : c;
        return;
    }
    if (!fr || starts(fr, "edit_") || starts(fr, "GJ_")) return;
    char variant[32];
    if (src->id == 9) {   // the original picks one of three looks for every spike pit
        const unsigned h = ((unsigned)(int)src->x * 2654435761u) ^ ((unsigned)(int)src->y * 40503u) ^ (unsigned)gm->ball_type;
        snprintf(variant, sizeof variant, "pit_%02u_001.png", 1 + (h >> 11) % 3);
        if (gfx_find(g, variant)) fr = variant;
    }
    o->main = gfx_find(g, fr);
    if (!o->main) return;
    o->fill = starts(fr, "square_c_") || starts(fr, "square_f_");
    o->faint = starts(fr, "d_pixelArt01_") != 0;
    const float ppu = g->atlas.px_per_unit;
    float w = o->main->srcw / ppu, h = o->main->srch / ppu;
    // (for the pits 06 / 07 the "_2" sprite is an object of its own, not a second layer)
    o->detail = starts(fr, "pit_0") ? NULL : sibling(g, fr, "_001.png", "_2_001.png");
    if (!o->detail) o->detail = sibling(g, fr, "_001.png", "_color_001.png");
    o->extra = sibling(g, fr, "_001.png", "_extra_001.png");
    o->spin = spin_speed(fr) * (o->sx * o->sy < 0 ? -1.0f : 1.0f);
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
        /* invisible blocks are drawn, faded by their distance to the icon (invisible_alpha) */
    }
}

// Type and hitbox from phystab.c (read out of the game). Objects the table does not know about do
// not collide.
static void apply_phys(Obj *o, int id)
{
    if (o->type == OT_TRIGGER) return;
    const GdPhys *ph = gd_phys_find(id);
    ObjType ot = OT_DECO;
    int sub = 0;
    switch (ph ? ph->type : -1) {
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
    case 40: ot = OT_PORTAL; sub = P_SWING; break;
    case 41: ot = OT_PORTAL; sub = P_GRAV_TOGGLE; break;
    case 42: ot = OT_PORTAL; sub = P_TELEPORT; break;
    case 8: ot = OT_PAD; sub = K_YELLOW; break;
    case 9: ot = OT_PAD; sub = K_PINK; break;
    case 10: ot = OT_PAD; sub = K_BLUE; break;
    case 34: ot = OT_PAD; sub = K_RED; break;
    case 43: ot = OT_PAD; sub = K_SPIDER; break;
    case 11: ot = OT_ORB; sub = K_YELLOW; break;
    case 12: ot = OT_ORB; sub = K_PINK; break;
    case 13: ot = OT_ORB; sub = K_BLUE; break;
    case 29: ot = OT_ORB; sub = K_GREEN; break;
    case 35: ot = OT_ORB; sub = K_RED; break;
    case 44: ot = OT_ORB; sub = K_BLACK; break;
    case 45: ot = OT_ORB; sub = K_DASH; break;
    case 46: ot = OT_ORB; sub = K_DASH_FLIP; break;
    case 47: ot = OT_ORB; sub = K_SPIDER; break;
    case 48: ot = OT_ORB; sub = K_TELEPORT; break;
    case 49: ot = OT_ORB; sub = K_TOGGLE; break;
    case 20:
        ot = OT_SPEED;
        sub = id == 200 ? 0 : id == 201 ? 1 : id == 202 ? 2 : id == 203 ? 3 : 4;
        break;
    default: break;
    }
    o->type = ot;
    o->sub = sub;
    o->rad = o->tilt = 0;
    o->hw = o->hh = o->hx = o->hy = o->bw = o->bh = 0;
    if (id == 1859 || id == 2866 || id == 1755 || id == 1829) {   // H, F, D and S block
        o->type = OT_HBLOCK;
        o->sub = id == 2866 ? SB_FLIP : id == 1755 ? SB_DART : id == 1829 ? SB_STOPDASH : SB_HEAD;
        o->main = o->detail = o->glow = NULL;
        o->hw = 15.0f * fabsf(o->sx);
        o->hh = 15.0f * fabsf(o->sy);
        return;
    }
    if (id == 2069 || id == 3645) {   // force block (square) / force circle
        o->type = OT_FORCE;
        o->hw = 15.0f * fabsf(o->sx);
        o->hh = 15.0f * fabsf(o->sy);
        return;
    }
    if (!ph || ot == OT_DECO) return;
    if (ot == OT_PORTAL && sub == P_TELEPORT) {
        // Narrower than the table says: with the full width the icon would leave levels like
        // Deadlocked right inside the spikes behind the exit.
        o->hw = 6.0f;
        o->hh = 45.0f;
        return;
    }

    // Blocks and slopes only know quarter turns: any other angle counts as none (and a block turned
    // by 450 degrees keeps its upright box).
    float rot = o->rot;
    if (ot == OT_SOLID) {
        const float q = rot / 90.0f;
        if (fabsf(q - roundf(q)) > 0.001f || (!o->slope && fabsf(rot) > 270.5f && ((int)roundf(q) & 1))) rot = 0;
    }
    // Hitbox rectangle relative to the object centre, then flip -> rotate (clockwise) -> bounds.
    float rx[2] = {ph->x, ph->x + ph->w}, ry[2] = {ph->y, ph->y + ph->h};
    float r = rot * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
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
    o->bw = ph->w * fabsf(o->sx) * 0.5f;
    o->bh = ph->h * fabsf(o->sy) * 0.5f;
    if (fabsf(rot / 90.0f - roundf(rot / 90.0f)) > 0.001f) o->tilt = -r;   // the box itself is turned
    if (ot == OT_HAZARD && ph->radius > 0) {   // round, centred on the object
        o->rad = ph->radius * fmaxf(fabsf(o->sx), fabsf(o->sy));
        o->hx = o->hy = o->tilt = 0;
        o->hw = o->hh = o->rad;
    }
    if (o->slope) {
        // the unrotated shape has its right angle bottom right; find where flip + rotation put it
        const float px = (ph->x + ph->w) * o->sx, py = ph->y * o->sy;
        const float wx = px * c + py * s, wy = -px * s + py * c;
        o->slope = (unsigned char)(1 | (wx > o->hx ? 2 : 0) | (wy > o->hy ? 4 : 0));
    }
}

static int add_effect(Game *gm, Gfx *g, int id);

// Commands the engine acts on (colour triggers are known by their channel).
static int is_command(int id)
{
    static const short ids[] = {
        22, 23, 24, 25, 26, 27, 28, 32, 33, 55, 56, 57, 58, 59, 901, 1006, 1007, 1049, 1268, 1346, 1347, 1520, 1585,
        1595, 1611, 1612, 1613, 1616, 1811, 1812, 1814, 1815, 1817, 1912, 1913, 1914, 1915, 1916, 1917, 1932, 1934,
        1935, 2015, 2062, 2066, 2067, 2068, 2899, 2900, 2901, 2903, 2904, 2905, 2907, 2909, 2910, 2911, 2912, 2913,
        2914, 2915, 2916, 2917, 2919, 2920, 2921, 2922, 2923, 2924, 2925, 2999, 3006, 3007, 3008, 3009, 3010, 3011,
        3012, 3013, 3014, 3015, 3016, 3017, 3018, 3019, 3020, 3021, 3022, 3023, 3024, 3029, 3030, 3031, 3033, 3600,
        3602, 3603, 3604, 3605, 3606, 3607, 3608, 3609, 3612, 3613, 3614, 3615, 3617, 3618, 3619, 3620, 3641, 3660,
        3661};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++)
        if (ids[i] == id) return 1;
    return 0;
}

static TData *keep_properties(const GdObject *src)
{
    TData *d = malloc(sizeof *d + (size_t)src->nprops * sizeof(GdProp));
    if (!d) return NULL;
    d->nprops = src->nprops;
    memcpy(d->prop, src->props, (size_t)src->nprops * sizeof(GdProp));
    d->text = src->text ? strdup(src->text) : NULL;
    return d;
}

static void classify(Game *gm, Gfx *g, Obj *o, const GdObject *src)
{
    classify_visual(gm, g, o, src);
    const int id = src->id;
    if (o->type != OT_TRIGGER && is_command(id)) {
        o->type = OT_TRIGGER;
        o->main = o->detail = o->back = o->extra = NULL;
    }
    if (o->type == OT_TRIGGER) {
        o->tid = id;
        o->tgroup = (int)gd_obj_prop(src, 51, 0);
        o->tdx = gd_obj_prop(src, 28, 0);
        o->tdy = gd_obj_prop(src, 29, 0);
        if (trigger_channel(id) == -1) o->tdur = fmaxf(gd_obj_prop(src, 10, 0), 0);   // colour ones have it
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
        if (id == 1520) o->tdur = gd_obj_prop(src, 10, 0.5f);
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
        if (id == 1815 || id == 3609) {   // collision: two block ids, or the icon and one block id
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
        snprintf(bn, sizeof bn, "rod_ball_%02d_001.png", gm->ball_type);
        o->ball = gfx_find(g, bn);
    }
    {
        const char *name = gd_object_frame(id);
        o->invisible = name && !strncmp(name, "invis_", 6);
    }
    if (o->type != OT_TRIGGER && (o->effect = (unsigned char)add_effect(gm, g, id))) {
        o->fx_trigger = gd_obj_prop(src, 123, 0) != 0;   // "animate on trigger"
        o->fx_speed = gd_obj_prop(src, 107, 1.0f);
        if (o->fx_speed <= 0.01f) o->fx_speed = 1.0f;
    }
    // key 96: the object was placed with its glow switched off
    if (o->main && o->type != OT_TRIGGER && o->type != OT_ORB && o->type != OT_PAD && gd_obj_prop(src, 96, 0) == 0)
        o->glow = sibling(g, gd_object_frame(id), "_001.png", "_glow_001.png");
    int hidden_no_sprite = o->type == OT_HIDDEN && !o->main;
    apply_phys(o, src->id);
    if (id == 142 || id == 1329) {   // secret and user coins: picked up by touching them
        o->type = OT_COIN;
        o->sub = id == 142 ? (int)gd_obj_prop(src, 12, 0) : 0;   // official levels number them
        o->hw = 20.0f * fabsf(o->sx);
        o->hh = 20.0f * fabsf(o->sy);
        o->hx = o->hy = o->rad = o->tilt = o->spin = 0;
        o->detail = o->extra = o->glow = NULL;
        if (gd_obj_prop(src, 21, 0) == 0) o->ch_main = o->ch_detail = CH_WHITE;
    }
    {
        const char *name = gd_object_frame(id);
        o->beat = o->type == OT_ORB ? 2 : name && !strncmp(name, "d_ball_", 7) ? 1 : 0;
    }
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
                 : id == 1912 ? gd_obj_prop(src, 10, 50.0f)        // 1912: random chance in percent
                 : id == 1520 ? gd_obj_prop(src, 75, 1.0f) : 0;    // 1520: shake strength
        if (id == 1520) o->trate = gd_obj_prop(src, 84, 0);        // ... and seconds between jolts
        if (id == 1913) o->value = fminf(fmaxf(gd_obj_prop(src, 371, 1.0f), 0.2f), 5.0f);   // zoom factor
        if (id == 1914) o->ttarget = (int)gd_obj_prop(src, 101, 0);       // 1: x only, 2: y only
        if (id == 1916) o->ttarget = (int)gd_obj_prop(src, 101, 0);
        if (id == 2062) o->ttarget = (int)gd_obj_prop(src, 164, 0);       // 1 left, 2 right, 3 up, 4 down
        if (id == 2067) {   // scale: factors (keys 153 / 154: divide by them instead)
            const float kx = gd_obj_prop(src, 150, 1.0f), ky = gd_obj_prop(src, 151, 1.0f);
            o->tdx = gd_obj_prop(src, 153, 0) != 0 && kx != 0 ? 1.0f / kx : kx;
            o->tdy = gd_obj_prop(src, 154, 0) != 0 && ky != 0 ? 1.0f / ky : ky;
        }
        if (id == 1814) {   // follow the player's height
            o->value = gd_obj_prop(src, 90, 1.0f);    // speed
            o->tdy = gd_obj_prop(src, 92, 0);         // offset
            o->fymod = gd_obj_prop(src, 105, 0);      // fastest step per frame, 0 = no limit
        }
        if (id == 3029 || id == 3030) o->ttarget = (int)gd_obj_prop(src, 533, 0);
    }
    if (id == 3610 || id == 3611) {   // 2.2 damage square / circle: hitboxes without a picture
        o->main = o->detail = o->glow = NULL;
        o->kidset = -1;
    }
    // keep objects without a sprite only if they collide (invisible blocks, hitbox-only objects)
    if (hidden_no_sprite && o->type == OT_DECO) o->type = OT_HIDDEN;
    if (id == 1816) {   // collision block: invisible, watched by collision triggers (and moved by move triggers)
        o->type = OT_SENSOR;
        o->main = o->glow = NULL;
        o->value = gd_obj_prop(src, 80, 0);
        o->sub = SN_BLOCK;
    }
    if (id == 3640 || id == 3643 || id == 1813) {   // blocks that act when the icon touches them
        o->type = OT_SENSOR;
        o->sub = id == 3640 ? SN_STATE : id == 3643 ? SN_TOGGLE : SN_NOBUFFER;
        o->main = o->detail = o->glow = NULL;
        o->kidset = -1;
    }
    if (o->type == OT_TRIGGER || o->type == OT_SENSOR || id == 3032 || id == 2065) o->data = keep_properties(src);
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
    static const short r[][2] = {{1520,1520},{1585,1585},{1595,1595},{1611,1620},{1755,1755},{1811,1819},{1829,1829},{1859,1859},{1912,1917},
        {1931,1935},{2015,2016},{2062,2068},{2866,2866},{2899,2905},{2907,2907},{2909,2925},{2999,2999},{3006,3033},{3092,3092},{3600,3600},
        {3602,3620},{3640,3643},{3660,3662}};
    for (size_t i = 0; i < sizeof r / sizeof r[0]; i++)
        if (id >= r[i][0] && id <= r[i][1]) return 1;
    return 0;
}

// The frames of a 2.2 effect object ("gj22_anim_NN"): its number + 1, 0 if it is none.
static int add_effect(Game *gm, Gfx *g, int id)
{
    const char *fr = gd_object_frame(id);
    if (!fr || strncmp(fr, "gj22_anim_", 10)) return 0;
    for (int i = 0; i < gm->neffects; i++) if (gm->effect[i].id == id) return i + 1;
    const int max = (int)(sizeof gm->effect / sizeof gm->effect[0]);
    const char *end = strstr(fr, "_001.png");
    if (!end || gm->neffects >= max) return 0;
    Effect *e = &gm->effect[gm->neffects];
    int n = 0;
    for (; n < EFFECT_FRAMES; n++) {
        char name[96];
        snprintf(name, sizeof name, "%.*s_%03d.png", (int)(end - fr), fr, n + 1);
        if (!(e->main[n] = gfx_find(g, name))) break;
        snprintf(name, sizeof name, "%.*s_color_%03d.png", (int)(end - fr), fr, n + 1);
        e->color[n] = gfx_find(g, name);
    }
    if (n < 2) return 0;
    e->id = id;
    e->n = n;
    return ++gm->neffects;
}

// Collects the numbered frames of a looping animation object (fires).
static void add_loop(Game *gm, Gfx *g, int id)
{
    for (int i = 0; i < gm->nloops; i++) if (gm->loops[i].id == id) return;
    if (gm->nloops >= (int)(sizeof gm->loops / sizeof gm->loops[0])) return;
    const char *fr = gd_object_frame(id), *at = strstr(fr, "_looped_");
    int cnt;
    const GdChild *ch = gd_children_find(id, &cnt);
    const char *kid = cnt > 0 ? ch[0].tex : NULL, *kat = kid ? strstr(kid, "_looped_") : NULL;
    const int i = gm->nloops;
    int n = 0;
    for (; n < 16; n++) {
        char name[96];
        snprintf(name, sizeof name, "%.*s_looped_%03d.png", (int)(at - fr), fr, n + 1);
        if (!(gm->loops[i].main[n] = gfx_find(g, name))) break;
        gm->loops[i].kid[n] = NULL;
        if (kat) {
            snprintf(name, sizeof name, "%.*s_looped_%03d.png", (int)(kat - kid), kid, n + 1);
            gm->loops[i].kid[n] = gfx_find(g, name);
        }
    }
    if (n < 2) return;
    gm->loops[i].id = id;
    gm->loops[i].n = n;
    gm->nloops++;
}

// The orange exit of a blue teleport portal is not stored in the level: it stands above or below
// the entrance (key 54). It is added as an object of its own, for the picture only.
static void add_teleport_exits(GdLevel *lvl)
{
    size_t n = 0;
    for (size_t i = 0; i < lvl->count; i++) n += lvl->objects[i].id == 747;
    if (!n) return;
    GdObject *all = realloc(lvl->objects, (lvl->count + n) * sizeof *all);
    if (!all) return;
    lvl->objects = all;
    const size_t stored = lvl->count;
    for (size_t i = 0; i < stored; i++) {
        if (all[i].id != 747) continue;
        GdObject *e = &all[lvl->count++];
        *e = all[i];
        e->text = NULL;
        e->id = 749;
        e->y += gd_obj_prop(&all[i], 54, 0);
        e->flipx = !e->flipx;   // it opens the other way
    }
}

// A gradient trigger: two colour channels spread over the area between the objects of up to four
// groups; sides without a group reach to the screen edge.
static void add_gradient(Game *gm, int obj, const GdObject *s)
{
    if (!(gm->ngradients & 31)) {
        void *p = realloc(gm->gradients, (size_t)(gm->ngradients + 32) * sizeof *gm->gradients);
        if (!p) return;
        gm->gradients = p;
    }
    Gradient *d = &gm->gradients[gm->ngradients++];
    memset(d, 0, sizeof *d);
    d->obj = obj;
    d->id = (int)gd_obj_prop(s, 209, 0);
    if (d->id < 0 || d->id >= GD_MAX_GRADIENTS) d->id = 0;
    d->layer = (int)gd_obj_prop(s, 202, 1);
    d->blend = (int)gd_obj_prop(s, 174, 0);
    d->ch1 = (int)gd_obj_prop(s, 21, 0);
    d->ch2 = (int)gd_obj_prop(s, 22, 0);
    d->off = gd_obj_prop(s, 208, 0) != 0 || (d->ch1 <= 0 && d->ch2 <= 0);
    d->vertex = gd_obj_prop(s, 207, 0) != 0;
    for (int i = 0; i < 4; i++) d->side[i] = (int)gd_obj_prop(s, 203 + i, 0);
    d->rot = s->rot;
}

int game_load(Game *gm, Gfx *g, const char *path, const char *data)
{
    memset(gm, 0, sizeof *gm);
    GdLevel lvl;
    if (data ? gd_level_from_data(data, path, &lvl) : gd_level_load(path, &lvl)) return -1;
    snprintf(gm->name, sizeof gm->name, "%s", lvl.name);
    gm->path = path;
    // the original picks one look for all rod balls of a level at random
    gm->ball_type = getenv("GD_HIDDEN") ? 2 : 1 + (int)(SDL_GetPerformanceCounter() % 3);
    parse_header(gm, lvl.header);
    gm->bg_id0 = gm->bg_id;
    gm->ground_id0 = gm->ground_id;
    gm->pulse = gm->loud = -1.0f;
    memcpy(gm->ch0, gm->ch, sizeof gm->ch);
    gm->ch0_valid = 1;
    add_teleport_exits(&lvl);

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
        if (s->has_copy_hsv) o->copy_hsv = (short)add_hsv(gm, s->copy_hsv);
        {   // draw order: stored in the level only when it differs from the object default
            const GdObjZ *oz = gd_objz_find(s->id);
            o->zlayer = (int)gd_obj_prop(s, 24, 0);
            o->zorder = (int)gd_obj_prop(s, 25, 0);
            if (o->zlayer == 0) o->zlayer = oz && oz->layer ? oz->layer : 5;
            if (o->zorder == 0 && oz) o->zorder = oz->order;
            // the monsters lie in front of the waves of their layer
            if (gd_obj_prop(s, 25, 0) == 0 && (s->id == 918 || s->id == 1327 || s->id == 1328 || s->id == 1584 || s->id == 2012))
                o->zorder = 20;
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
        {   // "black" objects (pits, fill squares, saws) stay black when a level names the object colour
            const GdObjChan *oc = gd_objchan_find(s->id);
            if (oc && oc->kind == 2 && o->ch_main == CH_OBJ) o->ch_main = CH_BLACK;
        }
        for (int k = 0; k < s->ngroups && k < GD_MAX_GROUPS; k++)
            if (s->groups[k] > 0 && s->groups[k] < GD_GROUPS) o->grp[o->ngrp++] = (short)s->groups[k];
        o->kidset = -1;
        o->kx = o->ky = 1;
        classify(gm, g, o, s);
        // "no touch" (key 121) makes any object mere decoration
        o->notouch = o->type != OT_TRIGGER && gd_obj_prop(s, 121, 0) != 0;
        if (o->type == OT_TRIGGER && s->id == 2903) add_gradient(gm, gm->n, s);
        if (o->type == OT_TRIGGER && s->id == 3031) {
            const int mg = (int)gd_obj_prop(s, 533, 0);
            int k = 0;
            while (k < gm->nmg_used && gm->mg_used[k] != mg) k++;
            if (mg > 0 && k == gm->nmg_used && k < 3) gm->mg_used[gm->nmg_used++] = mg;
        }
        if (o->type == OT_TRIGGER && o->ttarget > 0 && (s->id == 3029 || s->id == 3030)) {
            int *list = s->id == 3029 ? gm->bg_used : gm->ground_used, *n = s->id == 3029 ? &gm->nbg_used : &gm->nground_used;
            int k = 0;
            while (k < *n && list[k] != o->ttarget) k++;
            if (k == *n && *n < 8) list[(*n)++] = o->ttarget;
        }
        if (s->id == 749 || s->id == 2064) {   // teleport exit: looks like a portal, does nothing
            o->type = OT_PORTAL;
            o->sub = P_OTHER;
            o->hw = o->hh = 0;
        }
        if (o->type == OT_SPEED && gd_obj_prop(s, 24, 0) == 0) o->zlayer = 5;
        // portals, pads and orbs carry their colours in the sprites: no object colour, no blending
        if (o->type == OT_PORTAL || o->type == OT_PAD || o->type == OT_ORB || o->type == OT_SPEED) {
            if (gd_obj_prop(s, 21, 0) == 0) o->ch_main = CH_WHITE;
            if (gd_obj_prop(s, 22, 0) == 0) o->ch_detail = CH_WHITE;
        }
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
                // art are the children; textured ones are cut to the triangle
                // diagonal decorations: the child is the sprite turned into place, the main one is not shown
                for (int k = 0; k < cnt; k++)
                    if (o->main && gm->kidsets[o->kidset].f[k] == o->main && fmodf(fabsf(ch[k].rot), 90.0f) > 1.0f) {
                        o->main = o->glow = NULL;
                        if (o->type == OT_HIDDEN) o->type = OT_DECO;
                    }
                const char *fr = gd_object_frame(s->id);
                if (fr && (!strcmp(fr, "blockOutline_14_001.png") || !strcmp(fr, "blockOutline_15_001.png"))) {
                    o->clipw = fr[14] == '5' ? 60 : 30;
                    const GdObjChan *oc = gd_objchan_find(s->id);
                    // detail art keeps its own default colour when the level only recolours the base
                    if (oc && oc->primary > 0 && oc->primary < 1000 && gd_obj_prop(s, 22, 0) == 0)
                        o->ch_detail = oc->primary;
                    if (o->type == OT_DECO) o->main = NULL;
                    o->glow = NULL;
                    if (o->type == OT_HIDDEN) o->type = OT_DECO;
                }
            }
        }
        o->enter_ch = (unsigned char)gd_obj_prop(s, 343, 0);
        o->no_fade = gd_obj_prop(s, 64, 0) != 0;
        o->no_enter = gd_obj_prop(s, 67, 0) != 0;
        // the black sludge is not in the colour table
        if (s->id == 919 && gd_obj_prop(s, 21, 0) == 0) o->ch_main = o->ch_detail = CH_BLACK;
        {
            const GdObjChan *oc = gd_objchan_find(s->id);
            o->two_colour = oc && oc->secondary > 0;
            o->lighter = oc && oc->secondary == CH_LIGHTER;
        }
        {   // the original never fades blocks with an opaque black filling
            const char *fr = gd_object_frame(s->id);
            int cnt = 0;
            const GdChild *ch = s->id >= 0 && s->id < 5000 ? gd_children_find(s->id, &cnt) : NULL;
            if (fr && !strcmp(fr, "block005b_05_001.png")) o->solid_fill = 1;
            for (int k = 0; k < cnt; k++) if (!strcmp(ch[k].tex, "block005b_05_001.png")) o->solid_fill = 1;
            if (s->id >= 91 && s->id <= 96) o->solid_fill = 1;
        }
        if (o->main && gd_object_frame(s->id) && strstr(gd_object_frame(s->id), "_looped_")) add_loop(gm, g, s->id);
        // invisible objects stay if they are in a group: triggers use them to mark places
        if (o->type == OT_HIDDEN && !o->main && !o->ngrp && !o->data) continue;
        o->nodraw = o->type != OT_TRIGGER && (gd_obj_prop(s, 135, 0) != 0 || (o->type != OT_PORTAL && o->type != OT_ORB &&
                    o->type != OT_PAD && editor_only(s->id)));
        if (o->type == OT_SENSOR || s->id == 2065 || s->id == 3610 || s->id == 3611) o->nodraw = 1;
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

    gm->anim = calloc(AN_COUNT, sizeof(Anim));
    if (gm->anim) {   // missing files leave an animation empty: the single sprite is drawn then
        static const char *const an[AN_COUNT][2] = {
            {"GJBeast01", "GJBeast01_idle01"}, {"GJBeast02", "GJBeast02_idle01"}, {"GJBeast03", "GJBeast03_idle01"},
            {"GJBeast04", "GJBeast04_idle01"}, {"GJBeast05", "GJBeast05_idle01"}, {"Robot", "Robot_run"},
            {"Robot", "Robot_jump_loop"}, {"Robot", "Robot_fall_loop"}, {"Spider", "Spider_run"},
            {"Spider", "Spider_fall_loop"}, {"GJBeast01", "GJBeast01_bite"}};
        for (int i = 0; i < AN_COUNT; i++) {
            char file[64];
            snprintf(file, sizeof file, "%s_AnimDesc.plist", an[i][0]);
            anim_load(&gm->anim[i], g, file, an[i][1]);
        }
    }

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
        if (gm->obj[i].type == OT_TRIGGER && (gm->obj[i].tid == 901 || gm->obj[i].tid == 1346 || gm->obj[i].tid == 1347 ||
            gm->obj[i].tid == 1814 || gm->obj[i].tid == 2067 || gm->obj[i].tid == 3016 || gm->obj[i].tid == 3033) &&
            gm->obj[i].tgroup > 0 && gm->obj[i].tgroup < GD_GROUPS)
            moving[gm->obj[i].tgroup] = 1;
    // a spawn trigger may hand a command another group: those can move too
    for (int i = 0; i < gm->n; i++) {
        const TData *d = gm->obj[i].data;
        if (!d || !d->text || gm->obj[i].tid != 1268) continue;
        for (const char *c = d->text; *c;) {
            while (*c && *c != '.') c++;
            if (!*c) break;
            const int to = atoi(++c);
            if (to > 0 && to < GD_GROUPS) moving[to] = 1;
            while (*c && *c != '.') c++;
            if (*c) c++;
        }
    }
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
        if (o->type == OT_SENSOR && o->sub == SN_BLOCK && !o->notouch) {
            if (!(gm->ncblocks & 63)) gm->cblocks = realloc(gm->cblocks, (size_t)(gm->ncblocks + 64) * sizeof *gm->cblocks);
            gm->cblocks[gm->ncblocks++] = i;
        } else if (o->type == OT_SENSOR && o->sub != SN_BLOCK && !o->notouch) {
            if (!(gm->nsensors & 63)) gm->sensors = realloc(gm->sensors, (size_t)(gm->nsensors + 64) * sizeof *gm->sensors);
            gm->sensors[gm->nsensors++] = i;
        }
        if (o->type == OT_TRIGGER && o->tid == 2900) {
            if (!(gm->nturns & 63)) gm->turns = realloc(gm->turns, (size_t)(gm->nturns + 64) * sizeof *gm->turns);
            gm->turns[gm->nturns++] = i;
        }
        if ((o->type == OT_TRIGGER && (o->tid == 2900 || o->tid == 1917)) ||
            ((o->type == OT_PORTAL || o->type == OT_ORB) && o->tgroup > 0 &&
             (o->sub == (o->type == OT_PORTAL ? P_TELEPORT : K_TELEPORT)))) gm->has_jumps = 1;
        // movers are collected separately (the look-ahead saves and restores them)
        const int inert = o->type == OT_DECO || o->type == OT_HIDDEN || o->notouch;
        if (inert) continue;   // by_x and movers only hold what the simulation needs (drawing uses by_z)
        if (!o->mover) gm->by_x[gm->nx++] = i;
        else gm->movers[gm->nmov++] = i;
    }
    free(moving);
    // the coins in their order: by their number, else along the level
    gm->ncoins = 0;
    for (int i = 0; i < gm->n; i++) {
        const Obj *o = &gm->obj[i];
        if (o->type != OT_COIN || o->notouch || gm->ncoins >= GD_MAX_COINS) continue;
        int at = gm->ncoins++;
        for (; at > 0; at--) {
            const Obj *b = &gm->obj[gm->coin_obj[at - 1]];
            if (b->sub < o->sub || (b->sub == o->sub && b->x <= o->x)) break;
            gm->coin_obj[at] = gm->coin_obj[at - 1];
        }
        gm->coin_obj[at] = i;
    }
    g_sort_obj = gm->obj;
    qsort(gm->by_z, (size_t)gm->n, sizeof(int), cmp_z);
    qsort(gm->by_x, (size_t)gm->nx, sizeof(int), cmp_x);
    gm->end_x = maxx + 380;
    // the icon appears just above the floor, or at the height of the last object of the spawn group
    gm->start_y = 15;
    if (gm->start_group > 0 && gm->start_group < GD_GROUPS) {
        float last = -1e9f;
        for (int k = gm->grp_start[gm->start_group]; k < gm->grp_start[gm->start_group + 1]; k++) {
            const Obj *o = &gm->obj[gm->grp_items[k]];
            if (o->x >= last) { last = o->x; gm->start_y = o->y; }
        }
    }
    (void)g_sort_gfx;
    motion_load(gm);
    visual_load(gm);
    game_reset(gm);
    gm->attempts = 1;
    return 0;
}

void game_set_skin(Game *gm, const int icon[IC_COUNT], const float c1[3], const float c2[3])
{
    for (int i = 0; i < IC_COUNT; i++) gm->icon[i] = icon[i];
    for (int k = 0; k < 3; k++) { gm->c1[k] = c1[k]; gm->c2[k] = c2[k]; }
    gm->skin_set = 1;
    if (gm->ch0_valid) {
        gm->ch[CH_P1].r = c1[0]; gm->ch[CH_P1].g = c1[1]; gm->ch[CH_P1].b = c1[2];
        gm->ch[CH_P2].r = c2[0]; gm->ch[CH_P2].g = c2[1]; gm->ch[CH_P2].b = c2[2];
    }
}

void game_free(Game *gm)
{
    free(gm->by_z);
    free(gm->by_x);
    free(gm->kidsets);
    free(gm->movers);
    free(gm->grp_start);
    free(gm->grp_items);
    free(gm->hsv);
    free(gm->anim);
    free(gm->undo);
    free(gm->turns);
    free(gm->cblocks);
    free(gm->lists);
    free(gm->gradients);
    free(gm->sensors);
    motion_free(gm);
    visual_free(gm);
    for (int i = 0; i < gm->n; i++)
        if (gm->obj[i].data) free(gm->obj[i].data->text);
    for (int i = 0; i < gm->n; i++) free(gm->obj[i].data);
    free(gm->obj);
    memset(gm, 0, sizeof *gm);
}

// Makes an object kx times as wide and ky times as high, around its own position.
void gm_obj_scale(Obj *o, float kx, float ky)
{
    // the hitbox lies in the level: for an object placed on its side the factors change places
    const int side = fabsf(sinf(o->rot0 * 3.14159265f / 180.0f)) > 0.7f;
    const float wx = fabsf(side ? ky : kx), wy = fabsf(side ? kx : ky);
    const float before = fmaxf(fabsf(o->sx), fabsf(o->sy));
    o->sx *= kx;
    o->sy *= ky;
    o->hw *= wx;
    o->hh *= wy;
    o->bw *= fabsf(kx);
    o->bh *= fabsf(ky);
    o->hx *= side ? ky : kx;
    o->hy *= side ? kx : ky;
    if (o->rad > 0 && before > 0) {
        o->rad *= fmaxf(fabsf(o->sx), fabsf(o->sy)) / before;
        o->hw = o->hh = o->rad;
    } else if (o->tilt != 0) {
        const float c = fabsf(cosf(o->tilt)), s = fabsf(sinf(o->tilt));
        o->hw = c * o->bw + s * o->bh;
        o->hh = s * o->bw + c * o->bh;
    }
    o->kx *= kx;
    o->ky *= ky;
}

static void set_ctr(Game *gm, int mode, float obj_y);

void game_reset(Game *gm)
{
    gm->run++;
    gm->jumps = 0;
    gm->npt = 0; gm->dead_fx = 0; gm->emit_t = 0;
    gm->enter_fx = 0;
    gm->npu = gm->naf = 0;
    gm->ghost_on = 0;
    memset(gm->ghost, 0, sizeof gm->ghost);
    memset(gm->coin_fly, 0, sizeof gm->coin_fly);
    gm->shake_left = 0;
    Player *p = &gm->p;
    memset(p, 0, sizeof *p);
    memset(&gm->p2, 0, sizeof gm->p2);
    p->x = 0; p->y = gm->start_y; p->grav = gm->start_flip ? -1 : 1;
    p->gscale = 1.0f;
    gm->frame = gm->next_frame = (Frame){1, 0, 0, 1};
    gm->travel = 0;
    gm->timewarp = 1.0f;
    gm->channel = 0;
    trg_keep(gm);
    memset(&gm->logic, 0, sizeof gm->logic);
    gm->chain.n = 0;
    gm->free = 0;
    // kA4: 0 = normal, 1 = slow, 2 = fast, 3 = faster, 4 = fastest
    static const int map[5] = {1, 0, 2, 3, 4};
    int si = gm->start_speed;
    p->speed = k_speeds[map[(si >= 0 && si < 5) ? si : 0]];
    p->mode = gm->start_mode >= M_CUBE && gm->start_mode <= M_SWING ? gm->start_mode : M_CUBE;   // header order
    p->mini = gm->start_mini;
    p->last_ground_y = p->y;
    p->hold = p->queued = gm->input;
    gm->dual = gm->start_dual;
    if (gm->dual) {
        gm->p2 = *p;
        gm->p2.grav = -p->grav;
    }
    gm->mirror = gm->start_mirror;
    gm->mir_x = 190;
    gm->ctr = 240;
    gm->ground_y = 0;
    const int walls = gm->dual || (p->mode != M_CUBE && p->mode != M_ROBOT);   // a floor and a ceiling
    gm->has_ceil = walls;
    gm->ceil_y = 301;
    for (int i = 0; i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        o->used = o->used2 = 0;
        o->tdone = 0;
        o->off = 0;
        o->x = o->x0; o->y = o->y0;
        if (o->spin == 0) o->rot = o->rot0;
        if (o->kx != 1 || o->ky != 1) {
            gm_obj_scale(o, 1.0f / o->kx, 1.0f / o->ky);
            o->kx = o->ky = 1;
        }
    }
    memset(&gm->view, 0, sizeof gm->view);
    gm->view.zoom.from = gm->view.zoom.to = 1;
    gm->view.bg_id = gm->bg_id0;
    gm->view.ground_id = gm->ground_id0;
    visual_reset(gm);
    memset(gm->grad, 0, sizeof gm->grad);
    for (int i = 0; i < GD_GROUPS; i++) gm->galpha[i] = 1.0f;
    gm->nmv = gm->nsp = 0;
    motion_reset(gm);
    trg_attempt(gm);
    gm->time = 0;
    gm->finished = 0;
    gm->cam_x = -190;
    if (walls) set_ctr(gm, p->mode == M_CUBE || p->mode == M_ROBOT ? M_SHIP : p->mode, p->y);
    gm->cam_bottom = walls ? gm->ctr - 250.0f : fmaxf(-90.0f, p->y - 105.0f);
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
#define COIN_FLY_TIME 0.75f   // seconds a picked up coin stays visible

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
    p->square = 0;
}

// the little squares the icon kicks up
static void emit_square(Game *gm, float x, float y, float vx, float vy, float life, float size, float r, float g, float b)
{
    const int n = gm->npt;
    emit(gm, x, y, vx, vy, life, size, -3.0f, r, g, b, 0.9f);
    if (gm->npt > n) gm->pt[n].square = 1;
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
    case P_GRAV_TOGGLE: case P_CUBE: case P_SIZE_NORMAL: *r = 80; *g = 255; *b = 100; break;
    case P_SHIP: case P_SIZE_MINI: *r = 255; *g = 90; *b = 200; break;
    case P_BALL: case P_UFO: case P_DUAL_ON: *r = 255; *g = 150; *b = 30; break;
    case P_WAVE: case P_DUAL_OFF: *r = 40; *g = 200; *b = 255; break;
    default: *r = 255; *g = 255; *b = 255; break;
    }
}

float gm_random(void) { return frand(); }

// A gradient is shown until another trigger with the same id replaces it or switches it off.
void gm_add_gradient_state(Game *gm, int obj)
{
    if (trg_prop(&gm->obj[obj], 508, 0) != 0) {   // all of them off
        memset(gm->grad, 0, sizeof gm->grad);
        return;
    }
    for (int i = 0; i < gm->ngradients; i++) {
        if (gm->gradients[i].obj != obj) continue;
        gm->grad[gm->gradients[i].id] = gm->gradients[i].off ? 0 : (short)(i + 1);
        break;
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
    if (gm->sim || p->dead || gm->view.hidden) return;
    const float k = p->mini ? 0.6f : 1.0f, gsign = (float)p->grav;
    if (p->on_ground && !was_ground && (p->mode == M_CUBE || p->mode == M_ROBOT || p->mode == M_BALL || p->mode == M_SPIDER)) {
        float fy = p->y - 14.0f * k * gsign;   // landing puff
        for (int i = 0; i < 6; i++)
            emit_square(gm, p->x - 6 + frand() * 12, fy, (frand() - 0.7f) * 90.0f, gsign * (15.0f + frand() * 40.0f), 0.4f,
                        (2.5f + frand() * 2.5f) * k, gm->c1[0], gm->c1[1], gm->c1[2]);
    }
    // squares left behind while dashing, in the second colour
    if (p->dashing && (int)(p->dash_age * 120.0f) != (int)((p->dash_age - STEP) * 120.0f)) {
        const float d = p->dash_angle * 3.14159265f / 180.0f, dx = cosf(d), dy = -sinf(d);
        const float side = (frand() * 2.0f - 1.0f) * 12.0f * k, speed = 13.0f + frand() * 24.0f;
        const float size = (4.0f + frand() * 8.0f) * k * 0.7f, life = 0.2f + frand() * 0.4f;
        const float *col = p == &gm->p ? gm->c2 : gm->c1;
        const int n = gm->npt;
        emit(gm, p->x - dy * side, p->y + dx * side, -dx * speed, -dy * speed, life, size, -size / life,
             col[0], col[1], col[2], 0.6f);
        if (gm->npt > n) gm->pt[n].square = 1;
    }
    gm->emit_t += STEP;
    if (gm->emit_t < 0.012f) return;
    gm->emit_t = 0;
    float a = p->rot * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    if (p->dashing) return;
    if (p->mode == M_SHIP || p->mode == M_UFO) {
        float dx = (p->mode == M_SHIP ? -17.0f : -8.0f) * k, dy = (p->mode == M_SHIP ? -8.0f : -11.0f) * k * gsign;
        float wx = dx * c + dy * s, wy = -dx * s + dy * c;
        if (p->mode == M_SHIP && (p->hold || 1))
            emit(gm, p->x + wx, p->y + wy, -70.0f - frand() * 50.0f, (frand() - 0.5f) * 50.0f, 0.28f, 11.0f * k, -30.0f,
                 255, 150 + frand() * 60.0f, 30, 0.9f);
        else if (p->mode == M_UFO && p->hold)
            emit(gm, p->x + wx, p->y + wy, -30.0f * frand(), -gsign * (40.0f + frand() * 30.0f), 0.3f, 7.0f * k, -15.0f, 255, 255, 255, 0.7f);
    } else if (p->mode == M_WAVE) {
        emit(gm, p->x, p->y, -40.0f * frand(), (frand() - 0.5f) * 60.0f, 0.3f, 8.0f * k, -20.0f, gm->c1[0], gm->c1[1], gm->c1[2], 0.8f);
    } else if (p->on_ground) {   // squares thrown up while sliding along the floor
        emit_square(gm, p->x - 13 * k, p->y - 13.0f * k * gsign + frand() * 2.0f, 130.0f + frand() * 110.0f,
                    gsign * (5.0f + frand() * 45.0f), 0.32f, (2.0f + frand() * 3.0f) * k, gm->c1[0], gm->c1[1], gm->c1[2]);
    }
}

static void particles_step(Game *gm, float dt)
{
    for (int k = 0; k < GD_MAX_COINS; k++)
        if (gm->coin_fly[k].age > 0 && (gm->coin_fly[k].age += dt) > COIN_FLY_TIME) gm->coin_fly[k].age = 0;
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

// What pressing or releasing the button means for event triggers, by the form of the icon.
static void input_events(Game *gm, int down)
{
    const Player *p = &gm->p;
    trg_event(gm, down ? 69 : 70);
    if (down) {
        const int e = p->mode == M_UFO ? 15 : p->mode == M_BALL ? 19 : p->mode == M_SWING ? 20 : p->mode == M_SHIP ? 16
                    : p->mode == M_WAVE ? 21 : p->mode == M_ROBOT ? 13 : p->mode == M_SPIDER ? 18 : p->on_ground ? 12 : 0;
        trg_event(gm, e);
    } else {
        trg_event(gm, p->mode == M_SHIP ? 17 : p->mode == M_WAVE ? 22 : p->mode == M_ROBOT ? 14 : 0);
    }
}

void game_set_hold(Game *gm, int down)
{
    if (down && !gm->input && !gm->sim && !gm->p.dead && !gm->finished) gm->jumps++;
    down = down != 0;
    const int changed = down != gm->input;
    if (down && !gm->input) {
        gm->p.hold = gm->p.queued = gm->p.just_held = gm->p.fresh = 1;
        gm->p2.hold = gm->p2.queued = gm->p2.just_held = gm->p2.fresh = 1;
    } else if (!down) {
        gm->p.hold = gm->p2.hold = 0;
        gm->p.queued = gm->p2.queued = 0;
        gm->p.just_held = gm->p2.just_held = 0;
    }
    gm->input = down;
    if (changed && gm->logic.nlisten && !gm->p.dead && !gm->finished) input_events(gm, down);
}

int gm_teleport(Game *gm, Player *p, int group);

static int first_index(const Game *gm, float x)
{
    int lo = 0, hi = gm->nx;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (gm->obj[gm->by_x[mid]].x < x) lo = mid + 1; else hi = mid;
    }
    return lo;
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
#define STREAK_TIME 0.3f      // seconds a point of the streak stays visible
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
    // these borders are meant for an icon of size 30; spider and wave keep another distance from them
    const float adj = (p->mini ? 9.0f : 15.0f) - gd_floor_half(p);
    f -= adj; c += adj;
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

// Looks only: from now on the icon draws a streak behind it (until it lands).
static void start_streak(Player *p)
{
    if (p->mode == M_WAVE || p->streak) return;
    p->streak = 1;
    p->trail_n = 0;
    p->streak_idle = 0;
}

// forms that walk keep the streak until they touch the ground again, the others only while dashing
static int walks(const Player *p)
{
    return p->mode == M_CUBE || p->mode == M_ROBOT || p->mode == M_BALL || p->mode == M_SPIDER;
}

static void flip_gravity(Player *p, int flipped)
{
    if (is_flipped(p) != flipped) {
        if (walks(p)) start_streak(p);
        p->grav = flipped ? -1 : 1;
        p->yv /= 2.0;
        p->flip_grace = 0.1f;
    }
}

static void hit_ground(Player *p)
{
    p->yv = 0;
    p->queued = 0;
    p->on_ground = 1;
    p->last_ground_y = p->y;
    if (!p->dashing) p->streak = 0;
    if (p->rot_left > 0) stop_rotation(p);
}

static void set_mode(Player *p, int mode)
{
    if (p->mode == mode) return;
    p->rot_left = 0;
    p->snap = 0;
    p->trail_n = 0;
    p->streak = 0;
    if (mode == M_SHIP) {
        p->rot = 0;
        p->yv /= 2.0;
        p->on_ground = 0;
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
    if (walks(p)) start_streak(p);
    p->yv = flip_mod(p) * 16.0 * force * (p->mini ? 0.8 : 1.0);
    if (p->mode == M_BALL || p->mode == M_SPIDER) p->yv *= 0.6;
    if (p->mode == M_CUBE || p->mode == M_ROBOT) run_rotate_action(p);
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
        start_streak(p);
        stop_rotation(p);
        p->dash_age = STEP;
        p->boom_age = -1;
        p->boom_flip = o->sub == K_DASH_FLIP;
        return;
    }
    if (walks(p)) start_streak(p);
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
    if (m == M_CUBE || m == M_ROBOT) run_rotate_action(p);
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

// A block landed on or bumped into. Moving away from it fast, the icon does not count as grounded.
static void land(Player *p)
{
    const int away = flip_mod(p) * p->yv > 5.0;
    hit_ground(p);
    if (away) p->on_ground = 0;
}

// The icon's outer box touches a block (collidedWithObject, as gd-levelgen worked it out for
// 2.2081). It is put on top of the block if its "feet" (its lower edge plus a tolerance) are above
// it now or were before the last movement; flying forms and the ball also bump into blocks above
// them this way. Otherwise only the inner box decides: overlapping the block with it is deadly.
static void collided(Game *gm, Player *p, const Obj *o, Obj *orig, int ceil_before)
{
    float x0, x1, y0, y1;
    obj_rect(o, &x0, &x1, &y0, &y1);
    const float s = (float)flip_mod(p), half = gd_body_half(p);
    const int flies = p->mode == M_SHIP || p->mode == M_UFO || p->mode == M_SWING || p->mode == M_WAVE;
    const int flyish = flies || p->mode == M_BALL;
    const float tol = flies ? 6.0f : 10.0f, dy = p->y - p->prev_y;
    const float feet = p->y - s * half + s * tol, head = p->y + s * half - s * tol;
    const float feet_before = feet - dy, head_before = head - dy;
    const int clear = s > 0 ? y0 >= feet || y0 >= feet_before : feet >= y1 || feet_before >= y1;
    const int self = (int)(orig - gm->obj) + 1;
    if ((gm->head_safe || gm->head_flip) && !flies) {   // the head runs into a block inside an H or F block
        const float depth = s > 0 ? p->y + half - y0 : y1 - (p->y - half);
        if ((p->y - (y0 + y1) * 0.5f) * s < 0 && depth <= 14.0f) {
            p->y = s > 0 ? y0 - half : y1 + half;
            if (gm->head_flip) {   // gravity turns around: the ceiling is the floor now
                flip_gravity(p, !is_flipped(p));
                hit_ground(p);
            } else if (p->yv * s > 0) p->yv = 0;
            return;
        }
    }
    if (p->mode != M_WAVE || gm->dart_safe) {
        // the block lies below the icon in the level: landing with normal gravity
        const int below = s > 0 ? feet >= y1 || feet_before >= y1 : head >= y1 || head_before >= y1;
        if ((s > 0 || (flyish && !o->passable)) && below) {
            if (p->yv <= 0) {
                p->y = y1 + half;
                if (clear) p->yv = 0;
                else land(p);
            }
            return;
        }
        // the block lies above: landing with flipped gravity, bumping into it when flying
        if (s < 0 || (flyish && !o->passable)) {
            const float at = s > 0 ? head : feet;
            const int deep = y0 < at && y0 < at - dy;
            if (!deep || self == ceil_before) {
                if (p->yv >= 0) {
                    p->y = y0 - half;
                    if (clear) p->yv = 0;
                    else land(p);
                    p->ceil_obj = self;
                }
                return;
            }
        }
    }
    if (o->passable) return;   // one-way blocks never kill
    const float in = gd_inner_half(p);
    if (!rect_hit(p->x - in, p->x + in, p->y - in, p->y + in, x0, x1, y0, y1)) return;
    if (p->mode == M_SPIDER && p->spider_grace > 0 && p->x + in > x1) return;
    if (clear && p->flip_grace > 0) {
        p->y = s < 0 ? y1 + half : y0 - half;
        hit_ground(p);
        p->on_ground = 0;
        return;
    }
    if (o->breakable) { trg_touch(gm, orig); orig->off = 1; trg_event(gm, 61); }   // the brick shatters (restored on the next attempt)
    else die_why(gm, "inner", o);
}

// Looks only: the icon lies flat on a slope of h by w, tilted like it.
static void tilt_on_slope(const Game *gm, Player *p, int slope, float w, float h)
{
    float a = atan2f(h, w) * 180.0f / 3.14159265f;
    if (slope & 2) a = -a;                                    // uphill: the front goes up
    if (slope & 4) a = -a;                                    // hanging from a ceiling slope
    if (gm->frame.fx * gm->frame.uy - gm->frame.fy * gm->frame.ux < 0) a = -a;   // mirrored movement
    p->slope_rot = a;
    p->slope_t = 0.05f;
}

// Triangles: the icon slides along the long side, down as well as up; running into one of the short
// sides kills. Ceiling slopes are handled by mirroring everything vertically.
static void slope_collide(Game *gm, Player *p, const Obj *o)
{
    float x0, x1, y0, y1;
    obj_rect(o, &x0, &x1, &y0, &y1);
    const float half = gd_body_half(p), in = gd_inner_half(p);
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
    const int towards = p->grav * (ceiling ? -1 : 1) > 0; // gravity pulls onto this surface
    const int flies = p->mode == M_SHIP || p->mode == M_UFO || p->mode == M_SWING;
    const float step_rise = p->speed * STEP * h / w;
    const int downhill = towards && !flies && !high_right && p->mode != M_WAVE;
    // beyond the lower end the next slope or the ground takes over: it must not push up there
    if (downhill && p->x > x1) return;
    if (py >= rest) {   // above the slope
        // An icon that was on the ground and has not jumped stays on a slope that falls away under
        // it: it slides down instead of dropping, landing and dropping again.
        if (downhill && p->x >= x0 && p->on_ground && !p->rising && p->yv * m <= 0.5 &&
            py - rest <= half * h / w + 2.0f * step_rise + 3.0f) {
            p->y = fmaxf(rest, py - 2.0f * step_rise - 1.5f) * m;
            hit_ground(p);
            tilt_on_slope(gm, p, o->slope, w, h);
        }
        return;
    }
    const float tol = (p->mini ? 8.0f : 11.0f) + step_rise;
    // how deep the centre line of the icon is inside the triangle itself
    const int inside = p->x >= x0 - 3.0f && p->x <= x1 + 3.0f && py >= lo - half;
    if (p->mode == M_WAVE && !gm->dart_safe) {
        if (p->x + half > x0 && p->x - half < x1 && py - half < surface && py + half > lo) die_why(gm, "wave-slope", o);
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
        if (towards && !flies) tilt_on_slope(gm, p, o->slope, w, h);
        // going uphill: keep the upward speed, so that the icon takes off at the top end
        if (towards && high_right && t < 1.0f)
            p->yv = m * (double)(p->speed * STEP * h / w) / (FRAMES_PER_STEP * 0.9);
        return;
    }
    // inner box against the triangle: hit a short side, or came from the wrong direction
    if (p->x + in > x0 && p->x - in < x1 && py + in > lo && py - in < surface) die_why(gm, "slope", o);
}

// Does a straight rectangle (the icon) overlap an object's box, which may be turned?
static int box_hit(float ax0, float ax1, float ay0, float ay1, const Obj *o)
{
    float x0, x1, y0, y1;
    obj_rect(o, &x0, &x1, &y0, &y1);
    if (!rect_hit(ax0, ax1, ay0, ay1, x0, x1, y0, y1)) return 0;
    if (o->tilt == 0) return 1;
    // separating axes: the two of the turned box (the straight ones are covered by its bounds)
    const float c = cosf(o->tilt), s = sinf(o->tilt), aw = (ax1 - ax0) * 0.5f, ah = (ay1 - ay0) * 0.5f;
    const float dx = (ax0 + ax1) * 0.5f - (o->x + o->hx), dy = (ay0 + ay1) * 0.5f - (o->y + o->hy);
    return fabsf(dx * c + dy * s) <= aw * fabsf(c) + ah * fabsf(s) + o->bw &&
           fabsf(dy * c - dx * s) <= aw * fabsf(s) + ah * fabsf(c) + o->bh;
}

// The icon's outer box against a round hitbox. Old levels test whether the centre of the circle is
// inside the box or a corner of the box inside the circle; newer ones (header kA39) compare the
// distance of the centres.
static int round_hit(const Game *gm, float px, float py, float half, const Obj *o)
{
    const float dx = fabsf(o->x - px), dy = fabsf(o->y - py);
    if (gm->fix_radius) return dx * dx + dy * dy <= (half + o->rad) * (half + o->rad);
    if (dx <= half && dy <= half) return 1;
    return (dx - half) * (dx - half) + (dy - half) * (dy - half) < o->rad * o->rad;
}

// The hitbox of an object that triggers have turned since the level began (tmp holds the copy).
static const Obj *posed(const Obj *o, Obj *tmp)
{
    const float d = o->rot - o->rot0;
    if (o->spin != 0 || fabsf(d) < 0.01f || o->rad > 0 || o->hw <= 0) return o;
    *tmp = *o;
    if (o->type == OT_SOLID || o->type == OT_HBLOCK) {   // blocks and slopes: the nearest quarter turn
        const int turns = (((int)lroundf(d / 90.0f)) % 4 + 4) % 4;
        for (int i = 0; i < turns; i++) {
            const float w = tmp->hw, x = tmp->hx;
            tmp->hw = tmp->hh; tmp->hh = w;
            tmp->hx = tmp->hy; tmp->hy = -x;
            if (tmp->slope) {   // the corner with the right angle goes round clockwise
                const int right = tmp->slope & 2, top = tmp->slope & 4;
                tmp->slope = (unsigned char)(1 | (top ? 2 : 0) | (right ? 0 : 4));
            }
        }
        return tmp;
    }
    const float a = d * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
    tmp->hx = o->hx * c + o->hy * s;
    tmp->hy = o->hy * c - o->hx * s;
    const float t = -o->rot * 3.14159265f / 180.0f, q = o->rot / 90.0f;
    tmp->hw = fabsf(cosf(t)) * o->bw + fabsf(sinf(t)) * o->bh;
    tmp->hh = fabsf(sinf(t)) * o->bw + fabsf(cosf(t)) * o->bh;
    tmp->tilt = fabsf(q - roundf(q)) > 0.001f ? t : 0;
    return tmp;
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
    if (o->tilt != 0) {   // the turned box's first axis, seen from the frame
        float ax, ay;
        to_local(f, cosf(o->tilt), sinf(o->tilt), &ax, &ay);
        out->tilt = atan2f(ay, ax);
    }
    if (o->slope) {
        float cx, cy;
        to_local(f, (o->slope & 2) ? 1.0f : -1.0f, (o->slope & 4) ? 1.0f : -1.0f, &cx, &cy);
        out->slope = (unsigned char)(1 | (cx > 0 ? 2 : 0) | (cy > 0 ? 4 : 0));
    }
}

// Moves the player to the first object of a group (2.2 teleport portals and orbs).
int gm_teleport(Game *gm, Player *p, int group)
{
    if (group <= 0 || group >= GD_GROUPS || gm->grp_start[group] >= gm->grp_start[group + 1]) return 0;
    const Obj *t = &gm->obj[gm->grp_items[gm->grp_start[group]]];
    if (gm->local) to_local(gm->frame, t->x, t->y, &p->x, &p->y);
    else { p->x = t->x; p->y = t->y; }
    p->on_ground = 0;
    p->trail_n = 0;
    return 1;
}

static void apply_portal(Game *gm, Player *p, const Obj *o)
{
    const int is1 = p == &gm->p;
    int mode = -1;
    switch (o->sub) {
    case P_GRAV_DOWN: flip_gravity(p, 0); break;
    case P_GRAV_UP: flip_gravity(p, 1); break;
    case P_GRAV_TOGGLE: flip_gravity(p, !is_flipped(p)); break;
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
        if (!gm_teleport(gm, p, o->tgroup)) p->y = o->y + o->tdy;   // exit height relative to the entry
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
    if (is1 && gm->logic.nlisten) {
        const int e = o->sub == P_TELEPORT ? 59 : mode >= 0 ? 26 + (mode == M_SHIP ? 1 : mode == M_BALL ? 2 : mode == M_UFO ? 3
                    : mode == M_WAVE ? 4 : mode == M_ROBOT ? 5 : mode == M_SPIDER ? 6 : mode == M_SWING ? 7 : 0)
                    : o->sub == P_MIRROR_ON ? 53 : o->sub == P_MIRROR_OFF ? 54 : o->sub == P_SIZE_NORMAL ? 55
                    : o->sub == P_SIZE_MINI ? 56 : o->sub == P_DUAL_ON ? 57 : o->sub == P_DUAL_OFF ? 58
                    : o->sub == P_GRAV_DOWN ? 51 : o->sub == P_GRAV_UP ? 52
                    : o->sub == P_GRAV_TOGGLE ? (is_flipped(p) ? 52 : 51) : 0;
        trg_event(gm, e);
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

int game_coins(const Game *gm)
{
    int mask = 0;
    for (int k = 0; k < gm->ncoins; k++) mask |= gm->obj[gm->coin_obj[k]].used ? 1 << k : 0;
    return mask;
}

static int coin_number(const Game *gm, const Obj *o)
{
    for (int k = 0; k < gm->ncoins; k++)
        if (&gm->obj[gm->coin_obj[k]] == o) return k;
    return -1;
}

// Either icon takes the coin; it is back with the next attempt. Only finishing the level keeps it.
static void coin_collect(Game *gm, Obj *o)
{
    o->used = o->used2 = 1;
    const int k = coin_number(gm, o);
    if (gm->sim || k < 0) return;
    const int user = o->id == 1329, owned = (gm->coins_owned >> k) & 1;
    gm->coin_fly[k] = (CoinFly){o->x, o->y, 0.0001f, frand() * 120.0f - 60.0f, (unsigned char)user,
                                (unsigned char)owned};
    const float r = user ? 225.0f : 255.0f, g = user ? 235.0f : 215.0f, b = user ? 245.0f : 40.0f;
    emit_burst(gm, o->x, o->y, 22, 170.0f, 0.6f, 7.0f, r, g, b);
}

// Looks only: the streak's points and the ages of what a dash draws, once per step.
static void streak_step(const Game *gm, Player *p)
{
    if (p->streak && !p->dashing && !walks(p)) p->streak = 0;
    if (p->mode != M_WAVE) {
        if (p->streak) {
            p->streak_idle = 0;
            if ((++p->trail_tick & 1) == 0 || p->trail_n == 0) trail_push(p);
        } else if (p->trail_n && (p->streak_idle += STEP) > STREAK_TIME) p->trail_n = 0;
    }
    if (p->dashing) {   // the direction of flight as the screen shows it
        const Frame f = gm->frame;
        p->dash_angle = -atan2f(f.fy + f.uy * p->dash_slope, f.fx + f.ux * p->dash_slope) * 180.0f / 3.14159265f;
        p->dash_age += STEP;
    } else if (p->dash_age > 0) {   // the flame flies off
        p->dash_age = 0;
        p->off_age = STEP;
        p->off_x = p->x; p->off_y = p->y;
        p->off_angle = p->dash_angle;
    }
    if (p->boom_age < 0) {
        p->boom_age = STEP;
        p->boom_x = p->x; p->boom_y = p->y;
    } else if (p->boom_age > 0 && (p->boom_age += STEP) > 0.45f) p->boom_age = 0;
    if (p->off_age > 0 && (p->off_age += STEP) > 0.2f) p->off_age = 0;
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
    const float oh = gd_body_half(p);   // half of the outer box
    float floor_y = gd_floor_half(p), ceil_y = 0;
    const int ceil_before = p->ceil_obj;
    p->ceil_obj = 0;

    if (gm->has_jumps && (wpy < -200.0f || wpy > 4000.0f || wpx < -300.0f)) { die_why(gm, "outside", NULL); return; }
    // rotate triggers of the active channel fire when passed in the current movement direction
    if (is1) {
        for (int i = 0; i < gm->nturns; i++) {
            Obj *t = &gm->obj[gm->turns[i]];
            if (t->tdone || t->off || t->spawn_only || t->channel != gm->channel) continue;
            float tx = t->x, ty = t->y;
            if (rot) to_local(gm->frame, t->x, t->y, &tx, &ty);
            if (p->x < tx || p->x > tx + 400.0f) continue;
            trg_touch(gm, t);
            t->tdone = 1;
            trg_fire(gm, t, 0);
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
    gm->head_safe = gm->head_flip = gm->dart_safe = 0;
    for (int ci = 0; ci < nc; ci++) {
        const Obj *o = &gm->obj[cand[ci]];
        Obj turned;
        if (o->type != OT_HBLOCK || o->off) continue;
        if (rot) { local_obj(gm->frame, o, &turned); o = &turned; }
        if (fabsf(p->x - o->x) >= oh + o->hw || fabsf(p->y - o->y) >= oh + o->hh) continue;
        if (o->sub == SB_HEAD) gm->head_safe = 1;
        else if (o->sub == SB_FLIP) gm->head_flip = 1;
        else if (o->sub == SB_DART) gm->dart_safe = 1;
        else p->dashing = 0;
    }
    // first triggers, portals, pads, orbs and slopes, then the blocks, then everything deadly
    int blocks[256], nb = 0;
    for (int ci = 0; ci < nc; ci++) {
        {
            Obj *r = &gm->obj[cand[ci]], turned, local;   // r holds the state, o the geometry
            if (r->off) continue;
            const Obj *o = posed(r, &turned);
            if (rot) { local_obj(gm->frame, o, &local); o = &local; }
            if (o->type == OT_TRIGGER) {
                // normal triggers fire when passed, touch triggers only when the icon runs into them
                const int reached = o->touch ? fabsf(p->x - o->x) <= oh + 15.0f && fabsf(p->y - o->y) <= oh + 15.0f
                                             : p->x >= o->x;
                if (is1 && !r->tdone && !r->spawn_only && reached && r->tid != 2900 &&
                    (r->touch || r->channel == gm->channel)) {
                    trg_touch(gm, r);
                    r->tdone = 1;
                    trg_fire(gm, r, 0);
                }
                continue;
            }
            if (o->type == OT_DECO || o->type == OT_HIDDEN || o->type == OT_SENSOR || o->type == OT_HBLOCK) continue;
            if (o->hw <= 0 || o->hh <= 0 || o->type == OT_HAZARD) continue;
            if (o->type == OT_SOLID) {
                if (o->slope) {
                    float x0, x1, y0, y1;
                    obj_rect(o, &x0, &x1, &y0, &y1);
                    const float h = gd_body_half(p);
                    if (rect_hit(p->x - h - 1.0f, p->x + h + 1.0f, p->y - h * 2.0f, p->y + h * 2.0f, x0, x1, y0, y1))
                        slope_collide(gm, p, o);
                    if (gm->p.dead) return;
                } else if (nb < 256 && fabsf(p->x - (o->x + o->hx)) <= o->hw + 40.0f &&
                           fabsf(p->y - (o->y + o->hy)) <= o->hh + 40.0f) {
                    int k = nb++;   // the order of the original: the last object of the level first
                    while (k > 0 && blocks[k - 1] < cand[ci]) { blocks[k] = blocks[k - 1]; k--; }
                    blocks[k] = cand[ci];
                }
                continue;
            }
            const float h = gd_body_half(p);   // a portal may just have changed the form
            if (!box_hit(p->x - h, p->x + h, p->y - h, p->y + h, o)) continue;
            int *used = is1 ? &r->used : &r->used2;
            if (*used) continue;
            if (o->type != OT_FORCE) trg_touch(gm, r);
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
            case OT_COIN: coin_collect(gm, r); break;
            case OT_PAD:
                if (is1 && gm->logic.nlisten) {
                    trg_event(gm, 9);
                    trg_event(gm, o->sub == K_YELLOW ? 45 : o->sub == K_PINK ? 46 : o->sub == K_RED ? 47
                                : o->sub == K_BLUE ? 48 : o->sub == K_SPIDER ? 49 : 0);
                }
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
                    if (o->sub == K_TOGGLE) trg_activate(gm, o->tgroup, o->tact, 0);
                    else if (o->sub == K_TELEPORT) gm_teleport(gm, p, o->tgroup);
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
                    if (!was_used && *used && is1 && gm->logic.nlisten) {
                        static const short ev[] = {0, 34, 37, 35, 38, 36, 39, 41, 42, 40, 43, 44};
                        trg_event(gm, 8);
                        trg_event(gm, o->sub >= 0 && o->sub < 12 ? ev[o->sub] : 0);
                    }
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
    for (int i = 0; i < nb; i++) {
        Obj *r = &gm->obj[blocks[i]], turned, local;
        if (r->off) continue;
        const Obj *o = posed(r, &turned);
        if (rot) { local_obj(gm->frame, o, &local); o = &local; }
        float x0, x1, y0, y1;
        obj_rect(o, &x0, &x1, &y0, &y1);
        const float h = gd_body_half(p);
        if (!rect_hit(p->x - h, p->x + h, p->y - h, p->y + h, x0, x1, y0, y1)) continue;
        collided(gm, p, o, r, ceil_before);
        if (gm->p.dead) return;
    }
    const float half = gd_body_half(p);
    for (int ci = 0; ci < nc; ci++) {
        const Obj *r = &gm->obj[cand[ci]];
        Obj turned, local;
        if (r->off || r->type != OT_HAZARD) continue;
        const Obj *o = posed(r, &turned);
        if (rot) { local_obj(gm->frame, o, &local); o = &local; }
        if (o->hw <= 0 || o->hh <= 0) continue;
        float x0, x1, y0, y1;
        obj_rect(o, &x0, &x1, &y0, &y1);
        if (o->slope) {   // sloped spike pit: only the filled half of the rectangle
            const float hx = p->x < x0 ? x0 : p->x > x1 ? x1 : p->x;
            float t = x1 > x0 ? (hx - x0) / (x1 - x0) : 0;
            if (!(o->slope & 2)) t = 1.0f - t;
            const float fill = t * (y1 - y0) - 6.0f;
            const int in = p->x + half * 0.5f > x0 && p->x - half * 0.5f < x1 &&
                           ((o->slope & 4) ? p->y + half * 0.5f > y1 - fill && p->y - half * 0.5f < y1
                                           : p->y - half * 0.5f < y0 + fill && p->y + half * 0.5f > y0);
            if (in) { die_why(gm, "hazard", o); return; }
            continue;
        }
        const int hit = o->rad > 0 ? round_hit(gm, p->x, p->y, half, o)
                                   : box_hit(p->x - half, p->x + half, p->y - half, p->y + half, o);
        if (hit) { die_why(gm, "hazard", o); return; }
    }
    if (p->mode == M_SHIP) p->queued = 0;
}

// Spider: a click teleports the icon to the opposite surface (nearest block face or the level
// floor/ceiling in the new gravity direction).
static void spider_teleport(Game *gm, Player *p)
{
    const float oh = gd_body_half(p);
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
    p->spider_grace = 0.04f;
    p->ceil_obj = 0;
}

static void step_player(Game *gm, Player *p)
{
    // running rotation action / snap back to a multiple of 90 degrees
    if (p->mode == M_BALL) {
        p->rot += 720.0f * STEP * (float)flip_mod(p);
    } else if (p->dashing && p->mode == M_CUBE) {
        p->rot = fmodf(p->rot + 960.0f * STEP * (float)flip_mod(p), 360.0f);
        p->snap = 1;
    } else if (p->slope_t > 0 && !p->rising && (p->mode == M_CUBE || p->mode == M_ROBOT || p->mode == M_SPIDER)) {
        // the nearest way to lie flat on the slope (the cube on any of its four sides)
        const float turn = p->mode == M_CUBE ? 90.0f : 360.0f;
        const float target = p->slope_rot + floorf((p->rot - p->slope_rot) / turn + 0.5f) * turn;
        p->rot += (target - p->rot) * fminf(1.0f, STEP / 0.04f);
        p->rot_left = 0;
        p->snap = 1;
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

    if (p->slope_t > 0) p->slope_t -= STEP;

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
    p->prev_y = prev_y;
    if (p->flip_grace > 0) p->flip_grace -= STEP;
    if (p->spider_grace > 0) p->spider_grace -= STEP;
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
    p->fresh = 0;
    // rotated and free sections have no floor: an icon that only falls for 1.5 s has left the level
    if (gm->has_jumps && p->mode != M_SHIP && p->mode != M_SWING && p->mode != M_UFO && p->mode != M_WAVE &&
        !p->on_ground && fabs(p->yv) >= 14.9) {
        if (++p->fall > 360 && !gm->noclip) die_why(gm, "outside", NULL);
    } else p->fall = 0;
    const float travelled = p->y - prev_y, lx = p->x, ly = p->y;
    if (rot) to_world(gm->frame, lx, ly, &p->x, &p->y);
    gm->local = 0;
    // dual mode: while both icons have the same form, their gravities stay opposite
    if (gm->dual && p->grav != grav_before && !(gm->logic.options & OPT_FREE_DUAL)) {
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
        if ((++p->trail_tick & 1) == 0 || p->trail_n == 0) trail_push(p);
    } else if (p->mode == M_UFO || (p->mode == M_SPIDER && p->slope_t <= 0)) {
        p->rot = 0;
    }
    streak_step(gm, p);
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
    trg_step(gm);
    {
        int g1 = gm->p.on_ground, g2 = gm->p2.on_ground;
        const double fall = gm->p.yv;
        const int grav = gm->p.grav, dash = gm->p.dashing;
        step_player(gm, &gm->p);
        if (gm->logic.nlisten && !gm->p.dead) {
            if (gm->p.on_ground && !g1) {   // landings, told apart by the speed of the fall (own limits)
                const double v = fabs(fall);
                trg_event(gm, gm->p.mini ? 1 : v < 5 ? 2 : v < 9 ? 3 : v < 13 ? 4 : 5);
            }
            if (gm->p.grav != grav) trg_event(gm, gm->p.grav < 0 ? 10 : 11);
            if ((gm->p.dashing != 0) != (dash != 0)) trg_event(gm, gm->p.dashing ? 23 : 24);
        }
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

// A new image of the ghost trail every 0.05 s; each one fades within 0.4 s.
static void ghosts_step(Game *gm, float dt)
{
    for (int i = 0; i < GD_GHOSTS; i++)
        if (gm->ghost[i].age > 0 && (gm->ghost[i].age += dt) > 0.4f) gm->ghost[i].age = 0;
    gm->ghost_t += dt;
    if (!gm->ghost_on || gm->p.dead || gm->ghost_t < 0.05f) return;
    gm->ghost_t = 0;
    const Player *p = &gm->p;
    gm->ghost[gm->ghost_next] = (Ghost){p->x, p->y, p->rot, 0.001f, p->mode, p->mini, p->grav};
    gm->ghost_next = (gm->ghost_next + 1) % GD_GHOSTS;
}

void game_update(Game *gm, float dt)
{
    if (dt > 0.1f) dt = 0.1f;
    gm->anim_t += dt;
    particles_step(gm, dt);
    ghosts_step(gm, dt);
    if (gm->shake_left > 0) gm->shake_left -= dt;
    trg_camera_step(&gm->view, dt);
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
        if (k >= 1.0f) { c->fading = 0; c->player = c->next_player; }
    }

    if (gm->p.dead) {
        gm->p.dead_t += dt;
        if (!gm->dead_fx) trg_death(gm);   // once per crash
        death_fx(gm);
        if (gm->p.dead_t > (gm->logic.respawn > 0 ? gm->logic.respawn : 1.0f)) { // restart attempt
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
    if (c->lbg) {   // a paler background; only a (nearly) black background hands over to player colour 1
        const Channel *bg = &gm->ch[CH_BG], *p1 = &gm->ch[CH_P1];
        float h, sat, val, r, gg, b;
        rgb_to_hsv(bg->r / 255.0f, bg->g / 255.0f, bg->b / 255.0f, &h, &sat, &val);
        hsv_to_rgb(h, fmaxf(sat - 0.2f, 0.0f), val, &r, &gg, &b);
        const float w = fmaxf(0.0f, 1.0f - val * 5.0f);
        o.r = r * 255.0f * (1.0f - w) + p1->r * w;
        o.g = gg * 255.0f * (1.0f - w) + p1->g * w;
        o.b = b * 255.0f * (1.0f - w) + p1->b * w;
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

static GdColorF chan_color(const Game *gm, int id, float *additive);

GdColorF gm_channel(const Game *gm, int id)
{
    float additive;
    return chan_color(gm, id, &additive);
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
    if (k->pw > 0) { pw = k->pw; ph = k->ph; }
    float dx = (k->x - pw * 0.5f) * (o->sx < 0 ? -1.0f : 1.0f) * fabsf(o->sx);
    float dy = (k->y - ph * 0.5f) * (o->sy < 0 ? -1.0f : 1.0f) * fabsf(o->sy);
    float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    float wx = dx * c + dy * sn, wy = -dx * sn + dy * c;
    float add;
    int ch = k->kind == 2 ? CH_BLACK : k->kind == 1 ? o->ch_detail : o->ch_main;
    GdColorF col = chan_color(gm, ch, &add);
    if (k->kind != 2) {   // the colours in the child table are only the editor's placeholders
        obj_hsv(gm, k->kind == 1 ? o->hsv2 : o->hsv1, &col);
        if (gm->npu) group_pulses(gm, o, &col, k->kind == 1 ? 2 : 1);
    }
    if (o->lighter) {   // highlight and shadow only brighten or darken what lies below them
        if (k->kind == 1 && ch == CH_LIGHTER) { col = (GdColorF){255, 255, 255, 0.3f}; add = 0; }
        else if (k->kind == 2) col.a *= 0.3f;
    }
    col.a *= oa;
    gfx_frame(g, f, cx + wx * scale, cy - wy * scale, o->rot + k->rot,
              o->sx * k->sx * (k->flipx ? -1.0f : 1.0f) * s, o->sy * k->sy * (k->flipy ? -1.0f : 1.0f) * s, col, (int)add);
}

static void draw_portal_fx(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, float oa);

static int beast_index(int id)
{
    return id == 918 ? AN_BEAST1 : id == 1327 ? AN_BEAST2 : id == 1328 ? AN_BEAST3
         : id == 1584 ? AN_BEAST4 : id == 2012 ? AN_BEAST5 : -1;
}

// Animated monsters: parts in the object colour (black unless the level names one), white eyes,
// a glow in the light background colour.
static void draw_beast(Game *gm, Gfx *g, const Obj *o, int which, float cx, float cy, float s, float oa)
{
    const float near = o->x - gm->p.x;
    const int bite = which == AN_BEAST1 && near > -40.0f && near < 260.0f && gm->anim[AN_BITE].nframes;
    const Anim *a = &gm->anim[bite ? AN_BITE : which];
    const int fr = (int)(gm->anim_t * (bite ? 20.0f : 12.0f) + o->x * 0.01f) % a->nframes;
    const float unit = s * g->atlas.px_per_unit;
    const float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    const float turn = (o->sx < 0) != (o->sy < 0) ? -1.0f : 1.0f;
    float add;
    GdColorF lit = chan_color(gm, 1007, &add), body = {0, 0, 0, 1};
    if (o->ch_main != CH_OBJ && o->ch_main != CH_BLACK) {
        body = chan_color(gm, o->ch_main, &add);
        lit = body;
    }
    lit.a *= oa;
    body.a = oa;
    const GdColorF white = {255, 255, 255, oa};
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < a->nparts[fr]; i++) {
            const AnimPart *p = &a->part[fr][i];
            // eyes are the small parts of the second and third monster and the last of the first
            const int eye = ((which == AN_BEAST2 || which == AN_BEAST3) && p->z > 0) ||
                            (which == AN_BEAST4 && p->f->srcw < 16) || (which == AN_BEAST5 && p->z > 1);
            const float dx = p->x * o->sx, dy = p->y * o->sy;
            const float x = cx + (dx * c + dy * sn) * unit, y = cy - (-dx * sn + dy * c) * unit;
            const float rot = o->rot + p->rot * turn;
            if (pass == 0) {
                if (p->glow && !g->no_glow) gfx_frame(g, p->glow, x, y, rot, o->sx * p->sx * s, o->sy * p->sy * s, lit, 1);
            } else {
                gfx_frame(g, p->f, x, y, rot, o->sx * p->sx * s, o->sy * p->sy * s, eye ? white : body, 0);
            }
        }
    if (which == AN_BEAST1) {   // its eye is not part of the animation: it sits on the upper jaw
        const GdFrame *e = gfx_find(g, "GJBeast01_03_001.png");
        const AnimPart *head = NULL;
        for (int i = 0; i < a->nparts[fr]; i++)
            if (!head || a->part[fr][i].z > head->z) head = &a->part[fr][i];
        if (e && head) {
            const float dx = (head->x - 4.0f) * o->sx, dy = (head->y + 9.0f) * o->sy;
            gfx_frame(g, e, cx + (dx * c + dy * sn) * unit, cy - (-dx * sn + dy * c) * unit, o->rot,
                      o->sx * s, o->sy * s, white, 0);
        }
    }
}

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
    // saw blades glow in the light background colour, everything else in its own colour
    GdColorF col = chan_color(gm, o->spin != 0 ? 1007 : o->ch_main, &add);
    if (o->ch_main == CH_BLACK && o->spin == 0) return;
    col.a *= oa;
    const float rot = o->rot + o->spin * gm->anim_t;
    gfx_frame(g, o->glow, cx, cy, rot, o->sx * s, o->sy * s, col, 1);
    if (o->kidset < 0 || !o->main) return;
    // objects built from mirrored copies of one sprite mirror their glow the same way
    const KidSet *ks = &gm->kidsets[o->kidset];
    const float r = rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r), unit = s * g->atlas.px_per_unit;
    for (int i = 0; i < ks->n; i++) {
        const GdChild *k = &ks->c[i];
        if (ks->f[i] != o->main || k->pw <= 0) continue;
        const float dx = (k->x - k->pw * 0.5f) * o->sx, dy = (k->y - k->ph * 0.5f) * o->sy;
        gfx_frame(g, o->glow, cx + (dx * c + dy * sn) * unit, cy - (-dx * sn + dy * c) * unit, rot + k->rot,
                  o->sx * k->sx * (k->flipx ? -1.0f : 1.0f) * s, o->sy * k->sy * (k->flipy ? -1.0f : 1.0f) * s, col, 1);
    }
}

// Small squares around orbs (drifting inwards) and above pads (rising), in their colour.
static void draw_sparks(const Game *gm, Gfx *g, const Obj *o, float cx, float cy, float unit, float oa)
{
    float cr = 255, cg = 255, cb = 255;
    switch (o->sub) {
    case K_YELLOW: cr = 255; cg = 175; cb = 50; break;
    case K_PINK: cr = 255; cg = 100; cb = 255; break;
    case K_BLUE: cr = 60; cg = 220; cb = 255; break;
    case K_GREEN: case K_DASH: cr = 80; cg = 255; cb = 90; break;
    case K_RED: cr = 255; cg = 70; cb = 60; break;
    case K_DASH_FLIP: cr = 255; cg = 90; cb = 220; break;
    case K_SPIDER: cr = 170; cg = 80; cb = 255; break;
    default: break;
    }
    const int pad = o->type == OT_PAD, n = g->no_particles ? 0 : 18;
    const float r = o->rot * 3.14159265f / 180.0f, c = cosf(r), sn = sinf(r);
    for (int i = 0; i < n; i++) {
        const float life = gm->anim_t * (pad ? 1.9f : 1.6f) + (float)i / n + o->x * 0.0113f;
        const float ph = life - floorf(life);
        // a new place for every life of a spark
        const float seed = sinf((floorf(life) * 7.31f + i * 12.9898f + o->x * 0.071f)) * 43758.5453f;
        const float rnd = seed - floorf(seed);
        float dx, dy;
        if (pad) {
            dx = (rnd - 0.5f) * 26.0f;
            dy = (-2.0f + ph * (14.0f + 14.0f * rnd)) * (o->sy < 0 ? -1.0f : 1.0f);
        } else {
            const float ang = rnd * 6.2831853f, dist = 20.0f - 9.0f * ph;
            dx = cosf(ang) * dist;
            dy = sinf(ang) * dist;
        }
        const float size = (2.0f + 2.8f * rnd) * (1.0f - 0.5f * ph) * unit;
        GdColorF col = {cr, cg, cb, oa * g->fade * fminf(1.0f, 1.25f * sinf(ph * 3.14159265f))};
        float x = cx + (dx * c + dy * sn) * unit;
        const float y = cy - (-dx * sn + dy * c) * unit;
        gfx_glow_fill(g, (SDL_FRect){x - size * 0.5f, y - size * 0.5f, size, size}, col);
    }
}

// The original sizes some decorations with the loudness of the song: 1 on a beat, dying away
// until the next one. Without music a steady 140 bpm beat stands in.
static float music_pulse(const Game *gm)
{
    if (gm->pulse >= 0) return gm->pulse;
    return expf(-4.0f * fmodf(gm->anim_t * (140.0f / 60.0f), 1.0f));
}

static void draw_obj_layer(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, int pass)
{
    float add;
    float oa = 1.0f;
    Obj shaped;
    if (o->spin != 0 || o->beat) {   // rotating saw blades and decorations that jump with the music
        shaped = *o;
        shaped.rot += o->spin * gm->anim_t;
        if (o->beat) {
            float k = o->beat == 1 ? 0.25f + 0.65f * music_pulse(gm) : 0.65f + 0.45f * music_pulse(gm);
            // orbs also shrink while the music is quiet (in the original the first orb of Dash is small)
            if (o->beat == 2 && gm->loud >= 0) k *= fminf(0.7f + 2.0f * gm->loud, 1.0f);
            shaped.sx *= k;
            shaped.sy *= k;
        }
        o = &shaped;
    }
    for (int k = 0; k < o->ngrp; k++) oa *= gm->galpha[o->grp[k]];
    if (oa <= 0.003f) return;
    if (gm->draw_under && (o->kidset < 0 || o->type == OT_PORTAL || (o->id >= 920 && o->id <= 925))) return;
    const int beast = beast_index(o->id);
    if (beast >= 0 && gm->anim && gm->anim[beast].nframes) {
        draw_beast(gm, g, o, beast, cx, cy, s, oa);
        return;
    }
    Obj frame;
    if (o->effect) {   // 2.2 effects: sprite in the first colour, "_color" core in the second
        const Effect *e = &gm->effect[o->effect - 1];
        const float length = 0.05f / o->fx_speed;   // seconds per frame
        int fr;
        if (o->fx_trigger) {
            if (o->fx_run != gm->run) return;
            fr = (int)((gm->anim_t - o->fx_at) / length);
            if (fr < 0 || fr >= e->n) return;
        } else {
            fr = (int)(gm->anim_t / length + o->x * 0.37f + o->y * 0.11f) % e->n;
            if (fr < 0) fr += e->n;
        }
        frame = *o;
        frame.main = e->main[fr];
        frame.detail = e->color[fr];
        if (o->id == 2047) {   // the burst: a recording of the original shows the colours exchanged
            frame.ch_main = o->ch_detail;
            frame.ch_detail = o->ch_main;
            frame.hsv1 = o->hsv2;
            frame.hsv2 = o->hsv1;
        }
        frame.kidset = -1;
        o = &frame;
    }
    GdColorF col = chan_color(gm, o->ch_main, &add);
    if (o->id >= 920 && o->id <= 925) {   // fires cycle through their frames
        for (int i = 0; i < gm->nloops; i++) {
            if (gm->loops[i].id != o->id || o->kidset < 0 || o->clipw) continue;
            const int fr = (int)(gm->anim_t * 14.0f + o->x * 0.07f) % gm->loops[i].n;
            const KidSet *ks = &gm->kidsets[o->kidset];
            const GdFrame *kf = gm->loops[i].kid[fr];
            for (int k = 0; k < ks->n; k++)
                if (ks->c[k].z < 0) draw_kid(gm, g, o, &ks->c[k], kf ? kf : ks->f[k], cx, cy, s, o->main, oa);
            if (o->hsv1) obj_hsv(gm, o->hsv1, &col);
            col.a *= oa;
            gfx_frame(g, gm->loops[i].main[fr], cx, cy, o->rot, o->sx * s, o->sy * s, col, (int)add);
            for (int k = 0; k < ks->n; k++)
                if (ks->c[k].z >= 0) draw_kid(gm, g, o, &ks->c[k], kf ? kf : ks->f[k], cx, cy, s, o->main, oa);
            return;
        }
    }
    if (o->hsv1) obj_hsv(gm, o->hsv1, &col);
    if (gm->npu) group_pulses(gm, o, &col, 1);
    if (gm->tinted) motion_tint(gm, &col);
    col.a *= oa;
    if (o->fill) add = 0;
    if (o->faint && o->ch_main == CH_BLACK) col.a *= 0.3f;
    if (o->type == OT_PORTAL) {
        if (pass == 0 && o->back) gfx_frame(g, o->back, cx, cy, o->rot, o->sx * s, o->sy * s, col, 0);
        if (pass == 1 && o->main) gfx_frame(g, o->main, cx, cy, o->rot, o->sx * s, o->sy * s, col, 0);
        if (pass == 1) draw_portal_fx(gm, g, o, cx, cy, s, oa);
        return;
    }
    if (o->kidset >= 0) {
        const KidSet *ks = &gm->kidsets[o->kidset];
        for (int k = 0; k < ks->n; k++) {
            // the colour sprite of two-colour objects (not the parts of one-colour objects)
            const int under = ks->c[k].z < 0 && ks->c[k].kind == 1 && o->two_colour;
            if (ks->c[k].z < 0 && under == gm->draw_under)
                draw_kid(gm, g, o, &ks->c[k], ks->f[k], cx, cy, s, o->main, oa);
        }
        if (gm->draw_under) return;
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
        const float size = 0.25f + 0.65f * music_pulse(gm);
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
    if (o->type == OT_ORB || o->type == OT_PAD) draw_sparks(gm, g, o, cx, cy, s * g->atlas.px_per_unit, oa);
    if (o->detail) {
        GdColorF dc = chan_color(gm, o->ch_detail, &add);
        if (o->hsv2) obj_hsv(gm, o->hsv2, &dc);
        if (gm->npu) group_pulses(gm, o, &dc, 2);
        if (gm->tinted) motion_tint(gm, &dc);
        dc.a *= oa;
        gfx_frame(g, o->detail, cx, cy, o->rot, o->sx * s, o->sy * s, dc, (int)add);
    }
}

// the tab on the exit side of a portal with the symbol of the form (the symbol stays upright)
static void draw_portal_tab(const Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s)
{
    GdColorF w = {255, 255, 255, 1};
    for (int k = 0; k < o->ngrp; k++) w.a *= gm->galpha[o->grp[k]];
    const float r = o->rot * 3.14159265f / 180.0f, off = 17.4f * (o->sx < 0 ? -1.0f : 1.0f) * s * g->atlas.px_per_unit;
    const float x = cx + cosf(r) * off, y = cy + sinf(r) * off;
    gfx_frame(g, o->fx1, x, y, o->rot, o->sx * s, o->sy * s, w, 0);
    if (o->fx2) gfx_frame(g, o->fx2, x, y, 0, fabsf(o->sx) * s, fabsf(o->sy) * s, w, 0);
}

// Portal decoration: dots drawn into the portal.
static void draw_portal_fx(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s, float oa)
{
    float pr, pg, pb;
    portal_rgb(o->sub, &pr, &pg, &pb);
    if (o->id == 749) { pr = 255; pg = 160; pb = 20; }
    GdColorF c = {pr, pg, pb, oa};
    const float t = gm->anim_t, ppu = g->atlas.px_per_unit, w2s = s * ppu;
    const float flip = o->sx < 0 ? -1.0f : 1.0f;
    // dots drifting into the portal
    const float hh = o->hh > 8 ? o->hh : 30.0f;
    for (int i = 0; i < 10 && !g->no_particles; i++) {
        float ph = fmodf(t * 0.8f + i / 10.0f + o->x * 0.0071f, 1.0f);
        float ang = i * 2.399f + o->x * 0.37f;
        float r = 1.0f - ph;
        float dx = cosf(ang) * 34.0f * r * flip, dy = sinf(ang) * hh * r;
        float sz = 5.0f * (0.5f + 0.5f * r) * w2s;
        GdColorF d = c;
        d.a *= sinf(ph * 3.14159265f);
        gfx_glow_fill(g, (SDL_FRect){cx + dx * w2s - sz / 2, cy - dy * w2s - sz / 2, sz, sz}, d);
    }
}

// chosen icon of a form (the bot plays without a skin)
static int icon_id(const Game *gm, int form) { return gm->icon[form] > 0 ? gm->icon[form] : 1; }

static void draw_icon(Game *gm, Gfx *g, const Player *p, float px, float py, float rot, float s,
                      float scale, int swap)
{
    float add;
    GdColorF c1 = chan_color(gm, swap ? CH_P2 : CH_P1, &add), c2 = chan_color(gm, swap ? CH_P1 : CH_P2, &add);
    c1.a = c2.a = 1;
    const float mini = p->mini ? 0.6f : 1.0f;
    const float k = mini * s;
    const float flip = (float)p->grav;
    rot += atan2f((float)gm->frame.ux, (float)gm->frame.uy) * 180.0f / 3.14159265f;   // turned gameplay
    if ((p->mode == M_ROBOT || p->mode == M_SPIDER) && gm->anim) {
        const int robot = p->mode == M_ROBOT;
        const int which = p->on_ground ? (robot ? AN_ROBOT_RUN : AN_SPIDER_RUN)
                        : !robot ? AN_SPIDER_FALL : p->rising ? AN_ROBOT_JUMP : AN_ROBOT_FALL;
        const Anim *a = &gm->anim[which];
        if (a->nframes) {
            const int fr = (int)(gm->anim_t * (robot ? 28.0f : 20.0f)) % a->nframes;
            anim_draw_icon(g, a, fr, !robot, icon_id(gm, robot ? IC_ROBOT : IC_SPIDER), px, py, rot,
                           scale * mini, k, flip, c1, c2);
            return;
        }
    }
#define DRAW(form, x, y, r, sx, sy) \
    gfx_icon_draw(g, g_icon_types[form], icon_id(gm, form), x, y, r, sx, sy, c1, c2)
    // Ship and UFO as in the original: the little cube sits 5 units above the player's centre, the
    // ship 5 and the UFO 7 units below it, along the vehicle's own "up".
    const float a = rot * 3.14159265f / 180.0f, unit = scale * mini * flip;
    const float upx = sinf(a) * unit, upy = -cosf(a) * unit;
    switch (p->mode) {
    case M_SHIP:
        DRAW(IC_CUBE, px + upx * 5.0f, py + upy * 5.0f, rot, k * 0.55f, k * 0.55f * flip);
        DRAW(IC_SHIP, px - upx * 5.0f, py - upy * 5.0f, rot, k, k * flip);
        break;
    case M_UFO:
        DRAW(IC_CUBE, px + upx * 5.0f, py + upy * 5.0f, rot, k * 0.55f, k * 0.55f * flip);
        DRAW(IC_UFO, px - upx * 7.0f, py - upy * 7.0f, rot, k, k * flip);
        break;
    case M_SWING: DRAW(IC_SWING, px, py, rot, k, k * flip); break;
    case M_BALL:  DRAW(IC_BALL, px, py, rot, k, k); break;
    case M_WAVE:  DRAW(IC_WAVE, px, py, rot, k, k); break;
    default:      DRAW(IC_CUBE, px, py, rot, k, k); break;
    }
#undef DRAW
}

// screen mapping helpers for the trail (set by game_draw)
static float g_ox, g_oy, g_vh, g_scale;
static float trail_sx(const Game *gm, float x) { return g_ox + (x - gm->cam_x) * g_scale; }
static float trail_sy(const Game *gm, float y) { return g_oy + (g_vh - (y - gm->cam_bottom)) * g_scale; }

// The streak: a soft band along the last positions that fades with their age. (hx, hy): where the
// icon is drawn right now.
static void draw_streak(Game *gm, Gfx *g, const Player *p, float hx, float hy, float scale, int swap)
{
    if (p->trail_n < 2) return;
    float add;
    const GdColorF c = chan_color(gm, swap ? CH_P1 : CH_P2, &add);
    const float half = (p->mini ? 3.0f : 5.0f) * scale;
    static const float across[4] = {-1.0f, -0.4f, 0.4f, 1.0f}, strong[4] = {0, 0.7f, 0.7f, 0};
    float px[65], py[65], age[65];
    int n = 0;
    if (p->streak) { px[n] = trail_sx(gm, hx); py[n] = trail_sy(gm, hy); age[n++] = 0; }
    for (int i = 0; i < p->trail_n; i++) {
        const int a = (p->trail_i - i - 1) & 63;
        const float x = trail_sx(gm, p->trail[a][0]), y = trail_sy(gm, p->trail[a][1]);
        if (n && p->streak && i == 0 && fabsf(x - px[0]) < 0.5f && fabsf(y - py[0]) < 0.5f) continue;
        px[n] = x; py[n] = y; age[n++] = p->streak_idle + (float)i * 2.0f * STEP;
    }
    float last_x = 0, last_y = 0, last_a = 0, nx0 = 0, ny0 = 0;
    for (int i = 0; i < n; i++) {
        const int before = i > 0 ? i - 1 : i, after = i + 1 < n ? i + 1 : i;
        float dx = px[after] - px[before], dy = py[after] - py[before];
        const float len = hypotf(dx, dy);
        const float nx = len > 0.01f ? -dy / len : nx0, ny = len > 0.01f ? dx / len : ny0;
        const float a = fmaxf(0.0f, 1.0f - age[i] / STREAK_TIME);
        if (i > 0 && (a > 0 || last_a > 0)) {
            for (int k = 0; k < 3; k++) {
                const float qx[4] = {last_x + nx0 * half * across[k], last_x + nx0 * half * across[k + 1],
                                     px[i] + nx * half * across[k + 1], px[i] + nx * half * across[k]};
                const float qy[4] = {last_y + ny0 * half * across[k], last_y + ny0 * half * across[k + 1],
                                     py[i] + ny * half * across[k + 1], py[i] + ny * half * across[k]};
                GdColorF q[4] = {c, c, c, c};
                q[0].a = last_a * strong[k]; q[1].a = last_a * strong[k + 1];
                q[2].a = a * strong[k + 1]; q[3].a = a * strong[k];
                gfx_quad_colors(g, qx, qy, q, 1);
            }
        }
        last_x = px[i]; last_y = py[i]; last_a = a; nx0 = nx; ny0 = ny;
    }
}

static GdColorF mix_colour(GdColorF a, GdColorF b, float k)
{
    k = fminf(fmaxf(k, 0.0f), 1.0f);
    return (GdColorF){a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
}

// One flame of a dash: the fire (added light) with its outline.
static void draw_flame(Gfx *g, int frame, float x, float y, float rot, float sx, float sy, GdColorF c)
{
    char name[40];
    snprintf(name, sizeof name, "playerDash2_%03d.png", frame);
    const GdFrame *fire = gfx_find(g, name);
    snprintf(name, sizeof name, "playerDash2_outline_%03d.png", frame);
    const GdFrame *line = gfx_find(g, name);
    if (fire) gfx_frame(g, fire, x, y, rot, sx, sy, c, 1);
    if (line) gfx_frame(g, line, x, y, rot, sx, sy, (GdColorF){255, 255, 255, c.a * 150.0f / 255.0f}, 0);
}

// What a dash draws behind the icon. (x, y): the icon on the screen; the other parts stay where
// they began. Sizes and timing as in the original (PlayerObject::startDashing / stopDashing).
static void draw_dash(Game *gm, Gfx *g, const Player *p, float x, float y, float scale, int swap)
{
    float add;
    GdColorF colour = chan_color(gm, swap ? CH_P1 : CH_P2, &add);
    colour.a = 1;
    const GdColorF white = {255, 255, 255, 1};
    const float s = scale / g->atlas.px_per_unit * (p->mini ? 0.6f : 1.0f);
    if (p->boom_age > 0) {
        char name[40];
        const int n = 1 + (int)(p->boom_age / 0.05f);
        snprintf(name, sizeof name, "playerDash2_boom2_%03d.png", n > 9 ? 9 : n);
        const GdFrame *f = gfx_find(g, name);
        const GdColorF orb = p->boom_flip ? (GdColorF){255, 0, 255, 1} : (GdColorF){0, 255, 0, 1};
        GdColorF c = mix_colour(white, orb, p->boom_age / 0.3f);
        c.a = fminf(1.0f, fmaxf(0.0f, 1.0f - (p->boom_age - 0.2f) / 0.3f));
        if (f) gfx_frame(g, f, trail_sx(gm, p->boom_x), trail_sy(gm, p->boom_y), p->dash_angle, 1.2f * s, 1.2f * s, c, 1);
    }
    if (p->off_age > 0) {   // shrinks to a fifth, drifts back and fades
        const float k = p->off_age / 0.2f, a = p->off_angle * 3.14159265f / 180.0f, size = 1.0f - 0.8f * k;
        GdColorF c = colour;
        c.a = 1.0f - k;
        draw_flame(g, 1, trail_sx(gm, p->off_x) - cosf(a) * 20.0f * k * scale,
                   trail_sy(gm, p->off_y) - sinf(a) * 20.0f * k * scale, p->off_angle, 0.99f * size * s,
                   0.96f * size * s, c);
    }
    if (p->dash_age > 0) {
        const float t = p->dash_age;
        // it shoots out in the first fifth of a second, overshooting in height
        float sx = 0.99f, sy = 0.96f;
        if (t < 0.1f) {
            const float k = t / 0.1f;
            sx = 0.3f + (0.891f - 0.3f) * k; sy = 0.2f + (1.152f - 0.2f) * k;
        } else if (t < 0.2f) {
            const float k = (t - 0.1f) / 0.1f;
            sx = 0.891f + (0.99f - 0.891f) * k; sy = 1.152f + (0.96f - 1.152f) * k;
        }
        draw_flame(g, 1 + (int)(t / 0.04f) % 12, x, y, p->dash_angle, sx * s, sy * s,
                   mix_colour(white, colour, (t - 0.1f) / 0.2f));
    }
}

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
static float edge_effect(const Game *gm, const Gfx *g, Obj *o, float vw, float scale, float *cx, float *cy)
{
    if (gm->enter_fx == 1915) return 1.0f;
    const int no_fade = o->no_fade || (o->solid_fill && (gm->enter_fx == 0 || gm->enter_fx == 22));
    if (no_fade && o->no_enter) return 1.0f;
    const float x = o->x - gm->cam_x;
    float t = fminf(x, vw - x) / 70.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.0f) t = 0.0f;
    const float off = (1.0f - t) * 100.0f * scale;
    switch (o->no_enter ? 0 : gm->enter_fx) {
    case 23: *cy += off; break;
    case 24: *cy -= off; break;
    case 25: *cx -= off; break;
    case 26: *cx += off; break;
    case 27: o->sx *= t; o->sy *= t; break;
    case 28: o->sx *= 1.0f + 0.75f * (1.0f - t); o->sy *= 1.0f + 0.75f * (1.0f - t); break;
    case 58: *cy += *cy < 160.0f * scale ? -off : off; break;   // the two halves of the screen drift apart
    case 59: *cy += *cy < 160.0f * scale ? off : -off; break;   // ... or come together
    default: break;
    }
    return no_fade ? 1.0f : t;
}

// Invisible blocks and spikes show clearly at the screen edges and almost vanish around the icon.
static float invisible_alpha(const Game *gm, const Obj *o)
{
    if (!o->invisible) return 1.0f;
    const float d = fabsf(o->x - (gm->p.x + 40.0f));
    return 0.05f + 0.75f * fminf(fmaxf((d - 70.0f) / 130.0f, 0.0f), 1.0f);
}

// Frame of a spinning coin; coins the player owns already show as an outline.
static const GdFrame *coin_frame(Gfx *g, int user, int owned, int n)
{
    char name[48];
    snprintf(name, sizeof name, "secretCoin_%s%s01_%03d.png", user ? "2_" : "", owned ? "b_" : "", 1 + (n & 3));
    return gfx_find(g, name);
}

// Picked up coins hop out of the level while two rings spread from the place.
static void draw_coin_fly(Game *gm, Gfx *g, float scale)
{
    for (int k = 0; k < GD_MAX_COINS; k++) {
        const CoinFly *c = &gm->coin_fly[k];
        if (c->age <= 0) continue;
        const float t = c->age, ring = t / 0.3f;
        if (ring < 1.0f) {
            const GdColorF white = c->user ? (GdColorF){200, 200, 200, 1} : (GdColorF){255, 225, 90, 1};
            for (int w = 0; w < 2; w++) {
                const float radius = (5.0f + ((w ? 60.0f : 25.0f) - 5.0f) * ring) * scale, thick = 2.5f * scale;
                const float x = trail_sx(gm, c->x), y = trail_sy(gm, c->y);
                GdColorF q = white;
                q.a = 1.0f - ring;
                const GdColorF col[4] = {q, q, q, q};
                for (int i = 0; i < 28; i++) {
                    const float a = i * 6.2831853f / 28.0f, b = (i + 1) * 6.2831853f / 28.0f;
                    const float px[4] = {x + cosf(a) * radius, x + cosf(b) * radius, x + cosf(b) * (radius - thick),
                                         x + cosf(a) * (radius - thick)};
                    const float py[4] = {y + sinf(a) * radius, y + sinf(b) * radius, y + sinf(b) * (radius - thick),
                                         y + sinf(a) * (radius - thick)};
                    gfx_quad_colors(g, px, py, col, 1);
                }
            }
        }
        const GdFrame *f = coin_frame(g, c->user, c->owned, (int)(t / 0.05f));
        const float s = scale / g->atlas.px_per_unit;
        const GdColorF col = {255, 255, 255, fminf(1.0f, fmaxf(0.0f, (COIN_FLY_TIME - t) / 0.3f))};
        if (f) gfx_frame(g, f, trail_sx(gm, c->x + c->drift * t), trail_sy(gm, c->y + 260.0f * t - 360.0f * t * t), 0,
                         s, s, col, 0);
    }
}

static void draw_obj_fx(Game *gm, Gfx *g, const Obj *o, float vw, float scale, float cx, float cy, float s, int pass)
{
    if (o->type == OT_PORTAL) { draw_obj_layer(gm, g, o, cx, cy, s, pass); return; }
    Obj e = *o;
    if (o->type == OT_COIN) {
        if (o->used) return;   // picked up
        const int k = coin_number(gm, o);
        e.main = coin_frame(g, o->id == 1329, k >= 0 && ((gm->coins_owned >> k) & 1), (int)(gm->anim_t / 0.12f));
        if (!e.main) e.main = o->main;
    }
    const float seen = motion_apply(gm, (int)(o - gm->obj), &e);
    cx += (e.x - o->x) * scale;
    cy -= (e.y - o->y) * scale;
    g->fade = edge_effect(gm, g, &e, vw, scale, &cx, &cy) * invisible_alpha(gm, o) * seen;
    draw_obj_layer(gm, g, &e, cx, cy, s, pass);
    g->fade = 1;
    gm->tinted = NULL;
}

// The ground darkens towards both screen edges. `edge` is the screen y of the ground line;
// `ceiling` draws the upside-down version.
static void ground_shade(Gfx *g, float ox, float edge, float W, float scale, int ceiling)
{
    const GdFrame *f = gfx_find(g, "groundSquareShadow_001.png");
    if (!f) return;
    const float ppu = g->atlas.px_per_unit, kx = 0.7f * scale / ppu, ky = scale / ppu;
    const float hw = f->srcw * kx * 0.5f, hh = f->srch * ky * 0.5f;
    const float y = ceiling ? edge - hh : edge + hh;
    const GdColorF c = {255, 255, 255, 100.0f / 255.0f};
    gfx_frame(g, f, ox + hw - scale, y, 0, kx, ceiling ? -ky : ky, c, 0);
    gfx_frame(g, f, ox + W - hw + scale, y, 0, -kx, ceiling ? -ky : ky, c, 0);
}

// The layers the shader effects reach, numbered as by their setup trigger: 1 background,
// 2 middleground, 3-7 the object layers behind the icon, 8 the icon, 9-12 those in front of it,
// 13 ground, 14 what is fixed to the screen.
typedef struct { int bottom, top, open, done; } ShaderSpan;

// what the object passes need to know about the picture
typedef struct { float left, right, vw, vh, scale, ox, oy, s; ShaderSpan *span; } View;

// To be called before something of layer `layer` is drawn (in rising order).
static void shader_layer(Game *gm, Gfx *g, const View *v, int layer)
{
    ShaderSpan *p = v->span;
    if (p->done) return;
    if (layer > p->top) {
        // with a lowest layer above the middleground the effects work on a layer of their own
        if (p->open) visual_shaders(gm, g, v->ox, v->oy, v->vw, v->vh, v->scale, 1);
        else if (p->bottom <= 2) visual_shaders(gm, g, v->ox, v->oy, v->vw, v->vh, v->scale, 0);
        p->done = 1;
    } else if (!p->open && p->bottom > 2 && layer >= p->bottom) {
        gfx_layer_begin(g);
        p->open = 1;
    }
}

static int in_view(const View *v, const Obj *o)
{
    return !(o->type == OT_TRIGGER || o->off || o->nodraw || o->on_screen || o->x < v->left || o->x > v->right);
}

// Objects in draw order: pass 0 the layers behind the icon (and the back halves of portals), pass 1
// those in front of it. Within one layer all colour sprites that lie below come first.
void gm_draw_object(Game *gm, Gfx *g, const Obj *o, float cx, float cy, float s)
{
    draw_obj_layer(gm, g, o, cx, cy, s, 0);
    if (o->type == OT_PORTAL) draw_obj_layer(gm, g, o, cx, cy, s, 1);
}

static void draw_layers(Game *gm, Gfx *g, const View *v, int front)
{
    for (int k = 0, end; k < gm->n; k = end) {
        const int layer = gm->obj[gm->by_z[k]].zlayer;
        for (end = k + 1; end < gm->n && gm->obj[gm->by_z[end]].zlayer == layer; end++) {}
        // z layers -5, -3, .. 3 lie behind the icon, 5 .. 11 in front of it
        const int behind = (layer + 5) / 2 + 3, before = (layer - 5) / 2 + 9;
        if (front) { if (layer >= 5) shader_layer(gm, g, v, before < 12 ? before : 12); }
        else if (layer < 5) shader_layer(gm, g, v, behind < 3 ? 3 : behind > 7 ? 7 : behind);
        for (gm->draw_under = 1; gm->draw_under >= 0; gm->draw_under--)
            for (int j = k; j < end; j++) {
                const Obj *o = &gm->obj[gm->by_z[j]];
                if (!in_view(v, o)) continue;
                const float x = v->ox + (o->x - gm->cam_x) * v->scale;
                const float y = v->oy + (v->vh - (o->y - gm->cam_bottom)) * v->scale;
                if (o->type == OT_PORTAL) draw_obj_layer(gm, g, o, x, y, v->s, front);
                else if ((o->zlayer >= 5) == front) draw_obj_fx(gm, g, o, v->vw, v->scale, x, y, v->s, 0);
            }
    }
    gm->draw_under = 0;
}

// Turns the picture of the following camera into the one the camera triggers ask for: zoom, offset,
// edges and static camera, all around the middle of the view.
static void camera_apply(Game *gm, float *vw, float *vh, float *scale)
{
    const Camera *v = &gm->view;
    const float zoom = fmaxf(trg_tween(&v->zoom), 0.05f);
    const float offx = trg_tween(&v->offx) - trg_tween(&v->shiftx), offy = trg_tween(&v->offy) - trg_tween(&v->shifty);
    int moved = zoom != 1.0f || offx != 0 || offy != 0;
    for (int i = 0; i < 2; i++) moved |= trg_tween(&v->hold[i]) > 0;
    for (int i = 0; i < 4; i++) moved |= trg_tween(&v->bound[i]) > 0;
    if (!moved) return;
    float c[2] = {gm->cam_x + *vw * 0.5f + offx, gm->cam_bottom + *vh * 0.5f + offy};
    *vw /= zoom;
    *vh /= zoom;
    *scale *= zoom;
    for (int side = 0; side < 4; side++) {   // left, right, up, down
        const float w = trg_tween(&v->bound[side]);
        const Obj *o = trg_first(gm, v->edge[side]);
        if (w <= 0 || !o) continue;
        const int axis = side >= 2, low = side == 0 || side == 3;
        const float half = 0.5f * (axis ? *vh : *vw);
        const float limit = (axis ? o->y : o->x) + (low ? half : -half);
        if (low ? c[axis] < limit : c[axis] > limit) c[axis] += (limit - c[axis]) * w;
    }
    for (int axis = 0; axis < 2; axis++) {
        const float w = trg_tween(&v->hold[axis]);
        if (w > 0) c[axis] += (trg_static_pos(gm, axis) - c[axis]) * w;
    }
    gm->cam_x = c[0] - *vw * 0.5f;
    gm->cam_bottom = c[1] - *vh * 0.5f;
}

void game_screen_pos(const Game *gm, float x, float y, float *sx, float *sy)
{
    float px = gm->pic.ox + (x - gm->pic.x) * gm->pic.scale;
    float py = gm->pic.oy + (gm->pic.vh - (y - gm->pic.bottom)) * gm->pic.scale;
    if (gm->pic.mirror) px = gm->pic.ox * 2 + gm->pic.w - px;
    const float mx = gm->pic.ox + gm->pic.w * 0.5f, my = gm->pic.oy + gm->pic.vh * gm->pic.scale * 0.5f;
    *sx = mx + (px - mx) * gm->pic.turn_cos - (py - my) * gm->pic.turn_sin;
    *sy = my + (px - mx) * gm->pic.turn_sin + (py - my) * gm->pic.turn_cos;
}

// The gradients shown on the layers from..to (1 background, 2 middleground, 3-7 behind the icon,
// 8 icon, 9-12 in front of it, 13 ground, above that everything).
static void draw_gradients(Game *gm, Gfx *g, const View *v, float margin, int from, int to)
{
    for (int id = 0; id < GD_MAX_GRADIENTS; id++) {
        const int at = gm->grad[id] - 1;
        if (at < 0 || at >= gm->ngradients) continue;
        const Gradient *d = &gm->gradients[at];
        if (d->layer < from || d->layer > to) continue;
        const Obj *t = &gm->obj[d->obj];
        float alpha = 1;
        for (int k = 0; k < t->ngrp; k++) alpha *= gm->galpha[t->grp[k]];
        if (alpha <= 0.003f) continue;
        const float extra = margin / v->scale;
        float x[4], y[4];   // top left, top right, bottom right, bottom left in the level
        if (d->vertex) {   // the groups mark: bottom left and right, top left and right
            static const int group[4] = {2, 3, 1, 0};
            int all = 1;
            for (int i = 0; i < 4; i++) {
                const Obj *o = trg_first(gm, d->side[group[i]]);
                if (o) { x[i] = o->x; y[i] = o->y; } else all = 0;
            }
            if (!all) continue;
        } else {
            const Obj *up = trg_first(gm, d->side[0]), *down = trg_first(gm, d->side[1]);
            const Obj *left = trg_first(gm, d->side[2]), *right = trg_first(gm, d->side[3]);
            const float l = left ? left->x : gm->cam_x - extra, r = right ? right->x : gm->cam_x + v->vw + extra;
            const float top = up ? up->y : gm->cam_bottom + v->vh + extra;
            const float low = down ? down->y : gm->cam_bottom - extra;
            x[0] = x[3] = l; x[1] = x[2] = r;
            y[0] = y[1] = top; y[2] = y[3] = low;
        }
        float add;
        const GdColorF a = chan_color(gm, d->ch1 > 0 ? d->ch1 : CH_WHITE, &add);
        const GdColorF b = chan_color(gm, d->ch2 > 0 ? d->ch2 : CH_WHITE, &add);
        // the first colour fades into the second along the trigger's direction (unturned: to the right)
        const float dir = -d->rot * 3.14159265f / 180.0f, dx = cosf(dir), dy = sinf(dir);
        float px[4], py[4];
        GdColorF c[4];
        for (int i = 0; i < 4; i++) {
            const float u = i == 1 || i == 2 ? 1.0f : -1.0f, w = i < 2 ? 1.0f : -1.0f;
            const float k = fminf(fmaxf(0.5f + 0.5f * (u * dx + w * dy), 0.0f), 1.0f);
            c[i] = (GdColorF){a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k,
                              (a.a + (b.a - a.a) * k) * alpha};
            px[i] = v->ox + (x[i] - gm->cam_x) * v->scale;
            py[i] = v->oy + (v->vh - (y[i] - gm->cam_bottom)) * v->scale;
        }
        gfx_quad_colors(g, px, py, c, d->blend >= 0 && d->blend <= 3 ? d->blend : 0);
    }
}

void game_draw(Game *gm, Gfx *g, float vw, float vh, float scale, float ox, float oy,
               const GfxImage *bg, const GfxImage *ground)
{
    const float ppu = g->atlas.px_per_unit;
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
    // 6 % of the way per 60th of a second, whatever the frame rate
    float since = gm->anim_t - gm->mir_t;
    if (since < 0 || since > 0.25f) since = 1.0f / 60.0f;
    gm->mir_t = gm->anim_t;
    gm->mir_x += (target - gm->mir_x) * (1.0f - powf(0.94f, since * 60.0f));
    gm->cam_x = ix - (gm->mirror ? vw - gm->mir_x : gm->mir_x);
    // at the start the camera waits until the icon has reached its place
    if (!gm->mirror && gm->frame.fx > 0 && gm->cam_x < 0) gm->cam_x = 0;
    // the following camera stays as it is; what the picture shows instead is only used below
    const float follow_x = gm->cam_x, follow_bottom = gm->cam_bottom, base_scale = scale;
    camera_apply(gm, &vw, &vh, &scale);
    const float s = scale / ppu; // screen px per sheet px
    if (gm->shake_left > 0 && !g->no_shake) {
        const float every = gm->shake_every > 0.001f ? gm->shake_every : 1.0f / 60.0f;
        const float jolt = floorf(gm->anim_t / every);
        gm->cam_x += sinf(jolt * 91.7f) * gm->shake_amp;
        gm->cam_bottom += sinf(jolt * 47.3f + 2.0f) * gm->shake_amp;
    }
    // camera rotation: everything is drawn turned, so more of the level shows towards the corners
    const float turn = trg_tween(&gm->view.turn) * 3.14159265f / 180.0f;
    float margin = 0;
    g->turned = fabsf(turn) > 0.0002f;
    if (g->turned) {
        g->turn_x = ox + W * 0.5f;
        g->turn_y = oy + H * 0.5f;
        g->turn_cos = cosf(turn);
        g->turn_sin = sinf(turn);
        margin = ceilf(0.5f * (hypotf(W, H) - H));
    }
    gm->pic.x = gm->cam_x; gm->pic.bottom = gm->cam_bottom; gm->pic.vh = vh; gm->pic.scale = scale;
    gm->pic.ox = ox; gm->pic.oy = oy; gm->pic.w = W; gm->pic.mirror = gm->mirror;
    gm->pic.turn_cos = g->turned ? g->turn_cos : 1; gm->pic.turn_sin = g->turned ? g->turn_sin : 0;
    g_ox = ox; g_oy = oy; g_vh = vh; g_scale = scale;
    SDL_Rect clip = {(int)ox, (int)oy, (int)W, (int)H};
    gfx_set_clip(g, &clip);
    g->mirror = gm->mirror;
    g->mir_w = ox * 2 + W;

    float add;
    GdColorF bgc = chan_color(gm, CH_BG, &add);
    bgc.a = 1;
    gfx_fill(g, (SDL_FRect){ox - margin, oy - margin, W + 2 * margin, H + 2 * margin}, bgc);
    if (bg && bg->tex) {
        // as in the original: much taller than the view (1024 / 1.875 units), bottom aligned; the
        // zoom does not change its size
        const float step = 1024.0f / 1.875f * base_scale;
        float off = fmodf((gm->cam_x * gm->view.bg_mx - 1.6f) * base_scale, step);
        if (off < 0) off += step;
        for (float x = -off - ceilf(margin / step) * step; x < W + margin; x += step) {
            gfx_image_rect(g, bg, (SDL_FRect){0, 0, (float)bg->w, (float)bg->h},
                           (SDL_FRect){ox + x, oy + H - step + 0.4f * base_scale, step, step}, bgc);
        }
    }

    // gradients on the background layer, then the middleground
    // world -> screen
    #define SX(x) (ox + ((x) - gm->cam_x) * scale)
    #define SY(y) (oy + (vh - ((y) - gm->cam_bottom)) * scale)

    // glow of all visible objects first, so that it only shows around them
    float view_l = gm->cam_x - 120 - margin / scale, view_r = gm->cam_x + vw + 120 + margin / scale;
    for (int k = 0; k < gm->n && !g->no_glow; k++) {
        const Obj *o = &gm->obj[gm->by_z[k]];
        if (!o->glow || o->off || o->nodraw || o->on_screen || o->x < view_l || o->x > view_r) continue;
        Obj e = *o;
        float ex = SX(o->x), ey = SY(o->y);
        const float seen = motion_apply(gm, gm->by_z[k], &e);
        ex += (e.x - o->x) * scale;
        ey -= (e.y - o->y) * scale;
        g->fade = edge_effect(gm, g, &e, vw, scale, &ex, &ey) * invisible_alpha(gm, o) * seen;
        draw_glow(gm, g, &e, ex, ey, s);
        g->fade = 1;
        gm->tinted = NULL;
    }

    ShaderSpan span = {gm->view.shader_bottom, gm->view.shader_top > 0 ? gm->view.shader_top : 14, 0, 0};
    const View v = {view_l, view_r, vw, vh, scale, ox, oy, s, &span};

    motion_areas(gm, gm->cam_x - margin / scale, gm->cam_x + vw + margin / scale);
    draw_gradients(gm, g, &v, margin, 0, 1);
    shader_layer(gm, g, &v, 2);
    visual_middleground(gm, g, ox, oy, W, H, margin, scale);
    draw_gradients(gm, g, &v, margin, 2, 2);
    // pass 0: back layers
    draw_layers(gm, g, &v, 0);
    visual_particles(gm, g, 0, ox, oy, vw, vh, scale);
    draw_gradients(gm, g, &v, margin, 3, 8);

    shader_layer(gm, g, &v, 8);
    // images of the ghost trail, behind the icon and shrinking as they fade
    for (int i = 0; i < GD_GHOSTS; i++) {
        const Ghost *q = &gm->ghost[i];
        if (q->age <= 0 || gm->view.hidden) continue;
        // the main layer of the form, in the first colour, added onto the picture
        const int form = q->mode == M_SHIP ? IC_SHIP : q->mode == M_BALL ? IC_BALL : q->mode == M_UFO ? IC_UFO
                       : q->mode == M_WAVE ? IC_WAVE : q->mode == M_SWING ? IC_SWING : IC_CUBE;
        const GdFrame *f = gfx_icon_frame(g, g_icon_types[form], icon_id(gm, form), "");
        if (!f) continue;
        const float left = 1.0f - q->age / 0.4f, k = s * (q->mini ? 0.6f : 1.0f) * (0.6f + 0.4f * left);
        GdColorF c = chan_color(gm, CH_P1, &add);
        c.a = 0.75f * left;
        gfx_frame(g, f, SX(q->x), SY(q->y), q->rot, k, k * q->grav, c, 1);
    }

    // player
    if (gm->view.hidden) {
        // a trigger has hidden the icon
    } else if (!gm->p.dead) {
        const int show1 = !(gm->logic.options & OPT_HIDE_P1), show2 = gm->dual && !(gm->logic.options & OPT_HIDE_P2);
        if (show1) draw_trail(gm, g, &gm->p, scale, 0);
        if (show2) draw_trail(gm, g, &gm->p2, scale, 1);
        if (show1 && gm->p.mode != M_WAVE) draw_streak(gm, g, &gm->p, ix, iy, scale, 0);
        if (show2 && gm->p2.mode != M_WAVE) draw_streak(gm, g, &gm->p2, gm->p2.x, gm->p2.y, scale, 1);
        if (show2) draw_dash(gm, g, &gm->p2, SX(gm->p2.x), SY(gm->p2.y), scale, 1);
        if (show1) draw_dash(gm, g, &gm->p, SX(ix), SY(iy), scale, 0);
        if (show2) draw_icon(gm, g, &gm->p2, SX(gm->p2.x), SY(gm->p2.y), gm->p2.rot, s, scale, 1);
        if (show1) draw_icon(gm, g, &gm->p, SX(ix), SY(iy), irot, s, scale, 0);
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
        for (int i = 0; i < gm->npt && !g->no_particles; i++) {
            const Particle *p = &gm->pt[i];
            float k = p->life / p->max;
            GdColorF pc = {p->r, p->g, p->b, p->a * k};
            float sz = p->size * scale;
            if (p->square) {
                pc.a = p->a * fminf(1.0f, k * 1.8f);
                gfx_glow_fill(g, (SDL_FRect){SX(p->x) - sz / 2, SY(p->y) - sz / 2, sz, sz}, pc);
                continue;
            }
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
    draw_layers(gm, g, &v, 1);
    draw_coin_fly(gm, g, scale);
    // the tabs lie above all portals (overlapping ones would hide them otherwise)
    for (int k = 0; k < gm->n; k++) {
        const Obj *o = &gm->obj[gm->by_z[k]];
        if (o->type == OT_PORTAL && o->fx1 && in_view(&v, o)) draw_portal_tab(gm, g, o, SX(o->x), SY(o->y), s);
    }
    visual_particles(gm, g, 1, ox, oy, vw, vh, scale);
    draw_gradients(gm, g, &v, margin, 9, 12);
    shader_layer(gm, g, &v, 13);
    // ground and ceiling cover whatever reaches into them (fixed tile size: it must not stretch
    // with the camera height)
    GdColorF gc = chan_color(gm, CH_GROUND, &add);
    gc.a = 1;
    float tile = (ground && ground->tex) ? ground->w / ppu * scale : 128.0f * scale;
    float gy = SY(gm->ground_y + 0.6f);       // the ground reaches slightly above the physics floor
    const float beyond = ceilf(margin / tile) * tile;   // further tiles for a turned picture
    const int no_ground = (gm->logic.options & OPT_HIDE_GROUND) != 0;
    if (gy < oy + H + margin && !no_ground) {
        float off = fmodf((gm->cam_x - 15.0f) * scale, tile);   // tiles start half a block in
        if (off < 0) off += tile;
        if (ground && ground->tex) {
            for (float y = gy; y < oy + H + margin; y += tile)
                for (float x = -off - beyond; x < W + margin; x += tile)
                    gfx_image_rect(g, ground, (SDL_FRect){0, 0, (float)ground->w, (float)ground->h},
                                   (SDL_FRect){ox + x, y, tile, tile}, gc);
        } else {
            gfx_fill(g, (SDL_FRect){ox - margin, gy, W + 2 * margin, oy + H + margin - gy}, gc);
        }
        const GfxImage *g2 = gm->ground2;
        if (g2 && g2->tex) {
            GdColorF c2 = chan_color(gm, CH_GROUND2, &add);
            c2.a = 1;
            for (float x = -off - beyond; x < W + margin; x += tile)
                gfx_image_rect(g, g2, (SDL_FRect){0, 0, (float)g2->w, (float)g2->h},
                               (SDL_FRect){ox + x, gy, tile, tile * g2->h / g2->w}, c2);
        }
        ground_shade(g, ox - margin, gy, W + 2 * margin, scale, 0);
        const GdFrame *line = gfx_find(g, "floorLine_01_001.png");
        if (line) {
            GdColorF lc = chan_color(gm, CH_LINE, &add);
            gfx_frame(g, line, ox + W * 0.5f, gy, 0, (W + 2 * margin) / (float)line->w, s, lc, 1);
        }
    }
    if (gm->has_ceil && !no_ground) {
        float cy = SY(gm->ceil_y - 0.6f);
        if (cy > oy - margin) {
            float off = fmodf((gm->cam_x - 15.0f) * scale, tile);
            if (off < 0) off += tile;
            if (ground && ground->tex) {
                for (float y = cy; y > oy - tile - margin; y -= tile)
                    for (float x = -off - beyond; x < W + margin; x += tile)
                        gfx_image_rect(g, ground, (SDL_FRect){0, (float)ground->h, (float)ground->w, -(float)ground->h},
                                       (SDL_FRect){ox + x, y - tile, tile, tile}, gc);
            } else {
                gfx_fill(g, (SDL_FRect){ox - margin, oy - margin, W + 2 * margin, cy - oy + margin}, gc);
            }
            const GfxImage *g2 = gm->ground2;
            if (g2 && g2->tex) {
                GdColorF c2 = chan_color(gm, CH_GROUND2, &add);
                c2.a = 1;
                for (float x = -off - beyond; x < W + margin; x += tile)
                    gfx_image_rect(g, g2, (SDL_FRect){0, (float)g2->h, (float)g2->w, -(float)g2->h},
                                   (SDL_FRect){ox + x, cy - tile * g2->h / g2->w, tile, tile * g2->h / g2->w}, c2);
            }
            ground_shade(g, ox - margin, cy, W + 2 * margin, scale, 1);
            const GdFrame *line = gfx_find(g, "floorLine_01_001.png");
            if (line) {
                GdColorF lc = chan_color(gm, CH_LINE, &add);
                gfx_frame(g, line, ox + W * 0.5f, cy, 0, (W + 2 * margin) / (float)line->w, -s, lc, 1);
            }
        }
    }

    draw_gradients(gm, g, &v, margin, 13, 13);
    shader_layer(gm, g, &v, 14);
    visual_screen_objects(gm, g, ox, oy, vw, vh, scale);
    shader_layer(gm, g, &v, 99);   // apply what has not been applied yet
    draw_gradients(gm, g, &v, margin, 14, 99);

    #undef SX
    #undef SY
    gm->cam_x = follow_x;
    gm->cam_bottom = follow_bottom;
    g->turned = 0;
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
        if (o->kx != u->kx || o->ky != u->ky) gm_obj_scale(o, u->kx / o->kx, u->ky / o->ky);
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
        s->mpos = malloc((size_t)s->mcap * 5 * sizeof(float));
        s->mflags = malloc((size_t)s->mcap * 4);
    }
    for (int i = 0; i < s->nm; i++) {
        const Obj *o = &gm->obj[gm->movers[i]];
        float *m = &s->mpos[i * 5];
        m[0] = o->x; m[1] = o->y; m[2] = o->rot; m[3] = o->kx; m[4] = o->ky;
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
        const float *m = &s->mpos[i * 5];
        o->x = m[0]; o->y = m[1];
        if (o->spin == 0) o->rot = m[2];
        if (o->kx != m[3] || o->ky != m[4]) gm_obj_scale(o, m[3] / o->kx, m[4] / o->ky);
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
    c->view = gm->view;
    memcpy(c->grad, gm->grad, sizeof gm->grad);
    memcpy(c->area, gm->area, sizeof gm->area);
    c->narea = gm->narea;
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
    gm->view = c->view;
    memcpy(gm->grad, c->grad, sizeof gm->grad);
    memcpy(gm->area, c->area, sizeof gm->area);
    gm->narea = c->narea;
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
    IO(gm->view); IO(gm->grad); IO(gm->area); IO(gm->narea); IO(gm->keep); IO(gm->nkeep);
    for (int i = 0; ok && i < gm->n; i++) {
        Obj *o = &gm->obj[i];
        float kx = o->kx, ky = o->ky;
        IO(o->x); IO(o->y); IO(o->rot); IO(o->used); IO(o->used2); IO(o->tdone); IO(o->off); IO(kx); IO(ky);
        if (ok && (kx != o->kx || ky != o->ky)) gm_obj_scale(o, kx / o->kx, ky / o->ky);
    }
#undef IO
    fclose(f);
    return ok ? 0 : -1;
}
