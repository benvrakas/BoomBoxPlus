# Building and packaging

## Prerequisites

- Engine `D:\SFMod\UnrealEngine-CSS` (5.6.1-CSS), starter project `D:\SFMod\SatisfactoryModLoader`, Wwise
  integration `2023.1.14.3555` (see the plan for the pinning story).
- Tools: `powershell -ExecutionPolicy Bypass -File Tools\FetchTools.ps1` (fills `Resources/Tools/`).
- Run build commands from `.bat` files: Git Bash rewrites `/`-prefixed arguments (e.g. `/E` → `E:/`).

## Compile-check (editor target, ~30 s incremental)

```
D:\SFMod\UnrealEngine-CSS\Engine\Build\BatchFiles\Build.bat FactoryEditor Win64 Development ^
  -project="D:\SFMod\SatisfactoryModLoader\FactoryGame.uproject" -waitmutex -MaxParallelActions=4
```

The target is `FactoryEditor`, not `FactoryGameEditor`. Keep `-MaxParallelActions` low: this machine's
commit limit (~42 GB) is exhausted by 7+ parallel compiles ("paging file is too small", C3859/C1076).
Only one UBT can run at a time (global mutex).

## Package (what Alpakit does)

```
D:\SFMod\UnrealEngine-CSS\Engine\Build\BatchFiles\RunUAT.bat ^
  -ScriptsForProject="D:\SFMod\SatisfactoryModLoader\FactoryGame.uproject" PackagePlugin ^
  -project="D:\SFMod\SatisfactoryModLoader\FactoryGame.uproject" -clientconfig=Shipping -serverconfig=Shipping ^
  -utf8output -DLCName=BoomBoxPlus -build -platform=Win64 -nocompileeditor -Target=FactoryGameSteam
```

- `-Target=FactoryGameSteam` matters: without it (and without `-CopyToGameDirectory_Windows=...`),
  `PackagePlugin` builds both the EGS and Steam targets.
- Alpakit adds `-CopyToGameDirectory_Windows="<game dir>"` to install straight into the game.
- First run also builds `UnrealPak` and needs the `FactoryGameSteam` Shipping target built once
  (~1–2 h from source); after that packaging takes a few minutes.
- Output: `D:\SFMod\SatisfactoryModLoader\Saved\ArchivedPlugins\BoomBoxPlus\BoomBoxPlus-Windows.zip`
  (~106 MB: DLL, PDB, `Resources/` with tools and demo track, a tiny pak).

## Install for testing

The zip's contents go in `<Satisfactory>\FactoryGame\Mods\BoomBoxPlus\`. SML 3.12.0 must be installed in the
game (via Satisfactory Mod Manager) or the mod won't load.
