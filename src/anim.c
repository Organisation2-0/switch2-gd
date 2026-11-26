// SPDX-License-Identifier: GPL-3.0-or-later
#include "anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *s = n > 0 && n < (4 << 20) ? malloc((size_t)n + 1) : NULL;
    if (s && fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); s = NULL; }
    if (s) s[n] = 0;
    fclose(f);
    return s;
}

// The <string> following "<key>name</key>" inside [from, to); copied into out.
static int value_of(const char *from, const char *to, const char *name, char *out, size_t cap)
{
    char key[48];
    snprintf(key, sizeof key, "<key>%s</key>", name);
    const char *k = strstr(from, key);
    if (!k || k >= to) return 0;
    const char *a = strstr(k, "<string>");
    if (!a || a >= to) return 0;
    a += 8;
    const char *b = strstr(a, "</string>");
    if (!b || b > to) return 0;
    size_t n = (size_t)(b - a);
    if (n >= cap) n = cap - 1;
    memcpy(out, a, n);
    out[n] = 0;
    return 1;
}

static void pair(const char *s, float *a, float *b)
{
    while (*s == '{' || *s == ' ') s++;
    *a = (float)atof(s);
    const char *c = strchr(s, ',');
    *b = c ? (float)atof(c + 1) : 0;
}

static const GdFrame *variant(Gfx *g, const char *tex, const char *suffix)
{
    char name[96];
    size_t n = strlen(tex);
    if (n < 8 || strcmp(tex + n - 8, "_001.png")) return NULL;
    snprintf(name, sizeof name, "%.*s%s", (int)(n - 8), tex, suffix);
    return gfx_find(g, name);
}

// End of the <dict> that starts at or after s (nesting aware).
static const char *dict_end(const char *s)
{
    int depth = 0;
    const char *p = strstr(s, "<dict>");
    while (p) {
        const char *open = strstr(p, "<dict>"), *close = strstr(p, "</dict>");
        if (!close) return NULL;
        if (open && open < close) { depth++; p = open + 6; continue; }
        if (--depth == 0) return close;
        p = close + 7;
    }
    return NULL;
}

int anim_load(Anim *a, Gfx *g, const char *file, const char *name)
{
    memset(a, 0, sizeof *a);
    char path[700];
    snprintf(path, sizeof path, "%s/%s", g->resdir, file);
    char *text = read_file(path);
    if (!text) return -1;
    for (int n = 0; n < ANIM_FRAMES; n++) {
        char key[96];
        snprintf(key, sizeof key, "<key>%s_%03d.png</key>", name, n + 1);
        const char *start = strstr(text, key);
        if (!start) break;
        const char *end = dict_end(start);
        if (!end) break;
        int np = 0;
        const char *s = strstr(start, "<key>sprite_");
        while (s && s < end && np < ANIM_PARTS) {
            const char *next = strstr(s + 12, "<key>sprite_");
            const char *stop = next && next < end ? next : end;
            char tex[96], v[64];
            if (value_of(s, stop, "texture", tex, sizeof tex)) {
                AnimPart *p = &a->part[n][np];
                float fx = 0, fy = 0;
                p->f = gfx_find(g, tex);
                p->f2 = variant(g, tex, "_2_001.png");
                p->extra = variant(g, tex, "_extra_001.png");
                p->glow = variant(g, tex, "_glow_001.png");
                p->sx = p->sy = 1.0f;
                if (value_of(s, stop, "position", v, sizeof v)) pair(v, &p->x, &p->y);
                if (value_of(s, stop, "scale", v, sizeof v)) pair(v, &p->sx, &p->sy);
                if (value_of(s, stop, "rotation", v, sizeof v)) p->rot = (float)atof(v);
                if (value_of(s, stop, "flipped", v, sizeof v)) pair(v, &fx, &fy);
                if (value_of(s, stop, "zValue", v, sizeof v)) p->z = atoi(v);
                if (fx != 0) p->sx = -p->sx;
                if (fy != 0) p->sy = -p->sy;
                if (p->f) np++;
            }
            s = next;
        }
        // back to front
        for (int i = 1; i < np; i++)
            for (int j = i; j > 0 && a->part[n][j - 1].z > a->part[n][j].z; j--) {
                AnimPart t = a->part[n][j];
                a->part[n][j] = a->part[n][j - 1];
                a->part[n][j - 1] = t;
            }
        a->nparts[n] = np;
        if (!np) break;
        a->nframes = n + 1;
    }
    free(text);
    return a->nframes ? 0 : -1;
}
