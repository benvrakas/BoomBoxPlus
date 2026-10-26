# Custom Music through the game's Wwise

Since 1.5.5 (setting "Play through the game's audio engine", on by default), each Boom Box's music plays through the
game's own Wwise sound engine on the Boom Box's own emitter, the `UAkComponent` the vanilla Boom Box plays its tapes on
(`AFGBoomBoxPlayer::mAkComponent`, found by reflection: `UBBPPlaybackController::FindWwiseEmitter`). So the game's
rooms, reverb volumes, occlusion and Boom Box mix (bus, effects, falloff, volume sliders) apply to it like to a tape.
Where that isn't possible, it plays through Unreal as before (`UAudioComponent` + `UBBPStreamingSoundWave`).
Nothing is imitated in Unreal: the fallback is simply the old path.

## The sound

Wwise has a source plugin for audio a program supplies, Audio Input (plugin 200, `0x00C80002`), and the shipped game
contains it. The mod needs a Wwise event whose sound uses it, routed like a Boom Box song. `BBPWwiseMusic::EnsureReady`
builds that bank once per game from one of the game's own Boom Box song banks (`Content/WwiseAudio/Event/*/
Play_Boombox_*.bnk`, read from the pak at runtime, not shipped; the first in name order that has one event whose Play
action targets a sound):

- The song's Sound keeps its node settings (parent mixer chain, positioning, attenuation), but its 22-byte source
  header becomes Audio Input: plugin `0x00C80002`, stream type 0, source id `BBP_Music_Input`, no media, followed by
  a 4-byte parameter size (0) that only plugin sources have. (The first builds left that out; Wwise then read the
  node settings 4 bytes early, took the parent mixer's id for the output bus, and refused the bank with result 92,
  `AK_InitBankNotLoaded`: the code it uses for an output bus that isn't loaded.) Before it
  goes a plugin-settings object (HIRC type 17) with that id: plugin id, parameter size 0 (the plugin's defaults),
  then 7 zero bytes (no media, RTPCs or states). This is the layout of the game's own plugin sources (Silence in
  `Play_GolfCartUnfold`, Tone Generator in `Play_Parachute_Open`).
- Every object defined in the bank gets a new id (`BBP_Music_<old id>`), and every 4-byte occurrence of an old id
  in any object is replaced, so the copies never clash with the real tapes' objects (the Boom Box mixer chain is
  shared by every song bank). Ids the bank doesn't define (buses, RTPCs, state groups) are left alone, so the
  Boom Box bus and its game parameters still apply.
- The bank gets id `BBP_Music_Bank`, user type 0 in its header, and every action names it as a user bank. Then
  `BBPWwise::PrepareBank` (shared with ADA's bank) and `BBPWwise::LoadBank`.

The plugin has to be registered first. Its code is compiled into the game's Wwise DLL (`AkFXSrcAudioInput.cpp`, with
the integration's `FAudioInputPlugin`), but Wwise 2025 only registers the plugins the Init bank's `INIT` chunk lists,
and the game's project doesn't use Audio Input. Loading the music bank without it failed with result 92
(`AK_InitBankNotLoaded`) in the first 1.5.5 test. `BBPWwise::RegisterBuiltInPlugin` walks the DLL's exported
`g_pAKPluginList` (the static `AK::PluginRegistration` objects) to the Audio Input entry and passes its factory
functions to the exported `AK::SoundEngine::RegisterPlugin`. The built-in copy matters: the game also ships
`AkAudioInput.dll`, but `RegisterPluginDLL` would load a second copy whose callbacks the mod can't reach, since the
DLL exports only its plugin list.

The vanilla song bank needs no reordering (its sounds come before their mixers); see EasterEggs.md for the three
things that made hand-built banks load at all.

## Feeding it

`BBPWwise::SetAudioInputCallbacks` hands two callbacks to the game's Audio Input plugin through the sound engine API
object `IWwiseSoundEngineModule::SoundEngine` (an exported variable), calling `AudioInputPlugin->
SetAudioInputCallbacks` from the kit's headers (the kit's Wwise matches the game's, 2025.1.6). There is one set for the
whole game; the integration's own `FAkAudioInputManager` would replace it if the game ever used Audio Input itself.

Telescreen feeds Audio Input too, so the two mods share the callbacks through `Private/Audio/SFModWwiseAudioInput.h`
(an identical copy in each mod, agreed with the Telescreen session on 2026-10-22). Each mod registers an
`ISFModWwiseAudioInputSource` with `IModularFeatures` under "SFModWwiseAudioInputSource" and installs the same
dispatcher: under the modular-feature lock it asks each source in turn, the first that knows the playing id fills the
buffer (or format), and if none does it hands over silence (48 kHz stereo). So it doesn't matter which mod installs
the callbacks last. The plugin registration may then already be done by the other mod, so its result is only logged;
the bank load tells whether Audio Input is there.

- `BBPWwiseMusic::Play` posts the event on the emitter's game object (its id is the component's address, as in
  `UAkGameObject::GetAkGameObjectID`) with an end-of-event callback, and registers a feed (wave, rate, channels,
  gain, paused) under the playing id. The registry lock is held while posting, so the feed exists before Wwise asks
  for its format.
- Format: the stream's rate and mono/stereo; Wwise's default float, non-interleaved samples.
- Each buffer: `UBBPStreamingSoundWave::GeneratePCM`, the same function Unreal's mixer calls (decoding, ring buffer,
  pitch-only Doppler), converted to float and scaled by the feed's gain. Paused hands over silence without advancing.
  Song position, drift correction and sync therefore work unchanged.
- Gain = Boom Box volume x My Volume x ADA ducking (0 while muted in the background or by the listening mode). The
  game's Master and Boom Box sliders apply on the Wwise bus, so `GameVolumeScale` isn't used here.
- Every update, `UAkComponent::UpdateGameObjectPosition` (exported) moves the emitter's Wwise position, spatial-audio
  room and reverb volumes to the Boom Box: the component doesn't know about events posted around it.
- If Wwise ends the event itself (the game stopping every sound on the emitter), the track restarts at the channel's
  position; ending twice within a second of starting sends that Boom Box to the Unreal path.

## In the background

The game mutes Wwise when its window isn't in front (its "Mute When Minimized" option, `FG.MuteWhenMinimized`, on by
default): the function that reads that option sets the global game parameter `Menu_Volume_Master` to 0 with
`UAkGameplayStatics::SetGlobalRTPCValue`, and back to the Master slider's saved value (`RTPC.Menu_Volume_Master`) when
the window is in front again. (Found in the game binary, 2026-10-23; the game's Wwise settings don't use the
integration's own `SuspendAudioDuringFocusLoss`, and Wwise outputs directly, not through Unreal's mixer, so Unreal's
unfocused volume doesn't reach it.) In `Init.bnk` that parameter drives the master bus's voice volume, which Wwise
evaluates per game object. So with the mod's "Mute when the game is in the background" off, while the window is behind
others, `UBBPPlaybackController::UpdateMasterVolumeHold` sets `Menu_Volume_Master` on each Boom Box's own emitter to
the Master slider's value (`AK::SoundEngine::SetRTPCValue` on that game object), and resets it
(`ResetRTPCValue`) when the window is in front again or the music stops. Only those emitters stay audible; the rest of
the game stays muted as its option says. (The vanilla Boom Box's own sounds on the same emitter are heard too.)

## Tested

Heard in game on 2026-10-22 (shipped in 1.6.0): the bank loads, the Boom Box plays through Wwise ("plays through
Wwise (playing id ..., 48000 Hz, 2 ch)", and 44100 Hz for a 44.1 kHz file), and the user confirmed it sounds like
the game's audio. Not yet checked one by one: reverb indoors, muffling behind walls, and pitch of 44.1 kHz files.
Falloff: the vanilla Boom Box's attenuation (140 m) replaced the mod's 250 m while lyrics and the game-music fade still
used 250 m; since 1.7.0 both come from each Boom Box's Range setting (Multiplayer.md, "Range"). Telescreen sharing the callbacks was built but not yet heard with both mods.
