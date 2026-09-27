# Changelog

## 1.3.4

- Fixed **Downloaded songs to keep** and **Game music fade time** in **Mods → BoomBoxPlus** showing red and refusing
  their values. Every number setting now accepts its full range.

**Good to know**

- Everyone in a multiplayer session needs 1.3.4, as usual.

## 1.3.3

- Fixed internet radio sounding garbled for the first few seconds after tuning in (new in 1.3.2).
- The My Volume slider and the nearby Boom Box sliders move in **5% steps**. Typing a number still sets any value.

**Good to know**

- Everyone in a multiplayer session needs 1.3.3, as usual.

## 1.3.2

- **Boom Boxes you can hear.** The Custom Music page lists every Boom Box you can hear, nearest first, with what it's
  playing and a volume slider for each, so you can turn down the one next door without walking over to it. These
  are the same per-player volumes as My Volume.
- **Choose which Boom Boxes you hear**: all of them (as before), only the **nearest of each linked group**, or
  **only the one you carry**. Set it in **Mods → BoomBoxPlus** or with the **Hear:** button on the Custom Music page.
  It only changes what you hear, and muted Boom Boxes keep their place in the song.
- Custom Music now follows the game's **Music** volume slider too (as well as Master and Boom Box).
- **Linked Boom Boxes play radio in step.** Each Boom Box used to connect to the station on its own and buffer a
  different amount, so a group could echo. Now your game makes one connection per station and every Boom Box
  plays the same moment of it (and uses less bandwidth).
- Fixed other players' My Volume settings quietly going back to 100% after they walked away from a Boom Box and came
  back, and not being remembered between sessions. (Only the host's were kept.)
- Songs whose title contains an emoji (or that are over 20 minutes long) no longer ask LRCLIB for lyrics every
  time they play; a lookup that can't succeed is now remembered.
- **Accept or decline link requests.** When another Boom Box asks to link with yours, its page shows **Accept** and
  **Decline** buttons, instead of you having to type the other code back. The asker sees if you decline.
- YouTube/SoundCloud results and pasted links now show at the **top** of the results, above your own matching
  music; a pasted link shows only its own tracks instead of your whole library first.
- The **%** sign now sits next to the My Volume box instead of inside it, so you only type the number.

**Good to know**

- Everyone in a multiplayer session needs 1.3.2, as usual.
- Other players' My Volume settings (not the host's) start back at 100% once, since they were stored in a way that
  didn't last anyway.

## 1.3.0 — Stations

**New**

- **A list of your stations.** The radio stations under the search box are now a list you manage, not just
  your last four: up to 12, each with an **x** to remove it. Clicking one plays it without reshuffling the list.
  **YouTube live streams** are saved there too when you queue one.
- **Mute when the game is in the background** (new setting, on by default): Custom Music goes quiet while you're
  in another window and picks up at the right spot when you come back.
- **My Volume is per Boom Box.** Each Boom Box has its own My Volume, just for you, so two players with Boom Boxes
  side by side can each turn the other's down and listen to their own music. It's remembered between sessions.
  Everyone's volumes start back at 100% with this update.
- **Type your volume.** Click the percentage next to the My Volume slider, type a number from 0 to 200 and press
  Enter.
- **Fairer start in multiplayer.** A new song still starts the moment everyone has it loaded. If some players are
  slower, it now waits until most players have it, then gives the rest 10 more seconds (it used to start 10 seconds
  after the song began loading, even if hardly anyone had it yet). A download that hangs can hold a song up for at
  most 2 minutes.

**Good to know**

- Your saved stations carry over from 1.2.6.

## 1.2.6 — Now playing, everywhere

**New**

- **What's playing reads the same everywhere.** The Custom Music page, the Boom Box's own screen and the
  "now playing" notification all show the song title, and under it the artist and where it's from, e.g.
  "Koven, YouTube: Monstercat Uncaged". The Boom Box's own screen used to show a static "Your own music, YouTube
  and SoundCloud."
- **Radio shows the current song.** The title is the song the station announces, updating as it moves on, and
  the notification pops up for each new song.
- **Copy Link** on the Custom Music page copies the playing song's YouTube or SoundCloud page, or the radio
  station's stream address.
- The progress bar on the Boom Box's own screen now follows Custom Music, matching the Custom Music page's
  seek bar (it stays empty for radio, which has no length).
- Lyrics on the Custom Music page have a line of their own.
- The "now playing" notification moved to bottom-centre, just above the lyrics. In the top-right corner it
  could end up hidden behind the milestone panel.
- The recent station buttons have a "Recent stations:" label.

**Fixes**

- **Mods → BoomBoxPlus** is a normal list of settings again, instead of one long row you had to scroll sideways.
- Pasting a radio stream address into the search box works again. 1.2.5 always answered "That's a song or
  playlist link, not a live stream." (The recent station buttons still worked.)
- Queuing a song 24 hours or longer, or a local file with an extremely long title or artist tag, no longer
  disconnects the player from a multiplayer game.
- Turbo Bass no longer says the music is paused after pausing and resuming a radio station.
- A Boom Box starting Custom Music no longer plays a split second at the wrong volume.
- A YouTube/SoundCloud song that can't be downloaded is no longer retried every second while it's coming up in
  the queue; it's tried again when it actually plays.
- The download cache now keeps the songs you play often, as the "Downloaded songs to keep" setting says. Before,
  it deleted the oldest downloads first, however recently they'd been played.
- Lyrics for YouTube/SoundCloud songs are now cached in normal files (they were written somewhere Windows hides),
  so they're fetched once more after updating.

**Good to know**

- Updating resets the mod's settings to their defaults once, including the Spotify Client ID and Secret; enter
  them again.
- Songs queued before this update show just "YouTube" or "SoundCloud" as their source. Queue them again to see
  the channel. Songs downloaded before it pick up their channel after being played once from a search.

## 1.2.5 — Radio joins the search box

- **No more separate Radio box.** Paste a live stream address into the same search box as everything else
  and press Enter; the station shows up as a normal search result with Play Now, Play Next and Add, exactly
  like a YouTube link. Recent stations are still one click away, now just underneath the search box.
- Songs queue normally whether or not a station is currently playing — nothing about that changed, it's just
  one field to use now instead of two.

## 1.2.4 — Turbo Bass fix

- Fixed **Turbo Bass** refusing to fire ("the music is paused") while Custom Music was actually playing.
- **Boom Boxes remember their queue.** The queue, the song that was playing and where it was, pause,
  shuffle and repeat are now kept in your save, along with which Boom Boxes are linked. Load the game and
  the music carries on from where it was. (Link codes are still new each session.)
- A Boom Box gets its link code as soon as you open its Custom Music page, instead of only after playing or
  queuing something.
- **My Volume can now boost Custom Music, not just lower it.** Range is 0 to 2 (200%), default 1 (unboosted,
  same loudness as before); 2 doubles it above the game's own mix.
- Fixed the **My Volume** slider being stuck at 50%. Since 1.2.0 the mod couldn't read or save any of its
  settings, so every setting in **Mods → BoomBoxPlus** was ignored and its default used instead.
- Updating from 1.1.0 or earlier resets the mod's settings, including the Spotify Client ID and Secret; enter
  them again. Links saved by earlier versions aren't carried over either.

## 1.2.1 — Linked Boom Box fix

- Fixed a linked Boom Box getting stuck replaying the tape-change animation and never playing, and its
  window jumping back to the Custom Music page when you pressed Back. (1.2.0 kept re-inserting the tape
  before the animation could finish.)
- Picking a Boom Box up into your inventory resets it: placed again, it has its own new queue and link code
  and has left any group it was in.

## 1.2.0 — Linking fixes

- **Linking now switches every Boom Box in the group onto Custom Music automatically.** Previously, a Boom
  Box you linked in kept playing (or sitting silent on) whatever tape it already had until you opened its
  own page and pressed something. This also now applies generally: any Boom Box that ends up sharing a
  channel that's already playing (linking, being reunited after a save reload, or having had its tape
  changed away mid-song) gets Custom Music loaded automatically.
- **Linking no longer restarts a song that was already playing.** If the Boom Box you linked to had nothing
  playing yet, it used to pick up the other side's song from 0:00 instead of from where it already was.
- Music volume is no longer a separate setting in **Mods → BoomBoxPlus**; it's the same value as "My Volume"
  on the Custom Music page, which is the easier place to reach it from.
- Fixed **Mods → BoomBoxPlus** showing every setting as one row running off the edge of the screen instead
  of a normal stacked list.
- Smaller download: dedicated server packages no longer carry the bundled ffmpeg/yt-dlp, which they never
  use (only players' own games download and play music; the server just relays the queue).

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
- Dedicated server builds (Windows and Linux) are included, but haven't been tested on a real server yet. Please
  report how they work.

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
