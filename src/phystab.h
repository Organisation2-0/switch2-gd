// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GD_PHYSTAB_H
#define GD_PHYSTAB_H
typedef struct { int id, type; float w, h, x, y, radius; } GdPhys;
extern const GdPhys gd_phys[]; // sorted by id
extern const int gd_phys_count;
const GdPhys *gd_phys_find(int id);
#endif
