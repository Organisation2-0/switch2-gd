// SPDX-License-Identifier: GPL-3.0-or-later
#include "objects.h"

#include "objtable.h"

const char *gd_object_frame(int id)
{
    int lo = 0, hi = gd_objtable_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gd_objtable[mid].id == id) return gd_objtable[mid].frame;
        if (gd_objtable[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
