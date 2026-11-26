// SPDX-License-Identifier: GPL-3.0-or-later
// Animations of multi-part sprites (monsters, robot, spider), read at run time from the
// game's *_AnimDesc.plist files.
#ifndef GD_ANIM_H
#define GD_ANIM_H

#include "gfx.h"

#define ANIM_FRAMES 16
#define ANIM_PARTS 12

typedef struct {
    const GdFrame *f, *f2, *extra, *glow;
    int limb;                     // robot / spider: number of the body part in the icon file
    float x, y, sx, sy, rot;      // units, y up; degrees clockwise; sx / sy carry the flips
    int z;
} AnimPart;

typedef struct {
    int nframes;
    int nparts[ANIM_FRAMES];
    AnimPart part[ANIM_FRAMES][ANIM_PARTS];
} Anim;

// Loads the frames "<name>_001.png" ... from <resources>/<file>. Returns 0 when at least one
// frame with parts was found.
int anim_load(Anim *a, Gfx *g, const char *file, const char *name);

// Draws one frame of the robot or spider animation with the parts of icon <id>. (x,y): centre on
// screen; unit: screen px per game unit; k: screen px per sheet px; flip: 1 or -1 (upside down).
void anim_draw_icon(Gfx *g, const Anim *a, int frame, int spider, int id, float x, float y,
                    float rot, float unit, float k, float flip, GdColorF c1, GdColorF c2);

#endif
