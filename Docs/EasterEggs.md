# ADA easter eggs

Now and then ADA comments on what the local player is listening to. `UBBPAdaEasterEggs`
(`Private/Playback/BBPAdaEasterEggs.cpp`) is created and ticked by `UBBPPlaybackController`, on every machine for its
own player only; nothing is replicated. The `AdaEasterEggs` setting (on by default) turns it off.

## Lines and audio (not in this repository)

The voice lines are kept out of the public repository so the jokes aren't spoiled: `Resources/Audio/ADA/` is
git-ignored, and packaging picks the files up from disk. Each line is `<name>.ogg` (radio static, then the line, 48 kHz
mono), with the subtitles in `lines.json` (`[{ "name", "subtitle" }]`). With the folder missing, the log says
`ADA: no lines in ...` and no easter eggs play.

They're made outside the repository, in `D:\SFMod\Tools\ADA` (`ada_voice.py`, `lines.json` with the text-to-speech
spelling and the subtitles):

- Voice: Google Cloud Text-to-Speech, `en-US-Wavenet-C`, the community's usual stand-in for ADA. FICSIT is spelled
  "Fix it" for it, and HUD as "H U D".
- Effect (the community recipe, done in code): one chorus voice (12 ms ± 2.9 ms at 1.9 Hz, half wet), plus two echoes
  at 0 s delay, -18.7 dB and -0.12 semitones each; normalized to -1 dB.
- `python ada_voice.py google [names]` regenerates lines (needs `GOOGLE_TTS_API_KEY`), `python ada_voice.py install
  google` copies them into `Resources/Audio/ADA` with the static in front.

## Playing a line

`TryPlay` refuses when the setting is off, the game window is in the background, the line already played this
session, another line is playing, or the last one started under 10 minutes ago (also for the first minute after
loading). A line streams through a `UBBPStreamingSoundWave` on a 2D `UAudioComponent`, at the game's Master x
Dialogue sliders (`RTPC.VO_Bus_Volume`, like ADA's own messages). While it plays, Custom Music fades to 20%
(`GetMusicDuck`, applied in `SyncEmitters`) and the HUD overlay shows "ADA: <subtitle>" on the lyric line, wrapped at
1000 px, whether or not lyrics are on.

## Triggers

All are about the channel the player hears (`GetAudibleChannel(true)`):

- Standing still (under 1 m and 5° of camera) for 5 minutes while music plays; a follow-up line after 15. Time with a
  menu open (mouse cursor shown) doesn't count.
- The same track starting 4 and 10 times in a row (a new entry with the same track id, or the position jumping back
  more than 5 s on the same entry, which is how repeat-one restarts).
- Keywords in the title, artist or uploader when a track starts: the Rick Astley video (id or title), Coffee Stain or
  "Satisfactory", metal/doom/thrash/deathcore, lofi/lo-fi/chillhop/"beats to relax". Checked in that order; the first
  that plays wins.
- A track from a YouTube "VEVO" or "- Topic" channel: 10% chance.
- An audible Boom Box at full volume (the Boom Box's own volume) with another player within 30 m of it. The subtitle
  names the player carrying it, if anyone is.
- Opening a Custom Music page: 1 in 500.
