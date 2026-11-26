// SPDX-License-Identifier: GPL-3.0-or-later
#include "aio.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_FILE (512u << 20)

FILE *gd_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t wpath[1024], wmode[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024) &&
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8))
        return _wfopen(wpath, wmode);
    return NULL;
#else
    return fopen(path, mode);
#endif
}

// n bytes from the present position, with a 0 byte after them
static unsigned char *take(FILE *f, size_t n)
{
    unsigned char *b = malloc(n + 1);
    if (!b) return NULL;
    if (fread(b, 1, n, f) != n) { free(b); return NULL; }
    b[n] = 0;
    return b;
}

static long length_of(FILE *f)
{
    if (fseek(f, 0, SEEK_END)) return -1;
    const long n = ftell(f);
    return fseek(f, 0, SEEK_SET) ? -1 : n;
}

unsigned char *gd_read_file(const char *path, size_t *n)
{
    *n = 0;
    FILE *f = gd_fopen(path, "rb");
    if (!f) return NULL;
    const long size = length_of(f);
    unsigned char *b = size >= 0 && (unsigned long)size < MAX_FILE ? take(f, (size_t)size) : NULL;
    if (b) *n = (size_t)size;
    fclose(f);
    return b;
}

// Opens a bundle and leaves it behind the magic; NULL for every other file.
static FILE *open_bundle(const char *path, long *size)
{
    FILE *f = gd_fopen(path, "rb");
    if (!f) return NULL;
    char magic[8];
    *size = length_of(f);
    if (*size < 8 || fread(magic, 1, 8, f) != 8 || memcmp(magic, AIO_MAGIC, 8)) { fclose(f); return NULL; }
    return f;
}

int aio_is_bundle(const char *path)
{
    long size;
    FILE *f = open_bundle(path, &size);
    if (f) fclose(f);
    return f != NULL;
}

unsigned char *aio_chunk(const char *path, const char *tag, size_t *n)
{
    *n = 0;
    long size;
    FILE *f = open_bundle(path, &size);
    if (!f) return NULL;
    unsigned char *found = NULL, head[8];
    long at = 8;
    while (at + 8 <= size && fread(head, 1, 8, f) == 8) {
        const unsigned long len = head[4] | (unsigned long)head[5] << 8 | (unsigned long)head[6] << 16 |
                                  (unsigned long)head[7] << 24;
        at += 8;
        if (len > (unsigned long)(size - at) || len >= MAX_FILE) break;   // cut off or damaged
        if (!memcmp(head, tag, 4)) {
            found = take(f, (size_t)len);
            if (found) *n = (size_t)len;
            break;
        }
        at += (long)len;
        if (fseek(f, at, SEEK_SET)) break;
    }
    fclose(f);
    return found;
}

unsigned char *aio_level_file(const char *path, size_t *n)
{
    return aio_is_bundle(path) ? aio_chunk(path, "LEVL", n) : gd_read_file(path, n);
}

unsigned char *aio_song_file(const char *path, size_t *n)
{
    return aio_is_bundle(path) ? aio_chunk(path, "SONG", n) : gd_read_file(path, n);
}

static int put_chunk(FILE *f, const char *tag, const void *data, size_t n)
{
    const unsigned char head[8] = {(unsigned char)tag[0], (unsigned char)tag[1], (unsigned char)tag[2],
                                   (unsigned char)tag[3], (unsigned char)n, (unsigned char)(n >> 8),
                                   (unsigned char)(n >> 16), (unsigned char)(n >> 24)};
    return fwrite(head, 1, 8, f) == 8 && fwrite(data, 1, n, f) == n;
}

int aio_write(const char *path, const void *level, size_t level_n, const void *song, size_t song_n,
              const char *song_name)
{
    if (level_n >= MAX_FILE || song_n >= MAX_FILE) return -1;
    FILE *f = gd_fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(AIO_MAGIC, 1, 8, f) == 8 && put_chunk(f, "LEVL", level, level_n) &&
             put_chunk(f, "NAME", song_name, strlen(song_name)) && put_chunk(f, "SONG", song, song_n);
    if (fclose(f)) ok = 0;
    return ok ? 0 : -1;
}
