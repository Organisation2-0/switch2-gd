// SPDX-License-Identifier: GPL-3.0-or-later
// The code of the audio decoders in vendor/ (public domain / MIT-0).
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../vendor/dr_mp3.h"

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../vendor/stb_vorbis.c"
