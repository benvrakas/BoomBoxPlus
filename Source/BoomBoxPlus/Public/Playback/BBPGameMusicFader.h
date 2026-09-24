#pragma once

#include "CoreMinimal.h"

// Lowers the game's own (Wwise) music while Custom Music is audible, and puts it back afterwards.
class FBBPGameMusicFader
{
public:
	// Fades game music towards Level (0-1 of the player's setting) while bLower is true, and back up once it's false.
	void Update(bool bLower, float Level, float FadeSeconds, float DeltaSeconds);

	// Puts game music back to the player's setting immediately.
	void Restore();

private:
	// Fades the music volume parameter to Value over FadeSeconds.
	void SetMusicVolume(float Value, float FadeSeconds) const;

	// Returns the current value of the music volume parameter.
	float GetMusicVolume() const;

	// Makes the game re-apply the player's music volume setting.
	void ReapplyPlayerSetting() const;

	// True while game music is lowered.
	bool bLowered = false;

	// Music volume parameter value before lowering (the player's setting).
	float BaseValue = 0.f;

	// Value this fader last set.
	float AppliedValue = 0.f;

	// Seconds until the parameter is next checked for outside changes.
	float CheckTimer = 0.f;

	// Seconds until a finished fade-up hands control back to the game's setting.
	float RestoreTimer = -1.f;
};
