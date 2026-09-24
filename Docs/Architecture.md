# Architecture

Detailed notes per area: [Decoders.md](Decoders.md), [Streaming.md](Streaming.md), [Library.md](Library.md).
Authoritative design plan (outside this repo): `C:\Users\benvr\.claude\plans\i-want-a-satisfactory-magical-key.md`.

## Plugin type

A plain SML mod plugin (Alpakit's "C++ & Blueprint **Library**" template), **not** a GameFeature plugin.
The GameFeature template additionally needs a `UGameFeatureData` asset created in the editor; a plain plugin
is fully hand-authorable. It can be converted later if GameFeature actions become useful.

`.uplugin` fields mirror what Alpakit's mod wizard writes (`ModWizardDefinition.cpp`): `SemVersion`,
`GameVersion` (`>=` the CL in `Source/FactoryGame/currentVersion.txt`, currently 502094), and an SML
dependency `^3.12.0`.

UE natively treats plugins under `<Project>/Mods/` as mod plugins, so no `.uproject` entry is needed.

## How SML activates the mod — no Blueprint assets required

SML finds a mod's root modules with `FPluginModuleLoader::FindRootModulesOfType`, which checks **native
C++ classes** as well as Blueprint assets, provided the CDO sets `bRootModule = true`. The owning plugin is
resolved from the class path (`/Script/BoomBoxPlus.*`).

- `UBBPGameInstanceModule` (root `UGameInstanceModule`) — lists our remote call objects in
  `RemoteCallObjects`.
- `UBBPGameWorldModule` (root `UGameWorldModule`) — lists our `AModSubsystem`s in `ModSubsystems`.

So the entire C++ core activates without any editor-authored asset.

`UBBPLibrarySubsystem` is an ordinary `UGameInstanceSubsystem`; UE creates those automatically and SML is
not involved.

## Native hooks

Registered from `FBoomBoxPlusModule::StartupModule` via `RegisterBBPHooks()` (`Private/Hooks/`). SML says
not to hook inside the editor, so only the *call* to `InstallBBPHooks()` is under `#if !WITH_EDITOR`; the
hook code itself is compiled in every build so editor builds still type-check it. (Guarding the hook bodies
with `if constexpr (WITH_EDITOR) return;` fails: UE treats the resulting C4702 "unreachable code" as an
error.)

Transport hooks (`BeginPlaySequence`, `BeginStopSequence`, `BeginNext/PreviousSongSequence`,
`TogglePlaybackNow`) cancel the vanilla call when Custom Music is loaded and route the action to the
playlist — directly if they fire on the server, through the RCO on a client — so they work whichever side
the game calls them on. `PlayNow/StopNow/NextNow/PrevNow` are cancelled but not routed, to stop Wwise
playing the empty playlist without acting twice. Every intercepted call is logged at `Log`: the real call
flow is unknown until observed in-game (stub source), and these lines are how we'll find out.

## Access transformers

`Config/AccessTransformers.ini` uses the **bare class name** for `Friend`/`Accessor`
(`Class="AFGBoomBoxPlayer"`); the `/Script/...` path form only applies to the property-flag transformers
like `EditAnywhere`. A wrong name fails the build at UHT with "Unused accessor access transformer".
Accessors generate `Get<Prop>()`/`Set<Prop>(v)` by value. Adding one regenerates the target's
`.generated.h`, so expect a partial FactoryGame recompile.

Handler shapes (from `Mods/SML/.../Patching/NativeHookManager.h`):

- after-hook on a member function: `void(Self*, Args...)`, or `void(const Result&, Self*, Args...)` for a
  non-void return; `Self` is `const` for const methods.
- before-hook: `void(TCallScope<...>& Scope, Self*, Args...)`; `Scope.Cancel()` skips the original.

Remember the starter project ships **stubs** — `.cpp` bodies are empty, real code is resolved from the
game's PDBs at runtime. Signatures are trustworthy; behaviour must be observed in-game.

## The Custom Music tape

`UBBPCustomMusicTape` is a native subclass of `UFGTapeData` with an empty playlist. It exists only so the
Boom Box's tape list shows a "Custom Music" entry and `GetCurrentTape()` has something recognisable for our
playback hooks to branch on (`IsCustomMusicTape`). `FSongData::Song` is a Wwise event, so real tracks can't
live in a tape playlist.

### Why it's added by hooking `GetUnlockedTapes`, not by unlocking it

`BPW_BoomBox_TapeSelect` builds its list from `AFGUnlockSubsystem`, whose `mUnlockedTapes` is
`UPROPERTY(SaveGame, Replicated)`. Calling `UnlockTape()` would write our class into the player's save, and
uninstalling the mod would leave a dangling class reference there. Appending to the result of
`GetUnlockedTapes` in an after-hook makes the tape appear everywhere without ever persisting it.

Open question for in-game testing: if the tape-select widget caches the list rather than re-querying, it may
need a nudge (broadcasting the unlock subsystem's new-tape delegate once) to show the entry.

The icon currently reuses the game's `TXUI_Tape_EmptyTape` texture; a proper icon is future editor work.

## Configuration

`UBBPConfig` is a native `UModConfiguration` (registered in `UBBPGameInstanceModule::ModConfigurations`),
so it appears in SMM and the in-game mod config without an editor asset. Its root section and properties
are `Instanced` default subobjects built in the constructor. Read values with `UBBPConfig::GetBool/GetFloat`,
which fall back to a default (and log at Verbose) if SML's `UConfigManager` hasn't registered it yet.

| Key | Default | Used by |
|---|---|---|
| `MusicVolume` | 0.8 | Playback controller — multiplied with each Boom Box's own volume. Needed because Unreal audio ignores the game's Wwise volume sliders. |
| `ShowLyrics` | on | HUD overlay |
| `ShowNowPlaying` | on | HUD overlay |
| `HostOnlyControl` | off | RCO, server side; listen servers only (on a dedicated server there's no host player) |

## `GetCurrentSong` override

With the Custom Music tape loaded, the vanilla Boom Box calls `GetCurrentSong()` (sometimes every frame) and
logs `Invalid current song index was found: 0` because the tape's playlist is empty. A before-hook overrides
the result for Custom Music with a valid `FSongData`: the current track's title/artist/duration, or a
"Custom Music / BoomBoxPlus" placeholder. `Song` stays null — nothing should play it, since transport calls
are intercepted. (SML emits a harmless C4191 warning for hooks on functions returning structs by value.)

## Unreal audio device

Satisfactory does all its sound through Wwise and ships with `[Audio] AudioMixerModuleName` empty, so the
engine never creates an Unreal audio device (`Audio Device Manager Initialization Failed!` in the log) and
any `UAudioComponent` is silent. The mixer modules themselves are shipped (`AudioMixerXAudio2`, `...Wasapi`).

`BBPAudioDevice::ConfigureBeforeEngineInit()` runs from the module's `StartupModule`, which in the shipped
game happens about half a second **before** the engine initialises audio, and sets the config value to
`AudioMixerXAudio2`; the engine then starts its audio device normally. If the device is still missing when
the playback controller starts, `BBPAudioDevice::EnsureAvailable()` sets the value and calls the engine's
protected `UEngine::InitializeAudioDeviceManager()` itself (through a derived-class member pointer, which
avoids an access transformer on `UEngine` and the engine-wide rebuild it would cause). Skipped in the
editor and on dedicated servers.

## Fading the game's music

`FBBPGameMusicFader` lowers the Wwise global parameter `Music_Bus_Volume` — the one behind the game's
Music slider (option id `RTPC.Music_Bus_Volume`) — while the local player can hear a Boom Box actually
producing Custom Music, using Wwise's own interpolation for the fade. It reads the current value first
and scales it by the "Game music level" setting (default 0), so no assumption about the parameter's range
is needed. If the value changes while lowered (the player moved the slider), that becomes the new base.
After fading back up, it calls `UFGGameUserSettings::UpdateAudioOption` so the game re-applies the
player's exact setting. Settings: Fade game music (on), Game music level (0), Game music fade time (2 s).
