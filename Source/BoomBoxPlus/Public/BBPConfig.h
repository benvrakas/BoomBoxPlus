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
	static const FString MaxCachedSongsKey;
	static const FString FadeGameMusicKey;
	static const FString GameMusicLevelKey;
	static const FString GameMusicFadeTimeKey;
	static const FString SpotifyClientIdKey;
	static const FString SpotifyClientSecretKey;

	// Rebuilds the default configuration with SML's Blueprint property classes, which carry the editor widgets
	// SML's Mods menu shows. Call before SML registers the configuration.
	static void UseSMLEditorClasses();

	// Returns a boolean setting, or Fallback if the configuration isn't available.
	static bool GetBool(const UObject* WorldContext, const FString& Key, bool Fallback);

	// Returns a number setting, or Fallback if the configuration isn't available.
	static float GetFloat(const UObject* WorldContext, const FString& Key, float Fallback);

	// Returns a text setting, or Fallback if the configuration isn't available.
	static FString GetString(const UObject* WorldContext, const FString& Key, const FString& Fallback);

	// Returns a whole-number setting, or Fallback if the configuration isn't available.
	static int32 GetInt(const UObject* WorldContext, const FString& Key, int32 Fallback);
};
