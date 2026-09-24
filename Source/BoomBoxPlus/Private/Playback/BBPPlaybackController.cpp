#include "Playback/BBPPlaybackController.h"
#include "Audio/BBPStreamingSoundWave.h"
#include "BoomBoxPlus.h"
#include "BBPConfig.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "FGBoomBoxPlayer.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Lyrics/BBPLyricsSubsystem.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "UI/BBPHudOverlay.h"

namespace
{
	constexpr float DriftCheckInterval = 1.f;
	constexpr float DriftTolerance = 0.25f;
	constexpr float InnerRadius = 4000.f;
	constexpr float FalloffDistance = 21000.f;
}

float UBBPPlaybackController::GetAudibleRange()
{
	return InnerRadius + FalloffDistance;
}

void UBBPPlaybackController::Initialize(ABBPPlaylistSubsystem* InPlaylist)
{
	Playlist = InPlaylist;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback controller ready"));
}

void UBBPPlaybackController::Shutdown()
{
	for (FBBPEmitter& Emitter : Emitters)
	{
		StopTrack(Emitter);
		if (Emitter.Component)
		{
			Emitter.Component->DestroyComponent();
		}
	}
	Emitters.Empty();
	if (HudOverlay)
	{
		HudOverlay->RemoveFromParent();
		HudOverlay = nullptr;
	}
	Playlist = nullptr;
}

void UBBPPlaybackController::Tick(float DeltaSeconds)
{
	if (!Playlist)
	{
		return;
	}
	EnsureHudOverlay();
	SyncEmitters();
	for (FBBPEmitter& Emitter : Emitters)
	{
		UpdateEmitter(Emitter, DeltaSeconds);
	}
}

void UBBPPlaybackController::EnsureHudOverlay()
{
	if (HudOverlay)
	{
		return;
	}
	APlayerController* Controller = UGameplayStatics::GetPlayerController(Playlist, 0);
	if (!Controller || !Controller->IsLocalController())
	{
		return;
	}
	HudOverlay = CreateWidget<UBBPHudOverlay>(Controller, UBBPHudOverlay::StaticClass());
	if (!HudOverlay)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: could not create the lyric/now-playing overlay"));
		return;
	}
	HudOverlay->AddToViewport(-10);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: lyric/now-playing overlay added to viewport"));
}

void UBBPPlaybackController::SyncEmitters()
{
	const TArray<FBBPActiveBoomBox>& Active = Playlist->GetActiveBoomBoxes();
	const float MusicVolume = FMath::Clamp(UBBPConfig::GetFloat(Playlist, UBBPConfig::MusicVolumeKey, 0.8f), 0.f, 1.f);

	for (int32 i = Emitters.Num() - 1; i >= 0; --i)
	{
		FBBPEmitter& Emitter = Emitters[i];
		const bool bStillActive = Emitter.BoomBox.IsValid()
			&& Active.ContainsByPredicate([&Emitter](const FBBPActiveBoomBox& A) { return A.BoomBox == Emitter.BoomBox.Get(); });
		if (!bStillActive)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: Boom Box %s no longer has Custom Music; stopping its audio"), *GetNameSafe(Emitter.BoomBox.Get()));
			StopTrack(Emitter);
			if (Emitter.Component)
			{
				Emitter.Component->DestroyComponent();
			}
			Emitters.RemoveAt(i);
		}
	}

	for (const FBBPActiveBoomBox& ActiveBoomBox : Active)
	{
		AFGBoomBoxPlayer* BoomBox = ActiveBoomBox.BoomBox;
		if (FBBPEmitter* Existing = Emitters.FindByPredicate([BoomBox](const FBBPEmitter& E) { return E.BoomBox == BoomBox; }))
		{
			const float Volume = FMath::Clamp(ActiveBoomBox.Volume, 0.f, 1.f) * MusicVolume;
			if (Existing->Component && Existing->AppliedVolume != Volume)
			{
				Existing->AppliedVolume = Volume;
				Existing->Component->SetVolumeMultiplier(Volume);
				UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playback: volume %.2f on %s (Boom Box %.2f x music %.2f)"),
					Volume, *GetNameSafe(BoomBox), ActiveBoomBox.Volume, MusicVolume);
			}
			continue;
		}
		if (!BoomBox)
		{
			continue;
		}
		UAudioComponent* Component = CreateAudioComponent(BoomBox);
		if (!Component)
		{
			continue;
		}
		FBBPEmitter& Emitter = Emitters.AddDefaulted_GetRef();
		Emitter.BoomBox = BoomBox;
		Emitter.Component = Component;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: attached audio to Boom Box %s"), *GetNameSafe(BoomBox));
	}
}

UAudioComponent* UBBPPlaybackController::CreateAudioComponent(AFGBoomBoxPlayer* BoomBox)
{
	USceneComponent* Root = BoomBox->GetRootComponent();
	if (!Root)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: Boom Box %s has no root component; cannot attach audio"), *GetNameSafe(BoomBox));
		return nullptr;
	}

	UAudioComponent* Component = NewObject<UAudioComponent>(BoomBox);
	Component->bAutoActivate = false;
	Component->bAutoDestroy = false;
	Component->bAllowSpatialization = true;
	Component->bOverrideAttenuation = true;
	Component->AttenuationOverrides.bAttenuate = true;
	Component->AttenuationOverrides.bSpatialize = true;
	Component->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
	Component->AttenuationOverrides.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
	Component->AttenuationOverrides.FalloffDistance = FalloffDistance;
	Component->AttenuationOverrides.DistanceAlgorithm = EAttenuationDistanceModel::Logarithmic;
	Component->SetupAttachment(Root);
	Component->RegisterComponent();

	return Component;
}

void UBBPPlaybackController::UpdateEmitter(FBBPEmitter& Emitter, float DeltaSeconds)
{
	const FBBPPlaybackState& State = Playlist->GetPlaybackState();

	if (State.CurrentEntryId == INDEX_NONE)
	{
		if (Emitter.EntryId != INDEX_NONE)
		{
			StopTrack(Emitter);
		}
		Emitter.AppliedRevision = State.Revision;
		return;
	}

	const float Expected = Playlist->GetPlaybackPosition();

	if (Emitter.EntryId != State.CurrentEntryId)
	{
		StartTrack(Emitter, State.CurrentEntryId, Expected);
		Emitter.AppliedRevision = State.Revision;
		if (Emitter.Component && Emitter.Wave)
		{
			Emitter.Component->SetPaused(State.bPaused);
		}
		return;
	}

	if (!Emitter.Wave || !Emitter.Component)
	{
		return;
	}

	if (Emitter.Wave->HasFailed())
	{
		if (!Emitter.bReportedProblem)
		{
			Emitter.bReportedProblem = true;
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: stream for entry %d failed; staying silent for this track"), Emitter.EntryId);
		}
		return;
	}

	if (Emitter.AppliedRevision != State.Revision)
	{
		Emitter.AppliedRevision = State.Revision;
		Emitter.Component->SetPaused(State.bPaused);
		// Only seek when the change moved the position (seek/restart), not for shuffle or repeat changes.
		const float Actual = Emitter.Wave->GetPlaybackSeconds();
		const bool bNeedsSeek = FMath::Abs(Actual - Expected) > DriftTolerance;
		if (bNeedsSeek)
		{
			Emitter.Wave->Seek(Expected);
			Emitter.DriftCheckTimer = DriftCheckInterval;
		}
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playback: applied state rev %d (%s at %.2f s%s)"),
			State.Revision, State.bPaused ? TEXT("paused") : TEXT("playing"), Expected, bNeedsSeek ? TEXT(", seeked") : TEXT(""));
		return;
	}

	if (State.bPaused)
	{
		return;
	}

	Emitter.DriftCheckTimer -= DeltaSeconds;
	if (Emitter.DriftCheckTimer <= 0.f)
	{
		Emitter.DriftCheckTimer = DriftCheckInterval;
		const float Actual = Emitter.Wave->GetPlaybackSeconds();
		const float Drift = Actual - Expected;
		if (FMath::Abs(Drift) > DriftTolerance)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: drift %+.2f s (playing %.2f, expected %.2f, %d underruns); resyncing"),
				Drift, Actual, Expected, Emitter.Wave->GetUnderrunCount());
			Emitter.Wave->Seek(Expected);
		}
	}
}

void UBBPPlaybackController::StartTrack(FBBPEmitter& Emitter, int32 EntryId, float Position)
{
	StopTrack(Emitter);
	Emitter.EntryId = EntryId;
	Emitter.bReportedProblem = false;
	Emitter.DriftCheckTimer = DriftCheckInterval;

	FBBPQueueEntry Entry;
	if (!Playlist->GetCurrentEntry(Entry))
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playback: entry %d not replicated yet; waiting"), EntryId);
		Emitter.EntryId = INDEX_NONE;
		return;
	}

	const UWorld* World = Playlist->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	const FBBPLocalTrack* Local = Library ? Library->FindLocalTrack(Entry.Track.Id) : nullptr;
	if (UBBPLyricsSubsystem* Lyrics = GameInstance ? GameInstance->GetSubsystem<UBBPLyricsSubsystem>() : nullptr)
	{
		Lyrics->RequestLyrics(Entry.Track, Local ? Local->FilePath : FString());
	}
	if (!Local)
	{
		Emitter.bReportedProblem = true;
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: '%s' - '%s' (id %s) is not in this machine's library; staying silent for this track"),
			*Entry.Track.Artist, *Entry.Track.Title, *Entry.Track.Id);
		return;
	}

	UBBPStreamingSoundWave* Wave = NewObject<UBBPStreamingSoundWave>(this);
	if (!Wave->StartStream(Local->FilePath, Local->SampleRate, Local->Channels, Position))
	{
		Emitter.bReportedProblem = true;
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: could not start stream for '%s'"), *Local->FilePath);
		return;
	}
	Emitter.Wave = Wave;
	Emitter.Component->SetSound(Wave);
	Emitter.Component->Play();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: playing '%s' on %s from %.2f s"), *Entry.Track.Title, *GetNameSafe(Emitter.BoomBox.Get()), Position);
}

void UBBPPlaybackController::StopTrack(FBBPEmitter& Emitter)
{
	if (Emitter.Component)
	{
		Emitter.Component->Stop();
		Emitter.Component->SetSound(nullptr);
	}
	if (Emitter.Wave)
	{
		Emitter.Wave->StopStream();
		Emitter.Wave = nullptr;
	}
	Emitter.EntryId = INDEX_NONE;
}
