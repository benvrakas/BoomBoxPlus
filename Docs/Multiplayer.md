# Multiplayer: shared queue and synced playback

## Pieces

| Class | Runs on | Does |
|---|---|---|
| `ABBPPlaylistSubsystem` | Server (authoritative) + replicated to clients | Queue, transport state, track-end detection, which Boom Boxes have Custom Music |
| `UBBPRemoteCallObject` | Client → server | Carries every player request; validates and logs rejections |
| `UBBPBlueprintLibrary` | Anywhere | The one API UI and hooks call (`Request*`); always goes through the local player's RCO |
| `UBBPPlaybackController` | Every non-dedicated machine | Owns the audio components and streams, keeps them in sync |

The subsystem is registered by `UBBPGameWorldModule`, the RCO by `UBBPGameInstanceModule` (native root
modules, see Architecture.md).

## One queue per session

There is a single shared queue. Every Boom Box with the Custom Music tape loaded plays it — normally one,
but several work and stay in sync. (sPEAKer has exactly one speaker; per-Boom-Box queues would multiply the
UI and replication complexity for little gain.)

## Replicated state

- `Queue` — `TArray<FBBPQueueEntry>`. Entries have a server-assigned `EntryId`, so remove/move requests stay
  correct when two players edit concurrently or a track is queued twice.
- `PlaybackState` — `FBBPPlaybackState`, replicated **as one struct** so clients never see a half-applied
  change (e.g. a new entry id with the old start time). `Revision` increments on every change, which is how
  clients notice a restart or seek of the same track.
- `ActiveBoomBoxes` — Boom Boxes with Custom Music loaded, plus each one's volume.

The subsystem is `bAlwaysRelevant` (inherited from `AFGSubsystem`); SML turns replication on because
`ReplicationPolicy = SpawnOnServer_Replicate` (`AModSubsystem`'s constructor sets `bReplicates = false`, so
the policy is what matters).

## Time base

`PlaybackState.TrackStartServerTime` is the server world time at which position 0 would have played.
Everyone computes the expected position as `GetServerWorldTimeSeconds() - TrackStartServerTime` (or
`PausedPosition` while paused). UE's server-time estimate on clients isn't latency-compensated, so expect
errors of roughly half the ping — well inside the 250 ms drift tolerance. If that proves too loose, imitate
the vanilla Boom Box's RCO round-trip with a client timestamp (see Architecture.md).

## Drift correction

`UBBPPlaybackController` compares each stream's real position (`GetPlaybackSeconds`, which only advances on
real audio) with the expected position once a second, and seeks when they differ by more than 0.25 s. It
logs every correction with the drift and underrun count — frequent corrections are the first sign of a
sync problem.

A new state `Revision` (pause/resume/seek/restart) is applied immediately with a seek, then drift checks
resume a second later so the seek has time to land.

## Shuffle and repeat

**The server alone chooses the next track**; clients only follow `CurrentEntryId`. This supersedes the
original plan's "replicate a shuffle seed" idea: that was only needed if clients derived the play order
themselves, which they never do. Shuffle picks randomly among entries not yet played this cycle
(`ShufflePlayed`, server-only); when exhausted it reshuffles (repeat all) or stops (repeat off).
`Previous` uses a server-only play history; within the first 3 s of a track it goes back, otherwise it
restarts the track.

Adding to an empty/stopped queue starts playback immediately. `+Front` means "play next" (inserted after the
current entry).

## Missing tracks

A client without the file stays silent for that track but keeps following the shared timeline (the user's
choice over skip-ahead or white noise). The row shows `NotInLibrary` via
`UBBPBlueprintLibrary::GetEntryAvailability`. The controller doesn't re-check the library mid-track; a
rescan helps from the next track on.

## Volume

The vanilla Boom Box syncs its volume through its own multicast, and `mState` isn't replicated. Instead the
server reads each active Boom Box's authoritative `mState.mVolume` through an access-transformer accessor
(`GetmState()`) and replicates it in `ActiveBoomBoxes`; clients apply it with `SetVolumeMultiplier`.

**Not yet handled:** Unreal's own audio (which we use) is not affected by the game's Wwise volume sliders.
A mod-config volume will probably be needed.

## Controls

`HostOnlyControl` from the plan is **not implemented yet** — everyone can control, which is also the chosen
default. When added, the check belongs in `UBBPRemoteCallObject::GetPlaylistForRequest`.
