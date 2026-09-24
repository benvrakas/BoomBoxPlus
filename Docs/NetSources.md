# Network sources: YouTube, SoundCloud, Spotify links

## Decision: bundle the tools

The user chose to **bundle yt-dlp and ffmpeg** with the mod (over asking users to install them), accepting:

- **Size:** ~175 MB uncompressed (ffmpeg shared DLLs ~157 MB + yt-dlp ~17 MB), ~75 MB zipped.
- **Licensing:** yt-dlp is Unlicense (public domain). ffmpeg is the **LGPL v3** build (BtbN
  `win64-lgpl-shared`), not the default GPL builds; ship its `LICENSE.txt` (the fetch script copies it).
- **Distribution:** check ficsit.app's rules on bundled executables before publishing; downloader exes can
  also trip antivirus heuristics.
- **Terms of service:** downloading from YouTube is against YouTube's ToS however it's done.

The binaries are **not committed**. `Tools/FetchTools.ps1` downloads pinned versions into `Resources/Tools/`
(gitignored), verifying SHA-256; `Config/PluginSettings.ini` stages `Resources/` with the packaged mod.

| Tool | Pin | SHA-256 |
|---|---|---|
| yt-dlp.exe | 2026.08.19 | `66674953…dd3e7a` |
| ffmpeg | BtbN `autobuild-2026-09-23-14-55`, `ffmpeg-n8.1.3-win64-lgpl-shared-8.1.zip` | `60a05579…e13432` |

BtbN's `latest` tag is rolling (assets overwritten daily), so the pin uses a dated `autobuild-*` tag.
`ffplay.exe` is dropped; `ffmpeg.exe` links every other DLL, and yt-dlp wants `ffprobe.exe`.

## Why ffmpeg is needed at all

YouTube and SoundCloud serve Opus or AAC; our decoders read MP3/WAV/OGG Vorbis. yt-dlp transcodes with
ffmpeg. This build has `libvorbis` and `libmp3lame` (verified with `ffmpeg -encoders`).

## Verified behaviour (tested 2026-09-24)

**Search** — `yt-dlp "ytsearchN:<query>" --flat-playlist --dump-json` prints one JSON object per line:

```
YouTube:    id, title, channel, uploader, duration (int s), url (watch URL)
SoundCloud: id, title, uploader (no channel), duration (float s), url (api.soundcloud.com URL)
```

`scsearchN:` works the same way. Results can include hour-long loops of the same song — filter or rank by
duration.

**Download** — this produced a valid OGG Vorbis (48 kHz stereo) in ~6.5 s for a 2-minute track:

```
yt-dlp <url> -f bestaudio -x --audio-format vorbis --audio-quality 5
       --ffmpeg-location <Tools/ffmpeg> --no-playlist --no-warnings --no-progress
       -o "<cache>/<prefix>_%(id)s.%(ext)s" --print after_move:filepath
```

`--print after_move:filepath` prints the final file path, so there's no guessing the output name.

## Safety rules for running yt-dlp

- Only pass URLs whose host is youtube.com / youtu.be / soundcloud.com (and search specs we build).
- Always add `--ignore-config` (a user's global yt-dlp config could contain `--exec`) and put `--` before
  the positional URL/search argument.
- Quote every argument with Windows `CreateProcess` rules; never concatenate user text unquoted.

## Spotify links

Resolved over HTTP with no credentials (see the plan): oEmbed for a track's title, `og:description` for its
primary artist (text before the first comma), and the embed page's `trackList` for playlists/albums. The
resolved `Title Artist` text goes **into the search box** for the user to see, per the design decision.

## Implementation

All in the core module under `Net/` (not a separate plugin — the plan split it out only so the core could
ship without a downloader, which bundling makes moot; the folder boundary keeps a later split easy).

- `BBPProcess` — `BBPRunProcess` wraps `FPlatformProcess::ExecProcess` (which uses `DETACHED_PROCESS`, so
  no console windows, and passes parameters verbatim); `BBPQuoteArg` quotes each argument by the
  `CommandLineToArgvW` rules (cross-checked against Python's `subprocess.list2cmdline`). Refuses to run on
  the game thread.
- `UBBPNetSubsystem` (`UGameInstanceSubsystem`) — `Search`, `ResolveLink`, `ResolveSpotifyTrack`,
  `ResolveSpotifyCollection`, `EnsureDownloaded`. Every callback lands on the game thread. Downloads run one
  at a time.
- Track ids are `yt:<videoId>` / `sc:<trackId>` so every client derives the same id; `SourceRef` is the URL
  handed back to yt-dlp. YouTube titles of the form `Artist - Title (Official Video)` are split and cleaned,
  which gives LRCLIB a much better chance of finding lyrics.
- Downloads land in `<Saved>/BoomBoxPlus/Cache/` with an index `NetCache.json`; finished files are handed to
  `UBBPLibrarySubsystem::RegisterExternalFile`, which probes them and keeps the source id (unlike scanned
  files, whose id is a content hash). Least-recently-used downloads beyond `MaxCachedSongs` (config, default
  50) are deleted, never touching anything in the current queue.

## How network tracks play in multiplayer

Every client downloads a queued network track **itself** (`UBBPPlaybackController::PrefetchNetworkTracks`
fetches the current track and the next three). A client without the file yet stays silent, then — as soon
as the file is registered — starts the stream **at the synced position** and joins everyone mid-song. The
same late-join path covers local tracks that appear after a library rescan.

## UI

Typing searches the local library live. **Enter** runs the online part:

| Search box contains | Enter does |
|---|---|
| Plain text | YouTube (8) + SoundCloud (4) search, appended under local results with `[YouTube]`/`[SoundCloud]` tags |
| YouTube/SoundCloud track link | Shows that track |
| YouTube playlist / SoundCloud set | Lists up to 100 tracks, with an "Add all N to queue" button |
| Spotify track link | Replaces the text with `Title PrimaryArtist`, then searches |
| Spotify playlist/album | Matches each track on YouTube (closest duration of the top 3), then "Add all" |

Each search bumps a generation counter; responses to older searches are dropped, so a slow Spotify playlist
match can't overwrite a newer search.
