// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GD_OBJCHAN_H
#define GD_OBJCHAN_H
typedef struct { int id, kind, primary, secondary; } GdObjChan;
extern const GdObjChan gd_objchan[];
extern const int gd_objchan_count;
const GdObjChan *gd_objchan_find(int id);
#endif
