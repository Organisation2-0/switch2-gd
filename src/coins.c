// SPDX-License-Identifier: GPL-3.0-or-later
#include "coins.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char key[200]; int mask, count; } Entry;

static Entry *s_list;
static int s_n, s_cap;
static char s_path[600];

static Entry *find(const char *key)
{
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_list[i].key, key)) return &s_list[i];
    return NULL;
}

static Entry *add(const char *key)
{
    if (s_n == s_cap) {
        const int cap = s_cap ? s_cap * 2 : 64;
        Entry *more = realloc(s_list, (size_t)cap * sizeof *more);
        if (!more) return NULL;
        s_list = more;
        s_cap = cap;
    }
    Entry *e = &s_list[s_n++];
    memset(e, 0, sizeof *e);
    snprintf(e->key, sizeof e->key, "%s", key);
    return e;
}

// One line per level: key, tab, owned coins as bits, tab, number of coins.
void coins_load(const char *path)
{
    s_n = 0;
    snprintf(s_path, sizeof s_path, "%s", path);
    FILE *f = fopen(s_path, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        Entry *e = find(line) ? find(line) : add(line);
        if (e) sscanf(tab + 1, "%d\t%d", &e->mask, &e->count);
    }
    fclose(f);
}

int coins_owned(const char *level_key)
{
    const Entry *e = find(level_key);
    return e ? e->mask : 0;
}

int coins_count(const char *level_key)
{
    const Entry *e = find(level_key);
    return e ? e->count : 0;
}

void coins_store(const char *level_key, int mask, int count)
{
    Entry *e = find(level_key);
    if (e && (e->mask | mask) == e->mask && e->count == count) return;
    if (!e) e = add(level_key);
    if (!e) return;
    e->mask |= mask;
    e->count = count;
    FILE *f = fopen(s_path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", s_path); return; }
    for (int i = 0; i < s_n; i++) fprintf(f, "%s\t%d\t%d\n", s_list[i].key, s_list[i].mask, s_list[i].count);
    fclose(f);
}
