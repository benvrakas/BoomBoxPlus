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
has Custom Music loaded, or the first time someone sends a request for it. That includes just opening the
music page (`UBBPMusicPage::ShowPage` sends `Server_EnsureChannel` the first time it's shown), so a link
code is there to read as soon as the page is, rather than only after playing or queuing something. Every
channel gets a random, unused **4-digit link code**, shown on the music page.

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

**No Boom Box in a playing channel is left on the wrong tape.** Pressing Play, Play Next or Add on the music
page loads the Custom Music tape into that specific Boom Box if another tape is in. That only covers the one
Boom Box whose page you're on, though — a Boom Box that joins a channel some other way (linking into a group
that's already playing, being restored by `RestoreSavedChannels` after a reload, or just having had its tape
changed away while its channel kept playing) would otherwise sit silent until someone opened its own page.
`ABBPPlaylistSubsystem::EnsureMembersLoaded` closes that gap: every `MaintainChannels` tick, for each channel
with a current track, it loads Custom Music onto any member that isn't already on it
(`BeginChangeTapeSequence`, using the Boom Box's own carrier as the instigator via a `mOwningCharacter`
accessor). `MergeChannels` also calls it directly right after a link completes, so the switch is immediate
rather than waiting up to one tick.

`BeginChangeTapeSequence` only *starts* the eject/insert animation; `GetCurrentTape()` changes when it
finishes. The first version asked again on every tick, which restarted the animation every 0.5 s: the tape
looped forever, the Boom Box never played, and each restart's tape-change hook pulled any open window for
that Boom Box back onto the music page (the Back button seemed not to work). Now each Boom Box is asked at
most once per 10 s (`TapeLoadRequests`, retried with a warning if the tape still isn't in), and
`UBBPMusicPage::bSuppressShowOnTapeChange` keeps these mod-initiated loads from bringing pages forward. A
placed Boom Box has no carrier, so the nearest player is named as the instigator
(`FindTapeChangeInstigator`).

**Putting a Boom Box in an inventory takes it out of its group.** The game destroys the actor when it's
picked up and spawns a new one when it's placed; the item keeps its tape, volume and repeat mode
(`FFGBoomBoxItemState`) but nothing of ours, so the placed Boom Box gets a fresh channel and link code.
This is intended: storing a Boom Box is the way to reset it.

**Queues, playback and groupings are kept in the save.** `ABBPPlaylistSubsystem` implements
`IFGSaveInterface` and saves `SavedChannels`: one `FBBPSavedChannel` per channel, stored as a JSON string
(`FJsonObjectConverter`) so the struct can change without every nested field needing the `SaveGame` flag.
Each holds the members' `GetPathName()` (the save system keeps an actor's path stable across a reload, the
same guarantee foundations and pipes rely on to reconnect), the whole queue with its entry ids, the current
entry, its position, and paused/shuffle/repeat. `SaveChannels` rebuilds the list in `PreSaveGame` (exact
position) and every 10 s as a fallback. A lone Boom Box with an empty queue isn't saved. Link codes and
pending link requests are not kept: codes are rolled fresh each session.

On load, `BeginPlay` parses them into `PendingRestores`; `RestoreSavedChannels` (start of every
`MaintainChannels`) rebuilds a channel once all its Boom Boxes exist, or after 5 s with whichever do, and
`GetOrCreateChannel` restores a Boom Box's saved channel immediately if something asks for its channel first
(so it never gets an empty one instead). `ABBPMusicChannel::RestoreFrom` puts the queue back and restarts the
current track at the saved position through the usual load wait, or leaves it paused there. Saved channels
whose Boom Boxes are all gone are dropped. Saves from 1.2.1 and earlier (which only had `SavedGroups`, the
group memberships) aren't read.

**Leaving is the only way a group loses a member**, and it's per Boom Box: `UnlinkBoomBox` ("Leave Group"
on the page) takes one Boom Box off a shared channel and gives it its own again; the rest of the group
keeps playing together and stays paired, restart or not. There's no automatic disconnect any more — a group
only shrinks when a player explicitly presses Leave Group at one of its Boom Boxes.

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

## Waiting for players to load a track

Every time a track starts (`StartEntry`, except radio), the channel sets `PlaybackState.bLoading`, bumps
`LoadGeneration` and holds the position at `PausedPosition`: `GetPlaybackPosition` doesn't advance, so nothing
auto-advances and everyone who starts the stream starts it at the same place. Each machine starts the stream
**paused** and, once it is running (or once it's clear this machine can't play the track: download failed, file not
in the library, stream failed), sends `Server_ReportLoaded(Channel, LoadGeneration)`
(`UBBPPlaybackController::ReportLoadedTracks`). The server (`ABBPMusicChannel::UpdateLoading`) ends the wait and
starts the clock from `PausedPosition` when:

- every player in `GameState->PlayerArray` has reported the current generation (a lone player always waits for
  themselves, however long the download takes);
- or no Boom Box with Custom Music loaded has played the channel for 3 s (nobody to wait for);
- or there is more than one player and 10 s have passed, so one slow download can't hold everyone. That player
  joins mid-track once their download finishes, as before.

This replaces the old behaviour, where the clock started immediately and anyone still downloading joined a few
seconds into the song (which sounded like silence at the start of every new song). The generation number keeps a
late report for an earlier start (for example the same song replayed by Repeat One) from counting. A track that is
playing now also moves to the front of the download queue (`EnsureDownloaded(Track, true)`).

## Missing tracks

A client without the file stays silent for that track but keeps following the timeline. Network tracks are
downloaded by each client itself (current + next three of every channel a nearby Boom Box plays), and a
client joins mid-song as soon as its download finishes.

## Turbo Bass

The vanilla `CanFireTurboBass()`/`IsCurrentlyPlaying()` checks read `mState.mPlaybackState`'s
`PlaybackEnabled` bit (`EBoomBoxPlaybackStateBitfield`), which is normally set by the real
`BeginPlaySequence`/Wwise playback path. Custom Music cancels that path (see the hook table above), so the
bit was never set and Turbo Bass always refused with "the music is paused", even while a track was actually
playing. `UBBPPlaybackController::ApplyPaused` (used everywhere an emitter's audio component is
paused/resumed) now also writes this bit through `GetmState()`/`SetmState()`, and `ReleaseWave` clears it
when an emitter's audio is torn down. `mState` isn't replicated, so this runs locally on every machine that
plays a given Boom Box's audio, not just the server - matching how Turbo Bass is itself a local, per-player
check on whichever Boom Box a player is currently holding.

## Volume

Final volume of a Boom Box's audio is the product of four sliders, each 0–1:

```
game Master volume              (option RTPC.Menu_Volume_Master, per player)
x game Boom Box volume          (option RTPC.Boombox_Bus_Volume, per player)
x the Boom Box's own volume     (mState.mVolume, per Boom Box, replicated via ActiveBoomBoxes)
x Music volume ("My Volume" on the page)  (default 1.0, clamped to 0–2 - can boost above the game's own mix)
```

There is no hidden gain. An earlier fixed 0.3 "headroom" factor was removed: it was tuned while the
Master slider was being ignored (see below), and "My Volume" (the `MusicVolume` setting, hidden from the
Mods menu since it's reached from the page instead) is the one place to compensate for Unreal audio being
louder or quieter than the game's own Wwise mix. The game sliders are read from `UFGGameUserSettings` once a
second as variants; values above 1 are treated as 0-100 and divided, and an option that can't be read
counts as full volume (the first version read a missing Master option as 0 and muted everything).
The Master slider's saved option id is `RTPC.Menu_Volume_Master` (seen in `GameUserSettings.ini` as
`mFloatValues=(("RTPC.Menu_Volume_Master", 0.1), ...)`). The options menu asset also names
`RTPC.Master_Bus_Volume`, but that id is never stored, so reading it always failed and Custom Music
ignored the Master slider — at 10% Master it played far louder than the game.

## Controls

Everyone can control by default. `HostOnlyControl` (mod config) rejects requests from non-host players on a
listen server, including link/unlink; the check is `UBBPRemoteCallObject::MayControl`. Link and unlink
replies go back to the requesting player (`Client_LinkResult`) and show under the page's Link field.
