// Compiles the vendored decoder implementations. Must be the only file that does so.

#include "CoreMinimal.h"

#define DR_MP3_NO_STDIO
#define DR_WAV_NO_STDIO
#define STB_VORBIS_NO_STDIO

THIRD_PARTY_INCLUDES_START
#ifdef _MSC_VER
#pragma warning(disable : 4701) // Potentially uninitialized local variable, reported inside the vendored libraries.
#endif
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "stb_vorbis.c"
THIRD_PARTY_INCLUDES_END
