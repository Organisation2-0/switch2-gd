// SPDX-License-Identifier: GPL-3.0-or-later
// Prints a summary of a level file. Usage: leveldump <level.txt|level.gdlvl>
#include <stdio.h>
#include <stdlib.h>

#include "../src/level.h"

static int cmp(const void *a, const void *b)
{
    const int *x = a, *y = b;
    return y[1] - x[1];
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <level file>\n", argv[0]); return 2; }
    GdLevel l;
    int rc = gd_level_load(argv[1], &l);
    if (rc) { fprintf(stderr, "load failed (%d)\n", rc); return 1; }

    int counts[1024][2] = {{0}};
    float maxx = 0;
    for (size_t i = 0; i < l.count; i++) {
        int id = l.objects[i].id;
        if (id > 0 && id < 1024) { counts[id][0] = id; counts[id][1]++; }
        if (l.objects[i].x > maxx) maxx = l.objects[i].x;
    }
    qsort(counts, 1024, sizeof counts[0], cmp);
    printf("name: %s\nobjects: %zu  max x: %.0f\n", l.name, l.count, maxx);
    printf("most common ids:");
    for (int i = 0; i < 12 && counts[i][1]; i++) printf(" %d:%d", counts[i][0], counts[i][1]);
    printf("\n");
    gd_level_free(&l);
    return 0;
}
