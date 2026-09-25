# Changelog

## 1.1.0 — Internet radio

**New: Radio.** The Custom Music page has a **Radio** box. Paste a live stream address and press **Play Now**
or **Add to Queue**.

- Plays internet radio stations (MP3, AAC, OGG and HLS streams, and `.pls`/`.m3u` station links), for
  example `http://stream.sunshine-live.de/dnb/mp3-192`.
- **YouTube live streams** work too: paste a live video's link into the Radio box, or find it with the
  normal search (live videos are tagged LIVE).
- The station's name is read automatically. When a station announces its songs, the current one shows
  under Now Playing and in the "now playing" notification.
- Your last 4 stations are one click away.
- Everyone near the Boom Box hears the station. Live streams can't be synced to the second like songs, so
  players may hear it a few seconds apart. Pausing disconnects; resuming picks up the live broadcast.

**Changes since 1.0.0**

- **Songs no longer start in silence.** Each song now waits until it's loaded before it starts. Alone, it waits
  for you; with other players, it waits until everyone has it (at most 10 seconds), then everyone starts it together.
- Search results have a **Play Now** button, next to Play Next and Add.
- The demo track is now copied into your music folder once, so you can delete it.
- Moving songs in the queue uses a small stacked ^ / v control instead of separate Up and Down buttons.
- Emoji in song and station titles no longer show as "?" boxes (the game's font can't draw them, so they're left out).
- The link code line no longer runs into the Link button.
- A **My Volume** slider on the Custom Music page, next to the transport controls. It's the same setting
  as Music volume in the Mods menu and only affects what you hear.
- Custom Music now follows the game's **Master** volume slider (it used to ignore it and play too loud).
  Final volume is Master × Boom Box volume slider × the Boom Box's own volume × Music volume.
- Music volume now goes from 0 to 1, default 0.5.
- **Linked Boom Boxes stay linked** across saves and restarts. The button is now **Leave Group**, which takes
  just that Boom Box out; the others stay together.
- The Custom Music page keeps a fixed size instead of resizing while lists load, and long titles are cut
  off cleanly.
- Dedicated server builds (Windows and Linux) are included. They haven't been tested much yet.

## 1.0.0 — Initial release

**The Boom Box plays your music.** Open any Boom Box and press **Custom Music**.

- Play your own `.mp3`, `.ogg` and `.wav` files from `%LOCALAPPDATA%\FactoryGame\Saved\BoomBoxPlus\Music`.
- Search YouTube and SoundCloud, or paste a video, track or playlist link. "Add all" queues a whole
  playlist.
- Paste Spotify songs, albums and playlists. Each song is matched on YouTube, and playback starts as soon
  as the first ones are found. Whole playlists you own can be read after a one-time **Connect Spotify**
  (see the mod page); otherwise the first 100 songs.
- Queue with Play Next, Add, reorder, remove, clear, shuffle and repeat (off, all or one), plus a seek bar.
- 3D sound from the Boom Box itself, heard up to about 250 m away.
- Multiplayer: everyone hears the same song at the same moment, and late joiners start mid-song.
- Every Boom Box has its own queue. Link two by entering each other's 4-digit code within 3 minutes to
  share and merge queues.
- Synced lyrics (LRCLIB or `.lrc` files) and a "now playing" notification for the nearest Boom Box you
  can hear.
- The game's own music fades out while Custom Music plays.
- Settings in **Mods → BoomBoxPlus**: music volume, lyrics, now playing, host-only control, download cache
  size, game music level and fade time, Spotify app key.

**Known limitations**

- Dedicated servers are not supported yet.
- Gamepad support is partial.
- A song from your own music folder only plays for players who have the same file.
