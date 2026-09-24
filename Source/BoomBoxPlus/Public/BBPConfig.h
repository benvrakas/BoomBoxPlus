#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "BBPConfig.generated.h"

// BoomBoxPlus settings, editable in-game and in Satisfactory Mod Manager.
UCLASS()
class BOOMBOXPLUS_API UBBPConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	UBBPConfig();

	// Setting keys.
	static const FString MusicVolumeKey;
	static const FString ShowLyricsKey;
	static const FString ShowNowPlayingKey;
	static const FString HostOnlyControlKey;

	// Returns a boolean setting, or Fallback if the configuration isn't available.
	static bool GetBool(const UObject* WorldContext, const FString& Key, bool Fallback);

	// Returns a number setting, or Fallback if the configuration isn't available.
	static float GetFloat(const UObject* WorldContext, const FString& Key, float Fallback);
};
