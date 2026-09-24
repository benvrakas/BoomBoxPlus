# Debugging

Expect the first in-game runs to fail somewhere. Everything logs to the **`LogBoomBoxPlus`** category.

## Where the log is

- Game: `%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log`
- Filter it: `Select-String LogBoomBoxPlus "$env:LOCALAPPDATA\FactoryGame\Saved\Logs\FactoryGame.log"`

## Verbosity

| Level | Used for |
|---|---|
| `Error` | Programming errors (e.g. `StartStream` called twice, thread creation failed) |
| `Warning` | Something the user can hit: unreadable/undecodable file, seek failure, file changed since scan |
| `Log` | Lifecycle: module start, hooks installed, first hook call, library scan summary, stream start/stop |
| `Verbose` | Per-track and per-seek detail |

Enable verbose output with the launch argument `-LogCmds="LogBoomBoxPlus Verbose"`, or at runtime in the
console: `Log LogBoomBoxPlus Verbose`.

The audio render thread never logs (it runs every few milliseconds). Instead the stream counts underruns and
reports the total when it stops: `Stopping stream '...' at 12.34 s (N underruns)`. A non-zero count means the
decode thread fell behind.

## Expected startup sequence

```
LogBoomBoxPlus: BoomBoxPlus module starting
LogBoomBoxPlus: Hook installed: AFGUnlockSubsystem::GetUnlockedTapes
LogBoomBoxPlus: Library: music folder is '...\Saved\BoomBoxPlus\Music'
LogBoomBoxPlus: Library: no scan cache yet                     (first run only)
LogBoomBoxPlus: Music library: N tracks
LogBoomBoxPlus: Library scan: N tracks (a cached, b decoded, c skipped, d duplicates) in x s
LogBoomBoxPlus: Hook: GetUnlockedTapes fired for the first time (...)   (when the tape list is opened)
```

## Symptom → first thing to check

| Symptom | Look for |
|---|---|
| No `LogBoomBoxPlus` lines at all | Module didn't load: check SML's mod list in the log, and that `BoomBoxPlus.dll` was built |
| "Editor build: native hooks not installed" | Expected in the editor/PIE. Hooks only run in the packaged game |
| Custom Music missing from tape list | Was "GetUnlockedTapes fired" logged? If not, the widget gets its list another way. If yes, the widget may cache its list — see Architecture.md |
| Track skipped in library | `Library: skipping '...': <reason>` — reason comes from `FBBPDecoder::GetLastError()` |
| Silence when a track should play | `Streaming '...'` then `Decode thread opened '...'`? A `Could not decode` / `changed format since it was scanned` warning explains it |
| Choppy audio | Underrun count in the `Stopping stream` line |
| Streams start but nothing is heard; drift is exactly -1.0 s every check | No Unreal audio device. Satisfactory ships with `[Audio] AudioMixerModuleName` empty, so the engine logs `Audio Device Manager Initialization Failed!`. The module sets it to `AudioMixerXAudio2` at startup (`Audio: set engine audio mixer module ...`); look for that line, `Playback controller ready (Unreal audio device available)`, and any `LogAudio` / `LogAudioMixer` errors |
| Game music doesn't fade | `GameMusic: lowering 'Music_Bus_Volume' from X to Y`. X of 0 means the Wwise parameter name is wrong or unset |
| Custom Music too loud/quiet | `Playback: volume ... (Boom Box ... x mod setting/game sliders/headroom ...)` and `Playback: game volume sliders Master ... x Boom Box ...` |
| Linking | `Playlist: ... linked to channel NNNN`, `Channel NNNN: zipper-merged ...`, and on the requesting client `Link result for ...` |
| Wrong position after joining / skipping | `Seek '...' to x s (was at y s)` at Verbose; `Seek to frame ... failed` warnings |

## Never use `check()` in this mod

Satisfactory's build sets **`ChecksInShipping: True`** (visible in the UBT log), so `check`, `checkf`,
`verify` and `checkNoEntry` stay active in the shipped game and a failure **crashes the player's game**.
Use a logged early return instead (see `BBP_REQUIRE_AUTHORITY` in `BBPPlaylistSubsystem.cpp`). `ensure` is
acceptable for "should never happen" diagnostics since it only logs.

## Things confirmed from source (so you don't need to re-check)

- A pure C++ `UUserWidget` always ticks (`UUserWidget::UpdateCanTick`: `bCanTick |= !WidgetBPClass || ...`),
  so `NativeTick` on the page/overlay runs without a Blueprint subclass.
- SML creates the injected page with `UUserWidget::CreateWidgetInstance(*BPW_BoomBox, ...)`, so the page's
  outer chain reaches the `BPW_BoomBox` instance — that's how `GetBoomBox()` finds `mBoomBox`.
