// SPDX-License-Identifier: GPL-3.0-or-later
// Level loading: official Resources/levels/N.txt and exported .gdlvl files.
#ifndef GD_LEVEL_H
#define GD_LEVEL_H

#include <stddef.h>

#define GD_MAX_PROPS 64    // 2.2 objects carry far more properties than older ones
#define GD_MAX_GROUPS 10

typedef struct {
    int key;
    float val;
} GdProp;

typedef struct {
    int id;       // object id (key 1)
    float x, y;   // key 2, key 3
    float rot;    // key 6 (degrees)
    int flipx;    // key 4
    int flipy;    // key 5
    float scale;  // key 32, default 1
    float scale_x, scale_y;     // keys 128 / 129 (2.2), 0 = use scale
    signed char has_hsv1, has_hsv2, has_copy_hsv;
    float hsv1[5], hsv2[5];     // per-object colour shift of the base / detail colour (keys 41-44)
    float copy_hsv[5];          // colour triggers: shift of a copied channel (key 49)
    int ngroups;                // key 57, "a.b.c"
    int groups[GD_MAX_GROUPS];
    short list[16];             // key 152 (random weights) or 435 (sequence): "group.number" pairs
    int nlist;
    int nprops;
    GdProp props[GD_MAX_PROPS]; // every key/value pair, for later features
} GdObject;

typedef struct {
    char name[96];
    char *header;       // level header string (colors, speed, ...); owned
    GdObject *objects;  // owned
    size_t count;
} GdLevel;

// Decodes URL-safe base64 + gzip (the on-disk form of level data).
// Returns a malloc'd NUL-terminated string, or NULL on failure.
char *gd_decode_levelstring(const char *src, size_t len, size_t *out_len);

// Loads a level from a file. Accepts:
//  - official level files (Resources/levels/N.txt): one gzip+base64 string
//  - .gdlvl exports: gzip+base64 XML plist whose key k4 holds the level string
// Returns 0 on success.
int gd_level_load(const char *path, GdLevel *out);

void gd_level_free(GdLevel *lvl);

// Returns the value of a property or `def` when the object has none.
float gd_obj_prop(const GdObject *o, int key, float def);

#endif
