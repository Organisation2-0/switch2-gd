// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "aio.h"

#define DR_MP3_NO_STDIO
#include "../vendor/dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../vendor/stb_vorbis.c"

#define RATE 44100
#define CHUNK 2048   // frames decoded at a time

typedef enum { T_NONE, T_MP3, T_OGG, T_WAV } Kind;

typedef struct {
    char path[720];
    Kind kind;
    void *file;             // MP3 / Ogg: the whole file
    size_t size;
    drmp3 *mp3;             // on the heap: the decoder holds pointers to itself
    stb_vorbis *ogg;
    Uint8 *wav;             // WAV: the samples
    Uint32 wav_bytes, wav_at;
    int rate, channels, frame_bytes;
    SDL_AudioStream *conv;  // to the format of the device
} Track;

static SDL_AudioDeviceID s_dev;
static int s_failed;        // the device cannot be opened, do not try again
static Track s_track;
static int s_playing, s_paused, s_loop, s_ended;
static float s_volume = 1.0f, s_sfx_volume = 1.0f;
static float s_fade = 1.0f, s_fade_step;   // change per sample frame: rising > 0 > falling
static float s_pulse, s_average;
static struct { char path[720]; double offset; int loop, set; float fade; } s_wanted;   // asked while silent
// effects, decoded to the format of the device
static struct { Sint16 *pcm; int frames, at, playing; float gain; } s_sfx[SFX_COUNT];

static int decode(Track *t, void *out)
{
    if (t->kind == T_MP3) return (int)drmp3_read_pcm_frames_s16(t->mp3, CHUNK, out);
    if (t->kind == T_OGG)
        return stb_vorbis_get_samples_short_interleaved(t->ogg, t->channels, out, CHUNK * t->channels);
    Uint32 n = (t->wav_bytes - t->wav_at) / (Uint32)t->frame_bytes;
    if (n > CHUNK) n = CHUNK;
    memcpy(out, t->wav + t->wav_at, (size_t)n * t->frame_bytes);
    t->wav_at += n * (Uint32)t->frame_bytes;
    return (int)n;
}

static void seek(Track *t, double seconds)
{
    const Uint64 frame = seconds > 0 ? (Uint64)(seconds * t->rate) : 0;
    if (t->kind == T_MP3) drmp3_seek_to_pcm_frame(t->mp3, frame);
    else if (t->kind == T_OGG) {
        if (!frame) stb_vorbis_seek_start(t->ogg);
        else stb_vorbis_seek(t->ogg, (unsigned)frame);
    } else {
        const Uint64 at = frame * (Uint64)t->frame_bytes;
        t->wav_at = at < t->wav_bytes ? (Uint32)at : t->wav_bytes;
    }
    SDL_AudioStreamClear(t->conv);
}

static void close_track(Track *t)
{
    if (t->kind == T_MP3) drmp3_uninit(t->mp3);
    SDL_free(t->mp3);
    if (t->ogg) stb_vorbis_close(t->ogg);
    if (t->wav) SDL_FreeWAV(t->wav);
    if (t->conv) SDL_FreeAudioStream(t->conv);
    free(t->file);
    memset(t, 0, sizeof *t);
}

static int open_track(Track *t, const char *path)
{
    memset(t, 0, sizeof *t);
    t->file = aio_song_file(path, &t->size);   // of an .aiogdl: the song inside
    if (!t->file || t->size < 12) { close_track(t); return -1; }
    SDL_AudioFormat format = AUDIO_S16SYS;
    if (!memcmp(t->file, "OggS", 4)) {
        int err = 0;
        t->ogg = stb_vorbis_open_memory(t->file, (int)t->size, &err, NULL);
        if (t->ogg) {
            const stb_vorbis_info i = stb_vorbis_get_info(t->ogg);
            t->kind = T_OGG;
            t->rate = (int)i.sample_rate;
            t->channels = i.channels > 2 ? 2 : i.channels;
        }
    } else if (!memcmp(t->file, "RIFF", 4)) {
        SDL_AudioSpec spec;
        if (SDL_LoadWAV_RW(SDL_RWFromConstMem(t->file, (int)t->size), 1, &spec, &t->wav, &t->wav_bytes)) {
            t->kind = T_WAV;
            t->rate = spec.freq;
            t->channels = spec.channels;
            format = spec.format;
        }
    } else if ((t->mp3 = SDL_calloc(1, sizeof *t->mp3)) && drmp3_init_memory(t->mp3, t->file, t->size, NULL)) {
        t->kind = T_MP3;
        t->rate = (int)t->mp3->sampleRate;
        t->channels = (int)t->mp3->channels;
    }
    t->frame_bytes = t->channels * (SDL_AUDIO_BITSIZE(format) / 8);
    // the decode buffer holds up to 8 channels of 32 bit samples
    if (t->kind != T_NONE && t->channels >= 1 && t->channels <= 8 && t->rate > 0)
        t->conv = SDL_NewAudioStream(format, (Uint8)t->channels, t->rate, AUDIO_S16SYS, 2, RATE);
    if (!t->conv) { close_track(t); return -1; }
    snprintf(t->path, sizeof t->path, "%s", path);
    return 0;
}

static Uint8 s_chunk[CHUNK * 8 * 4];   // one decoded block (up to 8 channels of 32 bits)

static void mix_music(Uint8 *stream, int len)
{
    if (!s_playing) return;
    Track *t = &s_track;
    int rewound = 0;
    while (!s_ended && SDL_AudioStreamAvailable(t->conv) < len) {
        const int n = decode(t, s_chunk);
        if (n > 0) {
            SDL_AudioStreamPut(t->conv, s_chunk, n * t->frame_bytes);
            rewound = 0;
        } else if (s_loop && !rewound) {
            SDL_AudioStreamFlush(t->conv);
            if (t->kind == T_MP3) drmp3_seek_to_pcm_frame(t->mp3, 0);
            else if (t->kind == T_OGG) stb_vorbis_seek_start(t->ogg);
            else t->wav_at = 0;
            rewound = 1;   // a file without a single frame must not loop here for ever
        } else {
            SDL_AudioStreamFlush(t->conv);
            s_ended = 1;
        }
    }
    const int got = SDL_AudioStreamGet(t->conv, stream, len);
    if (got <= 0) {
        if (s_ended) s_playing = 0;
        return;
    }
    Sint16 *out = (Sint16 *)stream;
    const int samples = got / 2;
    double sum = 0;
    for (int i = 0; i < samples; i += 2) {
        sum += (double)out[i] * out[i] + (double)out[i + 1] * out[i + 1];
        const float gain = s_volume * s_fade;
        out[i] = (Sint16)(out[i] * gain);
        out[i + 1] = (Sint16)(out[i + 1] * gain);
        if (s_fade_step != 0) {
            s_fade += s_fade_step;
            if (s_fade >= 1.0f) { s_fade = 1.0f; s_fade_step = 0; }
            if (s_fade <= 0) { s_fade = 0; s_fade_step = 0; s_playing = 0; }
        }
    }
    // A beat is a block clearly louder than the recent ones; it dies away until the next one.
    const float level = samples ? (float)sqrt(sum / samples) / 32768.0f : 0;
    const float seconds = samples / 2 / (float)RATE;
    const float onset = fminf(fmaxf((level / (s_average + 0.004f) - 0.95f) * 2.2f, 0.0f), 1.0f);
    s_average += (level - s_average) * fminf(1.0f, seconds * 1.5f);
    s_pulse = fmaxf(s_pulse * expf(-5.0f * seconds), onset);
}

static Sint16 clip(int v) { return (Sint16)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static void mix_effects(Sint16 *out, int frames)
{
    for (int k = 0; k < SFX_COUNT; k++) {
        if (!s_sfx[k].playing) continue;
        int n = s_sfx[k].frames - s_sfx[k].at;
        if (n > frames) n = frames;
        const float gain = s_sfx[k].gain * s_sfx_volume;
        const Sint16 *in = s_sfx[k].pcm + s_sfx[k].at * 2;
        for (int i = 0; i < n * 2; i++) out[i] = clip(out[i] + (int)(in[i] * gain));
        s_sfx[k].at += n;
        if (s_sfx[k].at >= s_sfx[k].frames) s_sfx[k].playing = 0;
    }
}

// Runs on the audio thread (the device is locked meanwhile).
static void SDLCALL fill(void *user, Uint8 *stream, int len)
{
    (void)user;
    memset(stream, 0, (size_t)len);
    if (s_paused) return;
    mix_music(stream, len);
    mix_effects((Sint16 *)stream, len / 4);
}

static int open_device(void)
{
    if (s_dev) return 0;
    if (s_failed) return -1;
    fprintf(stderr, "audio: opening the device\n");
    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = fill;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) || !(s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0))) {
        fprintf(stderr, "audio: no sound (%s)\n", SDL_GetError());
        s_failed = 1;
        return -1;
    }
    fprintf(stderr, "audio: ok (%s, %d Hz)\n", SDL_GetCurrentAudioDriver(), have.freq);
    SDL_PauseAudioDevice(s_dev, 0);
    return 0;
}

int audio_play(const char *path, double offset, int loop, float fade_in)
{
    if (s_volume <= 0 && !s_dev) {
        snprintf(s_wanted.path, sizeof s_wanted.path, "%s", path);
        s_wanted.offset = offset;
        s_wanted.loop = loop;
        s_wanted.fade = fade_in;
        s_wanted.set = 1;
        return 0;
    }
    s_wanted.set = 0;
    if (open_device()) return -1;
    // stop first: the track may then be changed without holding up the audio thread
    audio_stop();
    if (strcmp(s_track.path, path)) {
        Track next;
        if (open_track(&next, path)) {
            fprintf(stderr, "audio: cannot play %s\n", path);
            return -1;
        }
        close_track(&s_track);
        s_track = next;
    }
    seek(&s_track, offset);
    SDL_LockAudioDevice(s_dev);
    s_loop = loop;
    s_ended = 0;
    s_fade = fade_in > 0 ? 0.0f : 1.0f;
    s_fade_step = fade_in > 0 ? 1.0f / (fade_in * RATE) : 0.0f;
    s_pulse = s_average = 0;
    s_paused = 0;
    s_playing = 1;
    SDL_UnlockAudioDevice(s_dev);
    return 0;
}

void audio_stop(void)
{
    s_wanted.set = 0;
    if (!s_dev) return;
    SDL_LockAudioDevice(s_dev);
    s_playing = 0;
    SDL_UnlockAudioDevice(s_dev);
}

void audio_pause(int on)
{
    if (!s_dev) return;
    SDL_LockAudioDevice(s_dev);
    s_paused = on;
    SDL_UnlockAudioDevice(s_dev);
}

void audio_fade_out(float seconds)
{
    s_wanted.set = 0;
    if (!s_dev || seconds <= 0) { audio_stop(); return; }
    SDL_LockAudioDevice(s_dev);
    s_fade_step = -1.0f / (seconds * RATE);
    SDL_UnlockAudioDevice(s_dev);
}

void audio_volume(float v)
{
    v = fminf(fmaxf(v, 0.0f), 1.0f);
    if (s_dev) SDL_LockAudioDevice(s_dev);
    s_volume = v;
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
    if (v > 0 && s_wanted.set) audio_play(s_wanted.path, s_wanted.offset, s_wanted.loop, s_wanted.fade);
}

void audio_sfx_volume(float v)
{
    if (s_dev) SDL_LockAudioDevice(s_dev);
    s_sfx_volume = fminf(fmaxf(v, 0.0f), 1.0f);
    if (s_dev) SDL_UnlockAudioDevice(s_dev);
}

// The whole file as 16 bit stereo at the rate of the device.
static Sint16 *decode_all(const char *path, int *frames)
{
    *frames = 0;
    Track t;
    if (open_track(&t, path)) return NULL;
    // the audio thread uses the block buffer too
    SDL_LockAudioDevice(s_dev);
    for (int n; (n = decode(&t, s_chunk)) > 0;)
        if (SDL_AudioStreamPut(t.conv, s_chunk, n * t.frame_bytes)) break;
    SDL_UnlockAudioDevice(s_dev);
    SDL_AudioStreamFlush(t.conv);
    const int bytes = SDL_AudioStreamAvailable(t.conv);
    Sint16 *pcm = bytes > 0 ? malloc((size_t)bytes) : NULL;
    if (pcm) *frames = SDL_AudioStreamGet(t.conv, pcm, bytes) / 4;
    close_track(&t);
    return pcm;
}

void audio_sfx(int id, const char *path, float gain)
{
    if (id < 0 || id >= SFX_COUNT || (s_sfx_volume <= 0 && !s_dev) || open_device()) return;
    if (!s_sfx[id].pcm) {
        int frames = 0;
        Sint16 *pcm = decode_all(path, &frames);
        if (!pcm || frames <= 0) { free(pcm); fprintf(stderr, "audio: cannot play %s\n", path); return; }
        s_sfx[id].pcm = pcm;
        s_sfx[id].frames = frames;
    }
    SDL_LockAudioDevice(s_dev);
    s_sfx[id].at = 0;
    s_sfx[id].gain = gain;
    s_sfx[id].playing = 1;
    s_paused = 0;
    SDL_UnlockAudioDevice(s_dev);
}

void audio_sfx_stop(void)
{
    if (!s_dev) return;
    SDL_LockAudioDevice(s_dev);
    for (int k = 0; k < SFX_COUNT; k++) s_sfx[k].playing = 0;
    SDL_UnlockAudioDevice(s_dev);
}

float audio_pulse(void) { return s_playing ? s_pulse : -1.0f; }

float audio_loudness(void) { return s_playing ? s_average : -1.0f; }

void audio_quit(void)
{
    if (s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    s_playing = 0;
    for (int k = 0; k < SFX_COUNT; k++) { free(s_sfx[k].pcm); memset(&s_sfx[k], 0, sizeof s_sfx[k]); }
    close_track(&s_track);
}
