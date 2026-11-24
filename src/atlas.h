// SPDX-License-Identifier: GPL-3.0-or-later
// Sprite sheets: cocos2d .plist (frame rectangles) + .png (pixels).
#ifndef GD_ATLAS_H
#define GD_ATLAS_H

typedef struct {
    char name[64];
    int x, y, w, h;      // textureRect in the sheet (w/h before rotation is applied)
    int rotated;         // stored rotated 90 degrees clockwise in the sheet
    float offx, offy;    // spriteOffset (pixels, y up) of the trimmed sprite
    int srcw, srch;      // spriteSourceSize (untrimmed size)
    int sheet;           // index into GdAtlas.sheets
} GdFrame;

typedef struct {
    int w, h;
    unsigned char *rgba; // owned
} GdSheet;

typedef struct {
    GdFrame *frames;     // owned
    int nframes;
    GdSheet sheets[16];
    int nsheets;
    float px_per_unit;   // 1 for SD, 2 for HD, 4 for UHD
} GdAtlas;

// Loads "<resdir>/<name><suffix>.plist/.png" for each given sheet name.
// suffix is "", "-hd" or "-uhd". Returns 0 on success.
int gd_atlas_load(GdAtlas *a, const char *resdir, const char *suffix,
                  const char *const *sheet_names, int nsheets);

const GdFrame *gd_atlas_find(const GdAtlas *a, const char *name);

void gd_atlas_free(GdAtlas *a);

#endif
