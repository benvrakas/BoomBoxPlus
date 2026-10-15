# ADA easter eggs

Now and then ADA comments on what the local player is listening to. `UBBPAdaEasterEggs`
(`Private/Playback/BBPAdaEasterEggs.cpp`) is created and ticked by `UBBPPlaybackController`, on every machine for its
own player only; nothing is replicated. The `AdaEasterEggs` setting (on by default) turns it off.

## Lines and audio (not in this repository)

The voice lines are kept out of the public repository so the jokes aren't spoiled: `Resources/Audio/ADA/` is
git-ignored, and packaging picks the files up from disk. Each line is `<name>.ogg` (48 kHz mono, at -23 LUFS), with
the subtitles in `lines.json` (`[{ "name", "subtitle" }]`). With the folder missing, the log says
`ADA: no lines in ...` and no easter eggs play.

They're made outside the repository, in `D:\SFMod\Tools\ADA` (`ada_voice.py`, `lines.json` with the text-to-speech
spelling and the subtitles):

- Voice: Google Cloud Text-to-Speech, `en-US-Wavenet-C`, the community's usual stand-in for ADA. FICSIT is spelled
  "Fix it" for it, and HUD as "H U D".
- Effect, done in code, in this order:
  - tone: 90 Hz low cut, and a presence lift above 5 kHz (+40% of the band), for a crisp dry voice;
  - a faint chorus (12 ms ± 2.9 ms at 1.9 Hz, 15% wet) and two echoes at 0 s delay, -18.7 dB and -0.12 semitones
    each (from the community recipe, which used a half-wet chorus);
  - a ducked reverb, like ADA's own lines (crisp while she speaks, the echo showing in the gaps): a 0.45 s room with
    30 ms pre-delay and a 6 kHz low-pass, at -8 dB, held down by up to 24 dB by a peak follower on the voice (5 ms
    attack, 60 ms release). Measured on "idle": the echo sits about 25 dB under the voice, both while she speaks and in
    the gaps (where nothing else covers it). A 0.8 s room at -2 dB was too long and strong by ear.
  1.4.0's first build (and 1.3.9) used the recipe's half-wet chorus and no reverb, which always smeared the voice
  ("softer and more muddy" next to ADA's own lines).
- `python ada_voice.py google [names]` regenerates lines (needs `GOOGLE_TTS_API_KEY`), `python ada_voice.py install
  google` copies them into `Resources/Audio/ADA`, each brought to -23 LUFS (EBU R128 dialogue level; measured with
  ffmpeg's `ebur128`). Straight out of the effect they were -15.6 to -20.2 LUFS, louder than the game's voice lines
  and uneven between lines. 1.3.9 shipped them like that, with a generated radio static in front.

## Playing a line (1.4.0)

`TryPlay` refuses when the setting is off, the game window is in the background, the line already played this
session, another line is queued or playing, one of the game's own messages is on screen, or the last line started under
10 minutes ago (also for the first minute after loading).

ADA's lines look and sound like her own messages. The game's message box (`UFGGameUI::AddPendingMessages`, the
`UFGAudioMessage` widget) only plays Wwise events, and our clips are files, so the box gets a runtime `UFGMessage`
without audio and the clip plays alongside it:

- The message is legacy-style (`mIsLegacyMessage`, `mDialogueData` with one dialogue and no `AudioEvent`), from ADA's
  sender (`/Game/FactoryGame/Interface/UI/Message/Sender/Sender_ADA.Sender_ADA_C`), so the box shows her name and
  plays her jingle (`UFGMessageSender::mMessageJingle`). The subtitle's `TimeOnScreen` and the message's
  `mSubtitleTimeMultiplier` (seconds per character) are set from the clip's length, to keep the text up while she
  speaks. Never shared with other players, can be queued, never interrupts.
- A hook on `UFGAudioMessage::StartPlayback` (`NotifyAudioMessageStarted`) starts the clip when the box starts the
  message, after its incoming animation and jingle.
- If the box hasn't started it 8 s after queuing, or there's no game UI, the clip plays anyway: ADA's jingle posted
  on the player (`UAkAudioEvent::PostOnActor`), the voice 1.2 s later, and the HUD overlay shows "ADA: <subtitle>" on
  the lyric line (wrapped at 1000 px) instead of the box.
- The log says `ADA: queued`, `message box started ... after queuing`, `playing`, and `message box closed after X s
  (voice clip Y s)`, to check the box's timing against the clip.

- The box keeps the text up 1 s longer than the clip (without it, it closed 0.9 s before the voice ended). If the box
  closes with more than 2 s of the clip left, the player skipped the message, and the voice stops too, as ADA's own
  messages do.

The clip streams through a `UBBPStreamingSoundWave` on a 2D `UAudioComponent`, at the game's Master x Dialogue
sliders (`RTPC.VO_Bus_Volume`, like ADA's own messages) times `VoiceGain`. The Music and Boom Box sliders don't affect
it. `VoiceGain` defaults to 5, matched by ear against the game's own ADA lines in game (4 was a little too quiet).

How the game sets its voice volume, from its `Init.bnk` (Wwise bank version 172, extracted with UnrealPak from
`FactoryGame-Windows.pak`; RTPC ids are FNV-1 hashes of the lowercase names): `Menu_Volume_Master` and
`VO_Bus_Volume` (the Dialogue slider) both map to bus volume through linear curves with dB scaling, stored as
(0, -1) to (1, 0), i.e. a gain equal to the slider value, the same straight multiply the mod uses. So the two paths
differ only by a constant (how loud ADA's recordings are mastered, the voice bus level, and Unreal's output level
against Wwise's), which `VoiceGain` absorbs, and a match at one slider setting holds at all of them. ADA's own media is
Wwise Opus (codec 0x140001, streamed `.wem`), which neither our decoders nor the bundled ffmpeg read, so its loudness
wasn't measured directly. While a line plays, Custom
Music fades to 20% (`GetMusicDuck`, applied in `SyncEmitters`).

## Testing: the /ada chat command

`ABBPAdaChatCommand` (registered in `UBBPGameWorldModule::mChatCommands`): `/ada <line>` plays a line now, ignoring
the once-a-session rule and the 10-minute gap; `/ada list` names them; `/ada volume <0-10>` sets `VoiceGain` for the
session (default 5), to compare against the game's own voice. SML runs chat commands where they're handled, so they
work for the host.

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
