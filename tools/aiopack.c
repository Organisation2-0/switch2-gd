// SPDX-License-Identifier: GPL-3.0-or-later
// aiopack: packs a level and its song into one .aiogdl file (see src/aio.h).
//
//   aiopack                                  asks for the files in dialogs
//   aiopack <level> <song> [-o out] [-f]     (or both files dragged onto aiopack.exe)
//   aiopack --info <file.aiogdl>
//   aiopack --unpack <file.aiogdl> [folder]
//
// <level> is a .gdlvl / .gmd export or a level string; an .aiogdl may be given to change its song.
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include "aio.h"
#include "level.h"
#define DR_MP3_NO_STDIO
#include "../vendor/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../vendor/stb_vorbis.c"

#define PATH_CAP 1024

static int s_windows;   // started without a terminal of its own: results appear in message boxes

static void to_wide(const char *s, wchar_t *w, int cap)
{
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cap)) w[0] = 0;
}

static void to_utf8(const wchar_t *w, char *s, int cap)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, s, cap, NULL, NULL)) s[0] = 0;
}

static void say(int error, const char *format, ...)
{
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    wchar_t wide[2048];
    to_wide(text, wide, 2048);
    if (s_windows) {
        MessageBoxW(NULL, wide, L"aiopack", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
        return;
    }
    const HANDLE out = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD done;
    if (!WriteConsoleW(out, wide, (DWORD)wcslen(wide), &done, NULL)) {   // redirected: write UTF-8
        fputs(text, error ? stderr : stdout);
        fputc('\n', error ? stderr : stdout);
    } else {
        WriteConsoleW(out, L"\n", 1, &done, NULL);
    }
}

static const char *base_name(const char *path)
{
    const char *name = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') name = p + 1;
    return name;
}

static int has_extension(const char *path, const char *ext)
{
    const size_t n = strlen(path), e = strlen(ext);
    return n >= e && !_stricmp(path + n - e, ext);
}

static int is_song_name(const char *path)
{
    return has_extension(path, ".mp3") || has_extension(path, ".ogg") || has_extension(path, ".wav");
}

static int exists(const char *path)
{
    FILE *f = gd_fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

// ".mp3" / ".ogg" / ".wav" if the data can be played, else NULL.
static const char *audio_kind(const unsigned char *d, size_t n)
{
    if (n < 64) return NULL;
    if (!memcmp(d, "OggS", 4)) {
        int err = 0;
        stb_vorbis *v = stb_vorbis_open_memory(d, (int)n, &err, NULL);
        if (v) stb_vorbis_close(v);
        return v ? ".ogg" : NULL;
    }
    if (!memcmp(d, "RIFF", 4)) return !memcmp(d + 8, "WAVE", 4) ? ".wav" : NULL;
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, d, n, NULL)) return NULL;
    static drmp3_int16 buffer[4096 * 2];
    const int ok = mp3.channels >= 1 && mp3.channels <= 2 && drmp3_read_pcm_frames_s16(&mp3, 4096, buffer) > 0;
    drmp3_uninit(&mp3);
    return ok ? ".mp3" : NULL;
}

// Number of objects of a level file, -1 if it is none.
static int level_objects(const char *path, char *name, size_t cap)
{
    GdLevel level;
    if (gd_level_load(path, &level)) return -1;
    const int n = level.count;
    snprintf(name, cap, "%s", level.name);
    gd_level_free(&level);
    return n > 0 ? n : -1;
}

static int pack(const char *level_path, const char *song_path, const char *out_path, int overwrite)
{
    char name[256];
    const int objects = level_objects(level_path, name, sizeof name);
    if (objects < 0) {
        say(1, "This is not a level file:\n%s", level_path);
        return 1;
    }
    size_t ln = 0, sn = 0;
    unsigned char *level = aio_level_file(level_path, &ln);
    unsigned char *song = aio_song_file(song_path, &sn);
    int rc = 1;
    if (!level || !song) {
        say(1, "Cannot read:\n%s", !level ? level_path : song_path);
    } else if (!audio_kind(song, sn)) {
        say(1, "This is not an MP3, Ogg Vorbis or WAV file:\n%s", song_path);
    } else if (!overwrite && exists(out_path)) {
        say(1, "The file already exists (use -f to replace it):\n%s", out_path);
    } else {
        // a bundle given as the song brings its song along, under its own name
        char song_name[256];
        size_t named = 0;
        unsigned char *old = aio_chunk(song_path, "NAME", &named);
        snprintf(song_name, sizeof song_name, "%s", old ? (const char *)old : base_name(song_path));
        free(old);
        if (aio_write(out_path, level, ln, song, sn, song_name)) {
            say(1, "Cannot write:\n%s", out_path);
        } else {
            say(0, "Packed %d objects and \"%s\" into\n%s\n(%.1f MB)", objects, song_name, out_path,
                (ln + sn) / 1048576.0);
            rc = 0;
        }
    }
    free(level);
    free(song);
    return rc;
}

// <folder and name of the level file>.aiogdl, with a number if that exists
static void default_output(const char *level_path, char *out, size_t cap)
{
    char stem[PATH_CAP];
    snprintf(stem, sizeof stem, "%s", level_path);
    char *dot = strrchr(stem, '.');
    if (dot && dot > base_name(stem)) *dot = 0;
    snprintf(out, cap, "%s" AIO_EXTENSION, stem);
    for (int i = 2; i < 100 && exists(out); i++) snprintf(out, cap, "%s (%d)" AIO_EXTENSION, stem, i);
}

static int info(const char *path)
{
    if (!aio_is_bundle(path)) { say(1, "Not an .aiogdl file:\n%s", path); return 1; }
    char name[256];
    const int objects = level_objects(path, name, sizeof name);
    size_t ln = 0, sn = 0, nn = 0;
    unsigned char *level = aio_chunk(path, "LEVL", &ln), *song = aio_chunk(path, "SONG", &sn),
                  *song_name = aio_chunk(path, "NAME", &nn);
    const char *kind = song ? audio_kind(song, sn) : NULL;
    say(0, "%s\nlevel: %s, %d objects, %.0f KB\nsong:  %s, %s, %.0f KB", path,
        objects < 0 ? "DAMAGED" : name, objects < 0 ? 0 : objects, ln / 1024.0,
        song_name ? (const char *)song_name : "(no name)", kind ? kind + 1 : "MISSING or damaged", sn / 1024.0);
    free(level);
    free(song);
    free(song_name);
    return objects < 0 || !kind;
}

// Writes one part of a bundle into the folder without replacing anything.
static int write_part(const char *folder, const char *stem, const char *ext, const unsigned char *data, size_t n)
{
    char path[PATH_CAP];
    snprintf(path, sizeof path, "%s/%s%s", folder, stem, ext);
    for (int i = 2; i < 100 && exists(path); i++) snprintf(path, sizeof path, "%s/%s (%d)%s", folder, stem, i, ext);
    FILE *f = exists(path) ? NULL : gd_fopen(path, "wb");
    int ok = f && fwrite(data, 1, n, f) == n;
    if (f && fclose(f)) ok = 0;
    if (ok) say(0, "wrote %s", path);
    else say(1, "Cannot write:\n%s", path);
    return ok ? 0 : 1;
}

static int unpack(const char *path, const char *folder)
{
    size_t ln = 0, sn = 0, nn = 0;
    unsigned char *level = aio_chunk(path, "LEVL", &ln), *song = aio_chunk(path, "SONG", &sn),
                  *name = aio_chunk(path, "NAME", &nn);
    int rc = 1;
    if (!level || !song) {
        say(1, "Not a complete .aiogdl file:\n%s", path);
    } else {
        wchar_t wide[PATH_CAP];
        to_wide(folder, wide, PATH_CAP);
        CreateDirectoryW(wide, NULL);
        char stem[200], song_stem[200];
        snprintf(stem, sizeof stem, "%s", base_name(path));
        char *dot = strrchr(stem, '.');
        if (dot) *dot = 0;
        // only the name of the song, whatever the file says: nothing is written outside the folder
        snprintf(song_stem, sizeof song_stem, "%s", name ? base_name((const char *)name) : stem);
        for (char *p = song_stem; *p; p++)
            if ((unsigned char)*p < 32 || strchr("<>:\"|?*", *p)) *p = '_';
        dot = strrchr(song_stem, '.');
        if (dot) *dot = 0;
        if (!song_stem[0] || !strcmp(song_stem, ".")) snprintf(song_stem, sizeof song_stem, "%s", stem);
        const char *kind = audio_kind(song, sn);
        rc = write_part(folder, stem, ".gdlvl", level, ln);
        rc |= write_part(folder, song_stem, kind ? kind : ".bin", song, sn);
    }
    free(level);
    free(song);
    free(name);
    return rc;
}

// ---- dialogs

static int ask(const wchar_t *title, const wchar_t *filter, wchar_t *path, int saving)
{
    OPENFILENAMEW o;
    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.lpstrTitle = title;
    o.lpstrFilter = filter;
    o.lpstrFile = path;
    o.nMaxFile = PATH_CAP;
    o.Flags = OFN_NOCHANGEDIR | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (saving) o.lpstrDefExt = L"aiogdl";
    return saving ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
}

static int dialogs(void)
{
    s_windows = 1;
    static wchar_t level[PATH_CAP], song[PATH_CAP], out[PATH_CAP];
    if (!ask(L"1/3  Choose the level",
             L"Level files (*.gdlvl, *.gmd, *.aiogdl, *.txt)\0*.gdlvl;*.gmd;*.aiogdl;*.txt\0All files\0*.*\0",
             level, 0))
        return 0;
    if (!ask(L"2/3  Choose the song", L"Songs (*.mp3, *.ogg, *.wav)\0*.mp3;*.ogg;*.wav\0All files\0*.*\0", song, 0))
        return 0;
    char level8[PATH_CAP * 3], song8[PATH_CAP * 3], out8[PATH_CAP * 3];
    to_utf8(level, level8, sizeof level8);
    to_utf8(song, song8, sizeof song8);
    default_output(level8, out8, sizeof out8);
    to_wide(out8, out, PATH_CAP);
    if (!ask(L"3/3  Save as", L"All-in-one level (*.aiogdl)\0*.aiogdl\0", out, 1)) return 0;
    to_utf8(out, out8, sizeof out8);
    return pack(level8, song8, out8, 1);
}

static int usage(void)
{
    say(1, "aiopack puts a level and its song into one .aiogdl file.\n\n"
           "aiopack                                (asks for the files)\n"
           "aiopack <level> <song> [-o out] [-f]\n"
           "aiopack --info <file.aiogdl>\n"
           "aiopack --unpack <file.aiogdl> [folder]");
    return 2;
}

int main(void)
{
    int argc = 0;
    wchar_t **wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wargv) return 2;
    static char arg[8][PATH_CAP * 3];
    if (argc > 8) return usage();
    for (int i = 0; i < argc; i++) to_utf8(wargv[i], arg[i], sizeof arg[i]);
    LocalFree(wargv);
    if (argc <= 1) {
        // double click: the console window that came with it is not needed
        DWORD list[2];
        if (GetConsoleProcessList(list, 2) == 1) FreeConsole();
        return dialogs();
    }
    // files dragged onto the program: its console would vanish at once, so answer in a window
    DWORD list[2];
    if (GetConsoleProcessList(list, 2) == 1 && GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR)
        s_windows = 1;
    if (!strcmp(arg[1], "--info")) return argc == 3 ? info(arg[2]) : usage();
    if (!strcmp(arg[1], "--unpack")) return argc == 3 || argc == 4 ? unpack(arg[2], argc == 4 ? arg[3] : ".") : usage();

    const char *level = NULL, *song = NULL, *out = NULL;
    int overwrite = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(arg[i], "-o") && i + 1 < argc) out = arg[++i];
        else if (!strcmp(arg[i], "-f")) overwrite = 1;
        else if (arg[i][0] == '-') return usage();
        else if (is_song_name(arg[i]) && !song) song = arg[i];
        else if (!level) level = arg[i];
        else if (!song) song = arg[i];
        else return usage();
    }
    if (!level || !song) return usage();
    static char name[PATH_CAP * 3];
    if (!out) {
        default_output(level, name, sizeof name);
        out = name;
    }
    return pack(level, song, out, overwrite);
}
