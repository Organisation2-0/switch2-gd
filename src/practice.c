// SPDX-License-Identifier: GPL-3.0-or-later
// Practice mode: checkpoints (set by hand or by themselves) from which the icon starts again
// after a crash, as in the original.
//
// A checkpoint is the complete state of the level. The engine respawns at gm->practice, which
// always points to the newest one.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

#define AUTO_EVERY 2.0f    // seconds of play between two automatic checkpoints
#define AUTO_PROOF 0.8f    // ... which only count if the icon lives this much longer

static void use_newest(App *a)
{
    a->game.practice = a->ncheck ? &a->check[a->ncheck - 1] : NULL;
}

// Forgets all checkpoints (their memory is kept for the next ones).
static void clear(App *a)
{
    a->ncheck = 0;
    a->pending = 0;
    a->since_check = 0;
    use_newest(a);
}

void practice_free(App *a)
{
    clear(a);
    for (int i = 0; i < MAX_CHECKPOINTS; i++) game_checkpoint_free(&a->check[i]);
    game_checkpoint_free(&a->trial);
    memset(a->check, 0, sizeof a->check);
    memset(&a->trial, 0, sizeof a->trial);
    a->practice = 0;
}

// Takes the state in *c as the newest checkpoint; the oldest makes room if all are in use.
static void push(App *a, const GameCheckpoint *c)
{
    if (a->ncheck == MAX_CHECKPOINTS) {
        const GameCheckpoint oldest = a->check[0];
        memmove(a->check, a->check + 1, (MAX_CHECKPOINTS - 1) * sizeof a->check[0]);
        a->check[--a->ncheck] = oldest;
    }
    // exchange the buffers: the slot's old memory becomes the next trial
    const GameCheckpoint spare = a->check[a->ncheck];
    a->check[a->ncheck++] = *c;
    a->trial = spare;
    use_newest(a);
}

static int can_save(const App *a)
{
    const Game *gm = &a->game;
    return a->practice && a->game_loaded && !gm->p.dead && !gm->finished && a->intro <= 0;
}

void practice_place(App *a)
{
    if (!can_save(a)) return;
    game_checkpoint_save(&a->game, &a->trial);
    const GameCheckpoint c = a->trial;
    push(a, &c);
    a->pending = 0;
    a->since_check = 0;
}

void practice_remove(App *a)
{
    if (!a->practice || !a->ncheck) return;
    a->ncheck--;
    a->pending = 0;
    a->since_check = 0;
    use_newest(a);
}

void practice_frame(App *a, float dt)
{
    if (!a->practice || !a->game_loaded) return;
    const Game *gm = &a->game;
    if (gm->p.dead) {   // a crash makes the state saved just before it worthless
        a->pending = 0;
        a->since_check = 0;
        return;
    }
    if (!can_save(a)) return;
    a->since_check += dt;
    if (a->pending > 0) {
        a->pending += dt;
        if (a->pending >= AUTO_PROOF) {
            const GameCheckpoint c = a->trial;
            push(a, &c);
            a->since_check = a->pending;
            a->pending = 0;
        }
    } else if (a->since_check >= AUTO_EVERY) {
        // the walking forms only while they stand on something, the others at any time
        const int walks = gm->p.mode == M_CUBE || gm->p.mode == M_ROBOT || gm->p.mode == M_BALL ||
                          gm->p.mode == M_SPIDER;
        if (!walks || gm->p.on_ground) {
            game_checkpoint_save(gm, &a->trial);
            a->pending = 1e-4f;
        }
    }
}

// The two best values of a level are kept apart: what is reached with checkpoints only counts
// for the practice bar.
void practice_keep_best(App *a)
{
    if (!a->game_loaded) return;
    LevelEntry *e = &a->levels[a->cur];
    float *best = a->practice ? &e->practice : &e->best;
    if (a->game.best_percent > *best) *best = a->game.best_percent;
}

// From the pause menu: on goes on from here with checkpoints, off starts the level again.
void practice_toggle(App *a)
{
    if (!a->game_loaded) return;
    practice_keep_best(a);
    if (!a->practice) {
        a->practice = 1;
        clear(a);
        a->game.best_percent = a->levels[a->cur].practice;
        a->paused = 0;
    } else {
        a->practice = 0;
        clear(a);
        a->game.best_percent = a->levels[a->cur].best;
        restart(a);
    }
}

// A restart begins at the start of the level again, without checkpoints.
void practice_restarted(App *a) { clear(a); }

// The checkpoints standing in the level.
void practice_draw(App *a)
{
    if (!a->practice) return;
    const Game *gm = &a->game;
    const GdFrame *f = gfx_find(&a->g, "checkpoint_01_001.png");
    if (!f) return;
    const float scale = a->lh / VIEW_H, k = scale / a->g.atlas.px_per_unit;
    for (int i = 0; i < a->ncheck; i++) {
        const GameCheckpoint *c = &a->check[i];
        float x, y;
        game_screen_pos(gm, c->s.p.x, c->s.p.y, &x, &y);
        if (x < -40.0f || x > a->lw + 40.0f) continue;
        gfx_frame(&a->g, f, x, y, 0, k, k, WHITE, 0);
    }
}
