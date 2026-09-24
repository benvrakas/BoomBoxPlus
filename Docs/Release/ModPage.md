<!--
Text for the BoomBoxPlus page on ficsit.app (Satisfactory Mod Repository).
- "Short description" field: the line under SHORT DESCRIPTION.
- "Full description" field: everything under FULL DESCRIPTION.
- The two transparency fields: the text under NETWORK ACTIVITY TRANSPARENCY and AI USAGE TRANSPARENCY.
Replace the [bracketed] placeholders before publishing.
-->

# SHORT DESCRIPTION

Turn the Boom Box into a real speaker: your own music, YouTube, SoundCloud and Spotify playlists, synced for everyone nearby, with lyrics.

# FULL DESCRIPTION

## BoomBoxPlus

The Boom Box finally plays **your** music. Load the new **Custom Music** tape and the Boom Box plays songs
from your own music folder, YouTube or SoundCloud, or whole Spotify playlists, and everyone within earshot
hears the same song at the same moment.

Inspired by the PEAK mod sPEAKer.

![The Custom Music page: now playing, search results and the queue](https://raw.githubusercontent.com/benvrakas/BoomBoxPlus/main/Docs/Images/music-page.png)

![A Boom Box playing, with the current lyric line on screen](https://raw.githubusercontent.com/benvrakas/BoomBoxPlus/main/Docs/Images/lyrics-in-world.png)

![The Boom Box's own screen, with the Custom Music button](https://raw.githubusercontent.com/benvrakas/BoomBoxPlus/main/Docs/Images/vanilla-boombox-page.png)

### Features

- **Your own music.** Drop `.mp3`, `.ogg` or `.wav` files into a folder and search them in game.
- **YouTube and SoundCloud.** Type a search and press Enter, or paste a video, track or playlist link.
- **Spotify links.** Paste a Spotify song and it searches for it. Paste a playlist or album and every song
  is matched on YouTube. Songs start playing as soon as the first ones are found; you don't wait for the
  whole list.
- **A real queue.** Play Next, Add, reorder, remove, clear, shuffle and repeat (off, whole queue, or one
  song). Drag the seek bar to jump anywhere in a song. The queue stops at the end unless repeat is on.
- **Real 3D sound.** Music comes from the Boom Box itself and fades with distance (about 250 m), whether you
  carry it or place it.
- **Your own volume, right on the page.** A "My Volume" slider sits next to the transport controls. It's
  local to you — it doesn't change what anyone else hears — and takes effect the instant you move it.
- **Multiplayer sync.** Everyone hears the same song at the same position. Players who join late start
  mid-song, in sync.
- **One queue per Boom Box, or share one.** Every Boom Box has its own queue. To share a queue, both
  players enter each other's 4-digit link code within 3 minutes; both queues are merged, alternating songs
  from each. A group stays paired across saves and restarts — press **Leave Group** on any Boom Box to take
  just that one out again.
- **Synced lyrics.** The current line appears at the bottom of the screen while you can hear the music
  (from LRCLIB, or a `.lrc` file next to your song).
- **Now playing card.** A short notification shows each new song.
- **Game music steps aside.** The game's soundtrack fades out while Custom Music plays and fades back in
  when it stops.
- **Looks like Satisfactory.** Built from the game's own buttons, fonts and colours. The vanilla Boom Box
  screen shows the current song and progress too.

### Getting started

1. Install with Satisfactory Mod Manager. Everyone in a multiplayer session needs the mod.
2. Pick up or open any Boom Box and press **Custom Music**.
3. Search, then press **Play Next** or **Add**. The Custom Music tape loads by itself when you press
   Play or add a song.

**Your own music** goes in:

```
%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\Music
```

Press **Open Folder** on the Custom Music page to open it, and **Rescan** after adding files. Subfolders
work. A demo track is included so there's something to play right away.

In multiplayer, a song from your own folder only plays for players who have the same file. Everyone else
sees it greyed out and stays silent for that song. YouTube, SoundCloud and Spotify songs are downloaded by
each player automatically.

### Whole Spotify playlists (optional)

Without any setup, Spotify only shares the **first 100 songs** of a playlist. Spotify also only lets mods
read the whole song list of playlists **you own**. To play a longer playlist made by someone else, copy it
into your own first: in Spotify, open it, select all songs (Ctrl+A), right-click, **Add to playlist →
New playlist**, and paste the new playlist's link.

To read whole playlists you own (up to 1000 songs):

1. Sign in at [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard) and click
   **Create app**. Any name and description work.
2. Under **Redirect URIs**, add exactly `http://127.0.0.1:8888/callback`. Tick **Web API** and save.
3. Open the app's **Settings** and copy the **Client ID**, then click **View client secret** and copy the
   **Client Secret**.
4. In game, open **Mods → BoomBoxPlus** and paste them into **Spotify Client ID** and **Spotify Client
   Secret**.
5. On the Custom Music page, press **Connect Spotify** and approve in your browser. You only do this once.

![The Connect Spotify button under the search box](https://raw.githubusercontent.com/benvrakas/BoomBoxPlus/main/Docs/Images/connect-spotify.png)

Pasting a playlist link shows the matches as they're found; **Add all** starts playing right away and
keeps adding the rest in order:

![A Spotify playlist being matched on YouTube](https://raw.githubusercontent.com/benvrakas/BoomBoxPlus/main/Docs/Images/spotify-matching.png)

Use the Spotify account that created the app. The mod only asks for permission to read playlists. The
Client Secret is only ever sent to Spotify. Spotify albums work with just the Client ID and Secret, with
no sign-in.

### Settings

Open **Mods → BoomBoxPlus**.

| Setting | Default | What it does |
|---|---|---|
| Music volume | 0.5 | Volume of Custom Music, from 0 (silent) to 1 (full). Multiplied with the game's Master and Boom Box volume sliders and the Boom Box's own volume. The same slider also sits directly on the Custom Music page as "My Volume", next to the transport controls, so you don't need to open this menu to adjust it. |
| Show lyrics | On | The lyric line at the bottom of the screen. Turning it off also stops lyric lookups. |
| Show "now playing" | On | The notification when the song changes. |
| Only the host controls music | Off | On a hosted game, only the host can change queues and playback. |
| Downloaded songs to keep | 50 | How many YouTube/SoundCloud downloads stay on disk. The oldest unused ones are deleted first. |
| Game music level while playing | 0 | How loud the game's music is while custom music is playing. (0 = silent, 1 = unchanged). |
| Game music fade time | 2 s | How long the fade takes. |
| Spotify Client ID / Secret | empty | Only needed for whole Spotify playlists (see above). |

### Good to know

- Only the current song and the next two are downloaded, so big playlists don't fill your disk.
  Downloads are kept in `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\Cache`.
- Whole Spotify playlists: only ones you own (copy others into your own, see above). Everyone else's
  playlists give their first 100 songs.
- Downloading from YouTube and SoundCloud uses the bundled [yt-dlp](https://github.com/yt-dlp/yt-dlp) and
  [FFmpeg](https://ffmpeg.org). Respect the terms of the services you use.
- Dedicated servers haven't been tested yet.
- Gamepad support is partial; keyboard and mouse work fully.

### Bugs and feedback

Report problems on [GitHub Issues](https://github.com/benvrakas/BoomBoxPlus/issues). Source code: [github.com/benvrakas/BoomBoxPlus](https://github.com/benvrakas/BoomBoxPlus). Please attach your log file:

```
%LOCALAPPDATA%\FactoryGame\Saved\Logs\FactoryGame.log
```

Lines starting with `LogBoomBoxPlus` explain most problems.

### Credits

- Demo track: "Monkeys Spinning Monkeys" by Kevin MacLeod ([incompetech.com](https://incompetech.com)),
  licensed under [Creative Commons: By Attribution 4.0](http://creativecommons.org/licenses/by/4.0/).
- [yt-dlp](https://github.com/yt-dlp/yt-dlp) (Unlicense) and [FFmpeg](https://ffmpeg.org) 8.1.3
  (LGPL v3, shared build from [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds); the license and
  source links are in the mod's `Resources/Tools/ffmpeg` folder).
- Audio decoders: [dr_mp3, dr_wav](https://github.com/mackron/dr_libs) and
  [stb_vorbis](https://github.com/nothings/stb) (public domain).
- Synced lyrics from [LRCLIB](https://lrclib.net).
- Idea from [sPEAKer](https://thunderstore.io/c/peak/p/onlystar/sPEAKer/) for PEAK.
- Thanks to the Satisfactory Modding community and SML.

# NETWORK ACTIVITY TRANSPARENCY

BoomBoxPlus connects to these services. Multiplayer sync between players uses the game's own connection.

- **YouTube and SoundCloud** (through the bundled yt-dlp): when you search online (press Enter in the
  search box) or paste a YouTube/SoundCloud link, and to download songs that are in a queue you can hear.
  This includes songs other players added to a Boom Box near you: each player's game downloads those songs
  itself.
- **Spotify** (open.spotify.com): only when you paste a Spotify link, to read the song or playlist names.
- **Spotify accounts and Web API** (accounts.spotify.com, api.spotify.com): only if you enter a Spotify
  Client ID and Secret in the settings. Used to read albums and, after you press Connect Spotify and sign
  in, whole playlists. During sign-in the game briefly listens on 127.0.0.1:8888 (your own computer only)
  for Spotify's reply. Your sign-in is stored locally in
  `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\SpotifyLogin.json`.
- **LRCLIB** (lrclib.net): looks up synced lyrics for each song that plays. Sends the song's title, artist
  and length. Turn off "Show lyrics" to stop these requests.

The mod has no telemetry or analytics, and sends nothing anywhere else.

# AI USAGE TRANSPARENCY

[Edit to match how you made the mod.]

This mod was built with the help of an AI coding assistant (Anthropic's Claude):

- **Source code:** most of the mod's C++ source code was written by the AI, directed, reviewed and tested
  in game by the author.
- **Mod page text:** this description was drafted by the AI and edited by the author.
- **Developer documentation** in the source repository was written by the AI.
- **Art and audio:** no AI-generated assets. The UI uses the game's own widgets, fonts and icons. The demo
  track is by Kevin MacLeod.
- **In game:** the mod doesn't use or give access to generative AI.
