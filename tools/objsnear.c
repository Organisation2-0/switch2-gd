// SPDX-License-Identifier: GPL-3.0-or-later
// Lists level objects in an x range. Usage: objsnear <level> <x0> <x1> [ymin ymax]
#include <stdio.h>
#include <stdlib.h>
#include "../src/level.h"
#include "../src/objects.h"
int main(int argc, char **argv)
{
    GdLevel l;
    if (argc < 4 || gd_level_load(argv[1], &l)) return 1;
    float x0 = (float)atof(argv[2]), x1 = (float)atof(argv[3]);
    float y0 = argc > 5 ? (float)atof(argv[4]) : -1e9f, y1 = argc > 5 ? (float)atof(argv[5]) : 1e9f;
    for (size_t i = 0; i < l.count; i++) {
        const GdObject *o = &l.objects[i];
        if (o->x >= x0 && o->x <= x1 && o->y >= y0 && o->y <= y1) {
            const char *fr = gd_object_frame(o->id);
            printf("id=%d x=%.0f y=%.0f rot=%.0f ch=%.0f/%.0f %s\n", o->id, o->x, o->y, o->rot,
                   gd_obj_prop(o, 21, -1), gd_obj_prop(o, 22, -1), fr ? fr : "-");
        }
    }
    printf("header: %.700s\n", l.header);
    return 0;
}
