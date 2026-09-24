#pragma once

// Installs BoomBoxPlus's native hooks in the game; does nothing in editor builds.
void RegisterBBPHooks();

// Subscribes every native hook. Compiled in all builds so hook code is always type-checked.
void InstallBBPHooks();
