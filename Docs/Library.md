# Music library

`UBBPLibrarySubsystem` (a `UGameInstanceSubsystem`, so UE creates it automatically — no SML registration)
knows which music files exist on *this* machine.

## Folders scanned

- `<Saved>/BoomBoxPlus/Music/` — created on first run with a `README.txt`. In the shipped game `<Saved>` is
  `%LOCALAPPDATA%\FactoryGame\Saved`.
Only the music folder is scanned. The bundled demo track (`<plugin>/Resources/Music/`, staged by
`Config/PluginSettings.ini`) is **copied into it once** (`CopyDemoMusicOnce`), so players can delete it like any
other file; `<Saved>/BoomBoxPlus/DemoMusicCopied.txt` records the copy so a deleted demo track doesn't come back.
Versions up to 1.1.0 scanned the plugin folder directly, which left the demo track undeletable.

Sub-folders are included. Only `.mp3`/`.ogg`/`.wav` are considered (extension filter for scan speed; the
format itself is still decided by magic bytes in `FBBPDecoder`).

## Track ids

`FBBPTrack::Id` is the SHA-1 of the **entire file**. That's what lets two players match a track even though
their files live at different paths. Consequence: re-tagged or re-encoded copies of the same song get
different ids and won't match — sharing works for identical files (e.g. a shared music pack or the same
YouTube download). Network tracks use their source id instead, which is naturally consistent.

Duplicate files (same hash) are listed once.

## Shared vs local data

`FBBPTrack` is what gets replicated and has nothing machine-specific. `FBBPLocalTrack` wraps it with the
file path, sample rate and channel count needed to play it locally. Keeping the path out of `FBBPTrack`
means it can never accidentally be replicated to other players.

## Scan cache

`<Saved>/BoomBoxPlus/LibraryCache.json` stores every entry keyed by path, size and modification time.
Unchanged files are reused without reading them, so after the first scan startup is near-instant. The
cache is loaded synchronously on startup so the UI has a list immediately, then a background rescan
refreshes it. Bump `LibraryCacheVersion` whenever `FBBPLocalTrack` changes shape.

## Threading

`Rescan()` runs `ScanFolders` on the thread pool and hands results back to the game thread with
`AsyncTask(GameThread)`. `Tracks`/`TrackIndexById` are only touched on the game thread. A rescan requested
while one is running is queued and run once afterwards.

## Title and artist

In order of preference:

1. Embedded tags — ID3v2.3/2.4 `TIT2`/`TPE1`, then ID3v1, for MP3; Vorbis `TITLE`/`ARTIST` comments for
   OGG; RIFF `LIST/INFO` `INAM`/`IART` for WAV. (`FBBPDecoder::ReadTags`.)
2. File name `Artist - Title.ext`.
3. The file name as the title.

ID3v2.2 (`TT2`/`TP1`, 3-byte frame sizes) is supported — the bundled demo track uses it. Limitations:
whole-tag unsynchronisation isn't undone (rare); multi-value v2.4 frames yield only the first value.

## Bundled demo track

`Resources/Music/Monkeys Spinning Monkeys.mp3` — Kevin MacLeod, **CC BY 4.0**, credit in
`Resources/Music/CREDITS.txt` and the README. Licence confirmed via Wikipedia's article on MacLeod (his
catalogue is CC BY); incompetech's standard credit line names version 4.0. Any replacement must be
redistributable: the user's first pick (Portal's radio tune) was rejected because it's Valve's copyrighted
audio.
