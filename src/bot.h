// SPDX-License-Identifier: GPL-3.0-or-later
// Test bot: receding-horizon depth-first search over button plans, simulated on the real game state.
#ifndef GD_BOT_H
#define GD_BOT_H

#include "game.h"

typedef struct {
    int last;        // previous decision
    long nodes;      // statistics of the last decision
    int depth_best;  // deepest plan found in the last decision
    int found;       // 1 if a plan of the full horizon survives
    signed char hint[32];  // previous solution, shifted by one decision
    int nhint;
    unsigned explore, rng;  // after failures: try the actions in a different (random) order
} Bot;

void bot_init(Bot *b);
// Returns 1 to hold the button, 0 to release it. Leaves the game state untouched.
int bot_decide(Bot *b, Game *gm);

#endif
