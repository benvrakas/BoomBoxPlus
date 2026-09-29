# Internet radio and YouTube live

A queue entry can be a **live stream** (`EBBPTrackSource::Radio`, `FBBPTrack::IsLive()`): an internet radio
station (`http://stream.sunshine-live.de/dnb/mp3-192` was the first request) or a YouTube video that is
broadcasting live. A live entry has no length, is never downloaded, and plays until someone presses Next.

## Why ffmpeg, not Unreal's HTTP + our decoders

The first plan was to stream the HTTP body (`SetResponseBodyReceiveStreamDelegateV2`) into the MP3 decoder.
Running the bundled ffmpeg instead (`FBBPLiveStreamWorker`) turned out simpler and covers far more:

- MP3, AAC, OGG and HLS (`.m3u8`) stations all work; our decoders only read MP3/WAV/Vorbis from whole files.
- ffmpeg resamples everything to **48 kHz stereo s16** (`BBPRadio::SampleRate/Channels`), so the sound wave's
  format is known before the first byte arrives. A procedural wave's rate must be set before it plays.
- ffmpeg reconnects by itself (`-reconnect*` options) and reads ICY metadata: the station name (`icy-name`)
  and song changes (`Metadata update for StreamTitle: ...`, logged at `-loglevel verbose`).

Tested against the requested station: connects in well under a second, 44.1 kHz MP3 in, 48 kHz PCM out,
station name "sunshine live - Drum n Bass" (this station also sends its name as the StreamTitle, so no
song titles show for it).

## Pieces

| Piece | Does |
|---|---|
| `BBPRadio` (`Private/Net/BBPRadio.*`) | URL checks, `.pls`/`.m3u` parsing, ffmpeg input arguments, ffmpeg log parsing |
| `UBBPNetSubsystem::TuneRadio` | Called from the search box (`EBBPLinkKind::Stream`) on Enter: validates, follows station playlists, **probes** the stream (ffmpeg decodes 1 s) for its name |
| `UBBPNetSubsystem` saved stations | Up to 12 stations and YouTube live streams (`GetStations`/`RememberStation`/`ForgetStation`), `Saved/BoomBoxPlus/RadioStations.json` |
| `FBBPLiveStreamWorker` | Thread per station playing on this machine: runs ffmpeg, hands stdout to `FBBPLiveFanOut`, reads stderr for titles, restarts on drops |
| `FBBPLiveFanOut` (`Private/Audio/BBPLiveFanOut.h`) | Delivers one station's audio to every wave playing it on this machine and keeps them at the same point (1.3.2) |
| `FBBPStreamState` (`Private/Audio/BBPStreamState.h`) | Ring buffer shared with file playback; live streams get a 6 s ring and a 2 s prebuffer |
| `UBBPStreamingSoundWave::StartLiveStream` | Live variant of `StartStream`; `Seek` does nothing |
| `ABBPMusicChannel::PlayTrackNow` / `Server_PlayTrackNow` | "Play Now": insert after the current entry and start it |

## Sync model

There is no shared position for a live stream: every machine connects to the station itself and hears it
"live", a few seconds apart depending on buffering.

**On one machine, though, every Boom Box playing a station is in step** (1.3.2). Before, each Boom Box's wave ran
its own ffmpeg and buffered on its own, so linked Boom Boxes on one client drifted apart by whatever each had
buffered, and echoed. Now `UBBPStreamingSoundWave::StartLiveStream` shares one `FBBPLiveSource` (one worker, one
connection) per stream Url, and `FBBPLiveFanOut` writes each chunk to every wave's own ring:

- a wave that joins copies a playing wave's unplayed audio (`FBBPStreamState::CopyBufferFrom`), so it starts at the
  same point;
- the worker hands over only as much as the ring with the most room can take (`FBBPLiveFanOut::Write` returns what
  it took; the rest waits in the worker and ffmpeg's pipe). That ring belongs to a wave that is playing, so audio
  arrives at the speed it's played. Stations send several seconds at once on connect, faster than real time; taking
  it all made the rings drop audio mid-playback, which garbled the first seconds in 1.3.2 (fixed in 1.3.3);
- rings with less room drop their oldest audio instead of refusing new audio (`WriteDroppingOldest`), so a wave that
  isn't being played (a silent or out-of-range Boom Box, which Unreal stops rendering) always holds the newest audio;
- after every write, a wave nobody is playing (its `FBBPStreamState::LastReadSeconds` is over 0.25 s old) and more
  than 60 ms behind the playing wave furthest ahead (least buffered) is trimmed to match (`FBBPLiveFanOut::Align`).
  Playing waves are only trimmed if more than 1 s behind: since 1.3.5 each plays at its own Doppler pitch, so two
  linked Boom Boxes can differ by up to their difference in distance / 343 m/s, and trimming that would click.
  (1.3.2-1.3.4 trimmed any wave more than 60 ms behind.)

The last wave to stop releases the source, which stops the worker. A source whose worker gave up isn't joined; the
next wave starts a fresh connection. The channel still replicates which entry plays and
whether it's paused. In `UBBPPlaybackController`:

- Drift correction and seeking are skipped for live emitters (`FBBPEmitter::bLive`).
- **Pause disconnects** (`ReleaseWave`, keeping the entry); resume reconnects to what the station is playing
  now, rather than resuming seconds-old buffered audio.
- The server never auto-advances a live entry (`ABBPMusicChannel::Tick`), and doesn't warn about its
  missing duration.
- Prefetch and `EnsureDownloaded` skip live entries; lyrics are never looked up for them.

## YouTube live

yt-dlp's flat listing marks broadcasts with `live_status: "is_live"`. `ParseTrackJson` turns those into
radio tracks (`Id` `ytlive:<id>`, `SourceRef` the watch URL). The watch URL is what replicates. The actual
stream address (`yt-dlp -g -f bestaudio/best`, an audio-only HLS manifest on `manifest.googlevideo.com`) is
**signed for the requesting IP and expires after a few hours**, so every machine looks it up itself, again
on every ffmpeg restart. The lookup takes about 3 s and runs as a child process the worker polls, so a
skip or pause kills it instead of blocking the game thread in `StopStream` (`Kill(true)` waits for the
worker).

A YouTube link pasted into the search box goes through the normal yt-dlp listing (`EBBPLinkKind::YouTubeVideo`,
not `TuneRadio`). If the video is broadcasting live, the result is a radio track with a LIVE tag; otherwise it's an
ordinary video. Either way it queues like any other result.

**No separate Radio panel since 1.2.5.** A stream address is just another link the search box classifies
(`EBBPLinkKind::Stream`); tuning in shows the station as a single search result with the normal Play Now/
Play Next/Add buttons. Under the search box, a list of saved stations (1.3.0; up to 12, each with a remove
button) plays one with a click. See UI.md.

## Security

Every client runs ffmpeg on a URL that some *other* player queued, so:

- The server rejects radio tracks whose `SourceRef` isn't a plain http(s) URL (`IsTrackValid` in the RCO),
  and each client checks again in `StartLiveStream` (`BBPRadio::IsStreamUrl`: web scheme, ≤ 500 chars, no
  spaces, quotes, backslashes or control characters).
- ffmpeg gets `-protocol_whitelist http,https,tcp,tls,crypto,httpproxy`, so `file:`, `concat:`, `data:` and
  similar can't be reached even through a playlist or HLS manifest (verified: `file:C:/Windows/win.ini` is
  refused with "Invalid argument").
- Arguments go through `BBPQuoteArg`; a URL always starts with `http`, so it can't be read as an option.
- yt-dlp only runs for YouTube hosts, with `--` before the URL.

Remaining exposure, stated on the mod page: a player can make everyone near their Boom Box connect to an
arbitrary web address (like opening a link).

## Debugging

`LogBoomBoxPlus` lines to look for:

```
Net: tuning in to '<url>' / Net: '<url>' is station '<name>'        probe OK
Net: radio probe of '<url>' failed, ffmpeg code N: <reason>          e.g. "Server returned 404 Not Found"
Radio: connecting to '<url>'                                         ffmpeg started
Radio: receiving audio from '<url>' (station '<name>')               first audio in the ring
Radio: '<station>' now playing '<song>'                              ICY title change
Radio: ffmpeg stopped (code N) after S s ... Last output: ...        drop; followed by a reconnect attempt
Radio: giving up on '<url>' after N attempts                         4 failed starts in a row
Radio: YouTube live '<url>' streams from https://manifest...          yt-dlp lookup OK
```

Every ffmpeg log line is also logged at `Verbose` as `Radio ffmpeg: ...`
(`log LogBoomBoxPlus Verbose` in the console).

To try a station outside the game, run the same command the worker runs:

```
ffmpeg -hide_banner -nostdin -loglevel verbose -protocol_whitelist http,https,tcp,tls,crypto,httpproxy ^
  -reconnect 1 -reconnect_streamed 1 -reconnect_on_network_error 1 -reconnect_delay_max 10 ^
  -rw_timeout 15000000 -i <url> -t 10 -vn -ac 2 -ar 48000 -f s16le out.pcm
```
