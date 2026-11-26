// SPDX-License-Identifier: GPL-3.0-or-later
#include "atlas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../vendor/stb_image.h"

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    size_t r = fread(b, 1, (size_t)n, f);
    b[r] = 0;
    fclose(f);
    return b;
}

// Returns the string content that follows <key>NAME</key><string>...</string>
// inside [chunk, chunk_end). Copies into buf. Returns 1 on success.
static int get_string(const char *chunk, const char *end, const char *key,
                      char *buf, size_t cap)
{
    char pat[64];
    snprintf(pat, sizeof pat, "<key>%s</key>", key);
    const char *p = strstr(chunk, pat);
    if (!p || p >= end) return 0;
    p = strstr(p, "<string>");
    if (!p || p >= end) return 0;
    p += 8;
    const char *e = strstr(p, "</string>");
    if (!e || e >= end) return 0;
    size_t n = (size_t)(e - p);
    if (n >= cap) n = cap - 1;
    memcpy(buf, p, n);
    buf[n] = 0;
    return 1;
}

static int get_bool(const char *chunk, const char *end, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "<key>%s</key>", key);
    const char *p = strstr(chunk, pat);
    if (!p || p >= end) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return strncmp(p, "<true/>", 7) == 0;
}

static int parse_nums(const char *s, float *out, int n)
{
    int k = 0;
    while (*s && k < n) {
        if ((*s >= '0' && *s <= '9') || *s == '-' || *s == '.') {
            char *e;
            out[k++] = strtof(s, &e);
            s = e;
        } else {
            s++;
        }
    }
    return k;
}

static void build_index(GdAtlas *a);

static int parse_plist(GdAtlas *a, const char *xml, int sheet)
{
    const char *p = strstr(xml, "<key>frames</key>");
    if (!p) return -1;
    p += 17;
    const char *meta = strstr(p, "<key>metadata</key>");
    const char *limit = meta ? meta : xml + strlen(xml);

    size_t cap = (size_t)(a->nframes) + 1024;
    GdFrame *fr = realloc(a->frames, cap * sizeof *fr);
    if (!fr) return -1;
    a->frames = fr;

    for (;;) {
        const char *k = strstr(p, "<key>");
        if (!k || k >= limit) break;
        const char *ke = strstr(k, "</key>");
        if (!ke) break;
        // Frame names end in ".png" (other keys are properties).
        if (ke - k < 9 || strncmp(ke - 4, ".png", 4) != 0) { p = ke + 6; continue; }
        char name[64];
        size_t nl = (size_t)(ke - (k + 5));
        if (nl >= sizeof name) nl = sizeof name - 1;
        memcpy(name, k + 5, nl);
        name[nl] = 0;

        // The frame's dict ends where the next frame key starts.
        const char *next = ke + 6;
        const char *chunk_end = limit;
        for (const char *q = next;;) {
            const char *nk = strstr(q, "<key>");
            if (!nk || nk >= limit) break;
            const char *nke = strstr(nk, "</key>");
            if (!nke) break;
            if (nke - nk >= 9 && strncmp(nke - 4, ".png", 4) == 0) { chunk_end = nk; break; }
            q = nke + 6;
        }

        char s[96];
        float v[4];
        GdFrame f;
        memset(&f, 0, sizeof f);
        memcpy(f.name, name, nl + 1);
        f.sheet = sheet;
        if (get_string(next, chunk_end, "textureRect", s, sizeof s) && parse_nums(s, v, 4) == 4) {
            f.x = (int)v[0]; f.y = (int)v[1]; f.w = (int)v[2]; f.h = (int)v[3];
        } else {
            p = chunk_end;
            continue;
        }
        f.rotated = get_bool(next, chunk_end, "textureRotated");
        if (get_string(next, chunk_end, "spriteOffset", s, sizeof s) && parse_nums(s, v, 2) == 2) {
            f.offx = v[0]; f.offy = v[1];
        }
        if (get_string(next, chunk_end, "spriteSourceSize", s, sizeof s) && parse_nums(s, v, 2) == 2) {
            f.srcw = (int)v[0]; f.srch = (int)v[1];
        }

        if ((size_t)a->nframes >= cap) {
            cap *= 2;
            fr = realloc(a->frames, cap * sizeof *fr);
            if (!fr) return -1;
            a->frames = fr;
        }
        a->frames[a->nframes++] = f;
        p = chunk_end;
    }
    return 0;
}

int gd_atlas_load(GdAtlas *a, const char *resdir, const char *suffix,
                  const char *const *names, int nsheets)
{
    memset(a, 0, sizeof *a);
    a->px_per_unit = !strcmp(suffix, "-uhd") ? 4.0f : !strcmp(suffix, "-hd") ? 2.0f : 1.0f;
    for (int i = 0; i < nsheets && i < 16; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s%s.plist", resdir, names[i], suffix);
        char *xml = read_text(path);
        if (!xml) {
            fprintf(stderr, "cannot read %s\n", path);
            if (i >= 7) break;   // the extra sheets (fire, pixel art) are optional
            return -1;
        }
        int first = a->nframes;
        int rc = parse_plist(a, xml, i);
        free(xml);
        if (rc) { fprintf(stderr, "bad plist %s\n", path); return -2; }

        snprintf(path, sizeof path, "%s/%s%s.png", resdir, names[i], suffix);
        int w, h, n;
        unsigned char *px = stbi_load(path, &w, &h, &n, 4);
        if (!px) { fprintf(stderr, "cannot decode %s\n", path); return -3; }
        a->sheets[i].w = w;
        a->sheets[i].h = h;
        a->sheets[i].rgba = px;
        a->nsheets = i + 1;
        (void)first;
    }
    build_index(a);
    return 0;
}

static unsigned name_hash(const char *s)
{
    unsigned h = 2166136261u;   // FNV-1a
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

// Built once all sheets are loaded. Of equal names the first frame wins.
static void build_index(GdAtlas *a)
{
    unsigned size = 64;
    while (size < (unsigned)a->nframes * 2u) size *= 2;
    free(a->index);
    a->index = calloc(size, sizeof *a->index);
    if (!a->index) return;
    a->index_mask = size - 1;
    for (int i = 0; i < a->nframes; i++) {
        unsigned at = name_hash(a->frames[i].name) & a->index_mask;
        while (a->index[at] && strcmp(a->frames[a->index[at] - 1].name, a->frames[i].name))
            at = (at + 1) & a->index_mask;
        if (!a->index[at]) a->index[at] = i + 1;
    }
}

const GdFrame *gd_atlas_find(const GdAtlas *a, const char *name)
{
    if (a->index) {
        for (unsigned at = name_hash(name) & a->index_mask; a->index[at]; at = (at + 1) & a->index_mask)
            if (!strcmp(a->frames[a->index[at] - 1].name, name)) return &a->frames[a->index[at] - 1];
        return NULL;
    }
    for (int i = 0; i < a->nframes; i++)
        if (!strcmp(a->frames[i].name, name)) return &a->frames[i];
    return NULL;
}

void gd_atlas_free(GdAtlas *a)
{
    for (int i = 0; i < a->nsheets; i++) if (a->sheets[i].rgba) stbi_image_free(a->sheets[i].rgba);
    free(a->frames);
    free(a->index);
    memset(a, 0, sizeof *a);
}
