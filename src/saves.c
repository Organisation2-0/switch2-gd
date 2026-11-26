// SPDX-License-Identifier: GPL-3.0-or-later
#include "saves.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aio.h"
#include "level.h"

// First occurrence of s that starts before `to`.
static const char *find(const char *from, const char *to, const char *s)
{
    const char *p = strstr(from, s);
    return p && p < to ? p : NULL;
}

// The value following <k>key</k>: text of <s>, <i> or <r>, or "1" for <t />. Returns its start
// and length, NULL if the key is not there.
static const char *value(const char *from, const char *to, const char *key, size_t *len)
{
    char pat[32];
    snprintf(pat, sizeof pat, "<k>%s</k>", key);
    const char *p = find(from, to, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    if (!strncmp(p, "<t", 2) && (p[2] == ' ' || p[2] == '/')) { *len = 1; return "1"; }
    if (p[0] != '<' || (p[1] != 's' && p[1] != 'i' && p[1] != 'r') || p[2] != '>') return NULL;
    p += 3;
    const char *e = memchr(p, '<', (size_t)(to - p));
    if (!e) return NULL;
    *len = (size_t)(e - p);
    return p;
}

static int number(const char *from, const char *to, const char *key, int fallback)
{
    size_t n;
    const char *v = value(from, to, key, &n);
    return v ? atoi(v) : fallback;
}

// Copies a text value, resolving the XML escapes.
static void text_value(const char *from, const char *to, const char *key, char *out, size_t cap)
{
    size_t n = 0, o = 0;
    const char *v = value(from, to, key, &n);
    static const struct { const char *in; char out; } esc[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    for (size_t i = 0; v && i < n && o + 1 < cap;) {
        char c = v[i];
        size_t step = 1;
        for (int k = 0; c == '&' && k < 5; k++)
            if (!strncmp(v + i, esc[k].in, strlen(esc[k].in))) { c = esc[k].out; step = strlen(esc[k].in); break; }
        out[o++] = c;
        i += step;
    }
    out[o] = 0;
}

void gd_info_parse(const char *from, const char *to, GdLevelInfo *out)
{
    memset(out, 0, sizeof *out);
    text_value(from, to, "k2", out->name, sizeof out->name);
    text_value(from, to, "k5", out->creator, sizeof out->creator);
    out->id = number(from, to, "k1", 0);
    out->stars = number(from, to, "k26", 0);
    out->length = number(from, to, "k23", -1);
    out->downloads = number(from, to, "k11", -1);
    out->likes = number(from, to, "k22", 0);
    out->has_stats = out->downloads >= 0;
    // difficulty = k10 / k9 (1 easy ... 5 insane)
    const int count = number(from, to, "k9", 0), sum = number(from, to, "k10", 0);
    int face = count > 0 ? sum / count : 0;
    if (face > 5) face = 5;
    if (number(from, to, "k25", 0)) face = 6;
    if (number(from, to, "k33", 0)) face = -1;
    out->face = face;
}

// Plain XML (an export written by this port or a .gmd file) or the packed form of a .gdlvl.
static char *level_xml(char *raw, size_t n)
{
    const char *p = raw;
    if (n >= 3 && !memcmp(p, "\xef\xbb\xbf", 3)) p += 3;
    while (*p == ' ' || *p == '\r' || *p == '\n') p++;
    if (*p == '<') return raw;
    size_t dn = 0;
    char *dec = gd_decode_levelstring(raw, n, &dn);
    free(raw);
    return dec;
}

int gd_info_file(const char *path, GdLevelInfo *out)
{
    memset(out, 0, sizeof *out);
    out->length = -1;
    size_t n = 0;
    char *raw = (char *)aio_level_file(path, &n);
    if (!raw) return -1;
    char *xml = level_xml(raw, n);
    if (!xml) return -1;
    const int ok = xml[0] && strstr(xml, "<k>k4</k>") != NULL;
    if (ok) gd_info_parse(xml, xml + strlen(xml), out);
    free(xml);
    return ok ? 0 : -1;
}

// Next dictionary tag at or after p: +1 opens, -1 closes, 0 none left. Empty ones are skipped.
static int dict_tag(const char **p)
{
    for (const char *s = *p; (s = strchr(s, '<')); s++) {
        if (!strncmp(s, "<d>", 3)) { *p = s; return 1; }
        if (!strncmp(s, "</d>", 4)) { *p = s; return -1; }
    }
    return 0;
}

int gd_saved_load(const char *path, GdSavedLevel **out)
{
    *out = NULL;
    size_t n = 0;
    char *raw = (char *)gd_read_file(path, &n);
    if (!raw) return 0;
    char *xml = raw;
    if (raw[0] != '<') {   // as stored on Windows: every byte xor 11, then base64 and gzip
        for (size_t i = 0; i < n; i++) raw[i] ^= 11;
        size_t dn = 0;
        xml = gd_decode_levelstring(raw, n, &dn);
        free(raw);
        if (!xml) return 0;
    }
    // GLM_03: dictionary of the saved online levels, one dictionary per level
    int count = 0, cap = 0;
    GdSavedLevel *list = NULL;
    const char *p = strstr(xml, "<k>GLM_03</k>");
    if (p && !strncmp(p + 13, "<d>", 3)) {
        p += 13;
        const char *start = NULL;
        int depth = 0, t;
        while ((t = dict_tag(&p))) {
            if (t > 0) {
                if (++depth == 2) start = p;
            } else {
                if (depth == 2) {
                    size_t len = 0;
                    const char *data = value(start, p, "k4", &len);
                    if (data && len > 20) {
                        if (count == cap) {
                            cap = cap ? cap * 2 : 32;
                            GdSavedLevel *more = realloc(list, (size_t)cap * sizeof *list);
                            if (!more) break;
                            list = more;
                        }
                        GdSavedLevel *l = &list[count];
                        gd_info_parse(start, p, &l->info);
                        l->data = malloc(len + 1);
                        if (l->data) {
                            memcpy(l->data, data, len);
                            l->data[len] = 0;
                            count++;
                        }
                    }
                }
                if (--depth == 0) break;
            }
            p += 3;
        }
    }
    free(xml);
    *out = list;
    return count;
}

void gd_saved_free(GdSavedLevel *list, int n)
{
    for (int i = 0; i < n; i++) free(list[i].data);
    free(list);
}

static void xml_text(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '&') fputs("&amp;", f);
        else if (*s == '<') fputs("&lt;", f);
        else if (*s == '>') fputs("&gt;", f);
        else fputc(*s, f);
    }
}

int gd_saved_export(const GdSavedLevel *list, int n, const char *dir)
{
    int written = 0;
    for (int i = 0; i < n; i++) {
        const GdLevelInfo *l = &list[i].info;
        char path[700];
        snprintf(path, sizeof path, "%s/%d.gdlvl", dir, l->id);
        FILE *f = gd_fopen(path, "wb");
        if (!f) continue;
        fputs("<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>", f);
        fprintf(f, "<k>k1</k><i>%d</i><k>k2</k><s>", l->id);
        xml_text(f, l->name);
        fputs("</s><k>k5</k><s>", f);
        xml_text(f, l->creator);
        fputs("</s>", f);
        if (l->face > 0 && l->face < 6) fprintf(f, "<k>k9</k><i>10</i><k>k10</k><i>%d</i>", l->face * 10);
        if (l->face == 6) fputs("<k>k25</k><t />", f);
        if (l->face < 0) fputs("<k>k33</k><t />", f);
        if (l->stars) fprintf(f, "<k>k26</k><i>%d</i>", l->stars);
        if (l->length >= 0) fprintf(f, "<k>k23</k><i>%d</i>", l->length);
        if (l->has_stats) fprintf(f, "<k>k11</k><i>%d</i><k>k22</k><i>%d</i>", l->downloads, l->likes);
        fputs("<k>k4</k><s>", f);
        fputs(list[i].data, f);
        fputs("</s></dict></plist>\n", f);
        if (!fclose(f)) written++;
    }
    return written;
}
