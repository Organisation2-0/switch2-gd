// SPDX-License-Identifier: GPL-3.0-or-later
// Level information from exported level files (.gdlvl) and from the levels saved in the player's
// own Geometry Dash (read only; nothing but the list of saved levels is looked at).
#ifndef GD_SAVES_H
#define GD_SAVES_H

#include <stddef.h>

typedef struct {
    char name[96];
    char creator[48];
    int id;
    int face;       // difficulty face: 0 unrated, 1 easy ... 5 insane, 6 demon, -1 auto
    int stars;
    int length;     // 0 tiny ... 4 XL, 5 platformer; -1 unknown
    int downloads, likes;
    int has_stats;  // downloads / likes are known
} GdLevelInfo;

typedef struct {
    GdLevelInfo info;
    char *data;     // level string as stored by the game (owned)
} GdSavedLevel;

// Reads the fields of the plist dictionary found in [from, to).
void gd_info_parse(const char *from, const char *to, GdLevelInfo *out);

// Information of a level file; returns 0 on success.
int gd_info_file(const char *path, GdLevelInfo *out);

// The saved online levels of a CCGameManager.dat whose level data is stored in it. Returns their
// number (0 if there are none or the file cannot be read).
int gd_saved_load(const char *path, GdSavedLevel **out);
void gd_saved_free(GdSavedLevel *list, int n);

// Writes each of these levels as <dir>/<id>.gdlvl (for the console). Returns the number written.
int gd_saved_export(const GdSavedLevel *list, int n, const char *dir);

#endif
