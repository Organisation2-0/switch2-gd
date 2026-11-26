// SPDX-License-Identifier: GPL-3.0-or-later
// Music (one track at a time) and sound effects through SDL audio: MP3, Ogg Vorbis or WAV.
//
// The sound device is only opened once something is to be heard, so a volume of 0 leaves the
// audio system of the machine alone.
#ifndef GD_AUDIO_H
#define GD_AUDIO_H

void audio_volume(float v);       // music, 0..1
void audio_sfx_volume(float v);   // sound effects, 0..1
// Plays a file from `offset` seconds on, rising from silence over `fade_in` seconds; loop: start
// again at the end. Playing the same file again does not read it again. Returns 0 on success.
int audio_play(const char *path, double offset, int loop, float fade_in);
void audio_stop(void);            // the music
void audio_pause(int on);         // music and effects
void audio_fade_out(float seconds);
float audio_pulse(void);      // how strongly the music beats right now, 0..1; -1 without music
float audio_loudness(void);   // average loudness of the last moments, 0..1; -1 without music

// Sound effects. Each has one voice: playing it again starts it over.
enum { SFX_PLAY, SFX_QUIT, SFX_CRASH, SFX_COUNT };
// Plays effect `id` from the file at `path` (read the first time) at `gain` times the effect volume.
void audio_sfx(int id, const char *path, float gain);
void audio_sfx_stop(void);
void audio_quit(void);

#endif
