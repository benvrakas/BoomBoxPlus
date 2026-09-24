# Audio decoders

`FBBPDecoder` (`Source/BoomBoxPlus/Private/Audio/BBPDecoder.*`) turns user music files into interleaved
16-bit PCM for `UBBPStreamingSoundWave`. It wraps three vendored single-file libraries.

## Vendored libraries

Location: `Source/ThirdParty/BBPAudioDecoders/` (deliberately **outside** the module folder, so UBT
never compiles these files on its own).

| File | Format | Licence | Source |
|---|---|---|---|
| `dr_mp3.h` | MP3 | Public domain or MIT-0 | github.com/mackron/dr_libs |
| `dr_wav.h` | WAV | Public domain or MIT-0 | github.com/mackron/dr_libs |
| `stb_vorbis.c` v1.22 | OGG Vorbis | Public domain | github.com/nothings/stb |

All three are permissive and safe to ship inside a distributed mod.

## How they are compiled (one-definition rule)

The libraries use the "single file, implementation behind a macro" convention. The implementation must be
compiled in exactly one translation unit, or the link fails with duplicate symbols.

- `BBPDecoderImpl.cpp` is that single unit. It defines `DR_MP3_IMPLEMENTATION` / `DR_WAV_IMPLEMENTATION`
  and includes `stb_vorbis.c` *without* `STB_VORBIS_HEADER_ONLY`, compiling the full Vorbis decoder as C++.
- Every other file includes `BBPDecoderLibs.h`, which pulls in declarations only
  (`stb_vorbis.c` with `STB_VORBIS_HEADER_ONLY` is the documented way to get just its header part).
- **Do not merge `BBPDecoderImpl.cpp` into another file** or include it anywhere.
- Both files define `DR_MP3_NO_STDIO`, `DR_WAV_NO_STDIO`, `STB_VORBIS_NO_STDIO` identically. We only ever
  decode from memory; excluding the `FILE*` APIs removes MSVC deprecation warnings and keeps declarations
  consistent across units.
- All vendored includes are wrapped in `THIRD_PARTY_INCLUDES_START/END` so UE's stricter warning levels
  don't reject third-party code.
- The module sets `bUseUnity = false`. The libraries define many generic macros (`CHECK`, `IS_PUSH_MODE`,
  …) and typedefs; in a unity build these would leak between files. The module is small, so unity buys
  nothing.
- `stb_vorbis.c` redeclares `uint8`/`int32`/etc. as typedefs. They are the same underlying types as UE's,
  which C++ permits, so this is harmless.

## Why decode from memory

The whole compressed file is loaded into `SourceBytes` and each library decodes out of that buffer
(`*_init_memory` / `stb_vorbis_open_memory`). Compressed files are a few MB even for long tracks; what we
avoid holding in memory is the *decoded* PCM (~10 MB per minute at 44.1 kHz stereo), which is streamed.
Memory decoding also sidesteps wide-character path handling and open file handles per track.

`SourceBytes` must stay alive and unmoved for as long as the decoder handle exists — the libraries keep
raw pointers into it. That is why `Open()` moves the bytes into the member *before* initialising.

## Format detection

By magic bytes, never by file extension, so a mislabelled file fails cleanly instead of misdecoding:

- `RIFF....WAVE` → WAV
- `OggS` → Vorbis
- `ID3` (ID3v2 tag) or an MPEG frame sync `0xFF 0xE?` at offset 0 → MP3

**Known limitation:** an MP3 with leading junk/padding before its first frame (and no ID3 tag) is rejected.
If users report valid MP3s being skipped, add a fallback that tries `drmp3_init_memory` on unknown data —
but keep the channel/sample-rate sanity check, since dr_mp3 is lenient and random data can contain
frame-sync-like byte patterns.

## Library gotchas worth remembering

- **stb_vorbis mixes units.** `stb_vorbis_get_samples_short_interleaved` takes the buffer size in
  *samples* (frames × channels) but returns *frames*. `stb_vorbis_seek`'s `sample_number` is a frame index.
- **Field names differ per library:** dr_libs use `sampleRate`/`channels`; stb_vorbis uses
  `stb_vorbis_info::sample_rate`/`channels`.
- `drmp3_get_pcm_frame_count` walks the whole stream (linear time) but restores the read position
  afterwards (verified in the source), so calling it inside `Open()` is safe. It runs on the decode thread,
  never the game thread.
- After opening, the reported channel count and sample rate are sanity-checked (1–8 channels,
  ≤ 192 kHz). A library can "succeed" on corrupt data; bad values must never reach `USoundWaveProcedural`.
