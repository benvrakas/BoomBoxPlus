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

## Through ADA's own Wwise bus (BBP_ADA.bnk)

The voice normally plays through Wwise, on ADA's own voice bus, so it gets her processing (compressors, a Harmonizer,
a third-party radio/lo-fi effect, Meter-driven ducking, reverb sends) and her volume, exactly like her own lines. Her
recordings are dry (a decoded line falls to -65 dB or less within 150 ms of each phrase): all of that is added live.

- `D:/SFMod/Tools/ADA/make_bank.py <engine>` builds `Resources/Audio/ADA/BBP_ADA.bnk` (bank version 172, the game's)
  from the dry speech in `raw/<engine>`: each line toned to match ADA's own recording and leveled to -20.7 LUFS (the
  loudness of a decoded ADA line). The tone match: a linear-phase EQ from the measured third-octave difference between
  all 78 of her decoded game lines (each leveled first) and our raw TTS (`D:/SFMod/Tools/ADA/compare_tone.py`): hers
  has about 4-5 dB less at 200-250 Hz and far more air above 10 kHz, which WaveNet's 24 kHz output lacks; the mids
  already match. Plus an exciter that makes 9-13 kHz harmonics from the 3-9 kHz band, and a 14 kHz low-pass (her
  recordings stop there). Afterwards ours is within about 1 dB of her average from 100 Hz to 12.5 kHz. A first match
  against a single line over-boosted 1.6-2.5 kHz and 6-10 kHz by 1.5-4 dB ("the mids are still too high"). Her
  streamed lines' media are loose `.wem` files in `FactoryGame-Windows.pak` (`WwiseAudio/Media/<xx>/<id>.wem`, named
  in each event asset's cooked data); their banks only hold a ~3 KB prefetch. Each line is then
  encoded to Opus with ffmpeg and repacked as a Wwise Opus `.wem` (fmt 0x3041, a `seek` chunk of u16 packet sizes, a
  `data` chunk of the packets), embedded in the bank. Its objects (one ActorMixer, then a Sound, a Play action and an
  Event per line) copy the layout of the game's `Play_VO_ADA_MILESTONE_2_1_0.bnk` with new ids; the mixer routes to
  bus 1690368967, ADA's voice bus, with her mixer's settings (-1 dB). Event names are `BBP_ADA_<line>`, ids their FNV-1
  hash. No game audio is copied.
- At startup, `LoadBank` reads the file and `AK::SoundEngine::LoadBankMemoryCopy` loads it, once per session (the log
  says whether, with the result code and bank type), and registers the mod's own Wwise game object (default listeners). When the
  message box starts the line, `PostWwiseVoice` posts the event on it; a skip calls `StopPlayingID`.
- The shipped game runs Wwise 2025.1 while the modding kit compiles against 2023.1. So the mod calls the game's plain
  `AK::SoundEngine` functions, found at runtime by their exported (mangled) names in
  `FactoryGame*-WwiseSoundEngine-Win64-Shipping.dll` (`FPlatformProcess::GetDllExport`), rather than the virtual
  `IWwiseSoundEngineAPI` (whose layout may differ between versions) or imports (the kit's library doesn't export
  them). The first try imported `UAkGameObject::GetAkGameObjectID`, which the game's AkAudio doesn't export, and Windows
  refused to load the mod (GetLastError 127). `D:/SFMod/Tools/BuildScripts/check_imports.py` checks a build's imports
  against the installed game.
- `LoadBankMemoryCopy` is the overload with an `AkBankType&` out parameter (mangled `...PEBXIAEAI1@Z`). The bank's
  header is copied from the game's event bank, so it says it's an Event bank (type 30), and the plain 3-argument
  overload only accepts User banks: up to 1.5.0 it returned 100 (`AK_InvalidBankType`) every time, so every line
  played through Unreal (the fallback below) even though the log printed the cause. The 3-argument one is still used
  if the other isn't exported.
- That overload still failed, with 15 (`AK_IDNotFound`), in every session of 1.5.1 and 1.5.2: a per-event bank (type
  30) is the kind the game's own asset system loads by its event's id, not one to load from memory. Since 1.5.3
  `LoadBank` changes the header's bank type (byte 28 of the file: 'BKHD', chunk size, version, bank id, language id,
  alignment/flags, project id, then the type) to 0, a user bank, before loading, and logs that it did. `make_bank.py`
  now writes 0 there too. It didn't help: the first 1.5.3 session logged the type change, then the same 15
  with type 0. The cause of the 15 is still unknown.
- 1.5.4 adds diagnostics instead of another guess. While `LoadBank` runs, `AK::Monitor::SetLocalOutput` sends Wwise's
  own monitor messages to the log ("ADA: Wwise says (code, level): ..."; switched off again afterwards; the game sets
  no local output of its own, its log never shows Wwise monitor text, and the Release build of the sound engine may
  not post any). If the load fails, `DiagnoseBank` logs the bank's chunks and test-loads three cut-down copies, each
  under its own bank id (the real id + 1, 2, 3): the header alone, header + media (DIDX, DATA), header + objects
  (HIRC). Each that loads is unloaded again (`UnloadBank(id, nullptr, type)`). Which ones load narrows the 15 down to
  the header, the media or the objects.
- The first 1.5.4 session: header 1, header + media 1, header + objects 15. So the objects. Each play action (HIRC type
  3) ends with the id and type of the bank holding its sound, copied from the game's bank as type 30. That explains
  both failures: as a type-30 bank (1.5.0-1.5.2) ours is taken for an auto-generated per-event bank, whose id must be
  its event's; as a user bank (1.5.3) its actions still point at a type-30 bank that doesn't exist. `LoadBank` now
  also rewrites every action's bank type to 0 (`RetargetActions`), and `make_bank.py` writes 0 there. (That first
  objects test was flawed anyway: the cut-down copies get their own id, but their actions still named the original.)
  The diagnostics now point each copy's actions at the copy, and add the objects type by type (actor-mixer, sounds,
  actions, events), so a remaining problem shows which type it's in.
- Result (first session on the action fix): the 11 actions were rewritten to type 0 and the whole bank still failed
  with 15. Test loads: header 1, header + media 1, + actor-mixer 15. So it is the actor-mixer (the object that
  routes to ADA's voice bus 1690368967): the first object type that fails, and every later variant includes it.
  Suspects: the bus id it names isn't known to Wwise at that point, or the mixer's child list (sounds that aren't in
  the cut-down copy). Next test: mixer with an empty child list; mixer with the bus id zeroed.
- Without the bank (or if Wwise won't post the event), the voice plays through Unreal as below, at `VoiceGain`.

How the game's routing was found: banks are inside `FactoryGame-Windows.pak` (UnrealPak `-List`/`-Extract`, not
encrypted); `Init.bnk` holds the buses, effects and RTPC curves; ADA's event bank holds her Sound and ActorMixer, whose
ids lead to her bus. Decoding a line: `.wem` rewrapped as Ogg Opus (the reverse of the builder) and decoded with ffmpeg.

## Keeping the lines out of the save (1.5.1)

When the message box plays a message, the game records it in two saved arrays: the player's
`AFGPlayerState::mPlayedMessages` (the message codex) and, on the server, `AFGStorySubsystem::mAllPlayedMessages`.
Our messages are runtime `UFGMessage` objects (outer: `UBBPAdaEasterEggs`), so the save stored their object paths and
every later load listed them under "Unknown Objects" (a save with 9 lines played gave 9 warnings). Every 5 s,
`RemoveFromPlayedMessages` removes any entry whose outer chain includes a `UBBPAdaEasterEggs` from both arrays,
found by reflection (`FindFProperty<FArrayProperty>`, so no access transformer), the story subsystem's only on the
server. Saves made before 1.5.1 still hold the old entries; whether the game drops them when it saves again hasn't
been checked.

## Testing: the /ada chat command

`ABBPAdaChatCommand` (registered in `UBBPGameWorldModule::mChatCommands`): `/ada <line>` plays a line now, ignoring
the once-a-session rule and the 10-minute gap; `/ada list` names them; `/ada volume <0-10>` sets `VoiceGain` for the
session (default 5), to compare against the game's own voice.

SML runs every chat command on the server. Before 1.5.1, a client's `/ada` therefore played the line on the host's
game (the host's log showed `fih: /ada copyright` and the host heard it). Now the command checks the sender: the host's
own command runs locally (`ABBPAdaChatCommand::RunLocally`); a remote player's is sent to their game with
`UBBPRemoteCallObject::Client_AdaCommand`, which runs it there and puts the reply in their chat through
`AFGChatManager::AddChatMessageToReceived` (as SML's own replies do). On a dedicated server every command is
forwarded.

## Triggers

**Who hears a line (1.5.1): the player holding the Boom Box.** Every game checks its own player:
`GetHeldBoomBox` finds the Custom Music Boom Box they carry (`IsInEquipmentMode`, `mOwningCharacter` is their pawn),
and every trigger but the page one is about that Boom Box's channel. Linked Boom Boxes share a channel, so when lofi
starts on a linked pair carried by two players, both hear the line; a player who only hears the music, or whose
Boom Box is placed, hears nothing. No networking is involved: the channel, its queue and the Boom Boxes' carriers
are replicated.

Before 1.5.1 the triggers followed the nearest channel the player could hear. A friend's carried Boom Box playing
a rickroll set off the line on the host's game (the host heard the song), not as the line intends ("an attempt to
deceive a coworker... FICSIT applauds your initiative"), and the volume line played for everyone near a loud Boom
Box. Refusals weren't logged either, so it couldn't be told from a log why a line didn't play. Now `TryPlay` logs
"'<line>' triggered but not played (<reason>)" once per line and reason.

The loop count also used to restart whenever nothing was heard for a moment between plays (the next song loading, a
station reconnecting); a gap no longer resets it.

The triggers:

- Standing still (under 1 m and 5° of camera) for 5 minutes while holding a playing Boom Box; a follow-up line
  after 15. Time with a menu open (mouse cursor shown) doesn't count.
- The same track starting 4 and 10 times in a row (a new entry with the same track id, or the position jumping back
  more than 5 s on the same entry, which is how repeat-one restarts).
- Keywords in the title, artist or uploader when a track starts: the Rick Astley video (id or title), Coffee Stain or
  "Satisfactory", metal/doom/thrash/deathcore, lofi/lo-fi/chillhop/"beats to relax". Checked in that order; the first
  that plays wins.
- A track from a YouTube "VEVO" or "- Topic" channel: 10% chance.
- The held Boom Box at full volume (the Boom Box's own volume) with another player within 30 m of it. The subtitle
  names the holder.
- Opening a Custom Music page: 1 in 500, for the player who opened it (held Boom Box or not).
