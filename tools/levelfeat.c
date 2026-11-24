// SPDX-License-Identifier: GPL-3.0-or-later
// Lists the gameplay features a level uses. Usage: levelfeat <level file>...
#include <stdio.h>
#include "../src/level.h"

int main(int argc, char **argv)
{
    static const struct { int id; const char *n; } F[] = {
        {10,"gravDown"},{11,"gravUp"},{12,"cube"},{13,"ship"},{47,"ball"},{111,"ufo"},{660,"wave"},
        {745,"robot"},{1331,"spider"},{99,"sizeNormal"},{101,"sizeMini"},{45,"mirrorOn"},{46,"mirrorOff"},
        {286,"dualOn"},{287,"dualOff"},{35,"yPad"},{67,"bPad"},{140,"pPad"},{1332,"rPad"},{36,"yOrb"},
        {84,"bOrb"},{141,"pOrb"},{1022,"gOrb"},{1333,"rOrb"},{1704,"dashOrb"},{200,"spd0"},{201,"spd1"},
        {202,"spd2"},{203,"spd3"},{1334,"spd4"},{901,"moveTrg"},{1049,"toggleTrg"},{1007,"alphaTrg"},
        {1006,"pulseTrg"},{1268,"spawnTrg"},{1346,"rotateTrg"},{1347,"followTrg"},{1585,"animTrg"},
        {1595,"touchTrg"},{1611,"countTrg"},{1612,"instCountTrg"},{1811,"instantCount"},{1912,"randomTrg"},
        {1914,"stopTrg"},{2067,"scaleTrg"},{3,"-"}};
    for (int a = 1; a < argc; a++) {
        GdLevel l;
        if (gd_level_load(argv[a], &l)) { printf("%s: load failed\n", argv[a]); continue; }
        printf("%s (%s, %zu objs):", argv[a], l.name, l.count);
        for (int k = 0; F[k].id != 3; k++) {
            int c = 0;
            for (size_t i = 0; i < l.count; i++) if (l.objects[i].id == F[k].id) c++;
            if (c) printf(" %s=%d", F[k].n, c);
        }
        printf("\n");
        gd_level_free(&l);
    }
    return 0;
}
