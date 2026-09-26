#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "Configuration/ConfigManager.h"
#include "Configuration/Properties/ConfigPropertyBool.h"
#include "Configuration/Properties/ConfigPropertyFloat.h"
#include "Configuration/Properties/ConfigPropertyInteger.h"
#include "Configuration/Properties/ConfigPropertySection.h"
#include "Configuration/Properties/ConfigPropertyString.h"
#include "Configuration/Properties/WidgetExtension/CP_Section.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

const FString UBBPConfig::MusicVolumeKey = TEXT("MusicVolume");
const FString UBBPConfig::ShowLyricsKey = TEXT("ShowLyrics");
const FString UBBPConfig::ShowNowPlayingKey = TEXT("ShowNowPlaying");
const FString UBBPConfig::HostOnlyControlKey = TEXT("HostOnlyControl");
const FString UBBPConfig::MaxCachedSongsKey = TEXT("MaxCachedSongs");
const FString UBBPConfig::GameMusicLevelKey = TEXT("GameMusicLevel");
const FString UBBPConfig::GameMusicFadeTimeKey = TEXT("GameMusicFadeTime");
const FString UBBPConfig::SpotifyClientIdKey = TEXT("SpotifyClientId");
const FString UBBPConfig::SpotifyClientSecretKey = TEXT("SpotifyClientSecret");

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
		const TObjectPtr<UConfigProperty>* Found = Root ? Root->SectionProperties.Find(Key) : nullptr;
		return Found ? Found->Get() : nullptr;
	}
}

UBBPConfig::UBBPConfig()
{
	ConfigId = MakeConfigId();
	DisplayName = LOCTEXT("ConfigName", "BoomBoxPlus");
	Description = LOCTEXT("ConfigDescription", "Custom music for the Boom Box.");

	RootSection = CreateDefaultSubobject<UConfigPropertySection>(TEXT("RootSection"));

	// Settings are listed in the order they're added; UseSMLEditorClasses makes the root section a vertical list.
	auto AddSetting = [this](const FString& Key, UConfigProperty* Property)
	{
		RootSection->SectionProperties.Add(Key, Property);
	};

	// Still a real setting (read/written by UBBPConfig::GetFloat/SetFloat, saved to disk) but hidden from the Mods
	// menu: it's the same value as the "My Volume" slider on the Custom Music page, which is the easier place to
	// reach it from and shows its effect immediately.
	UConfigPropertyFloat* MusicVolume = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("MusicVolume"));
	MusicVolume->DisplayName = LOCTEXT("MusicVolume", "Music volume");
	MusicVolume->Tooltip = LOCTEXT("MusicVolumeTip", "Volume of Custom Music, from 0 (silent) to 2 (double, to boost it above the game's own mix). 1 is unboosted. Also on the Custom Music page as \"My Volume\".");
	MusicVolume->DefaultValue = 1.f;
	MusicVolume->Value = 1.f;
	MusicVolume->bHidden = true;
	AddSetting(MusicVolumeKey, MusicVolume);

	UConfigPropertyBool* ShowLyrics = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ShowLyrics"));
	ShowLyrics->DisplayName = LOCTEXT("ShowLyrics", "Show lyrics");
	ShowLyrics->Tooltip = LOCTEXT("ShowLyricsTip", "Show the current lyric line at the bottom of the screen when you can hear a Boom Box.");
	ShowLyrics->DefaultValue = true;
	ShowLyrics->Value = true;
	AddSetting(ShowLyricsKey, ShowLyrics);

	UConfigPropertyBool* ShowNowPlaying = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("ShowNowPlaying"));
	ShowNowPlaying->DisplayName = LOCTEXT("ShowNowPlaying", "Show 'now playing'");
	ShowNowPlaying->Tooltip = LOCTEXT("ShowNowPlayingTip", "Briefly show the title and artist when the track changes.");
	ShowNowPlaying->DefaultValue = true;
	ShowNowPlaying->Value = true;
	AddSetting(ShowNowPlayingKey, ShowNowPlaying);

	UConfigPropertyBool* HostOnlyControl = CreateDefaultSubobject<UConfigPropertyBool>(TEXT("HostOnlyControl"));
	HostOnlyControl->DisplayName = LOCTEXT("HostOnlyControl", "Only the host controls music");
	HostOnlyControl->Tooltip = LOCTEXT("HostOnlyControlTip", "When hosting, stop other players from changing the queue or playback. Has no effect on dedicated servers.");
	HostOnlyControl->DefaultValue = false;
	HostOnlyControl->Value = false;
	AddSetting(HostOnlyControlKey, HostOnlyControl);

	UConfigPropertyInteger* MaxCachedSongs = CreateDefaultSubobject<UConfigPropertyInteger>(TEXT("MaxCachedSongs"));
	MaxCachedSongs->DisplayName = LOCTEXT("MaxCachedSongs", "Downloaded songs to keep");
	MaxCachedSongs->Tooltip = LOCTEXT("MaxCachedSongsTip", "How many YouTube/SoundCloud downloads to keep on disk. The oldest unused ones are deleted first; songs in the queue are never deleted.");
	MaxCachedSongs->DefaultValue = 50;
	MaxCachedSongs->Value = 50;
	AddSetting(MaxCachedSongsKey, MaxCachedSongs);

	UConfigPropertyFloat* GameMusicLevel = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("GameMusicLevel"));
	GameMusicLevel->DisplayName = LOCTEXT("GameMusicLevel", "Game music level while playing");
	GameMusicLevel->Tooltip = LOCTEXT("GameMusicLevelTip", "How loud the game's own music stays while you can hear Custom Music: 0 fades it out completely, 1 leaves it alone. Relative to your Music volume setting.");
	GameMusicLevel->DefaultValue = 0.f;
	GameMusicLevel->Value = 0.f;
	AddSetting(GameMusicLevelKey, GameMusicLevel);

	UConfigPropertyFloat* GameMusicFadeTime = CreateDefaultSubobject<UConfigPropertyFloat>(TEXT("GameMusicFadeTime"));
	GameMusicFadeTime->DisplayName = LOCTEXT("GameMusicFadeTime", "Game music fade time");
	GameMusicFadeTime->Tooltip = LOCTEXT("GameMusicFadeTimeTip", "Seconds the game's music takes to fade out and back in.");
	GameMusicFadeTime->DefaultValue = 2.f;
	GameMusicFadeTime->Value = 2.f;
	AddSetting(GameMusicFadeTimeKey, GameMusicFadeTime);

	UConfigPropertyString* SpotifyClientId = CreateDefaultSubobject<UConfigPropertyString>(TEXT("SpotifyClientId"));
	SpotifyClientId->DisplayName = LOCTEXT("SpotifyClientId", "Spotify Client ID (optional)");
	SpotifyClientId->Tooltip = LOCTEXT("SpotifyClientIdTip", "Without a Spotify app, only the first 100 songs of a Spotify playlist can be read. Create a free app at developer.spotify.com/dashboard and paste its Client ID and Client Secret here to read whole playlists.");
	AddSetting(SpotifyClientIdKey, SpotifyClientId);

	UConfigPropertyString* SpotifyClientSecret = CreateDefaultSubobject<UConfigPropertyString>(TEXT("SpotifyClientSecret"));
	SpotifyClientSecret->DisplayName = LOCTEXT("SpotifyClientSecret", "Spotify Client Secret (optional)");
	SpotifyClientSecret->Tooltip = LOCTEXT("SpotifyClientSecretTip", "The Client Secret of the same Spotify app. Only sent to Spotify, to read playlists.");
	AddSetting(SpotifyClientSecretKey, SpotifyClientSecret);
}

namespace
{
	const TCHAR* SMLPropertyPath = TEXT("/SML/Interface/UI/Menu/Mods/ConfigProperties/");

	// Loads SML's Blueprint subclass of a config property class, e.g. BP_ConfigPropertyBool for UConfigPropertyBool.
	UClass* LoadEditorClass(const UClass* NativeClass)
	{
		const FString Name = TEXT("BP_") + NativeClass->GetName();
		const FString Path = FString::Printf(TEXT("%s%s.%s_C"), SMLPropertyPath, *Name, *Name);
		UClass* Loaded = LoadClass<UConfigProperty>(nullptr, *Path);
		if (!Loaded || !Loaded->IsChildOf(NativeClass))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Config: SML editor class %s not found; that setting won't be editable in the Mods menu"), *Path);
			return nullptr;
		}
		return Loaded;
	}

	// Creates an instance of EditorClass under Outer with every property value of Source.
	UConfigProperty* CloneAs(const UConfigProperty* Source, UClass* EditorClass, UObject* Outer)
	{
		UConfigProperty* Clone = NewObject<UConfigProperty>(Outer, EditorClass, NAME_None, RF_Public);
		for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
		{
			It->CopyCompleteValue_InContainer(Clone, Source);
		}
		return Clone;
	}

	// Converts OldSection and everything under it to SML's Blueprint editor classes; each property was invisible in
	// the Mods menu without this.
	UConfigPropertySection* ConvertSectionRecursive(const UConfigPropertySection* OldSection, UClass* SectionEditorClass, UObject* Outer, int32& OutConverted, int32& OutTotal)
	{
		UConfigPropertySection* NewSection = Cast<UConfigPropertySection>(CloneAs(OldSection, SectionEditorClass, Outer));
		NewSection->SectionProperties.Reset();
		// The plain section it's cloned from has no WidgetType, and SML's Blueprint default is a horizontal row that
		// runs off the edge of the screen with more than a couple of settings.
		if (UCP_Section* EditorSection = Cast<UCP_Section>(NewSection))
		{
			EditorSection->WidgetType = ECP_SectionWidgetType::CPS_Vertical;
		}
		for (const TPair<FString, TObjectPtr<UConfigProperty>>& Pair : OldSection->SectionProperties)
		{
			UConfigProperty* Old = Pair.Value;
			if (!Old)
			{
				continue;
			}
			++OutTotal;
			if (const UConfigPropertySection* OldChildSection = Cast<UConfigPropertySection>(Old))
			{
				if (UClass* ChildSectionClass = LoadEditorClass(UConfigPropertySection::StaticClass()))
				{
					NewSection->SectionProperties.Add(Pair.Key, ConvertSectionRecursive(OldChildSection, ChildSectionClass, NewSection, OutConverted, OutTotal));
					++OutConverted;
					continue;
				}
			}
			else if (UClass* EditorClass = LoadEditorClass(Old->GetClass()))
			{
				NewSection->SectionProperties.Add(Pair.Key, CloneAs(Old, EditorClass, NewSection));
				++OutConverted;
				continue;
			}
			NewSection->SectionProperties.Add(Pair.Key, Old);
		}
		return NewSection;
	}
}

void UBBPConfig::UseSMLEditorClasses()
{
	UBBPConfig* Defaults = GetMutableDefault<UBBPConfig>();
	UConfigPropertySection* OldRoot = Defaults->RootSection;
	UClass* SectionClass = OldRoot ? LoadEditorClass(UConfigPropertySection::StaticClass()) : nullptr;
	if (!SectionClass)
	{
		return;
	}
	if (OldRoot->GetClass() == SectionClass)
	{
		return;
	}

	int32 Converted = 0, Total = 0;
	Defaults->RootSection = ConvertSectionRecursive(OldRoot, SectionClass, Defaults, Converted, Total);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Config: %d of %d settings/sections use SML's editor widgets"), Converted, Total);
}

void UBBPConfig::SetFloat(const UObject* WorldContext, const FString& Key, float Value)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UConfigManager* Manager = GameInstance ? GameInstance->GetSubsystem<UConfigManager>() : nullptr;
	UConfigPropertyFloat* Property = Manager ? Cast<UConfigPropertyFloat>(FindProperty(WorldContext, Key)) : nullptr;
	if (!Property)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Config: could not set '%s'; configuration not available"), *Key);
		return;
	}
	Property->Value = Value;
	// Playback reads the value live every frame, so it takes effect immediately; this just persists it to disk.
	Manager->MarkConfigurationDirty(MakeConfigId());
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

FString UBBPConfig::GetString(const UObject* WorldContext, const FString& Key, const FString& Fallback)
{
	if (const UConfigPropertyString* Property = Cast<UConfigPropertyString>(FindProperty(WorldContext, Key)))
	{
		return Property->Value;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Config: '%s' unavailable"), *Key);
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
