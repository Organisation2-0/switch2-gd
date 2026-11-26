// SPDX-License-Identifier: GPL-3.0-or-later
#include "level.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/stb_image.h" // stbi_zlib_decode_* (implementation lives in image.c)

// strsep is not available everywhere (MinGW), so keep a tiny copy.
static char *gd_strsep(char **sp, const char *delim)
{
    char *s = *sp;
    if (!s) return NULL;
    char *e = s + strcspn(s, delim);
    if (*e) { *e = 0; *sp = e + 1; } else { *sp = NULL; }
    return s;
}

static int b64val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}

static unsigned char *b64_decode(const char *s, size_t n, size_t *out_n)
{
    unsigned char *out = malloc(n / 4 * 3 + 4);
    if (!out) return NULL;
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < n; i++) {
        int v = b64val((unsigned char)s[i]);
        if (v < 0) continue; // skips '=' padding and whitespace
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)(acc >> bits);
            acc &= (1u << bits) - 1;
        }
    }
    *out_n = o;
    return out;
}

// Minimal gzip wrapper: skips the header, then raw inflate.
static char *gunzip(const unsigned char *d, size_t n, size_t *out_n)
{
    if (n < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) return NULL;
    int flags = d[3];
    size_t p = 10;
    if (flags & 4) { // FEXTRA
        if (p + 2 > n) return NULL;
        p += 2 + (d[p] | (d[p + 1] << 8));
    }
    if (flags & 8) { while (p < n && d[p++]) {} }  // FNAME
    if (flags & 16) { while (p < n && d[p++]) {} } // FCOMMENT
    if (flags & 2) p += 2;                         // FHCRC
    if (p >= n - 8) return NULL;
    int len = 0;
    char *buf = stbi_zlib_decode_noheader_malloc((const char *)d + p, (int)(n - p - 8), &len);
    if (!buf) return NULL;
    char *res = realloc(buf, (size_t)len + 1);
    if (!res) { free(buf); return NULL; }
    res[len] = 0;
    *out_n = (size_t)len;
    return res;
}

char *gd_decode_levelstring(const char *src, size_t len, size_t *out_len)
{
    size_t bn = 0;
    unsigned char *bin = b64_decode(src, len, &bn);
    if (!bin) return NULL;
    size_t gn = 0;
    char *res = gunzip(bin, bn, &gn);
    free(bin);
    if (out_len) *out_len = gn;
    return res;
}

static char *read_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    *n = fread(buf, 1, (size_t)sz, f);
    buf[*n] = 0;
    fclose(f);
    return buf;
}

// Finds <k>key</k><s>VALUE</s> in a GD plist; returns malloc'd VALUE or NULL.
static char *plist_get_string(const char *xml, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "<k>%s</k><s>", key);
    const char *p = strstr(xml, pat);
    if (!p) return NULL;
    p += strlen(pat);
    const char *e = strstr(p, "</s>");
    if (!e) return NULL;
    char *r = malloc((size_t)(e - p) + 1);
    if (!r) return NULL;
    memcpy(r, p, (size_t)(e - p));
    r[e - p] = 0;
    return r;
}

// "hue a saturation a value a sAdd a vAdd"
static void parse_hsv_string(const char *s, float out[5])
{
    out[0] = 0; out[1] = 1; out[2] = 1; out[3] = 0; out[4] = 0;
    for (int n = 0; *s && n < 5; n++) {
        out[n] = (float)atof(s);
        while (*s && *s != 'a') s++;
        if (*s == 'a') s++;
    }
}

static void parse_object(char *rec, GdObject *o)
{
    memset(o, 0, sizeof *o);
    o->scale = 1.0f;
    char *save = rec;
    char *tok;
    int have_key = 0, key = 0;
    while ((tok = gd_strsep(&save, ",")) != NULL) {
        if (!have_key) {
            key = atoi(tok);
            have_key = 1;
            continue;
        }
        have_key = 0;
        float v = (float)atof(tok);
        if (key == 152 || key == 435) {   // 2.2 lists: "group.weight.group.weight..."
            for (const char *g = tok; *g && o->nlist < 16;) {
                o->list[o->nlist++] = (short)atoi(g);
                while (*g && *g != '.') g++;
                if (*g == '.') g++;
            }
        }
        if (key == 57) {
            const char *g = tok;
            while (*g && o->ngroups < GD_MAX_GROUPS) {
                o->groups[o->ngroups++] = atoi(g);
                while (*g && *g != '.') g++;
                if (*g == '.') g++;
            }
        }
        switch (key) {
        case 1: o->id = (int)v; break;
        case 2: o->x = v; break;
        case 3: o->y = v; break;
        case 4: o->flipx = (int)v; break;
        case 5: o->flipy = (int)v; break;
        case 6: o->rot = v; break;
        case 32: o->scale = v; break;
        case 128: o->scale_x = v; break;
        case 129: o->scale_y = v; break;
        case 41: o->has_hsv1 = v != 0; break;
        case 42: o->has_hsv2 = v != 0; break;
        case 43: parse_hsv_string(tok, o->hsv1); break;
        case 44: parse_hsv_string(tok, o->hsv2); break;
        case 49: o->has_copy_hsv = 1; parse_hsv_string(tok, o->copy_hsv); break;
        default: break;
        }
        if (o->nprops < GD_MAX_PROPS) {
            o->props[o->nprops].key = key;
            o->props[o->nprops].val = v;
            o->nprops++;
        }
    }
}

static int parse_levelstring(char *s, GdLevel *out)
{
    // Records are separated by ';'. The first record is the header.
    size_t cap = 1024, n = 0;
    GdObject *objs = malloc(cap * sizeof *objs);
    if (!objs) return -1;
    char *save = s;
    char *rec = gd_strsep(&save, ";");
    out->header = rec ? strdup(rec) : strdup("");
    while ((rec = gd_strsep(&save, ";")) != NULL) {
        if (!*rec) continue;
        if (n == cap) {
            cap *= 2;
            GdObject *t = realloc(objs, cap * sizeof *objs);
            if (!t) { free(objs); return -1; }
            objs = t;
        }
        parse_object(rec, &objs[n]);
        if (objs[n].id > 0) n++;
    }
    out->objects = objs;
    out->count = n;
    return 0;
}

int gd_level_load(const char *path, GdLevel *out)
{
    memset(out, 0, sizeof *out);
    size_t n = 0;
    char *raw = read_file(path, &n);
    if (!raw) return -1;

    size_t dn = 0;
    char *dec = gd_decode_levelstring(raw, n, &dn);
    free(raw);
    if (!dec) return -2;

    char *lvlstr = NULL;
    if (strncmp(dec, "<?xml", 5) == 0) { // .gdlvl export
        char *name = plist_get_string(dec, "k2");
        if (name) { snprintf(out->name, sizeof out->name, "%s", name); free(name); }
        char *k4 = plist_get_string(dec, "k4");
        free(dec);
        if (!k4) return -3;
        size_t ln = 0;
        lvlstr = gd_decode_levelstring(k4, strlen(k4), &ln);
        free(k4);
    } else {
        lvlstr = dec; // official level: already the level string
        const char *slash = strrchr(path, '/');
        const char *bslash = strrchr(path, '\\');
        if (bslash > slash) slash = bslash;
        snprintf(out->name, sizeof out->name, "%s", slash ? slash + 1 : path);
    }
    if (!lvlstr) return -4;

    int rc = parse_levelstring(lvlstr, out);
    free(lvlstr);
    return rc;
}

void gd_level_free(GdLevel *lvl)
{
    free(lvl->header);
    free(lvl->objects);
    memset(lvl, 0, sizeof *lvl);
}

float gd_obj_prop(const GdObject *o, int key, float def)
{
    for (int i = 0; i < o->nprops; i++)
        if (o->props[i].key == key) return o->props[i].val;
    return def;
}
