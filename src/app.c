// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry Dash fan port: application, menus and main loop (SDL2).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

#include "../vendor/stb_image_write.h"
#ifdef GD_ALIGN
#include <dirent.h>
#include "../vendor/stb_image.h"
#endif
#include "aio.h"
#include "app.h"
#include "audio.h"
#include "bot.h"

static const struct { const char *name; int diff, stars; } k_official[] = {
    {"Stereo Madness", 1, 1},   {"Back On Track", 1, 2},       {"Polargeist", 2, 3},
    {"Dry Out", 2, 4},          {"Base After Base", 3, 5},     {"Can't Let Go", 3, 6},
    {"Jumper", 4, 7},           {"Time Machine", 4, 8},        {"Cycles", 4, 9},
    {"xStep", 5, 10},           {"Clutterfunk", 5, 11},        {"Theory of Everything", 5, 12},
    {"Electroman Adventures", 5, 10}, {"Clubstep", 6, 14},     {"Electrodynamix", 5, 12},
    {"Hexagon Force", 5, 12},   {"Blast Processing", 4, 10},   {"Theory of Everything 2", 6, 14},
    {"Geometrical Dominator", 4, 10}, {"Deadlocked", 6, 15},   {"Fingerdash", 5, 12},
    {"Dash", 5, 12},
};

// ------------------------------------------------------------ helpers

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f) fclose(f);
    return f != NULL;
}

static LevelEntry *add_level(App *a)
{
    LevelEntry *e = &a->levels[a->nlevels++];
    memset(e, 0, sizeof *e);
    e->info.length = -1;
    return e;
}

// A level file from the levels folder; its name, creator ... come from the file if it has them.
static void add_file(App *a, const char *path, const char *file)
{
    if (strlen(path) >= sizeof a->levels->path) return;
    LevelEntry *e = add_level(a);
    snprintf(e->path, sizeof e->path, "%.*s", (int)sizeof e->path - 1, path);
    if (gd_info_file(path, &e->info)) e->info.length = -1;
    if (aio_is_bundle(path)) {
        size_t n = 0;
        unsigned char *name = aio_chunk(path, "NAME", &n);
        snprintf(e->song_name, sizeof e->song_name, "%s", name && *name ? (const char *)name : "Song");
        free(name);
    }
    if (e->info.name[0]) snprintf(e->name, sizeof e->name, "%s", e->info.name);
    else {
        snprintf(e->name, sizeof e->name, "%.90s", file);
        char *dot = strrchr(e->name, '.');
        if (dot) *dot = 0;
    }
}

#define MAX_FILES 255

// The official levels, then the custom ones: files in the levels folder and the online levels
// saved in the player's own Geometry Dash.
static void scan_levels(App *a)
{
    a->nsaved = a->save_path[0] ? gd_saved_load(a->save_path, &a->saved) : 0;
    a->levels = calloc((size_t)(22 + MAX_FILES + a->nsaved), sizeof(LevelEntry));
    for (int i = 0; i < 22; i++) {
        char path[600];
        snprintf(path, sizeof path, "%s/levels/%d.txt", a->resdir, i + 1);
        if (!file_exists(path)) continue;
        LevelEntry *e = add_level(a);
        snprintf(e->path, sizeof e->path, "%s", path);
        snprintf(e->name, sizeof e->name, "%s", k_official[i].name);
        e->diff = k_official[i].diff;
        e->stars = k_official[i].stars;
        e->official = 1;
        e->track = i;
    }
    a->nofficial = a->nlevels;
    const int last = a->nofficial + MAX_FILES;
#ifdef _WIN32
    char pat[640];
    wchar_t wide[640];   // file names as UTF-8
    snprintf(pat, sizeof pat, "%s/*", a->custom_dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = MultiByteToWideChar(CP_UTF8, 0, pat, -1, wide, 640) ? FindFirstFileW(wide, &fd) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char name[600], path[1200];
            if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL)) continue;
            const char *dot = strrchr(name, '.');
            if (!dot || (SDL_strcasecmp(dot, ".gdlvl") && SDL_strcasecmp(dot, AIO_EXTENSION))) continue;
            snprintf(path, sizeof path, "%s/%s", a->custom_dir, name);
            add_file(a, path, name);
        } while (a->nlevels < last && FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    // On the console, custom levels are listed from a fixed file: levels.txt (one path per line).
    char lst[640];
    snprintf(lst, sizeof lst, "%s/levels.txt", a->custom_dir);
    FILE *f = fopen(lst, "rb");
    if (f) {
        char line[600];
        while (fgets(line, sizeof line, f) && a->nlevels < last) {
            line[strcspn(line, "\r\n")] = 0;
            if (!*line) continue;
            const char *s = strrchr(line, '/');
            add_file(a, line, s ? s + 1 : line);
        }
        fclose(f);
    }
#endif
    for (int i = 0; i < a->nsaved; i++) {
        LevelEntry *e = add_level(a);
        e->info = a->saved[i].info;
        e->data = a->saved[i].data;
        snprintf(e->name, sizeof e->name, "%s", e->info.name);
        snprintf(e->path, sizeof e->path, "saved level %d", e->info.id);
    }
}

void apply_skin(App *a)
{
    if (!a->game_loaded) return;
    float c1[3], c2[3];
    palette_color(a->cfg.col1, c1);
    palette_color(a->cfg.col2, c2);
    game_set_skin(&a->game, a->cfg.icon, c1, c2);
    game_reset(&a->game);
}

static void load_bg_images(App *a, const Game *gm)
{
    char name[128];
    gfx_free_image(&a->bg);
    gfx_free_image(&a->ground);
    snprintf(name, sizeof name, "game_bg_%02d_001-hd.png", gm->bg_id);
    if (gfx_load_image(&a->g, name, &a->bg)) gfx_load_image(&a->g, "game_bg_01_001-hd.png", &a->bg);
    snprintf(name, sizeof name, "groundSquare_%02d_001-hd.png", gm->ground_id);
    if (gfx_load_image(&a->g, name, &a->ground))
        gfx_load_image(&a->g, "groundSquare_01_001-hd.png", &a->ground);
    gfx_free_image(&a->ground2);
    if (gm->ground_id >= 8) {   // grounds from 8 on come in two layers
        snprintf(name, sizeof name, "groundSquare_%02d_2_001-hd.png", gm->ground_id);
        if (gfx_load_image(&a->g, name, &a->ground2)) memset(&a->ground2, 0, sizeof a->ground2);
    }
}

int start_level(App *a, int idx)
{
    if (a->game_loaded) game_free(&a->game);
    if (game_load(&a->game, &a->g, a->levels[idx].path, a->levels[idx].data)) { a->game_loaded = 0; return -1; }
    a->game_loaded = 1;
    snprintf(a->game.name, sizeof a->game.name, "%s", a->levels[idx].name);
    a->game.best_percent = a->levels[idx].best;
    load_bg_images(a, &a->game);
    a->game.ground2 = &a->ground2;
    apply_skin(a);
    a->cur = idx;
    a->music_key[0] = 0;
    a->hitch = 1;
    a->intro = 0;
    ui_go(a, SC_PLAY);
    a->paused = 0;
    return 0;
}

void leave_level(App *a)
{
    play_effect(a, SFX_QUIT);
    if (a->game_loaded) a->levels[a->cur].best = a->game.best_percent;
    if (a->cur < a->nofficial) {
        a->sel = a->cur;
        ui_go(a, SC_SELECT);
    } else {
        a->online_sel = a->cur - a->nofficial;
        ui_go(a, SC_LEVEL);
    }
}

void restart(App *a)
{
    a->intro = 0;
    game_reset(&a->game);
    a->game.attempts++;
    a->paused = 0;
}

// ------------------------------------------------------------ music

void level_song_key(const LevelEntry *e, char *out, size_t cap)
{
    const char *name = e->path;
    for (const char *p = e->path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    // a bundle is known by its file: a saved level with the same number may have another choice
    if (e->info.id > 0 && !e->song_name[0]) snprintf(out, cap, "id:%d", e->info.id);
    else snprintf(out, cap, "level:%s", name);
}

const char *level_choice(const LevelEntry *e)
{
    char key[SONG_NAME + 8];
    level_song_key(e, key, sizeof key);
    const char *picked = songs_choice(key);
    if (*picked && (strcmp(picked, "bundle") || e->song_name[0])) return picked;
    return e->song_name[0] ? "bundle" : "none";
}

int level_song(const App *a, int idx, char *out, size_t cap)
{
    const LevelEntry *e = &a->levels[idx];
    if (e->official) {
        char choice[24];
        snprintf(choice, sizeof choice, "official:%d", e->track);
        return songs_path(choice, a->resdir, a->music_dir, out, cap);
    }
    const char *choice = level_choice(e);
    if (!strcmp(choice, "bundle")) {   // the player reads the song out of the level file
        snprintf(out, cap, "%s", e->path);
        return 1;
    }
    return songs_path(choice, a->resdir, a->music_dir, out, cap);
}

// The effects with their loudness against the music, measured in a recording of the original
// (leaving a level was not in it and uses the level of the start sound).
void play_effect(const App *a, int id)
{
    static const struct { const char *file; float gain; } k[SFX_COUNT] = {
        {"playSound_01.ogg", 0.2f}, {"quitSound_01.ogg", 0.2f}, {"explode_11.ogg", 0.44f}};
    char path[700];
    snprintf(path, sizeof path, "%s/%s", a->resdir, k[id].file);
    audio_sfx(id, path, k[id].gain);
}

// Tells the player what belongs to the present screen: the menu loop (it rises from silence), the
// song of the level from the start of each attempt (silent after a crash), or the song tried out
// in the song list.
static void music_update(App *a)
{
    char path[720] = "", key[900];
    double offset = 0;
    int loop = 1;
    float fade_in = 0;
    const Game *gm = &a->game;
    const int playing = a->scene == SC_PLAY && a->game_loaded;
    if (playing && a->intro > 0) {
        snprintf(key, sizeof key, "intro");
    } else if (playing && gm->p.dead) {
        snprintf(key, sizeof key, "crash");
    } else if (playing) {
        snprintf(key, sizeof key, "level %d run %d", a->cur, gm->run);
        offset = gm->song_offset;
        loop = 0;
    } else if (a->scene == SC_SONGS && a->preview[0]) {
        snprintf(path, sizeof path, "%s", a->preview);
        snprintf(key, sizeof key, "try %s", path);
        fade_in = 0.4f;
    } else {
        snprintf(path, sizeof path, "%s/menuLoop.mp3", a->resdir);
        snprintf(key, sizeof key, "menu");
        fade_in = 1.0f;
    }
    if (strcmp(key, a->music_key)) {
        const int attempt = !strncmp(key, "level ", 6);
        // a new attempt cuts off the sound of the crash
        if (attempt && (!strcmp(a->music_key, "crash") || !strncmp(a->music_key, "level ", 6))) audio_sfx_stop();
        snprintf(a->music_key, sizeof a->music_key, "%s", key);
        a->music_faded = 0;
        if (attempt && !level_song(a, a->cur, path, sizeof path)) path[0] = 0;
        if (!strcmp(key, "crash")) {
            audio_stop();
            play_effect(a, SFX_CRASH);
        } else if (!strcmp(key, "intro")) {
            audio_fade_out(0.08f);   // the menu music ends with the press of the button
        } else if (path[0]) {
            audio_play(path, offset, loop, fade_in);
        } else {
            audio_stop();
        }
    }
    audio_pause(playing && a->paused);
    if (playing && gm->finished && !a->music_faded) {
        audio_fade_out(1.5f);
        a->music_faded = 1;
    }
    if (a->game_loaded) {
        const int heard = playing && a->intro <= 0;
        a->game.pulse = heard ? audio_pulse() : -1.0f;
        a->game.loud = heard ? audio_loudness() : -1.0f;
    }
}

// ------------------------------------------------------------ in-level display

static void text(App *a, const char *s, float x, float y, float sc, int align, GdColorF c)
{
    // shadow for readability
    gfx_text(&a->g, &a->big, s, x + 2 * sc * 2, y + 2 * sc * 2, sc, align, C(0, 0, 0, 0.35f * c.a));
    gfx_text(&a->g, &a->big, s, x, y, sc, align, c);
}

static char g_bot_hud[256];

// Progress bar and percentage as in the original.
static void draw_hud(App *a)
{
    Game *gm = &a->game;
    Gfx *g = &a->g;
    const float u = a->lh / VIEW_H;                    // pixels per game unit
    const float pct = fminf(fmaxf(gm->percent, 0.0f), 100.0f);
    const float w = 210.0f * u, h = 16.0f * u, x = a->lw / 2 - w / 2, y = 0.0f;
    if (a->groove.tex && a->stripes.tex) {
        const float tile = 32.0f * u, left = x + 2.0f * u, fill = (w - 4.0f * u) * pct / 100.0f;
        GdColorF c = {gm->c1[0], gm->c1[1], gm->c1[2], 1};
        for (float at = 0; at < fill; at += tile) {
            const float part = fminf(tile, fill - at);
            gfx_image_rect(g, &a->stripes, (SDL_FRect){0, 0, a->stripes.w * part / tile, (float)a->stripes.h},
                           (SDL_FRect){left + at, y + h * 0.25f, part, h * 0.5f}, c);
        }
        gfx_image_rect(g, &a->groove, (SDL_FRect){0, 0, (float)a->groove.w, (float)a->groove.h},
                       (SDL_FRect){x, y, w, h}, WHITE);
    } else {
        gfx_fill(g, (SDL_FRect){x, y + 8, w, 14}, C(0, 0, 0, 0.5f));
        gfx_fill(g, (SDL_FRect){x + 2, y + 10, (w - 4) * pct / 100.0f, 10}, C(90, 255, 70, 1));
    }
    char s[16];
    snprintf(s, sizeof s, "%d%%", (int)pct);
    text(a, s, x + w + 5.0f * u, y - 2.0f * u, 0.6f, 0, WHITE);
}

static void draw_play(App *a)
{
    a->nhot = 0;
    Game *gm = &a->game;
    Gfx *g = &a->g;
    float scale = a->lh / VIEW_H;
    float vw = a->lw / scale;
    game_draw(gm, g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);

    char s[128];
    // "Attempt N" stands in the level, in the middle of the first screen
    const float at_x = pt_width(a) * 0.5f - gm->cam_x;
    if (at_x > -150.0f && !gm->finished) {
        snprintf(s, sizeof s, "Attempt %d", gm->attempts);
        label(a, &a->big, s, at_x, 243.0f - (gm->cam_bottom - -90.0f), 1.0f, 1, WHITE, 0);
    }
    draw_hud(a);
#ifdef __SWITCH__
    const GdFrame *pause = gfx_find(g, "GJ_pauseBtn_001.png");   // the original shows it on touch devices
    if (pause) gfx_frame(g, pause, UI_W - 50, 50, 0, 0.7f, 0.7f, WHITE, 0);
#endif
    if (g_bot_hud[0]) text(a, g_bot_hud, 20, 672, 0.32f, 0, C(255, 255, 255, 0.9f));

    if (gm->finished) ui_draw_complete(a);
    else if (a->paused) ui_draw_pause(a);
}

// ------------------------------------------------------------ bot run (development aid)
//
// Plays levels with the look-ahead bot. Where a level cannot be passed (features the engine does
// not simulate, e.g. moving objects), the stretch is skipped with noclip after a few identical
// deaths so that every part of every level is still visited and tested.

#define BOT_MAX_SKIPS 64
#define BOT_MAX_CP 220
#define BOT_MAX_FAILS 80
#define BOT_MAX_DEATHS 1500
#define BOT_CP_SPACING 300.0f

typedef struct {
    Bot bot;
    int deaths, attempts_cap;
    float front_x;                        // furthest place where it failed, and how often
    int fails;
    float skip_from[BOT_MAX_SKIPS], skip_to[BOT_MAX_SKIPS];
    int nskips;
    unsigned modes;                       // bit per player mode seen
    int saw_dual, saw_mirror, saw_mini, saw_flip;
    int tick, nofound;
    float max_x;
    GameCheckpoint cps[BOT_MAX_CP];
    int ncp;
    float next_cp_x;
    char label[128];
} BotRun;

#define MODE_COUNT 8
static const char *const k_mode_names[MODE_COUNT] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};

static void botrun_end(App *a, BotRun *r)
{
    a->game.practice = NULL;
    a->game.noclip = 0;
    for (int i = 0; i < r->ncp; i++) game_checkpoint_free(&r->cps[i]);
    r->ncp = 0;
}

static void botrun_begin(BotRun *r)
{
    memset(r, 0, sizeof *r);
    bot_init(&r->bot);
}

static void botrun_summary(App *a, BotRun *r, int idx, const char *why)
{
    char m[128] = "";
    for (int i = 0; i < MODE_COUNT; i++)
        if (r->modes & (1u << i)) { strcat(m, m[0] ? "," : ""); strcat(m, k_mode_names[i]); }
    printf("level %2d %-24s %-9s deaths=%3d skips=%2d modes=[%s]%s%s%s%s\n", idx + 1,
           a->levels[idx].name, why, r->deaths, r->nskips, m, r->saw_dual ? " dual" : "",
           r->saw_mirror ? " mirror" : "", r->saw_mini ? " mini" : "", r->saw_flip ? " gravity" : "");
    for (int i = 0; i < r->nskips; i++) printf("    skipped x=%.0f..%.0f\n", r->skip_from[i], r->skip_to[i]);
    fflush(stdout);
}

// Progress along the level. Turned gameplay and teleports make x useless, the distance covered is not.
static float progress(const Game *gm) { return gm->has_jumps ? gm->travel : gm->p.x; }
static float cp_progress(const Game *gm, const GameCheckpoint *c) { return gm->has_jumps ? c->s.travel : c->s.p.x; }

// A failure (death, or no surviving plan) sends the bot back to an earlier checkpoint, where it
// tries other decisions; how far back varies from attempt to attempt. Only when that fails many
// times at the same place is the stretch skipped.
static void botrun_retry(App *a, BotRun *r, float x)
{
    if (x > r->front_x + 300.0f) { r->front_x = x; r->fails = 0; }
    r->fails++;
    if (r->fails > BOT_MAX_FAILS && r->nskips < BOT_MAX_SKIPS) {
        r->skip_from[r->nskips] = r->front_x - 300;
        r->skip_to[r->nskips] = r->front_x + 700;
        r->nskips++;
        r->fails = 1;
    }
    // each round of 13 attempts reaches further back (wrong turns can show up much later)
    const float back = 150.0f + 230.0f * (float)((r->fails - 1) % 13) + 900.0f * (float)((r->fails - 1) / 13);
    int k = 0;
    for (int i = 0; i < r->ncp; i++) if (cp_progress(&a->game, &r->cps[i]) <= r->front_x - back) k = i;
    if (r->ncp) { a->game.practice = &r->cps[k]; r->ncp = k + 1; r->next_cp_x = cp_progress(&a->game, &r->cps[k]) + BOT_CP_SPACING; }
    bot_init(&r->bot);
    r->bot.explore = r->fails > 1 ? (unsigned)r->fails : 0;
    r->bot.rng = (unsigned)(r->fails * 2654435761u) ^ (unsigned)r->front_x;
    r->nofound = 0;
}

// Advances the bot-controlled game by one 60 Hz frame. Returns 1 when the level is over.
static int botrun_frame(App *a, BotRun *r, int idx)
{
    Game *gm = &a->game;
    if (gm->finished) { botrun_summary(a, r, idx, "COMPLETE"); botrun_end(a, r); return 1; }
    int noclip = 0;
    for (int i = 0; i < r->nskips; i++)
        if (progress(gm) >= r->skip_from[i] && progress(gm) <= r->skip_to[i]) noclip = 1;
    gm->noclip = noclip;
    gm->bot_strict = !noclip;
    const int stop = getenv("BOT_STOP") != NULL;   // debugging: end at the first failure
    if (!gm->p.dead) {
        if ((r->tick++ & 1) == 0) game_set_hold(gm, bot_decide(&r->bot, gm));
        if (r->bot.explore && progress(gm) > r->front_x + 400.0f) r->bot.explore = 0;   // got past it
        // no survivable plan for 4 s of game time: treat it like a death
        if (!r->bot.found && !gm->noclip) r->nofound++; else if (r->bot.found) r->nofound = 0;
        if (r->nofound >= 240) {
            if (stop || getenv("BOT_LOG"))
                printf("  L%d no plan at x=%.0f y=%.0f mode=%d grav=%d\n", idx + 1, gm->p.x, gm->p.y, gm->p.mode, gm->p.grav);
            if (stop) { botrun_summary(a, r, idx, "STOPPED"); botrun_end(a, r); return 1; }
            botrun_retry(a, r, progress(gm));
            gm->p.dead = 1;
            gm->p.dead_t = 1.0f;
        }
        if (gm->noclip) {   // keep the icon in a sane place while ignoring collisions
            Player *pl = &gm->p;
            if (pl->y > 420.0f || (pl->grav < 0 && pl->mode == M_CUBE)) {
                pl->y = pl->mini ? 9.0f : 15.0f; pl->grav = 1; pl->yv = 0;
            }
        }
        if (r->bot.found && !gm->p.dead && progress(gm) >= r->next_cp_x && r->ncp < BOT_MAX_CP && !gm->noclip) {
            game_checkpoint_save(gm, &r->cps[r->ncp++]);
            r->next_cp_x = progress(gm) + BOT_CP_SPACING;
        }
    }
    int was_dead = gm->p.dead;
    game_update(gm, 1.0f / 60.0f);
    {
        const char *t0 = getenv("GDTX0"), *t1 = getenv("GDTX1");
        if (t0 && t1 && gm->p.x >= (float)atof(t0) && gm->p.x <= (float)atof(t1))
            printf("    x=%.1f y=%.1f yv=%.2f g=%d hold=%d og=%d rise=%d mode=%d found=%d depth=%d dual=%d y2=%.1f g2=%d m2=%d fwd=%d,%d up=%d,%d ch=%d free=%d\n",
                   gm->p.x, gm->p.y, gm->p.yv, gm->p.grav, gm->p.hold, gm->p.on_ground, gm->p.rising, gm->p.mode,
                   r->bot.found, r->bot.depth_best, gm->dual, gm->p2.y, gm->p2.grav, gm->p2.mode,
                   gm->frame.fx, gm->frame.fy, gm->frame.ux, gm->frame.uy, gm->channel, gm->free);
    }
    if (getenv("GDCAM") && ((int)gm->p.x % 60) < 3) printf("CAM x=%.0f y=%.0f cam=%.0f ctr=%.0f mode=%d dual=%d mini=%d grav=%d\n", gm->p.x, gm->p.y, gm->cam_bottom, gm->ctr, gm->p.mode, gm->dual, gm->p.mini, gm->p.grav);
    r->modes |= 1u << gm->p.mode;
    if (gm->dual) r->saw_dual = 1;
    if (gm->mirror) r->saw_mirror = 1;
    if (gm->p.mini) r->saw_mini = 1;
    if (gm->p.grav < 0) r->saw_flip = 1;
    if (gm->p.x > r->max_x) r->max_x = gm->p.x;
    if (!was_dead && gm->p.dead) {
        r->deaths++;
        if (stop || getenv("BOT_LOG"))
            printf("  L%d death %d at x=%.0f y=%.0f g=%d mini=%d dual=%d mode=%s (%s)\n", idx + 1, r->deaths, gm->p.x,
                   gm->p.y, gm->p.grav, gm->p.mini, gm->dual, k_mode_names[gm->p.mode], gm->death_info);
        botrun_retry(a, r, progress(gm));
        if (r->deaths > BOT_MAX_DEATHS) { botrun_summary(a, r, idx, "GAVE UP"); botrun_end(a, r); return 1; }
        if (stop) { botrun_summary(a, r, idx, "STOPPED"); botrun_end(a, r); return 1; }
    }
    return 0;
}

// Fast, headless: plays one level to the end and prints a summary.
static int save_shot(App *a, const char *path);
static void render(App *a);

static int selftest(App *a, int idx, float seconds)
{
    if (start_level(a, idx) != 0) { puts("load failed"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 4096);
    if (getenv("BOT_LOG")) setvbuf(stdout, NULL, _IONBF, 0);   // keep the log if the run is stopped
    static BotRun r;
    botrun_begin(&r);
    // development: BOT_LOAD=<file> starts from a saved state; BOT_SAVE=<x>:<file> writes one when the
    // bot passes x (BOT_SAVE_EXIT=1 stops there)
    const char *load = getenv("BOT_LOAD"), *save = getenv("BOT_SAVE");
    if (load) {
        if (game_state_io(&a->game, load, 0)) { puts("cannot load the saved state"); return 1; }
        game_checkpoint_save(&a->game, &r.cps[r.ncp++]);
        r.next_cp_x = progress(&a->game) + BOT_CP_SPACING;
        r.front_x = progress(&a->game);
    }
    float save_x = save ? (float)atof(save) : 0;
    const char *save_path = save ? strchr(save, ':') : NULL;
    // BOT_SAVE_EVERY=<prefix> keeps a state every 1500 units of progress (<prefix><n>.bin)
    const char *every = getenv("BOT_SAVE_EVERY");
    int saved_n = load ? (int)(progress(&a->game) / 1500.0f) : 0;
    // BOT_SHOT=<prefix> saves a picture every 600 units of progress (<prefix><n>.png)
    const char *shots = getenv("BOT_SHOT");
    int shot_n = 0;
    long frames = 0;
    while (frames < (long)(seconds * 60)) {
        if (botrun_frame(a, &r, idx)) return 0;
        frames++;
        if (shots && !a->game.p.dead && (int)(progress(&a->game) / 600.0f) > shot_n) {
            char path[512];
            shot_n = (int)(progress(&a->game) / 600.0f);
            snprintf(path, sizeof path, "%s%02d.png", shots, shot_n);
            render(a);
            save_shot(a, path);
        }
        if (every && !a->game.p.dead && (int)(progress(&a->game) / 1500.0f) > saved_n) {
            char path[512];
            saved_n = (int)(progress(&a->game) / 1500.0f);
            snprintf(path, sizeof path, "%s%d.bin", every, saved_n);
            if (!game_state_io(&a->game, path, 1))
                printf("checkpoint %d at x=%.0f (deaths %d, skips %d)\n", saved_n, a->game.p.x, r.deaths, r.nskips);
        }
        if (save_path && !a->game.p.dead && a->game.p.x >= save_x) {
            if (game_state_io(&a->game, save_path + 1, 1)) puts("cannot save the state");
            else printf("state saved at x=%.0f (deaths %d, skips %d)\n", a->game.p.x, r.deaths, r.nskips);
            save_path = NULL;
            if (getenv("BOT_SAVE_EXIT")) break;
        }
    }
    botrun_summary(a, &r, idx, "TIMEOUT");
    botrun_end(a, &r);
    return 0;
}

// Renders N frames of the current scene and prints the average frame time (development aid).
static void bench(App *a, int frames)
{
    Uint64 t0 = SDL_GetPerformanceCounter();
    for (int i = 0; i < frames; i++) {
        if (a->scene == SC_PLAY) game_update(&a->game, 1.0f / 60.0f);
        a->t += 1.0f / 60.0f;
        gfx_begin_frame(&a->g);
        if (a->scene == SC_PLAY) draw_play(a);
        gfx_present(&a->g);
    }
    double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
    printf("bench: %d frames, %.2f ms/frame (%.0f fps)\n", frames, ms / frames, 1000.0 / (ms / frames));
}

// ------------------------------------------------------------ screenshots (development aid)

static int save_shot(App *a, const char *path)
{
    int w, h;
    unsigned char *px = gfx_read_pixels(&a->g, &w, &h);
    if (!px) return -1;
    for (int i = 0; i < w * h; i++) px[i * 4 + 3] = 255;
    int ok = stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
    return ok ? 0 : -1;
}

static void render(App *a)
{
    gfx_begin_frame(&a->g);
    a->nhot = 0;
    if (a->scene == SC_PLAY) draw_play(a);
    else ui_draw(a);
    // a finished level offers its buttons as soon as it ends
    if (a->scene == SC_PLAY && a->game.finished && a->focus_id != H_RESTART && a->focus_id != H_MENU) {
        a->focus_id = H_RESTART;
        a->focus_arg = 0;
    }
    if (a->scene != a->shown) {
        a->shown = a->scene;
        a->shown_since = a->t;
    }
    float dark = 1.0f - (a->t - a->shown_since) / 0.25f;
    // a level chosen in a menu appears out of the dark like in the original
    if (a->scene == SC_PLAY && a->intro > 0) dark = fmaxf(dark, 1.0f - (LEVEL_INTRO - a->intro - 0.15f) / 0.5f);
    if (dark > 0) gfx_fill(&a->g, (SDL_FRect){0, 0, a->lw, a->lh}, C(0, 0, 0, fminf(dark, 1.0f)));
    gfx_present(&a->g);
}

// ------------------------------------------------------------ level tour (visual comparison aid)

#ifdef GD_ALIGN
// Alignment against recordings of the original game (GD_TOURREF=<folder with *_pNNNN.jpg>):
// the tour then looks for the camera position whose picture matches the recorded one best.
// Needs -DGD_ALIGN -DSTBI_ONLY_JPEG.
#define REF_W 320
#define REF_H 200

// horizontal brightness steps of the picture scaled to REF_W x REF_H (independent of colour tints)
static void ref_edges(const unsigned char *px, int w, int h, float *out)
{
    static float g[REF_H][REF_W];
    for (int y = 0; y < REF_H; y++)
        for (int x = 0; x < REF_W; x++) {
            int sum = 0, n = 0;
            for (int yy = y * h / REF_H; yy < (y + 1) * h / REF_H; yy++)
                for (int xx = x * w / REF_W; xx < (x + 1) * w / REF_W; xx++, n++) {
                    const unsigned char *p = px + (yy * w + xx) * 4;
                    sum += p[0] + 2 * p[1] + p[2];
                }
            g[y][x] = n ? sum / (4.0f * n) : 0;
        }
    for (int y = 0; y < REF_H; y++)
        for (int x = 0; x < REF_W; x++) {
            const float d = x > 0 && x < REF_W - 1 ? g[y][x + 1] - g[y][x - 1] : 0;
            // only hard edges: the soft ones of the background move at their own speed
            out[y * REF_W + x] = fabsf(d) < 22.0f ? 0.0f : fminf(fmaxf(d, -70.0f), 70.0f);
        }
}

// best normalised correlation over vertical shifts of up to +-24 rows (96 screen px)
static float ref_score(const float *a, const float *b, int *shift)
{
    float best = -2;
    for (int dy = -24; dy <= 24; dy++) {
        double ab = 0, aa = 0, bb = 0;
        for (int y = 36; y < 176; y++)
            for (int x = 1; x < REF_W - 1; x++) {
                const float p = a[y * REF_W + x], q = b[(y + dy) * REF_W + x];
                ab += p * q; aa += p * p; bb += q * q;
            }
        const float v = (float)(ab / sqrt(aa * bb + 1e-9)) - 0.0004f * abs(dy);
        if (v > best) { best = v; *shift = dy; }
    }
    return best;
}

static int ref_load(const char *dir, int mark, float *edges)
{
    char suffix[24], path[900] = "";
    snprintf(suffix, sizeof suffix, "_p%04d.jpg", mark);
    DIR *d = opendir(dir);
    if (!d) return -1;
    for (struct dirent *e; (e = readdir(d));) {
        const size_t n = strlen(e->d_name);
        if (e->d_name[0] == 'f' && n > 10 && !strcmp(e->d_name + n - 10, suffix)) {
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            break;
        }
    }
    closedir(d);
    int w, h, c;
    unsigned char *px = path[0] ? stbi_load(path, &w, &h, &c, 4) : NULL;
    if (!px) return -1;
    ref_edges(px, w, h, edges);
    stbi_image_free(px);
    return 0;
}
#endif

//
// Runs a level without input and without deaths (like the original game with noclip) and saves a
// picture each time the player reaches one of the requested progress values (per mille).
static int tour(App *a, int idx, const char *marks, const char *dir, float extra_len)
{
    if (start_level(a, idx) != 0) { puts("load failed"); return 1; }
    Game *gm = &a->game;
    const float len = gm->end_x - 380.0f + extra_len;   // progress bar of the original -> position
    const float scale = (float)a->g.h / VIEW_H, vw = (float)a->g.w / scale;
    static const float tc1[3] = {0, 255, 255}, tc2[3] = {0, 0, 255};   // colours used in the recordings
    game_set_skin(gm, a->cfg.icon, tc1, tc2);
    game_reset(gm);
    for (const char *s = marks; s && *s; ) {
        const int mark = atoi(s);
        float target = len * (float)mark / 1000.0f + 240.0f;
#ifdef GD_ALIGN
        static float want[REF_W * REF_H], have[REF_W * REF_H];
        const char *refdir = getenv("GD_TOURREF");
        const int aligned = refdir && !ref_load(refdir, mark, want);
        float best = -2, best_x = 0;   // spots: 100 units before and after the expected one
        int best_dy = 0;
        char best_path[700];
        snprintf(best_path, sizeof best_path, "%s/p%04d.png", dir, mark);
        const float from = target - 100.0f;
        if (aligned) target += 100.0f;
#endif
        for (int frame = 0; gm->p.x < target && !gm->finished && frame < 60 * 600; frame++) {
            gm->noclip = 1;
            Player *pl = &gm->p;
            if (pl->y > 1100.0f || (pl->y > 420.0f && pl->grav < 0 && pl->mode == M_CUBE)) {
                pl->y = 15.0f; pl->grav = 1; pl->yv = 0;
            }
            float dt = 1.0f / 60.0f;
            int shown = frame % 2 == 0 || gm->p.x >= target - 60.0f;
#ifdef GD_ALIGN
            if (aligned && gm->p.x >= from) { dt = 1.0f / 240.0f; shown = 1; }   // finer steps near the spot
#endif
            game_update(gm, dt);
            // the camera is advanced while drawing too, so draw (without showing) every frame
            if (shown) {
                gfx_begin_frame(&a->g);
                game_draw(gm, &a->g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);
#ifdef GD_ALIGN
                if (aligned && gm->p.x >= from) {
                    int w, h, dy = 0;
                    if (getenv("GD_TOURHUD")) draw_hud(a);
                    unsigned char *px = gfx_read_pixels(&a->g, &w, &h);
                    if (px) {
                        ref_edges(px, w, h, have);
                        free(px);
                        const float v = ref_score(have, want, &dy);
                        if (v > best) {
                            best = v; best_x = gm->p.x; best_dy = dy;
                            save_shot(a, best_path);
                        }
                    }
                }
#endif
            }
        }
#ifdef GD_ALIGN
        if (aligned && best > -2) {
            // dx: distance from the expected spot; dy: screen px the recording sits lower than the port
            printf("p%04d x=%.0f dx=%.0f dy=%d score=%.3f mode=%d\n", mark, best_x, best_x - (target - 100.0f),
                   best_dy * 4, best, gm->p.mode);
        } else
#endif
        {
            gfx_begin_frame(&a->g);
            game_draw(gm, &a->g, vw, VIEW_H, scale, 0, 0, &a->bg, &a->ground);
            if (getenv("GD_TOURHUD")) draw_hud(a);   // laid out for 1280x720, close enough at 1280x800
            char path[700];
            snprintf(path, sizeof path, "%s/p%04d.png", dir, mark);
            if (save_shot(a, path)) { printf("cannot write %s\n", path); return 1; }
            printf("p%04d x=%.0f y=%.0f mode=%d\n", mark, gm->p.x, gm->p.y, gm->p.mode);
        }
        gfx_present(&a->g);
        const char *c = strchr(s, ',');
        s = c ? c + 1 : NULL;
    }
    return 0;
}

int main(int argc, char **argv)
{
    App *a = calloc(1, sizeof *a);
    const char *shot = NULL, *scene = "title";
    int level = 0, fullscreen = 0, bench_frames = 0;
    float tp_x = -1, tp_y = 15;
    int mode = -1, selftest_level = -1, rttest_level = -1;
    float selftest_secs = 120, rt_secs = 0;
    int rt_intro = 0;
    const char *bot_spec = NULL;
    int shot_mirror = 0, shot_mini = 0, shot_flip = 0, shot_dual = 0;
    int bot_speed = 1;
    const char *tour_marks = NULL, *tour_dir = ".";
    int tall = 0;     // 16:10 screenshots, to compare with recordings of the original
    float tour_len = 0.0f;
    int shot_tab = 0, shot_page = -1, shot_online = 0, shot_focus = 0;
    const char *export_dir = NULL;
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");   // where the game keeps its save file
    if (local) snprintf(a->save_path, sizeof a->save_path, "%s/GeometryDash/CCGameManager.dat", local);
#endif
    snprintf(a->resdir, sizeof a->resdir, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/Resources");
#else
             "C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/Resources");
#endif
    snprintf(a->custom_dir, sizeof a->custom_dir, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/levels");
#else
             "gd-levels");
#endif
    snprintf(a->music_dir, sizeof a->music_dir, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/music");
#else
             "gd-music");
#endif
    snprintf(a->cfg_path, sizeof a->cfg_path, "%s",
#ifdef __SWITCH__
             "sdmc:/switch/gd/config.ini");
#else
             "gd-config.ini");
#endif
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--res") && i + 1 < argc) snprintf(a->resdir, sizeof a->resdir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--levels") && i + 1 < argc) snprintf(a->custom_dir, sizeof a->custom_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) snprintf(a->cfg_path, sizeof a->cfg_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--music") && i + 1 < argc) snprintf(a->music_dir, sizeof a->music_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene = argv[++i];
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--x") && i + 1 < argc) tp_x = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--y") && i + 1 < argc) tp_y = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--selftest") && i + 1 < argc) selftest_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rttest") && i + 1 < argc) rttest_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--secs") && i + 1 < argc) { selftest_secs = (float)atof(argv[++i]); rt_secs = selftest_secs; }
        else if (!strcmp(argv[i], "--bot") && i + 1 < argc) bot_spec = argv[++i];
        else if (!strcmp(argv[i], "--botspeed") && i + 1 < argc) bot_speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) fullscreen = 1;
        else if (!strcmp(argv[i], "--intro")) rt_intro = 1;
        else if (!strcmp(argv[i], "--mirror")) shot_mirror = 1;
        else if (!strcmp(argv[i], "--mini")) shot_mini = 1;
        else if (!strcmp(argv[i], "--flip")) shot_flip = 1;
        else if (!strcmp(argv[i], "--dual")) shot_dual = 1;
        else if (!strcmp(argv[i], "--tour") && i + 1 < argc) tour_marks = argv[++i];
        else if (!strcmp(argv[i], "--tall")) tall = 1;
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) {   // save file of the game, or "none"
            const char *v = argv[++i];
            snprintf(a->save_path, sizeof a->save_path, "%s", strcmp(v, "none") ? v : "");
        }
        else if (!strcmp(argv[i], "--export-saved") && i + 1 < argc) export_dir = argv[++i];
        else if (!strcmp(argv[i], "--tab") && i + 1 < argc) shot_tab = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) shot_page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--online") && i + 1 < argc) shot_online = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--focus")) shot_focus = 1;
        else if (!strcmp(argv[i], "--tourdir") && i + 1 < argc) tour_dir = argv[++i];
        else if (!strcmp(argv[i], "--tourlen") && i + 1 < argc) tour_len = (float)atof(argv[++i]);
    }
#ifdef __SWITCH__
    // Log to the SD card so a failed start can be diagnosed.
    freopen("sdmc:/switch/gd/log.txt", "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    fprintf(stderr, "gd-switch2 start, res=%s\n", a->resdir);
#endif
    if (export_dir) {   // writes the saved online levels as files, for the console
        GdSavedLevel *list = NULL;
        const int n = gd_saved_load(a->save_path, &list);
        const int written = gd_saved_export(list, n, export_dir);
        printf("exported %d of %d saved levels\n", written, n);
        gd_saved_free(list, n);
        return written == n ? 0 : 1;
    }
    // the tour renders in 16:10 like the recordings of the original game
    a->lw = UI_W;
    a->lh = tour_marks || tall ? 800.0f : UI_H;
    int rc_init = gfx_init(&a->g, a->resdir, (int)a->lw, (int)a->lh, fullscreen);
    if (rc_init != 0) {
        fprintf(stderr, "init failed rc=%d (resources: %s)\n", rc_init, a->resdir);
#ifdef __SWITCH__
        // Show the error on screen instead of a black screen.
        char sdl_err[256];
        snprintf(sdl_err, sizeof sdl_err, "%s", SDL_GetError());
        gfx_quit(&a->g);   // releases the screen so that the console can use it
        consoleInit(NULL);
        printf("Geometry Dash (fan port): start failed\n\nerror %d: %s\n", rc_init,
               rc_init == -4   ? "cannot load the sprite sheets"
               : rc_init == -5 ? "cannot create textures (out of memory?)"
                               : "cannot set up graphics");
        if (rc_init == -4) printf("resources: %s\n", a->resdir);
        else printf("SDL: %s\n", sdl_err);
        printf("\nDetails: sdmc:/switch/gd/log.txt\n\nPress + to exit.\n");
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        PadState ep;
        padInitializeDefault(&ep);
        while (appletMainLoop()) {
            padUpdate(&ep);
            if (padGetButtonsDown(&ep) & HidNpadButton_Plus) break;
            consoleUpdate(NULL);
        }
        consoleExit(NULL);
#endif
        return 1;
    }
    fprintf(stderr, "gfx ok (%dx%d), %d sheets\n", a->g.w, a->g.h, a->g.atlas.nsheets);
    ui_load(a);
    scan_levels(a);
    config_load(&a->cfg, a->cfg_path);
    {   // the chosen songs are kept next to the settings
        char list[600];
        const char *name = a->cfg_path;
        for (const char *p = a->cfg_path; *p; p++)
            if (*p == '/' || *p == '\\') name = p + 1;
        snprintf(list, sizeof list, "%.*sgd-songs.ini", (int)(name - a->cfg_path), a->cfg_path);
        songs_load(list);
    }
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#else
    for (int i = 0; i < SDL_NumJoysticks(); i++) SDL_JoystickOpen(i);
    SDL_ShowCursor(SDL_ENABLE);
#endif

    if (tour_marks) {
        int rc = tour(a, level, tour_marks, tour_dir, tour_len);
        gfx_quit(&a->g);
        return rc;
    }
    if (selftest_level >= 0) {
        int rc = selftest(a, selftest_level, selftest_secs);
        gfx_quit(&a->g);
        return rc;
    }
    if (shot || bench_frames) {
        a->sel = level;
        if (!strcmp(scene, "select")) a->scene = SC_SELECT;
        else if (!strcmp(scene, "garage") || !strcmp(scene, "colors")) {
            ui_open_garage(a, shot_tab, !strcmp(scene, "colors"));
            if (shot_page >= 0) a->page = shot_page;
        }
        else if (!strcmp(scene, "controls")) ui_go(a, SC_CONTROLS);
        else if (!strcmp(scene, "settings")) ui_go(a, SC_SETTINGS);
        else if (!strcmp(scene, "songs")) { a->online_sel = shot_online; ui_open_songs(a); }
        else if (!strcmp(scene, "online") || !strcmp(scene, "level")) {
            ui_go(a, !strcmp(scene, "level") ? SC_LEVEL : SC_ONLINE);
            a->online_sel = shot_online;
        }
        else if (!strcmp(scene, "play") || !strcmp(scene, "complete") || !strcmp(scene, "pause")) {
            if (start_level(a, level) != 0) { fprintf(stderr, "cannot start level\n"); return 1; }
            if (mode >= 0) a->game.p.mode = mode;
            if (shot_mirror) { a->game.mirror = 1; a->game.mir_x = a->game.mirror ? 1280.0f / (720.0f / 320.0f) - 190.0f : 190.0f; }
            if (shot_mini) a->game.p.mini = 1;
            if (shot_flip) a->game.p.grav = -1;
            if (shot_dual) { a->game.dual = 1; a->game.p2 = a->game.p; a->game.p2.grav = -a->game.p.grav; a->game.p2.y = 200 - a->game.p.y; }
            if (mode >= 0 && mode != 0 && mode != 5) a->game.has_ceil = 1;
            if (tp_x >= 0) {
                a->game.p.x = tp_x;
                a->game.p.y = tp_y;
                a->game.prev_x = tp_x;
                a->game.prev_y = tp_y;
                a->game.cam_x = tp_x - 190;
                a->game.time = 5;
                a->game.percent = tp_x / (a->game.end_x - 380) * 100.0f;
            }
            if (!strcmp(scene, "complete")) a->game.finished = 1;
            if (!strcmp(scene, "pause")) { a->paused = 1; a->focus_id = H_RESUME; }
            level = a->cur;
        }
        if (bench_frames) {
            bench(a, bench_frames);
            gfx_quit(&a->g);
            return 0;
        }
        a->show_focus = shot_focus;
        for (int i = 0; i < 4; i++) { a->t += 0.5f; render(a); }
        int rc = save_shot(a, shot);
        gfx_quit(&a->g);
        return rc;
    }

    Uint64 rt_start = 0;
    if (rttest_level >= 0) {
        if (start_level(a, rttest_level) != 0) return 1;
        if (rt_intro) {
            a->intro = LEVEL_INTRO;
            audio_volume(a->cfg.music / 100.0f);
            play_effect(a, SFX_PLAY);
        }
        rt_start = SDL_GetPerformanceCounter();
    }
    // --bot "all" | "1,4,7-9": watch the look-ahead bot play levels in the window
    int bot_list[64], bot_n = 0, bot_pos = -1, bot_next = 0;
    static BotRun brun;
    if (bot_spec) {
        const char *s = bot_spec;
        while (*s && bot_n < 64) {
            if (!strncmp(s, "all", 3)) { for (int i = 0; i < a->nlevels && i < 22 && bot_n < 64; i++) bot_list[bot_n++] = i; s += 3; }
            else if (*s >= '0' && *s <= '9') {
                int lo = atoi(s); while (*s >= '0' && *s <= '9') s++;
                int hi = lo;
                if (*s == '-') { s++; hi = atoi(s); while (*s >= '0' && *s <= '9') s++; }
                for (int v = lo; v <= hi && bot_n < 64; v++) if (v >= 1 && v <= a->nlevels) bot_list[bot_n++] = v - 1;
            } else s++;
        }
        printf("bot: %d level(s)\n", bot_n);
    }
    // sound only when playing with a window of one's own (not for the tour, tests or the bot)
    const int sound = !bot_spec;
    if (sound) {
        audio_volume(a->cfg.music / 100.0f);
        audio_sfx_volume(a->cfg.sfx / 100.0f);
    }
    Uint64 last = SDL_GetPerformanceCounter();
    float dt_smooth = 1.0f / 60.0f;
    while (!a->quit) {
        SDL_Event e;
#ifdef __SWITCH__
        if (!appletMainLoop()) break;
        // libnx pad -> SDL joystick button numbers (same layout SDL uses on the Switch).
        static const u64 pad_map[16] = {
            HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y,
            HidNpadButton_StickL, HidNpadButton_StickR, HidNpadButton_L, HidNpadButton_R,
            HidNpadButton_ZL, HidNpadButton_ZR, HidNpadButton_Plus, HidNpadButton_Minus,
            HidNpadButton_Left, HidNpadButton_Up, HidNpadButton_Right, HidNpadButton_Down};
        padUpdate(&pad);
        u64 pdown = padGetButtonsDown(&pad), pup = padGetButtonsUp(&pad);
        for (int b = 0; b < 16; b++) {
            if (!((pdown | pup) & pad_map[b])) continue;
            SDL_Event je;
            memset(&je, 0, sizeof je);
            je.type = (pdown & pad_map[b]) ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
            je.jbutton.button = (Uint8)b;
            ui_event(a, &je);
        }
#endif
        while (SDL_PollEvent(&e)) ui_event(a, &e);
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        // Frame times jitter a little; smoothing them keeps the motion even.
        if (dt > 0.25f) dt = 0.25f;
        // loading a level takes a while; the attempt and its song start together after that
        if (a->hitch) { dt = 1.0f / 60.0f; a->hitch = 0; }
        // Use the real elapsed time. (An earlier version smoothed it, which made the game run
        // about 15 % too fast: after every long frame the filter decayed slowly and added time.)
        dt_smooth = dt;
        a->t += dt_smooth;
        a->dbg_sum_dt += dt;
        a->dbg_sum_ds += dt_smooth;
        a->dbg_frames++;
        if (bot_spec) {
            if (bot_pos < 0 || bot_next) {
                bot_next = 0;
                if (++bot_pos >= bot_n) break;
                if (start_level(a, bot_list[bot_pos]) != 0) { printf("level %d: load failed\n", bot_list[bot_pos] + 1); continue; }
                botrun_begin(&brun);
                snprintf(g_bot_hud, sizeof g_bot_hud, "BOT  level %d  %s", bot_list[bot_pos] + 1, a->levels[bot_list[bot_pos]].name);
            }
            int over = 0;
            for (int k = 0; k < bot_speed && !over; k++) over = botrun_frame(a, &brun, bot_list[bot_pos]);
            if (over) { bot_next = 1; }
            else {
                static char m[64];
                m[0] = 0;
                for (int i = 0; i < MODE_COUNT; i++) if (brun.modes & (1u << i)) { if (m[0]) strcat(m, ","); strcat(m, k_mode_names[i]); }
                snprintf(g_bot_hud, sizeof g_bot_hud, "BOT  level %d  %s   deaths %d  skips %d  [%s]", bot_list[bot_pos] + 1,
                         a->levels[bot_list[bot_pos]].name, brun.deaths, brun.nskips, m);
            }
            render(a);
            {   // development aid: GDSHOTS="x1,x2,..." saves a screenshot when the player passes each x
                static const char *sp;
                static int init;
                if (!init) { sp = getenv("GDSHOTS"); init = 1; }
                if (sp && *sp) {
                    float nx = (float)atof(sp);
                    if (a->game.p.x >= nx) {
                        char fn[64];
                        snprintf(fn, sizeof fn, "shot_%d.png", (int)nx);
                        save_shot(a, fn);
                        const char *c = strchr(sp, ',');
                        sp = c ? c + 1 : "";
                    }
                }
            }
            continue;
        }
        if (a->scene == SC_PLAY && !a->paused) {
            if (a->intro > 0) a->intro -= dt_smooth;   // the attempt waits until the level is there
            else game_update(&a->game, dt_smooth);
        }
        if (sound) {
            audio_volume(a->cfg.music / 100.0f);
            audio_sfx_volume(a->cfg.sfx / 100.0f);
            music_update(a);
        }
        render(a);
        if (rttest_level >= 0 && rt_secs > 0 && a->dbg_sum_dt >= rt_secs) break;
        if (rttest_level >= 0 && rt_secs <= 0 && a->game.p.dead) {
            double wall = (double)(SDL_GetPerformanceCounter() - rt_start) / (double)SDL_GetPerformanceFrequency();
            printf("rttest: died at x=%.1f after %.3f s wall clock (game time %.3f s) -> %.1f units/s\n", a->game.p.x, wall, a->game.time, a->game.p.x / wall);
            printf("rttest: frames=%d sum(dt)=%.3f s sum(dt_smooth)=%.3f s\n", a->dbg_frames, a->dbg_sum_dt, a->dbg_sum_ds);
            break;
        }
    }
    audio_quit();
    config_save(&a->cfg, a->cfg_path);
    if (a->game_loaded) game_free(&a->game);
    gfx_quit(&a->g);
    return 0;
}
