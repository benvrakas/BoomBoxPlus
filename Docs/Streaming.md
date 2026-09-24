# Streaming playback

`UBBPStreamingSoundWave` (`Source/BoomBoxPlus/Public/Audio/BBPStreamingSoundWave.h`) plays one music file
through a normal `UAudioComponent`, so it gets spatialisation and attenuation for free.

## Threads

| Thread | Does |
|---|---|
| Game thread | `StartStream`, `Seek`, `StopStream`, reads `GetPlaybackSeconds` for drift correction |
| Decode thread (`FBBPDecodeWorker`, one per stream) | Loads the file, opens `FBBPDecoder`, handles seeks, keeps ~1 s of PCM buffered |
| Audio render thread | `OnGeneratePCMAudio` copies buffered PCM out to the mixer |

Shared state lives in `FBBPStreamState`, held by `TSharedPtr` from both the wave and the worker, so neither
can outlive it.

## Why not `USoundWaveProcedural::QueueAudio` / `ResetAudio`

The engine class already has a FIFO, but it is unsafe for seeking during playback:
`ResetAudio()` calls `QueuedAudio.Empty()` on an SPSC `TQueue` from the *calling* thread while the audio
thread may be dequeuing it — two consumers on a single-consumer queue. Its underrun path also pads silence
without any position bookkeeping, which would break drift measurement. (Read from
`Engine/Source/Runtime/Engine/Private/SoundWaveProcedural.cpp` in the 5.6.1-CSS engine.)

Instead we override `OnGeneratePCMAudio` and keep our own ring buffer.

## The ring buffer lock

The buffer is guarded by an `FCriticalSection`. Both sides hold it only for a `memcpy` of at most one chunk
(2048 frames); decoding happens outside the lock. The audio thread therefore blocks for microseconds at
worst. A lock-free SPSC ring was considered, but flushing it on seek needs a producer↔consumer handshake
that deadlocks when the component isn't playing (no consumer running). The lock is simpler and correct.

## Position tracking

`PlaybackFrame` is the file frame index of the next frame handed to the mixer. It advances only by frames of
*real* audio read out of the buffer — underrun padding does not advance it. So if the stream starves,
`GetPlaybackSeconds()` falls behind wall-clock time, and drift correction (in the playback controller)
sees the gap and seeks to catch up. On seek, `Reset()` sets it to the target frame under the same lock that
empties the buffer, so position and contents always agree.

Mixer output latency is a constant per machine and irrelevant to cross-client sync.

## Seeking

`Seek()` just stores `PendingSeekFrame` and wakes the decode thread; the decoder is only ever touched from
the decode thread. A chunk decoded from the old position while the seek is in flight is discarded by the
`Reset()` that follows.

MP3 has no index, so dr_mp3 seeks by decoding from the start of the file — a drift correction near the end
of a long track could take a second, overshoot, and trigger another correction. `FBBPDecoder` builds a
seek table on open (one point per ~2 s, max 4096) so seeks are near-instant. See `Decoders.md`.

A seek past the end of the file marks the stream ended rather than failing.

## One wave per track

`StartStream` can only be called once per wave. The audio thread reads `State` without locking the
pointer itself, so it must never be reassigned while playing. To change tracks, create a new wave and swap
it into the audio component. `State` is released only when the UObject is destroyed, after the engine has
finished rendering it.

## Configuration that matters

Mirrors the engine's own `USynthSound`:

- `VirtualizationMode = PlayWhenSilent` — otherwise the engine stops generating a sound that is inaudible
  (out of attenuation range). Our position would freeze and walking back in range would resume from a
  stale spot.
- `Duration = INDEFINITELY_LOOPING_DURATION`, `bLooping = true` — stops the engine ending the sound on its
  own; the playback controller decides when a track is over (`IsFinished()`).
- `bCanProcessAsync = true`.
- Output format is always `Int16`.

## Channels

Output is 1 or 2 channels. Files with more channels keep only the first two (front left/right). Mono and
stereo spatialise correctly; >2-channel sources don't, and multichannel music files are rare. If needed
later, replace the channel drop with a proper downmix.

## Format checks

Channels and sample rate come from the library scan, so the wave can be configured and played immediately
while the file opens in the background (a few ms of silence at most). If the file on disk no longer matches
(edited since the scan), the worker refuses it and `HasFailed()` becomes true; the controller should
rescan and skip.
