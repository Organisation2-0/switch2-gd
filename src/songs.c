// SPDX-License-Identifier: GPL-3.0-or-later
#include "songs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif
#include <SDL.h>

const OfficialSong g_official_songs[SONGS_OFFICIAL] = {
    {"StereoMadness.mp3", "Stereo Madness"},     {"BackOnTrack.mp3", "Back On Track"},
    {"Polargeist.mp3", "Polargeist"},            {"DryOut.mp3", "Dry Out"},
    {"BaseAfterBase.mp3", "Base After Base"},    {"CantLetGo.mp3", "Can't Let Go"},
    {"Jumper.mp3", "Jumper"},                    {"TimeMachine.mp3", "Time Machine"},
    {"Cycles.mp3", "Cycles"},                    {"xStep.mp3", "xStep"},
    {"Clutterfunk.mp3", "Clutterfunk"},          {"TheoryOfEverything.mp3", "Theory of Everything"},
    {"Electroman.mp3", "Electroman Adventures"}, {"Clubstep.mp3", "Clubstep"},
    {"Electrodynamix.mp3", "Electrodynamix"},    {"HexagonForce.mp3", "Hexagon Force"},
    {"BlastProcessing.mp3", "Blast Processing"}, {"TheoryOfEverything2.mp3", "Theory of Everything 2"},
    {"GeometricalDominator.mp3", "Geometrical Dominator"}, {"Deadlocked.mp3", "Deadlocked"},
    {"Fingerdash.mp3", "Fingerdash"},            {"Dash.mp3", "Dash"},
};

static int is_audio(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!SDL_strcasecmp(dot, ".mp3") || !SDL_strcasecmp(dot, ".ogg") || !SDL_strcasecmp(dot, ".wav"));
}

static int by_name(const void *a, const void *b) { return SDL_strcasecmp(a, b); }

// File names are UTF-8 everywhere (SDL opens them as such).
int songs_scan(const char *dir, char names[][SONG_NAME], int max)
{
    int n = 0;
#ifdef _WIN32
    char pattern[600];
    wchar_t wide[600];
    snprintf(pattern, sizeof pattern, "%s/*", dir);
    if (!MultiByteToWideChar(CP_UTF8, 0, pattern, -1, wide, 600)) return 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wide, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char name[SONG_NAME];
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL) && is_audio(name))
            snprintf(names[n++], SONG_NAME, "%s", name);
    } while (n < max && FindNextFileW(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return 0;
    for (struct dirent *e; n < max && (e = readdir(d));)
        if (is_audio(e->d_name) && strlen(e->d_name) < SONG_NAME)
            snprintf(names[n++], SONG_NAME, "%s", e->d_name);
    closedir(d);
#endif
    qsort(names, (size_t)n, SONG_NAME, by_name);
    return n;
}

// ---- choices: one line "level key <tab> choice" per level

#define MAX_CHOICES 1024

typedef struct { char key[SONG_NAME + 8], choice[SONG_NAME + 12]; } Choice;
static Choice *s_choice;
static int s_n;
static char s_path[600];

static Choice *find(const char *key)
{
    for (int i = 0; i < s_n; i++)
        if (!strcmp(s_choice[i].key, key)) return &s_choice[i];
    return NULL;
}

static Choice *add(const char *key)
{
    if (!s_choice) s_choice = calloc(MAX_CHOICES, sizeof *s_choice);
    if (!s_choice || s_n >= MAX_CHOICES) return NULL;
    Choice *c = &s_choice[s_n++];
    snprintf(c->key, sizeof c->key, "%s", key);
    return c;
}

void songs_load(const char *choices_path)
{
    s_n = 0;
    snprintf(s_path, sizeof s_path, "%s", choices_path);
    FILE *f = fopen(s_path, "rb");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = 0;
        Choice *c = find(line) ? find(line) : add(line);
        if (c) snprintf(c->choice, sizeof c->choice, "%s", tab + 1);
    }
    fclose(f);
}

const char *songs_choice(const char *level_key)
{
    const Choice *c = find(level_key);
    return c ? c->choice : "";
}

void songs_choose(const char *level_key, const char *choice)
{
    Choice *c = find(level_key);
    if (!c) c = add(level_key);
    if (!c) return;
    snprintf(c->choice, sizeof c->choice, "%s", choice);
    FILE *f = fopen(s_path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", s_path); return; }
    for (int i = 0; i < s_n; i++)
        if (s_choice[i].choice[0]) fprintf(f, "%s\t%s\n", s_choice[i].key, s_choice[i].choice);
    fclose(f);
}

int songs_path(const char *choice, const char *resdir, const char *music_dir, char *out, size_t cap)
{
    if (!strncmp(choice, "official:", 9)) {
        const int n = atoi(choice + 9);
        if (n < 0 || n >= SONGS_OFFICIAL) return 0;
        snprintf(out, cap, "%s/%s", resdir, g_official_songs[n].file);
        return 1;
    }
    if (!strncmp(choice, "file:", 5) && choice[5]) {
        snprintf(out, cap, "%s/%s", music_dir, choice + 5);
        return 1;
    }
    return 0;
}

int songs_present(const char *path)
{
    static char last[720];
    static Uint32 when;
    static int present;
    if (strcmp(last, path) || SDL_GetTicks() - when > 2000) {
        SDL_RWops *f = SDL_RWFromFile(path, "rb");
        if (f) SDL_RWclose(f);
        present = f != NULL;
        when = SDL_GetTicks();
        snprintf(last, sizeof last, "%s", path);
    }
    return present;
}
