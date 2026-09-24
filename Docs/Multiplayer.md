# Multiplayer: per-Boom Box queues, linking and synced playback

## Pieces

| Class | Runs on | Does |
|---|---|---|
| `ABBPPlaylistSubsystem` | Server (authoritative) + replicated to clients | Which Boom Boxes have Custom Music; the list of channels; link/unlink; creating and removing channels |
| `ABBPMusicChannel` | Server (authoritative) + replicated to clients | One queue + transport, its 4-digit link code and the Boom Boxes playing it; track-end detection |
| `UBBPRemoteCallObject` | Client → server (and link replies back) | Carries every player request, each naming its Boom Box; validates and logs rejections |
| `UBBPBlueprintLibrary` | Anywhere | The one API UI and hooks call (`Request*(BoomBox, ...)`); always goes through the local player's RCO |
| `UBBPPlaybackController` | Every non-dedicated machine | Owns the audio components and streams, keeps each in sync with its Boom Box's channel |

The subsystem is registered by `UBBPGameWorldModule`, the RCO by `UBBPGameInstanceModule` (native root
modules, see Architecture.md). Channels are spawned by the subsystem.

## Channels: one queue per Boom Box, shareable

Each Boom Box has its own queue and audio by default: the server gives it a **channel** the first time it
has Custom Music loaded, or the first time someone sends a request for it (so a queue can be built before
the tape goes in). Every channel gets a random, unused **4-digit link code**, shown on the music page.

**Linking is mutual and time-limited.** Typing another Boom Box's code into the page's Link field sends a
link request (`FBBPLinkRequest`: from-code, to-code, expiry), replicated on the subsystem. The link only
happens when someone at the other Boom Box enters this one's code **within 3 minutes**
(`ABBPPlaylistSubsystem::LinkWindowSeconds`); otherwise the request lapses. Both pages show the pending
request with a countdown ("Waiting for Boom Box 1234 to enter 5678. 2:41 left." / "Boom Box 5678 wants to
link. Enter 5678 within 2:41 to merge queues."). A channel has at most one outgoing request; entering a new
code replaces it. When the second code arrives in time, the whole second channel (every Boom Box on it) is
merged into the first requester's channel (`MergeChannels`) and all requests involving either code are
dropped. **Unlink** puts one Boom Box back on a channel of its own, starting from a copy of the shared queue
and playback (so the music doesn't cut out).

**Zipper merge on link.** When the linking Boom Box was the only one on its channel, its queue is merged
into the target's (`ABBPMusicChannel::MergeFrom`): the target's history and current track stay where they
are, then the upcoming tracks interleave — target, joiner, target, joiner. The joiner's queue is taken from
its current track onward, then its earlier tracks. Merged entries get new entry ids from the target. If the
target was idle and the joiner was playing, the joiner's track starts. The queue cap (1000) still applies.

A channel with no Boom Boxes left (all linked away or destroyed) is destroyed. When the **last** Boom Box
on a channel that had Custom Music loaded switches to another tape, the channel is paused so it doesn't run
through its queue in silence (`bWasHeard`). A channel that never had the tape loaded is left alone: the
first version paused those too, which paused every Play press on a Boom Box whose tape wasn't in yet.
Pressing Play, Play Next or Add on the music page now also loads the Custom Music tape if another tape is
in.

Channels are session state only — nothing is saved. Codes are re-rolled each session.

## Replicated state

On each channel (`bAlwaysRelevant`, net update 10 Hz plus `ForceNetUpdate` on every change):

- `Queue` — `TArray<FBBPQueueEntry>`. Entry ids are unique **within the channel**, so remove/move stay
  correct when two players edit concurrently or a track is queued twice.
- `PlaybackState` — replicated **as one struct** so clients never see a half-applied change. `Revision`
  increments on every change, which is how clients notice a restart or seek of the same track.
- `LinkCode`, `Members`.

On the subsystem: `ActiveBoomBoxes` (Boom Boxes with Custom Music, plus each one's volume) and `Channels`.

The subsystem is `bAlwaysRelevant` (inherited from `AFGSubsystem`); SML turns replication on because
`ReplicationPolicy = SpawnOnServer_Replicate`.

## Time base

`PlaybackState.TrackStartServerTime` is the server world time at which position 0 would have played.
Everyone computes the expected position as `GetServerWorldTimeSeconds() - TrackStartServerTime` (or
`PausedPosition` while paused). UE's server-time estimate on clients isn't latency-compensated, so expect
errors of roughly half the ping — well inside the 250 ms drift tolerance.

## Drift correction

`UBBPPlaybackController` compares each stream's real position (`GetPlaybackSeconds`, which only advances on
real audio) with the expected position once a second, and seeks when they differ by more than 0.25 s. It
logs every correction with the drift and underrun count.

A drift that is *exactly* one check interval every time (`playing 10.05, expected 11.06`, then
`playing 11.06, expected 12.06`) means the stream is never consumed at all — see "No Unreal audio device"
in Debugging.md.

A new state `Revision` is applied immediately: the pause state is set, and the stream is seeked **only if**
its position differs from the expected one by more than the tolerance (shuffle/repeat toggles also bump
`Revision` and must not cause a hiccup).

Each emitter remembers which channel it is playing. When its Boom Box's channel changes (link, unlink, or a
first channel), the stream is stopped and restarted from the new channel's position, because entry ids from
different channels can collide.

## Shuffle and repeat

**The server alone chooses the next track**; clients only follow `CurrentEntryId`. Shuffle picks randomly
among entries not yet played this cycle (`ShufflePlayed`, server-only); when exhausted it reshuffles
(repeat all) or stops (repeat off). `Previous` uses a server-only play history; within the first 3 s of a
track it goes back, otherwise it restarts the track.

**Repeat defaults to Off**, and only "repeat all" loops from the last song back to the first (or starts a
new shuffled round). Pressing Next on the last song does nothing: the song keeps playing
(`ABBPMusicChannel::Skip`). When the last song ends by itself with repeat off, playback stops. Repeat one
replays the song when it ends, but Next still moves on.

Adding to an empty/stopped queue starts playback immediately. "Play Next" inserts after the current entry.

## Missing tracks

A client without the file stays silent for that track but keeps following the timeline. Network tracks are
downloaded by each client itself (current + next three of every channel a nearby Boom Box plays), and a
client joins mid-song as soon as its download finishes.

## Volume

Final volume of a Boom Box's audio =

```
Boom Box's own volume (mState.mVolume, replicated via ActiveBoomBoxes)
x MusicVolume (mod config, default 0.8)
x the game's Master and Boom Box volume sliders (RTPC.Master_Bus_Volume, RTPC.Boombox_Bus_Volume options)
x 0.3 headroom (BaseGain in BBPPlaybackController.cpp)
```

The headroom exists because Unreal's mixer plays at full scale while the game's Wwise mix is much quieter;
the first in-game test at 0.8 was far too loud. The game sliders are read from `UFGGameUserSettings` once a
second as variants; values above 1 are treated as 0-100 and divided, and an option that can't be read
counts as full volume (the first version read a missing Master option as 0 and muted everything).

## Controls

Everyone can control by default. `HostOnlyControl` (mod config) rejects requests from non-host players on a
listen server, including link/unlink; the check is `UBBPRemoteCallObject::MayControl`. Link and unlink
replies go back to the requesting player (`Client_LinkResult`) and show under the page's Link field.
