// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GD_OBJTABLE_H
#define GD_OBJTABLE_H

typedef struct {
    int id;
    const char *frame;
} GdObjEntry;

extern const GdObjEntry gd_objtable[]; // sorted by id
extern const int gd_objtable_count;

#endif
