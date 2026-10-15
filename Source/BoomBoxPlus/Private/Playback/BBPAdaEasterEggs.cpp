#include "Playback/BBPAdaEasterEggs.h"
#include "Audio/BBPDecoder.h"
#include "Audio/BBPStreamingSoundWave.h"
#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Shortest time between two lines, and before the first one after loading.
	constexpr double MinGapSeconds = 600.0;
	constexpr double FirstLineDelaySeconds = 60.0;

	constexpr float IdleSeconds1 = 300.f;
	constexpr float IdleSeconds2 = 900.f;
	// Moving or turning less than this still counts as idle.
	constexpr float IdleMoveTolerance = 100.f;
	constexpr float IdleTurnToleranceDegrees = 5.f;

	constexpr int32 LoopPlays1 = 4;
	constexpr int32 LoopPlays2 = 10;
	// A position jump back by more than this on the same entry counts as the song starting again (repeat one).
	constexpr float RestartJumpSeconds = 5.f;

	constexpr float CopyrightChance = 0.1f;
	constexpr int32 AiOneIn = 500;

	// Another player this close to a full-volume Boom Box (cm).
	constexpr float VolumeNeighbourDistance = 3000.f;
	constexpr float VolumeCheckInterval = 1.f;

	constexpr float DuckLevel = 0.2f;
	constexpr float DuckFadeSeconds = 0.3f;

	bool ContainsAny(const FString& Text, std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* Word : Words)
		{
			if (Text.Contains(Word, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

void UBBPAdaEasterEggs::Initialize(UBBPPlaybackController* InController)
{
	Controller = InController;
	LastLineTime = FPlatformTime::Seconds() - MinGapSeconds + FirstLineDelaySeconds;
	LoadLines();
}

void UBBPAdaEasterEggs::Shutdown()
{
	StopLine();
}

void UBBPAdaEasterEggs::LoadLines()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BoomBoxPlus"));
	if (!Plugin)
	{
		return;
	}
	AudioDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Audio") / TEXT("ADA"));
	FString Json;
	TArray<TSharedPtr<FJsonValue>> Lines;
	if (!FFileHelper::LoadFileToString(Json, *(AudioDir / TEXT("lines.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Lines))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: no lines in '%s'; easter eggs won't play"), *AudioDir);
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : Lines)
	{
		const TSharedPtr<FJsonObject>* Line = nullptr;
		FString Name;
		FString Text;
		if (Value->TryGetObject(Line) && (*Line)->TryGetStringField(TEXT("name"), Name) && (*Line)->TryGetStringField(TEXT("subtitle"), Text))
		{
			Subtitles.Add(Name, Text);
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: %d easter-egg lines"), Subtitles.Num());
}

void UBBPAdaEasterEggs::Tick(float DeltaSeconds)
{
	if (Wave)
	{
		LineTimeLeft -= DeltaSeconds;
		if (Component)
		{
			Component->SetVolumeMultiplier(Controller && !Controller->IsMutedInBackground() ? Controller->GetVoiceVolumeScale() : 0.f);
		}
		if (LineTimeLeft <= 0.f || Wave->IsFinished() || Wave->HasFailed())
		{
			StopLine();
		}
	}
	const float DuckTarget = Wave ? DuckLevel : 1.f;
	MusicDuck = FMath::FInterpConstantTo(MusicDuck, DuckTarget, DeltaSeconds, (1.f - DuckLevel) / DuckFadeSeconds);

	if (!Controller || Subtitles.Num() == 0)
	{
		return;
	}
	const ABBPMusicChannel* Channel = Controller->GetAudibleChannel(true);
	CheckIdle(DeltaSeconds, Channel);
	CheckTrack(Channel);
	VolumeCheckTimer -= DeltaSeconds;
	if (VolumeCheckTimer <= 0.f)
	{
		VolumeCheckTimer = VolumeCheckInterval;
		CheckVolume();
	}
}

void UBBPAdaEasterEggs::NotifyMusicPageOpened()
{
	if (FMath::RandHelper(AiOneIn) == 0)
	{
		TryPlay(TEXT("ai"));
	}
}

bool UBBPAdaEasterEggs::TryPlay(const FString& Name, const FString& SubtitleText, bool bIgnoreGap)
{
	const double Now = FPlatformTime::Seconds();
	const FString* Text = Subtitles.Find(Name);
	if (!Controller || !Text || Wave || Played.Contains(Name) || (!bIgnoreGap && Now - LastLineTime < MinGapSeconds)
		|| Controller->IsMutedInBackground() || !UBBPConfig::GetBool(Controller, UBBPConfig::AdaEasterEggsKey, true))
	{
		return false;
	}
	const FString Path = AudioDir / (Name + TEXT(".ogg"));
	TArray<uint8> Bytes;
	FBBPDecoder Probe;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !Probe.Open(MoveTemp(Bytes)))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: can't open '%s'"), *Path);
		Played.Add(Name);
		return false;
	}
	const int32 SampleRate = Probe.GetSampleRate();
	const int32 Channels = Probe.GetChannels();
	const float Duration = Probe.GetDurationSeconds();
	Probe.Close();

	Wave = NewObject<UBBPStreamingSoundWave>(this);
	if (!Wave->StartStream(Path, SampleRate, Channels, 0.f))
	{
		Wave = nullptr;
		Played.Add(Name);
		return false;
	}
	Component = UGameplayStatics::CreateSound2D(Controller, Wave, Controller->GetVoiceVolumeScale(), 1.f, 0.f, nullptr, false, false);
	if (!Component)
	{
		StopLine();
		return false;
	}
	Component->Play();
	Played.Add(Name);
	LastLineTime = Now;
	LineTimeLeft = Duration + 0.5f;
	Subtitle = SubtitleText.IsEmpty() ? *Text : SubtitleText;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: playing '%s' (%.1f s)"), *Name, Duration);
	return true;
}

void UBBPAdaEasterEggs::StopLine()
{
	if (Component)
	{
		Component->Stop();
		Component->DestroyComponent();
		Component = nullptr;
	}
	if (Wave)
	{
		Wave->StopStream();
		Wave = nullptr;
	}
	Subtitle.Reset();
	LineTimeLeft = 0.f;
}

void UBBPAdaEasterEggs::CheckIdle(float DeltaSeconds, const ABBPMusicChannel* Channel)
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Controller, 0);
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	// Menus show the mouse cursor; time spent in one isn't idling.
	if (!Pawn || !Channel || PlayerController->ShouldShowMouseCursor())
	{
		IdleSeconds = 0.f;
		bIdleSaidThisStretch = false;
		if (Pawn)
		{
			IdleLocation = Pawn->GetActorLocation();
			IdleRotation = PlayerController->GetControlRotation();
		}
		return;
	}
	const FRotator Rotation = PlayerController->GetControlRotation();
	const bool bMoved = FVector::Dist(Pawn->GetActorLocation(), IdleLocation) > IdleMoveTolerance
		|| !Rotation.Equals(IdleRotation, IdleTurnToleranceDegrees);
	if (bMoved)
	{
		IdleSeconds = 0.f;
		bIdleSaidThisStretch = false;
		IdleLocation = Pawn->GetActorLocation();
		IdleRotation = Rotation;
		return;
	}
	IdleSeconds += DeltaSeconds;
	if (IdleSeconds >= IdleSeconds1 && !bIdleSaidThisStretch && TryPlay(TEXT("idle")))
	{
		bIdleSaidThisStretch = true;
	}
	if (IdleSeconds >= IdleSeconds2 && bIdleSaidThisStretch)
	{
		TryPlay(TEXT("idle2"), FString(), true);
	}
}

void UBBPAdaEasterEggs::CheckTrack(const ABBPMusicChannel* Channel)
{
	FBBPQueueEntry Entry;
	if (!Channel || !Channel->GetCurrentEntry(Entry))
	{
		TrackChannel = nullptr;
		TrackEntryId = INDEX_NONE;
		return;
	}
	const float Position = Channel->GetPlaybackPosition();
	const bool bNewChannel = TrackChannel.Get() != Channel;
	const bool bStarted = bNewChannel || Entry.EntryId != TrackEntryId || Position < LastTrackPosition - RestartJumpSeconds;
	LastTrackPosition = Position;
	if (!bStarted)
	{
		return;
	}
	TrackChannel = Channel;
	TrackEntryId = Entry.EntryId;
	const FBBPTrack& Track = Entry.Track;
	ConsecutivePlays = (!bNewChannel && Track.Id == LastTrackId) ? ConsecutivePlays + 1 : 1;
	LastTrackId = Track.Id;

	if (ConsecutivePlays == LoopPlays1 && TryPlay(TEXT("loop")))
	{
		return;
	}
	if (ConsecutivePlays == LoopPlays2 && TryPlay(TEXT("loop2")))
	{
		return;
	}
	const FString About = Track.Title + TEXT(" | ") + Track.Artist + TEXT(" | ") + Track.Uploader;
	if ((Track.Id == TEXT("yt:dQw4w9WgXcQ") || Track.Title.Contains(TEXT("never gonna give you up"), ESearchCase::IgnoreCase)) && TryPlay(TEXT("rickroll")))
	{
		return;
	}
	if ((ContainsAny(Track.Artist + Track.Uploader, { TEXT("coffee stain") }) || Track.Title.Contains(TEXT("satisfactory"), ESearchCase::IgnoreCase)) && TryPlay(TEXT("ost")))
	{
		return;
	}
	if (ContainsAny(About, { TEXT("metal"), TEXT("doom"), TEXT("thrash"), TEXT("deathcore") }) && TryPlay(TEXT("metal")))
	{
		return;
	}
	if (ContainsAny(About, { TEXT("lofi"), TEXT("lo-fi"), TEXT("lo fi"), TEXT("chillhop"), TEXT("beats to relax") }) && TryPlay(TEXT("lofi")))
	{
		return;
	}
	const bool bOfficialMusicChannel = Track.Uploader.EndsWith(TEXT("VEVO"), ESearchCase::IgnoreCase) || Track.Uploader.EndsWith(TEXT(" - Topic"), ESearchCase::IgnoreCase);
	if (bOfficialMusicChannel && FMath::FRand() < CopyrightChance)
	{
		TryPlay(TEXT("copyright"));
	}
}

void UBBPAdaEasterEggs::CheckVolume()
{
	const ABBPPlaylistSubsystem* Playlist = Controller->GetPlaylist();
	const UWorld* World = Controller->GetWorld();
	const APawn* LocalPawn = UGameplayStatics::GetPlayerPawn(Controller, 0);
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!Playlist || !LocalPawn || !GameState || Played.Contains(TEXT("volume")))
	{
		return;
	}
	TArray<FBBPNearbyBoomBox> Audible;
	Controller->GetAudibleBoomBoxes(Audible);
	for (const FBBPActiveBoomBox& Active : Playlist->GetActiveBoomBoxes())
	{
		if (!IsValid(Active.BoomBox) || Active.Volume < 0.99f
			|| !Audible.ContainsByPredicate([&Active](const FBBPNearbyBoomBox& Near) { return Near.BoomBox == Active.BoomBox && !Near.bMutedByMode; }))
		{
			continue;
		}
		const FVector Location = Active.BoomBox->GetActorLocation();
		const bool bNeighbour = GameState->PlayerArray.ContainsByPredicate([LocalPawn, &Location](const APlayerState* PlayerState)
		{
			const APawn* Other = PlayerState ? PlayerState->GetPawn() : nullptr;
			return Other && Other != LocalPawn && FVector::Dist(Other->GetActorLocation(), Location) <= VolumeNeighbourDistance;
		});
		if (!bNeighbour)
		{
			continue;
		}
		const APawn* Carrier = Active.BoomBox->IsInEquipmentMode() ? Cast<APawn>(Active.BoomBox->GetmOwningCharacter()) : nullptr;
		const APlayerState* CarrierState = Carrier ? Carrier->GetPlayerState() : nullptr;
		FString Text = Subtitles.FindRef(TEXT("volume"));
		Text.ReplaceInline(TEXT("Pioneer {name}"), CarrierState ? *(TEXT("Pioneer ") + CarrierState->GetPlayerName()) : TEXT("A Pioneer"));
		TryPlay(TEXT("volume"), Text);
		return;
	}
}
