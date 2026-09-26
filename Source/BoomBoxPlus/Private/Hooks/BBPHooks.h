#pragma once

// Installs BoomBoxPlus's native hooks in the game; does nothing in editor builds.
void RegisterBBPHooks();

// Subscribes every native hook. Compiled in all builds so hook code is always type-checked.
void InstallBBPHooks();

// Friend of AFGBoomBoxPlayer (Config/AccessTransformers.ini), so it can hook the Boom Box's private functions.
class FBBPBoomBoxAccess
{
public:
	static void InstallPrivateHooks();
};
