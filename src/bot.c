// SPDX-License-Identifier: GPL-3.0-or-later
#include "bot.h"

#include <stdlib.h>
#include <string.h>

#define NEAR_DEPTH 12
#define FRAMES_NEAR 2
#define FRAMES_FAR 6
#define MAX_DEPTH 28
static long budget_per_root(void) { static long v; if (!v) { const char *e = getenv("BOT_BUDGET"); v = e ? atol(e) : 3000; } return v; }
static int frames_at(int depth) { return depth < NEAR_DEPTH ? FRAMES_NEAR : FRAMES_FAR; }

static GameSnap g_snap[MAX_DEPTH + 2];
static long g_budget;
static int g_depth_reached;
static signed char g_path[MAX_DEPTH + 2];   // actions of the current DFS path
static signed char g_sol[MAX_DEPTH + 2];    // actions of the last full-depth solution
static const signed char *g_hint;           // previous solution (shifted by one decision)

void bot_init(Bot *b)
{
    b->last = 0;
    b->nodes = 0;
    b->depth_best = 0;
    b->found = 0;
    b->nhint = 0;
}

// Depth-first search; returns 1 if a plan of the full depth survives from the current state.
static int dfs(Game *gm, int depth, int prev, float x1)
{
    if (depth >= MAX_DEPTH) { memcpy(g_sol, g_path, sizeof g_sol); return 1; }
    if (depth > g_depth_reached) g_depth_reached = depth;
    game_snap_save(gm, &g_snap[depth], gm->p.x - 100, x1);
    int first = prev;
    if (g_hint && g_hint[depth] >= 0) first = g_hint[depth];
    int order[2] = {first, !first};
    for (int i = 0; i < 2; i++) {
        if (--g_budget < 0) break;
        int a = order[i];
        game_snap_restore(gm, &g_snap[depth]);
        int rc = game_sim(gm, a, frames_at(depth));
        if (rc == 1) continue;
        g_path[depth] = (signed char)a;
        if (rc == 2) { memcpy(g_sol, g_path, sizeof g_sol); for (int k = depth + 1; k < MAX_DEPTH; k++) g_sol[k] = (signed char)a; return 1; }
        if (dfs(gm, depth + 1, a, x1)) {
            game_snap_restore(gm, &g_snap[depth]);
            return 1;
        }
    }
    game_snap_restore(gm, &g_snap[depth]);
    return 0;
}

int bot_decide(Bot *b, Game *gm)
{
    const float range = gm->p.speed * ((NEAR_DEPTH * FRAMES_NEAR + (MAX_DEPTH - NEAR_DEPTH) * FRAMES_FAR) / 60.0f) + 250.0f;
    const float x1 = gm->p.x + range;
    const int input_before = gm->input;
    GameSnap root;
    memset(&root, 0, sizeof root);
    game_snap_save(gm, &root, gm->p.x - 100, x1);

    int choice = b->last, best_reach = -1, found = 0;
    long used = 0;
    g_hint = b->nhint ? b->hint : NULL;
    int first = g_hint && g_hint[0] >= 0 ? g_hint[0] : b->last;
    int order[2] = {first, !first};
    for (int i = 0; i < 2 && !found; i++) {
        int a = order[i];
        g_budget = budget_per_root();
        g_depth_reached = 0;
        game_snap_restore(gm, &root);
        int rc = game_sim(gm, a, FRAMES_NEAR);
        if (rc != 1) {
            g_path[0] = (signed char)a;
            if (rc == 2) { found = 1; choice = a; for (int k = 0; k < MAX_DEPTH; k++) g_sol[k] = (signed char)a; }
            else if (dfs(gm, 1, a, x1)) { choice = a; found = 1; }
            else if (g_depth_reached > best_reach) { best_reach = g_depth_reached; choice = a; }
        }
        used += budget_per_root() - g_budget;
    }
    game_snap_restore(gm, &root);
    game_snap_free(&root);
    gm->input = input_before;
    b->nodes = used;
    b->depth_best = best_reach;
    b->last = choice;
    b->found = found;
    if (found) {
        // expand the solution to a per-frame timeline, drop the frames just consumed and re-quantise
        static signed char tl[400];
        int nf = 0;
        for (int d = 0; d < MAX_DEPTH; d++)
            for (int f = 0; f < frames_at(d); f++) tl[nf++] = g_sol[d];
        int shift = FRAMES_NEAR, start = 0;
        for (int d = 0; d < MAX_DEPTH; d++) {
            int idx = start + shift;
            b->hint[d] = idx < nf ? tl[idx] : tl[nf - 1];
            start += frames_at(d);
        }
        b->nhint = MAX_DEPTH;
    } else {
        b->nhint = 0;
    }
    return choice;
}
