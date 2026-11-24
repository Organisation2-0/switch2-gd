// SPDX-License-Identifier: GPL-3.0-or-later
// Lists object ids whose sprite is unknown or missing from the atlas. Usage: missing <resdir> <level>...
#include <stdio.h>
#include <string.h>
#include "../src/atlas.h"
#include "../src/level.h"
#include "../src/objects.h"

int main(int argc, char **argv)
{
    static const char *const sheets[] = {"GJ_GameSheet", "GJ_GameSheet02", "GJ_GameSheet03", "GJ_GameSheet04",
                                         "GJ_GameSheetGlow", "GJ_GameSheetIcons", "GJ_LaunchSheet", "FireSheet_01", "PixelSheet_01"};
    GdAtlas at;
    if (gd_atlas_load(&at, argv[1], "-hd", sheets, 9)) { puts("atlas failed"); return 1; }
    static int noframe[5000], nofound[5000], total[5000];
    static char names[5000][64];
    for (int a = 2; a < argc; a++) {
        GdLevel l;
        if (gd_level_load(argv[a], &l)) continue;
        for (size_t i = 0; i < l.count; i++) {
            int id = l.objects[i].id;
            if (id < 0 || id >= 5000) continue;
            total[id]++;
            const char *fr = gd_object_frame(id);
            if (!fr) noframe[id]++;
            else if (!gd_atlas_find(&at, fr)) { nofound[id]++; snprintf(names[id], 64, "%s", fr); }
        }
        gd_level_free(&l);
    }
    for (int id = 0; id < 5000; id++) {
        if (noframe[id]) printf("id %4d: NO FRAME in table            x%d\n", id, noframe[id]);
        if (nofound[id]) printf("id %4d: frame not in atlas: %-32s x%d\n", id, names[id], nofound[id]);
    }
    return 0;
}
