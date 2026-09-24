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
| YouTube playlist (any link with `list=`, including `watch?v=...&list=...`) / SoundCloud set, artist or likes page | Lists up to 500 tracks, with an "Add all N to queue" button that queues exactly those tracks (no matching) |
| Spotify track link | Replaces the text with `Title PrimaryArtist`, then searches |
| Spotify playlist/album | Starts a background match job (below); results appear as they're matched |

## Big playlists: background matching and batched adds

**Reading the track list.** What Spotify allows (verified 2026-09-24 with a new app):

| Request | App key only (client credentials) | Signed-in user |
|---|---|---|
| `GET /v1/playlists/{id}` (name, owner) | 200, but **no songs** in the response | — |
| `GET /v1/playlists/{id}/tracks` | **403 Forbidden** | (old endpoint) |
| `GET /v1/playlists/{id}/items` | **401 "Valid user authentication required"** | used |
| `GET /v1/albums/{id}/tracks` | 200 | — |
| Public embed page `trackList` | first 100 songs, no key | — |

Signed in, it still only works for playlists **the signed-in user owns** (tested 2026-09-24: the user's
own playlist → 200; four playlists by other users that the user follows → 403 "Forbidden" on both
`/items` and `/tracks`). This is Spotify's development-mode restriction; extended quota is only granted to
registered organisations. On a 403 the mod falls back to the embed page's first 100 songs and tells the
player to copy the playlist into one they own. Playlist items wrap the track in `item` (confirmed).

So a playlist's full song list needs a **signed-in user who owns it**. `UBBPSpotifyAuth` does a one-time
authorization-code sign-in: "Connect Spotify" on the music page (shown when the Client ID/Secret are set
but nobody is signed in) opens `accounts.spotify.com/authorize` (scopes `playlist-read-private
playlist-read-collaborative`) in the browser; Spotify redirects to `http://127.0.0.1:8888/callback`, which
the game answers through Unreal's `HTTPServer` module, bound to the loopback address only (a
`HTTPServer.Listeners` `ListenerOverrides` entry added at runtime, so no firewall prompt). The `state`
value is checked, the code is exchanged for an access + refresh token with the app's Basic auth, and the
refresh token is saved to `Saved/BoomBoxPlus/SpotifyLogin.json` (with the Client ID it belongs to).
Access tokens are renewed from it when within 60 s of expiry; an `invalid_grant` reply deletes the saved
login. The listener route is removed as soon as the sign-in finishes or after 5 minutes.

**The Spotify app must list `http://127.0.0.1:8888/callback` as a Redirect URI.** Spotify apps in
development mode also only work for the app owner and users added under "User Management".

Order of attempts for a pasted link:

- Playlist, signed in: `/v1/playlists/{id}/items?limit=50`, following `next` up to 1000 songs. Items
  wrap the track in `item` (or `track` in the older format); both are read.
- Album with an app key: client-credentials token, `/v1/albums/{id}/tracks?limit=50`.
- Anything else, or if Spotify refuses: the embed page's first 100 songs, with a note explaining why
  (not signed in, no app key, or the HTTP code Spotify returned). Nothing fails outright any more just
  because the API refused.

**Spotify playlists are matched as a background job** (`UBBPNetSubsystem::StartSpotifyCollection`). After
the track list is read, three worker threads search
YouTube in parallel (`MatchWorkers`), taking tracks in playlist order so the first ones finish first. Each
result goes back to the game thread with its index; the job publishes the **settled prefix** only (track N
shows once tracks 0..N are matched or given up on), so results always appear in playlist order even though
workers finish out of order. Every published step calls the page's progress callback ("Matching on YouTube:
12 of 87 done").

**"Add all" doesn't wait for matching to finish.** While a job runs, the button reads "Add all 87 (queues as
they're found)". Pressing it hands the job a Boom Box (`QueueJobInto`): everything matched so far is queued
immediately — so the first song starts playing right away — and each later match is queued as it's
published, in order. This lives in the game-instance subsystem, not the widget, so it keeps going after the
Boom Box window is closed. A new search or closing the page cancels a job only if it isn't queueing
(`CancelJob`); cancelled workers stop after their current search.

**Adds are batched.** `UBBPBlueprintLibrary::RequestAddTracks` sends tracks in batches of 25
(`Server_AddTracks`) instead of one reliable RPC per track, which would risk overflowing the reliable
buffer and disconnecting a client that adds a 500-track playlist.

**Downloads stay small.** Nothing is downloaded when tracks are queued. Each machine downloads only the
current track and the next two of every channel a nearby Boom Box plays, one at a time, and drops any
queued download that is no longer among those (`SetWantedDownloads`) — so skipping through a 400-song
queue never builds up a backlog. Finished downloads are kept up to `MaxCachedSongs` (default 50), least
recently used deleted first.

Any YouTube/SoundCloud link that lists more than one track gets "Add all"; the first version only did this
for links it classified as playlists, so a video opened from a playlist (`watch?v=...&list=...`) listed 53
tracks with no way to add them all. Playlist placeholders (`[Deleted video]`, `[Private video]`,
`[Unavailable video]`) are dropped when parsing.

Each search bumps a generation counter; responses to older searches are dropped, so a slow Spotify playlist
match can't overwrite a newer search.
