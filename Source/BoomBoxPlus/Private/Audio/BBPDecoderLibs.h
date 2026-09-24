#pragma once

// Includes the vendored decoder declarations with the configuration shared by every file that uses them.

#include "CoreMinimal.h"

#define DR_MP3_NO_STDIO
#define DR_WAV_NO_STDIO
#define STB_VORBIS_NO_STDIO

THIRD_PARTY_INCLUDES_START
#include "dr_mp3.h"
#include "dr_wav.h"
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
THIRD_PARTY_INCLUDES_END
