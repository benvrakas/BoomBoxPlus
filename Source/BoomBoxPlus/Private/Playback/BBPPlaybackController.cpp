#include "Playback/BBPPlaybackController.h"
#include "Audio/BBPAudioDevice.h"
#include "Audio/BBPStreamingSoundWave.h"
#include "AudioDevice.h"
#include "BoomBoxPlus.h"
#include "BBPBlueprintLibrary.h"
#include "BBPConfig.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "FGBoomBoxPlayer.h"
#include "FGBoomboxListenerInterface.h"
#include "FGGameUserSettings.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Lyrics/BBPLyricsSubsystem.h"
#include "Net/BBPNetSubsystem.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "UI/BBPHudOverlay.h"

namespace
{
	constexpr float DriftCheckInterval = 1.f;
	constexpr float PrefetchInterval = 1.f;
	constexpr int32 PrefetchAhead = 2;
	constexpr float DriftTolerance = 0.25f;
	constexpr float InnerRadius = 4000.f;
	constexpr float FalloffDistance = 21000.f;

	constexpr float GameVolumeInterval = 1.f;
	constexpr float VanillaPositionInterval = 0.25f;

	// Returns a game volume slider as 0..1, whether the game stores it as 0..1 or 0..100. An option that can't be read counts as 1.
	float ReadVolumeOption(const UFGGameUserSettings& Settings, const TCHAR* Option)
	{
		const FVariant Raw = Settings.GetOptionValue(Option);
		float Value = 1.f;
		switch (Raw.GetType())
		{
		case EVariantTypes::Float: Value = Raw.GetValue<float>(); break;
		case EVariantTypes::Double: Value = (float)Raw.GetValue<double>(); break;
		case EVariantTypes::Int32: Value = (float)Raw.GetValue<int32>(); break;
		default:
		{
			static TSet<FString> Reported;
			if (!Reported.Contains(Option))
			{
				Reported.Add(Option);
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: game option '%s' unreadable (variant type %d); treating it as full volume"), Option, (int32)Raw.GetType());
			}
			return 1.f;
		}
		}
		return FMath::Clamp(Value > 1.f ? Value / 100.f : Value, 0.f, 1.f);
	}
}

float UBBPPlaybackController::GetAudibleRange()
{
	return InnerRadius + FalloffDistance;
}

void UBBPPlaybackController::Initialize(ABBPPlaylistSubsystem* InPlaylist)
{
	Playlist = InPlaylist;
	const bool bHasAudio = BBPAudioDevice::EnsureAvailable();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback controller ready (Unreal audio device %s)"), bHasAudio ? TEXT("available") : TEXT("MISSING"));
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
	GameMusicFader.Restore();
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
	GameVolumeTimer -= DeltaSeconds;
	if (GameVolumeTimer <= 0.f)
	{
		GameVolumeTimer = GameVolumeInterval;
		UpdateGameVolumeScale();
	}
	PrefetchTimer -= DeltaSeconds;
	if (PrefetchTimer <= 0.f)
	{
		PrefetchTimer = PrefetchInterval;
		PrefetchNetworkTracks();
	}
	SyncEmitters();
	for (FBBPEmitter& Emitter : Emitters)
	{
		UpdateEmitter(Emitter, DeltaSeconds);
	}
	ReportLoadedTracks();
	UpdateGameMusic(DeltaSeconds);
	UpdateVanillaPages(DeltaSeconds);
}

void UBBPPlaybackController::UpdateGameVolumeScale()
{
	const UFGGameUserSettings* Settings = UFGGameUserSettings::GetFGGameUserSettings();
	if (!Settings)
	{
		return;
	}
	// The Master slider is stored as "RTPC.Menu_Volume_Master"; "RTPC.Master_Bus_Volume" is not a saved option.
	const float Master = ReadVolumeOption(*Settings, TEXT("RTPC.Menu_Volume_Master"));
	const float BoomBox = ReadVolumeOption(*Settings, TEXT("RTPC.Boombox_Bus_Volume"));
	const float Scale = Master * BoomBox;
	if (!FMath::IsNearlyEqual(Scale, GameVolumeScale, 0.001f))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: game volume sliders Master %.2f x Boom Box %.2f"), Master, BoomBox);
		GameVolumeScale = Scale;
	}
}

void UBBPPlaybackController::UpdateVanillaPages(float DeltaSeconds)
{
	VanillaPositionTimer -= DeltaSeconds;
	const bool bSendPosition = VanillaPositionTimer <= 0.f;
	if (bSendPosition)
	{
		VanillaPositionTimer = VanillaPositionInterval;
	}

	TSet<UObject*> Seen;
	for (const FBBPEmitter& Emitter : Emitters)
	{
		AFGBoomBoxPlayer* BoomBox = Emitter.BoomBox.Get();
		const ABBPMusicChannel* Channel = Emitter.Channel.Get();
		if (!BoomBox || !Channel)
		{
			continue;
		}
		FBBPQueueEntry Current;
		const bool bHasCurrent = Channel->GetCurrentEntry(Current);
		const int32 EntryId = bHasCurrent ? Current.EntryId : INDEX_NONE;
		const bool bPlaying = Channel->IsPlaying();

		for (const TScriptInterface<IFGBoomboxListenerInterface>& Listener : BoomBox->GetmStateListeners())
		{
			UObject* Object = Listener.GetObject();
			if (!IsValid(Object) || !Object->GetClass()->ImplementsInterface(UFGBoomboxListenerInterface::StaticClass()))
			{
				continue;
			}
			Seen.Add(Object);
			FBBPVanillaPageState& Page = VanillaPages.FindOrAdd(Object);
			if (Page.Channel.Get() != Channel || Page.EntryId != EntryId)
			{
				const int32 Index = Channel->GetQueue().IndexOfByPredicate([EntryId](const FBBPQueueEntry& E) { return E.EntryId == EntryId; });
				IFGBoomboxListenerInterface::Execute_CurrentSongChanged(Object, BoomBox->GetCurrentSong(), FMath::Max(0, Index));
			}
			if (Page.Channel.Get() != Channel || Page.EntryId != EntryId || Page.bPlaying != bPlaying)
			{
				const int32 State = bPlaying ? static_cast<int32>(EBoomBoxPlaybackStateBitfield::MayActuallyPlay) : 0;
				IFGBoomboxListenerInterface::Execute_PlaybackStateChanged(Object, State);
				UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playback: told %s entry %d, %s"), *GetNameSafe(Object), EntryId, bPlaying ? TEXT("playing") : TEXT("paused"));
			}
			Page.Channel = Channel;
			Page.EntryId = EntryId;
			Page.bPlaying = bPlaying;
			if (bSendPosition && bHasCurrent)
			{
				IFGBoomboxListenerInterface::Execute_PlaybackPositionUpdate(Object, Channel->GetPlaybackPosition(), Current.Track.Duration);
			}
		}
	}
	for (auto It = VanillaPages.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !Seen.Contains(It.Key().Get()))
		{
			It.RemoveCurrent();
		}
	}
}

const ABBPMusicChannel* UBBPPlaybackController::GetAudibleChannel(bool bRequireSound) const
{
	const APawn* Pawn = Playlist ? UGameplayStatics::GetPlayerPawn(Playlist, 0) : nullptr;
	if (!Pawn)
	{
		return nullptr;
	}
	const ABBPMusicChannel* Nearest = nullptr;
	float NearestDistanceSquared = FMath::Square(GetAudibleRange());
	for (const FBBPEmitter& Emitter : Emitters)
	{
		const AFGBoomBoxPlayer* BoomBox = Emitter.BoomBox.Get();
		const ABBPMusicChannel* Channel = Emitter.Channel.Get();
		if (!BoomBox || !Channel || !Channel->IsPlaying())
		{
			continue;
		}
		if (bRequireSound && (!Emitter.Wave || Emitter.Wave->HasFailed() || Emitter.AppliedVolume <= 0.f))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(BoomBox->GetActorLocation(), Pawn->GetActorLocation());
		if (DistanceSquared <= NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			Nearest = Channel;
		}
	}
	return Nearest;
}

void UBBPPlaybackController::ReportLoadedTracks()
{
	const UWorld* World = Playlist->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	for (auto It = ReportedLoads.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (const FBBPEmitter& Emitter : Emitters)
	{
		ABBPMusicChannel* Channel = Emitter.Channel.Get();
		if (!Channel)
		{
			continue;
		}
		const FBBPPlaybackState& State = Channel->GetPlaybackState();
		const int32* Reported = ReportedLoads.Find(Channel);
		if (!State.bLoading || Emitter.EntryId != State.CurrentEntryId || (Reported && *Reported == State.LoadGeneration))
		{
			continue;
		}
		// Ready: the stream is running (paused at the start). Also "ready" when this machine can't play the track at all
		// (download failed, file not in the library, stream failed), so nobody waits for audio that will never come.
		const TCHAR* Status = nullptr;
		if (Emitter.Wave)
		{
			Status = Emitter.Wave->HasFailed() ? TEXT("can't decode it") : TEXT("loaded");
		}
		else if (Emitter.bWaitingForFile)
		{
			FBBPQueueEntry Entry;
			const bool bGaveUp = Channel->GetCurrentEntry(Entry) && Net && Library
				&& !Net->IsDownloadPending(Entry.Track.Id) && !Library->HasTrack(Entry.Track.Id);
			Status = bGaveUp ? TEXT("can't get it") : nullptr;
		}
		else if (Emitter.bReportedProblem)
		{
			Status = TEXT("can't play it");
		}
		if (Status)
		{
			ReportedLoads.Add(Channel, State.LoadGeneration);
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: reporting entry %d of channel %04d as ready here (%s)"), State.CurrentEntryId, Channel->GetLinkCode(), Status);
			UBBPBlueprintLibrary::ReportTrackLoaded(Channel, State.LoadGeneration);
		}
	}
}

void UBBPPlaybackController::UpdateGameMusic(float DeltaSeconds)
{
	const float Level = FMath::Clamp(UBBPConfig::GetFloat(Playlist, UBBPConfig::GameMusicLevelKey, 0.f), 0.f, 1.f);
	const float FadeSeconds = UBBPConfig::GetFloat(Playlist, UBBPConfig::GameMusicFadeTimeKey, 2.f);
	// A level of 1 means "leave the game's music alone", so it's never lowered at all.
	const bool bLower = Level < 1.f && GetAudibleChannel(true) != nullptr;
	GameMusicFader.Update(bLower, Level, FadeSeconds, DeltaSeconds);
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

void UBBPPlaybackController::PrefetchNetworkTracks()
{
	const UWorld* World = Playlist->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	if (!Net || !Library)
	{
		return;
	}
	// Only channels some Boom Box is playing need their tracks on disk.
	TSet<const ABBPMusicChannel*> Heard;
	for (const FBBPEmitter& Emitter : Emitters)
	{
		if (const ABBPMusicChannel* Channel = Playlist->FindChannel(Emitter.BoomBox.Get()))
		{
			Heard.Add(Channel);
		}
	}
	// Only the current track and the next few are downloaded; anything else still waiting is dropped.
	TSet<FString> Wanted;
	TArray<const FBBPTrack*> ToFetch;
	for (const ABBPMusicChannel* Channel : Heard)
	{
		const TArray<FBBPQueueEntry>& Queue = Channel->GetQueue();
		const int32 CurrentEntryId = Channel->GetPlaybackState().CurrentEntryId;
		const int32 CurrentIndex = FMath::Max(0, Queue.IndexOfByPredicate([CurrentEntryId](const FBBPQueueEntry& E) { return E.EntryId == CurrentEntryId; }));
		for (int32 i = CurrentIndex; i < Queue.Num() && i <= CurrentIndex + PrefetchAhead; ++i)
		{
			const FBBPTrack& Track = Queue[i].Track;
			if (Track.Source != EBBPTrackSource::Local && !Track.IsLive() && !Library->HasTrack(Track.Id))
			{
				Wanted.Add(Track.Id);
				ToFetch.Add(&Track);
			}
		}
	}
	Net->SetWantedDownloads(Wanted);
	for (const FBBPTrack* Track : ToFetch)
	{
		Net->EnsureDownloaded(*Track);
	}
}

void UBBPPlaybackController::SyncEmitters()
{
	const TArray<FBBPActiveBoomBox>& Active = Playlist->GetActiveBoomBoxes();
	const float MusicVolume = FMath::Clamp(UBBPConfig::GetFloat(Playlist, UBBPConfig::MusicVolumeKey, 0.5f), 0.f, 1.f) * GameVolumeScale;

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
			if (Existing->Component && !FMath::IsNearlyEqual(Existing->AppliedVolume, Volume, 0.001f))
			{
				Existing->AppliedVolume = Volume;
				Existing->Component->SetVolumeMultiplier(Volume);
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: volume %.3f on %s (Boom Box %.2f x music volume setting x game sliders %.3f)"),
					Volume, *GetNameSafe(BoomBox), ActiveBoomBox.Volume, MusicVolume);
			}
			continue;
		}
		if (!IsValid(BoomBox))
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
	ABBPMusicChannel* Channel = Playlist->FindChannel(Emitter.BoomBox.Get());
	if (Channel != Emitter.Channel.Get())
	{
		// Linked, unlinked, or given its first channel: entry ids belong to the old channel, so start over.
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: %s now plays channel %04d"), *GetNameSafe(Emitter.BoomBox.Get()), Channel ? Channel->GetLinkCode() : 0);
		StopTrack(Emitter);
		Emitter.Channel = Channel;
		Emitter.AppliedRevision = -1;
		Emitter.bWaitingForFile = false;
		Emitter.bLive = false;
	}
	if (!Channel)
	{
		return;
	}

	const FBBPPlaybackState& State = Channel->GetPlaybackState();

	if (State.CurrentEntryId == INDEX_NONE)
	{
		if (Emitter.EntryId != INDEX_NONE)
		{
			StopTrack(Emitter);
		}
		Emitter.AppliedRevision = State.Revision;
		return;
	}

	const float Expected = Channel->GetPlaybackPosition();

	if (Emitter.EntryId != State.CurrentEntryId)
	{
		StartTrack(Emitter, *Channel, State.CurrentEntryId, Expected);
		Emitter.AppliedRevision = State.Revision;
		if (Emitter.Component && Emitter.Wave)
		{
			Emitter.Component->SetPaused(State.bPaused || State.bLoading);
		}
		return;
	}

	if (Emitter.bLive)
	{
		// Pausing disconnects a live stream; resuming reconnects to whatever the station is playing now.
		if (State.bPaused)
		{
			if (Emitter.Wave)
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: radio paused on %s; disconnecting"), *GetNameSafe(Emitter.BoomBox.Get()));
				ReleaseWave(Emitter);
			}
			Emitter.bReportedProblem = false;
		}
		else if (!Emitter.Wave && !Emitter.bReportedProblem)
		{
			FBBPQueueEntry Entry;
			if (Channel->GetCurrentEntry(Entry))
			{
				StartLiveStream(Emitter, Entry);
			}
		}
		else if (Emitter.Wave && Emitter.Wave->HasFailed() && !Emitter.bReportedProblem)
		{
			Emitter.bReportedProblem = true;
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: radio stream for entry %d failed; silent until it is paused and resumed, or skipped"), Emitter.EntryId);
		}
		Emitter.AppliedRevision = State.Revision;
		return;
	}

	if (!Emitter.Wave && Emitter.bWaitingForFile)
	{
		const UWorld* World = Playlist->GetWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
		FBBPQueueEntry Entry;
		if (Library && Channel->GetCurrentEntry(Entry) && Library->HasTrack(Entry.Track.Id))
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: '%s' is now available; joining at %.2f s"), *Entry.Track.Title, Expected);
			StartTrack(Emitter, *Channel, State.CurrentEntryId, Expected);
			Emitter.AppliedRevision = State.Revision;
			if (Emitter.Component && Emitter.Wave)
			{
				Emitter.Component->SetPaused(State.bPaused || State.bLoading);
			}
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
		Emitter.Component->SetPaused(State.bPaused || State.bLoading);
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

	if (State.bPaused || State.bLoading)
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

void UBBPPlaybackController::StartTrack(FBBPEmitter& Emitter, const ABBPMusicChannel& Channel, int32 EntryId, float Position)
{
	StopTrack(Emitter);
	Emitter.EntryId = EntryId;
	Emitter.bReportedProblem = false;
	Emitter.bWaitingForFile = false;
	Emitter.bLive = false;
	Emitter.DriftCheckTimer = DriftCheckInterval;

	FBBPQueueEntry Entry;
	if (!Channel.GetCurrentEntry(Entry))
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playback: entry %d not replicated yet; waiting"), EntryId);
		Emitter.EntryId = INDEX_NONE;
		return;
	}

	if (Entry.Track.IsLive())
	{
		Emitter.bLive = true;
		// A paused station isn't connected until it's resumed (see UpdateEmitter).
		if (!Channel.GetPlaybackState().bPaused)
		{
			StartLiveStream(Emitter, Entry);
		}
		return;
	}

	const UWorld* World = Playlist->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	const FBBPLocalTrack* Local = Library ? Library->FindLocalTrack(Entry.Track.Id) : nullptr;
	// With lyrics turned off, nothing is looked up (no LRCLIB requests).
	UBBPLyricsSubsystem* Lyrics = GameInstance ? GameInstance->GetSubsystem<UBBPLyricsSubsystem>() : nullptr;
	if (Lyrics && UBBPConfig::GetBool(Playlist, UBBPConfig::ShowLyricsKey, true))
	{
		Lyrics->RequestLyrics(Entry.Track, Local ? Local->FilePath : FString());
	}
	if (!Local)
	{
		Emitter.bReportedProblem = true;
		Emitter.bWaitingForFile = true;
		if (Entry.Track.Source != EBBPTrackSource::Local)
		{
			if (UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr)
			{
				Net->EnsureDownloaded(Entry.Track, true);
			}
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: '%s' not downloaded yet; silent until it is"), *Entry.Track.Title);
		}
		else
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: '%s' - '%s' (id %s) is not in this machine's library; staying silent for this track"),
				*Entry.Track.Artist, *Entry.Track.Title, *Entry.Track.Id);
		}
		return;
	}

	UBBPStreamingSoundWave* Wave = NewObject<UBBPStreamingSoundWave>(this);
	if (!Wave->StartStream(Local->FilePath, Local->SampleRate, Local->Channels, Position))
	{
		Emitter.bReportedProblem = true;
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: could not start stream for '%s'"), *Local->FilePath);
		return;
	}
	PlayWave(Emitter, Wave);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: playing '%s' on %s from %.2f s"), *Entry.Track.Title, *GetNameSafe(Emitter.BoomBox.Get()), Position);
}

void UBBPPlaybackController::StartLiveStream(FBBPEmitter& Emitter, const FBBPQueueEntry& Entry)
{
	const UWorld* World = Playlist->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	const FString Ffmpeg = Net ? Net->GetFfmpegPath() : FString();
	if (Ffmpeg.IsEmpty())
	{
		Emitter.bReportedProblem = true;
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: can't play radio '%s': the bundled ffmpeg is missing"), *Entry.Track.Title);
		return;
	}
	UBBPStreamingSoundWave* Wave = NewObject<UBBPStreamingSoundWave>(this);
	if (!Wave->StartLiveStream(Ffmpeg, Net->GetYtDlpPath(), Entry.Track.SourceRef))
	{
		Emitter.bReportedProblem = true;
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playback: could not start radio '%s' (%s)"), *Entry.Track.Title, *Entry.Track.SourceRef);
		return;
	}
	PlayWave(Emitter, Wave);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playback: tuning %s in to radio '%s'"), *GetNameSafe(Emitter.BoomBox.Get()), *Entry.Track.Title);
}

void UBBPPlaybackController::PlayWave(FBBPEmitter& Emitter, UBBPStreamingSoundWave* Wave)
{
	Emitter.Wave = Wave;
	Emitter.Component->SetSound(Wave);
	Emitter.Component->Play();
	if (!Emitter.Component->GetAudioDevice() && !bReportedNoAudioDevice)
	{
		bReportedNoAudioDevice = true;
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playback: no Unreal audio device for %s; the track is streaming but nothing will be heard"), *GetNameSafe(Emitter.BoomBox.Get()));
	}
}

void UBBPPlaybackController::ReleaseWave(FBBPEmitter& Emitter)
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
}

void UBBPPlaybackController::StopTrack(FBBPEmitter& Emitter)
{
	ReleaseWave(Emitter);
	Emitter.EntryId = INDEX_NONE;
}

bool UBBPPlaybackController::GetLiveStatus(const ABBPMusicChannel* Channel, FString& OutSongTitle, bool& bOutBuffering) const
{
	for (const FBBPEmitter& Emitter : Emitters)
	{
		if (Channel && Emitter.Channel.Get() == Channel && Emitter.bLive && Emitter.Wave && !Emitter.Wave->HasFailed())
		{
			OutSongTitle = Emitter.Wave->GetLiveTitle();
			bOutBuffering = Emitter.Wave->IsBuffering();
			return true;
		}
	}
	return false;
}
