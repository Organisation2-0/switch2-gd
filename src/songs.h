// SPDX-License-Identifier: GPL-3.0-or-later
// Which music belongs to a level: the songs of the original for the official levels, and for
// custom levels the file the player chose (kept in a small text file).
#ifndef GD_SONGS_H
#define GD_SONGS_H

#include <stddef.h>

#define SONGS_OFFICIAL 22
#define SONGS_MAX_FILES 200
#define SONG_NAME 160

typedef struct { const char *file, *title; } OfficialSong;
extern const OfficialSong g_official_songs[SONGS_OFFICIAL];   // in the order of the levels

// The audio files (.mp3 .ogg .wav) of the music folder, sorted by name. Returns their number.
int songs_scan(const char *dir, char names[][SONG_NAME], int max);

// A choice is "" (nothing chosen yet), "none", "bundle" (the song inside the level's .aiogdl),
// "file:<name in the music folder>" or "official:<number>".
void songs_load(const char *choices_path);
const char *songs_choice(const char *level_key);
void songs_choose(const char *level_key, const char *choice);   // and saves the file

// The file to play for a choice; 0 if the choice names none.
int songs_path(const char *choice, const char *resdir, const char *music_dir, char *out, size_t cap);
// Whether that file is there. The answer is kept for a moment, so screens may ask in every frame.
int songs_present(const char *path);

#endif
