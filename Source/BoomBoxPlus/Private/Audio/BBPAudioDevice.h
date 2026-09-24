#pragma once

#include "CoreMinimal.h"

namespace BBPAudioDevice
{
	// Points the engine at the Windows audio mixer before engine audio starts. Call from module startup.
	void ConfigureBeforeEngineInit();

	// Returns true if Unreal's own audio device exists, creating it now if the engine started without one.
	bool EnsureAvailable();
}
