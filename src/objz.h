// SPDX-License-Identifier: GPL-3.0-or-later
// Default draw order of objects (levels only store it when it differs).
#ifndef GD_OBJZ_H
#define GD_OBJZ_H

typedef struct { short id, layer, order; } GdObjZ;

const GdObjZ *gd_objz_find(int id);

#endif
