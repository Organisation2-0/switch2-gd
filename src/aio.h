// SPDX-License-Identifier: GPL-3.0-or-later
// .aiogdl: a level and its song in one file.
//
//   8 bytes   "AIOGDL1\n"
//   chunks    4 letters, 4 bytes length (little endian), then that many bytes
//     LEVL    the level file as it was (.gdlvl, .gmd or a packed level string)
//     SONG    the audio file as it was (MP3, Ogg Vorbis or WAV)
//     NAME    file name of the song, UTF-8
//   Chunks may come in any order; unknown ones are skipped.
#ifndef GD_AIO_H
#define GD_AIO_H

#include <stddef.h>
#include <stdio.h>

#define AIO_MAGIC "AIOGDL1\n"
#define AIO_EXTENSION ".aiogdl"

// fopen with a UTF-8 path on every system.
FILE *gd_fopen(const char *path, const char *mode);
// A whole file (malloc'd, with a 0 byte after it), NULL if it cannot be read.
unsigned char *gd_read_file(const char *path, size_t *n);

// A chunk of a bundle (malloc'd, with a 0 byte after it); NULL if path is no bundle or the chunk
// is missing or damaged.
unsigned char *aio_chunk(const char *path, const char *tag, size_t *n);
int aio_is_bundle(const char *path);
// The level data of a file: the LEVL chunk of a bundle, any other file as a whole.
unsigned char *aio_level_file(const char *path, size_t *n);
// The audio data of a file: the SONG chunk of a bundle, any other file as a whole.
unsigned char *aio_song_file(const char *path, size_t *n);

// Writes a bundle; returns 0 on success.
int aio_write(const char *path, const void *level, size_t level_n, const void *song, size_t song_n,
              const char *song_name);

#endif
