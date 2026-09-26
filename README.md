# BoomBoxPlus

A Satisfactory mod (game build 502094 / 1.2.4, SML 3.12) that makes the Boom Box play your own music,
YouTube, SoundCloud, Spotify playlists and live internet radio, synced for everyone in the session, with lyrics. Inspired by the
PEAK mod sPEAKer.

**Status:** release candidate. Tested in game on a hosted session; dedicated servers not tested yet.

- Player-facing description (ficsit.app page, settings, Spotify setup, network and AI disclosures):
  [`Docs/Release/ModPage.md`](Docs/Release/ModPage.md)
- Discord announcement: [`Docs/Release/Discord.md`](Docs/Release/Discord.md)
- Developer documentation: [`Docs/`](Docs/Architecture.md), starting with Architecture.md

## Screenshots

The Custom Music page: now playing with its source ("YouTube: Luis Fonsi"), Copy Link, the current lyric,
the seek bar, recent radio stations, the queue and the link code for sharing a queue.

![The Custom Music page](Docs/Images/music-page.png)

Search results from YouTube and SoundCloud, each with Play Now, Play Next and Add.

![Search results](Docs/Images/search-results.png)

The now-playing notification for a radio station: the song it announces, the artist and the station.

![A radio station's announced song in the now-playing notification](Docs/Images/radio.png)

Pasting a Spotify playlist: songs are matched on YouTube as you watch, and **Add all** queues them as they're
found while the rest are still being matched.

![A Spotify playlist being matched](Docs/Images/spotify-matching.png)

![The whole playlist matched and in the queue, its first song already playing](Docs/Images/spotify-queueing.png)

The current lyric line while a Boom Box plays nearby, placed or carried.

![Lyrics on screen near a placed Boom Box](Docs/Images/lyrics-in-world.png)

![Lyrics on screen while carrying a Boom Box](Docs/Images/lyrics-carrying.png)

The Boom Box's own screen following Custom Music, and the Custom Music tape in the tape list.

![The vanilla Boom Box screen playing Custom Music](Docs/Images/vanilla-boombox-page.png)

![The Custom Music tape in the tape list](Docs/Images/tape-list.png)

The settings in **Mods → BoomBoxPlus**.

![The mod's settings](Docs/Images/settings.png)

## Building

1. Run `Tools/FetchTools.ps1` to download the pinned yt-dlp and FFmpeg into `Resources/Tools/` (not
   committed).
2. Build and package with Alpakit (or the `PackagePlugin` UAT command) for the `FactoryGameSteam` target.
   See [`Docs/Packaging.md`](Docs/Packaging.md).

## Credits

- Demo track: "Monkeys Spinning Monkeys" Kevin MacLeod (incompetech.com). Licensed under Creative Commons:
  By Attribution 4.0 License — http://creativecommons.org/licenses/by/4.0/
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense) and [FFmpeg](https://ffmpeg.org) 8.1.3 (LGPL v3,
  BtbN shared build), bundled.
- Audio decoders: [dr_mp3 and dr_wav](https://github.com/mackron/dr_libs) (public domain / MIT-0),
  [stb_vorbis](https://github.com/nothings/stb) (public domain).
- Synced lyrics: [LRCLIB](https://lrclib.net).
