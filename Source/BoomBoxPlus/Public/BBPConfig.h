#pragma once

#include "CoreMinimal.h"
#include "Configuration/ModConfiguration.h"
#include "BBPConfig.generated.h"

// Which Custom Music Boom Boxes this player hears (the ListenMode setting).
UENUM()
enum class EBBPListenMode : uint8
{
	// Every Custom Music Boom Box in range.
	All,
	// Of each group of linked Boom Boxes, only the one nearest to the player; unlinked Boom Boxes play as usual.
	NearestPerGroup,
	// Only the Boom Box the player is carrying; none when they aren't carrying one.
	OnlyCarried,
};

// BoomBoxPlus settings, editable in-game and in Satisfactory Mod Manager.
UCLASS()
class BOOMBOXPLUS_API UBBPConfig : public UModConfiguration
{
	GENERATED_BODY()

public:
	UBBPConfig();

	// Setting keys.
	static const FString ShowLyricsKey;
	static const FString ShowNowPlayingKey;
	static const FString HostOnlyControlKey;
	static const FString MaxCachedSongsKey;
	static const FString GameMusicLevelKey;
	static const FString GameMusicFadeTimeKey;
	static const FString MuteInBackgroundKey;
	static const FString DopplerKey;
	static const FString ListenModeKey;
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

	// Writes a whole-number setting and saves it to disk. No-op if the configuration isn't available.
	static void SetInt(const UObject* WorldContext, const FString& Key, int32 Value);

	// This player's listening mode (the ListenMode setting), All if unavailable.
	static EBBPListenMode GetListenMode(const UObject* WorldContext);

	// Short name of Mode, as shown in the Mods menu and on the music page.
	static FText GetListenModeName(EBBPListenMode Mode);
};
