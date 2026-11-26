// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GD_OBJCHILD_H
#define GD_OBJCHILD_H
typedef struct { int id; const char *tex; float x, y, rot, sx, sy; unsigned char flipx, flipy, kind; short z; float pw, ph; } GdChild;
extern const GdChild gd_children[];
extern const int gd_children_count;
const GdChild *gd_children_find(int id, int *count);
#endif
