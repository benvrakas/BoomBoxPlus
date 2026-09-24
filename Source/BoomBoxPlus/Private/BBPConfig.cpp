#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

const FString UBBPConfig::MusicVolumeKey = TEXT("MusicVolume");
const FString UBBPConfig::ShowLyricsKey = TEXT("ShowLyrics");
const FString UBBPConfig::ShowNowPlayingKey = TEXT("ShowNowPlaying");
const FString UBBPConfig::HostOnlyControlKey = TEXT("HostOnlyControl");
const FString UBBPConfig::MaxCachedSongsKey = TEXT("MaxCachedSongs");
const FString UBBPConfig::FadeGameMusicKey = TEXT("FadeGameMusic");
const FString UBBPConfig::GameMusicLevelKey = TEXT("GameMusicLevel");
const FString UBBPConfig::GameMusicFadeTimeKey = TEXT("GameMusicFadeTime");

namespace
{
	FConfigId MakeConfigId()
	{
		FConfigId Id;
		Id.ModReference = TEXT("BoomBoxPlus");
		return Id;
	}

	// Returns the live value of a setting, or null if the configuration isn't registered yet.
	UConfigProperty* FindProperty(const UObject* WorldContext, const FString& Key)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
		const UConfigPropertySection* Root = Manager ? Manager->GetConfigurationRootSection(MakeConfigId()) : nullptr;
		if (!Root)
		{
			return nullptr;
		}
		const TObjectPtr<UConfigProperty>* Property = Root->SectionProperties.Find(Key);
		return Property ? Property->Get() : nullptr;
	}
}

UBBPConfig::UBBPConfig()
{
	ConfigId = MakeConfigId();
	DisplayName = LOCTEXT("ConfigName", "BoomBoxPlus");
	Description = LOCTEXT("ConfigDescription", "Custom music for the Boom Box.");

	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	UConfigPropertyFloat* MusicVolume = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("MusicVolume"));
	MusicVolume->DisplayName = LOCTEXT("MusicVolume", "Music volume");
	MusicVolume->Tooltip = LOCTEXT("MusicVolumeTip", "Volume of Custom Music, 0 to 1. The game's own volume sliders don't affect it.");
	MusicVolume->DefaultValue = 0.8f;
	MusicVolume->Value = 0.8f;
	RootSection->SectionProperties.Add(MusicVolumeKey, MusicVolume);

	UConfigPropertyBool* ShowLyrics = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ShowLyrics"));
	ShowLyrics->DisplayName = LOCTEXT("ShowLyrics", "Show lyrics");
	ShowLyrics->Tooltip = LOCTEXT("ShowLyricsTip", "Show the current lyric line at the bottom of the screen when you can hear a Boom Box.");
	ShowLyrics->DefaultValue = true;
	ShowLyrics->Value = true;
	RootSection->SectionProperties.Add(ShowLyricsKey, ShowLyrics);

	UConfigPropertyBool* ShowNowPlaying = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ShowNowPlaying"));
	ShowNowPlaying->DisplayName = LOCTEXT("ShowNowPlaying", "Show 'now playing'");
	ShowNowPlaying->Tooltip = LOCTEXT("ShowNowPlayingTip", "Briefly show the title and artist when the track changes.");
	ShowNowPlaying->DefaultValue = true;
	ShowNowPlaying->Value = true;
	RootSection->SectionProperties.Add(ShowNowPlayingKey, ShowNowPlaying);

	UConfigPropertyBool* HostOnlyControl = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("HostOnlyControl"));
	HostOnlyControl->DisplayName = LOCTEXT("HostOnlyControl", "Only the host controls music");
	HostOnlyControl->Tooltip = LOCTEXT("HostOnlyControlTip", "When hosting, stop other players from changing the queue or playback. Has no effect on dedicated servers.");
	HostOnlyControl->DefaultValue = false;
	HostOnlyControl->Value = false;
	RootSection->SectionProperties.Add(HostOnlyControlKey, HostOnlyControl);

	UConfigPropertyInteger* MaxCachedSongs = CreateDefaultSubobject<UConfigPropertyInteger>(TEXT("MaxCachedSongs"));
	MaxCachedSongs->DisplayName = LOCTEXT("MaxCachedSongs", "Downloaded songs to keep");
	MaxCachedSongs->Tooltip = LOCTEXT("MaxCachedSongsTip", "How many YouTube/SoundCloud downloads to keep on disk. The oldest unused ones are deleted first; songs in the queue are never deleted.");
	MaxCachedSongs->DefaultValue = 50;
	MaxCachedSongs->Value = 50;
	RootSection->SectionProperties.Add(MaxCachedSongsKey, MaxCachedSongs);

	UConfigPropertyBool* FadeGameMusic = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("FadeGameMusic"));
	FadeGameMusic->DisplayName = LOCTEXT("FadeGameMusic", "Fade game music");
	FadeGameMusic->Tooltip = LOCTEXT("FadeGameMusicTip", "Turn the game's own music down while you can hear Custom Music, and back up when it stops.");
	FadeGameMusic->DefaultValue = true;
	FadeGameMusic->Value = true;
	RootSection->SectionProperties.Add(FadeGameMusicKey, FadeGameMusic);

	UConfigPropertyFloat* GameMusicLevel = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("GameMusicLevel"));
	GameMusicLevel->DisplayName = LOCTEXT("GameMusicLevel", "Game music level while playing");
	GameMusicLevel->Tooltip = LOCTEXT("GameMusicLevelTip", "How loud the game's music stays while Custom Music plays, 0 (silent) to 1 (unchanged), relative to your Music volume setting.");
	GameMusicLevel->DefaultValue = 0.f;
	GameMusicLevel->Value = 0.f;
	RootSection->SectionProperties.Add(GameMusicLevelKey, GameMusicLevel);

	UConfigPropertyFloat* GameMusicFadeTime = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("GameMusicFadeTime"));
	GameMusicFadeTime->DisplayName = LOCTEXT("GameMusicFadeTime", "Game music fade time");
	GameMusicFadeTime->Tooltip = LOCTEXT("GameMusicFadeTimeTip", "Seconds the game's music takes to fade out and back in.");
	GameMusicFadeTime->DefaultValue = 2.f;
	GameMusicFadeTime->Value = 2.f;
	RootSection->SectionProperties.Add(GameMusicFadeTimeKey, GameMusicFadeTime);
}

bool UBBPConfig::GetBool(const UObject* WorldContext, const FString& Key, bool Fallback)
{
	if (const UConfigPropertyBool* Property = Cast<UConfigPropertyBool>(FindProperty(WorldContext, Key)))
	{
		return Property->Value;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Config: '%s' unavailable, using %d"), *Key, Fallback);
	return Fallback;
}

float UBBPConfig::GetFloat(const UObject* WorldContext, const FString& Key, float Fallback)
{
	if (const UConfigPropertyFloat* Property = Cast<UConfigPropertyFloat>(FindProperty(WorldContext, Key)))
	{
		return Property->Value;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Config: '%s' unavailable, using %.2f"), *Key, Fallback);
	return Fallback;
}

int32 UBBPConfig::GetInt(const UObject* WorldContext, const FString& Key, int32 Fallback)
{
	if (const UConfigPropertyInteger* Property = Cast<UConfigPropertyInteger>(FindProperty(WorldContext, Key)))
	{
		return Property->Value;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Config: '%s' unavailable, using %d"), *Key, Fallback);
	return Fallback;
}

#undef LOCTEXT_NAMESPACE
