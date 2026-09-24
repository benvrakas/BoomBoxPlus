#include "Playlist/BBPPlaylistSubsystem.h"
#include "BoomBoxPlus.h"
#include "EngineUtils.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Playback/BBPPlaybackController.h"
#include "Subsystem/SubsystemActorManager.h"
#include "Tape/BBPCustomMusicTape.h"

// Returns early (with the given value) and logs when a server-only function is called on a client.
#define BBP_REQUIRE_AUTHORITY(ReturnValue) 	if (!HasAuthority()) 	{ 		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: %s called without authority; ignored"), ANSI_TO_TCHAR(__FUNCTION__)); 		return ReturnValue; 	}

namespace
{
	constexpr int32 MaxQueueLength = 1000;
	constexpr float ActiveBoomBoxRefreshInterval = 0.5f;
	constexpr float PreviousRestartThreshold = 3.f;
	constexpr double SkipDebounceSeconds = 0.25;
}

ABBPPlaylistSubsystem::ABBPPlaylistSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

ABBPPlaylistSubsystem* ABBPPlaylistSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USubsystemActorManager* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
	return Manager ? Manager->GetSubsystemActor<ABBPPlaylistSubsystem>() : nullptr;
}

void ABBPPlaylistSubsystem::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist subsystem started (%s)"), HasAuthority() ? TEXT("server") : TEXT("client"));

	if (GetNetMode() != NM_DedicatedServer)
	{
		PlaybackController = NewObject<UBBPPlaybackController>(this);
		PlaybackController->Initialize(this);
	}
}

void ABBPPlaylistSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PlaybackController)
	{
		PlaybackController->Shutdown();
		PlaybackController = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ABBPPlaylistSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABBPPlaylistSubsystem, Queue);
	DOREPLIFETIME(ABBPPlaylistSubsystem, PlaybackState);
	DOREPLIFETIME(ABBPPlaylistSubsystem, ActiveBoomBoxes);
}

void ABBPPlaylistSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ActiveBoomBoxRefreshTimer -= DeltaSeconds;
		if (ActiveBoomBoxRefreshTimer <= 0.f)
		{
			ActiveBoomBoxRefreshTimer = ActiveBoomBoxRefreshInterval;
			RefreshActiveBoomBoxes();
		}

		FBBPQueueEntry Current;
		if (IsPlaying() && GetCurrentEntry(Current))
		{
			if (Current.Track.Duration <= 0.f)
			{
				if (!bWarnedMissingDuration)
				{
					bWarnedMissingDuration = true;
					UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: '%s' has no duration; it will not advance on its own"), *Current.Track.Title);
				}
			}
			else if (GetPlaybackPosition() >= Current.Track.Duration)
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: '%s' finished"), *Current.Track.Title);
				Advance(true);
			}
		}
	}

	if (PlaybackController)
	{
		PlaybackController->Tick(DeltaSeconds);
	}
}

double ABBPPlaylistSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float ABBPPlaylistSubsystem::GetPlaybackPosition() const
{
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return 0.f;
	}
	if (PlaybackState.bPaused)
	{
		return PlaybackState.PausedPosition;
	}
	return FMath::Max(0.f, (float)(GetServerTime() - PlaybackState.TrackStartServerTime));
}

bool ABBPPlaylistSubsystem::GetCurrentEntry(FBBPQueueEntry& OutEntry) const
{
	const int32 Index = FindEntryIndex(PlaybackState.CurrentEntryId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutEntry = Queue[Index];
	return true;
}

int32 ABBPPlaylistSubsystem::FindEntryIndex(int32 EntryId) const
{
	if (EntryId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	return Queue.IndexOfByPredicate([EntryId](const FBBPQueueEntry& Entry) { return Entry.EntryId == EntryId; });
}

bool ABBPPlaylistSubsystem::AddTrack(const FBBPTrack& Track, bool bFront, const FString& AddedBy)
{
	BBP_REQUIRE_AUTHORITY(false)
	if (!Track.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: rejected track with no id from %s"), *AddedBy);
		return false;
	}
	if (Queue.Num() >= MaxQueueLength)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: queue full (%d); rejected '%s' from %s"), MaxQueueLength, *Track.Title, *AddedBy);
		return false;
	}

	FBBPQueueEntry Entry;
	Entry.EntryId = NextEntryId++;
	Entry.Track = Track;
	Entry.AddedBy = AddedBy;

	const int32 CurrentIndex = FindEntryIndex(PlaybackState.CurrentEntryId);
	const int32 InsertIndex = bFront ? CurrentIndex + 1 : Queue.Num();
	Queue.Insert(Entry, InsertIndex);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s added '%s' (entry %d) at %s, position %d of %d"),
		*AddedBy, *Track.Title, Entry.EntryId, bFront ? TEXT("front") : TEXT("end"), InsertIndex + 1, Queue.Num());
	MarkQueueChanged();

	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		StartEntry(Entry.EntryId, 0.f);
	}
	return true;
}

bool ABBPPlaylistSubsystem::RemoveEntry(int32 EntryId)
{
	BBP_REQUIRE_AUTHORITY(false)
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: remove failed, no entry %d"), EntryId);
		return false;
	}

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: removing '%s' (entry %d)"), *Queue[Index].Track.Title, EntryId);
	if (EntryId == PlaybackState.CurrentEntryId)
	{
		Advance(false);
		if (PlaybackState.CurrentEntryId == EntryId)
		{
			StopPlayback();
		}
	}
	Queue.RemoveAt(FindEntryIndex(EntryId));
	PlayHistory.Remove(EntryId);
	ShufflePlayed.Remove(EntryId);
	MarkQueueChanged();
	return true;
}

bool ABBPPlaylistSubsystem::MoveEntry(int32 EntryId, int32 NewIndex)
{
	BBP_REQUIRE_AUTHORITY(false)
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: move failed, no entry %d"), EntryId);
		return false;
	}
	const FBBPQueueEntry Entry = Queue[Index];
	Queue.RemoveAt(Index);
	NewIndex = FMath::Clamp(NewIndex, 0, Queue.Num());
	Queue.Insert(Entry, NewIndex);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: moved '%s' (entry %d) from %d to %d"), *Entry.Track.Title, EntryId, Index, NewIndex);
	MarkQueueChanged();
	return true;
}

void ABBPPlaylistSubsystem::ClearQueue()
{
	BBP_REQUIRE_AUTHORITY()
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: clearing %d entries"), Queue.Num());
	Queue.Empty();
	PlayHistory.Empty();
	ShufflePlayed.Empty();
	StopPlayback();
	MarkQueueChanged();
}

void ABBPPlaylistSubsystem::SetPlaying(bool bPlay)
{
	BBP_REQUIRE_AUTHORITY()
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		if (bPlay && Queue.Num() > 0)
		{
			StartEntry(Queue[0].EntryId, 0.f);
		}
		else
		{
			UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playlist: %s ignored, nothing queued"), bPlay ? TEXT("play") : TEXT("pause"));
		}
		return;
	}
	if (bPlay == !PlaybackState.bPaused)
	{
		return;
	}

	if (bPlay)
	{
		PlaybackState.TrackStartServerTime = GetServerTime() - PlaybackState.PausedPosition;
		PlaybackState.bPaused = false;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: resumed at %.1f s"), PlaybackState.PausedPosition);
	}
	else
	{
		PlaybackState.PausedPosition = GetPlaybackPosition();
		PlaybackState.bPaused = true;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: paused at %.1f s"), PlaybackState.PausedPosition);
	}
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::Skip()
{
	BBP_REQUIRE_AUTHORITY()
	const double Now = GetServerTime();
	if (Now - LastSkipTime < SkipDebounceSeconds)
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playlist: duplicate skip ignored"));
		return;
	}
	LastSkipTime = Now;
	Advance(false);
}

void ABBPPlaylistSubsystem::Previous()
{
	BBP_REQUIRE_AUTHORITY()
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	if (GetPlaybackPosition() > PreviousRestartThreshold || PlayHistory.Num() == 0)
	{
		SeekTo(0.f);
		return;
	}
	const int32 PreviousEntryId = PlayHistory.Pop();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: back to entry %d"), PreviousEntryId);
	const int32 CurrentBefore = PlaybackState.CurrentEntryId;
	StartEntry(PreviousEntryId, 0.f);
	// StartEntry pushed the track we just left; going back should not be undone by the next Previous.
	if (PlayHistory.Num() > 0 && PlayHistory.Last() == CurrentBefore)
	{
		PlayHistory.Pop();
	}
}

bool ABBPPlaylistSubsystem::PlayEntry(int32 EntryId)
{
	BBP_REQUIRE_AUTHORITY(false)
	if (FindEntryIndex(EntryId) == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: play failed, no entry %d"), EntryId);
		return false;
	}
	StartEntry(EntryId, 0.f);
	return true;
}

void ABBPPlaylistSubsystem::SeekTo(float PositionSeconds)
{
	BBP_REQUIRE_AUTHORITY()
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	PositionSeconds = FMath::Max(0.f, PositionSeconds);
	if (PlaybackState.bPaused)
	{
		PlaybackState.PausedPosition = PositionSeconds;
	}
	else
	{
		PlaybackState.TrackStartServerTime = GetServerTime() - PositionSeconds;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: seek to %.1f s"), PositionSeconds);
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::SetShuffle(bool bEnabled)
{
	BBP_REQUIRE_AUTHORITY()
	if (PlaybackState.bShuffle == bEnabled)
	{
		return;
	}
	PlaybackState.bShuffle = bEnabled;
	ShufflePlayed.Reset();
	if (PlaybackState.CurrentEntryId != INDEX_NONE)
	{
		ShufflePlayed.Add(PlaybackState.CurrentEntryId);
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: shuffle %s"), bEnabled ? TEXT("on") : TEXT("off"));
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::SetRepeatMode(EBBPRepeatMode Mode)
{
	BBP_REQUIRE_AUTHORITY()
	PlaybackState.RepeatMode = Mode;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: repeat mode %d"), (int32)Mode);
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::StartEntry(int32 EntryId, float StartPosition)
{
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: StartEntry given unknown entry %d"), EntryId);
		return;
	}
	if (PlaybackState.CurrentEntryId != INDEX_NONE && PlaybackState.CurrentEntryId != EntryId)
	{
		PlayHistory.Add(PlaybackState.CurrentEntryId);
	}
	PlaybackState.CurrentEntryId = EntryId;
	PlaybackState.bPaused = false;
	PlaybackState.PausedPosition = StartPosition;
	PlaybackState.TrackStartServerTime = GetServerTime() - StartPosition;
	ShufflePlayed.Add(EntryId);
	bWarnedMissingDuration = false;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: now playing '%s' - '%s' (entry %d, %.1f s)"),
		*Queue[Index].Track.Artist, *Queue[Index].Track.Title, EntryId, Queue[Index].Track.Duration);
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::StopPlayback()
{
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: stopped"));
	PlaybackState.CurrentEntryId = INDEX_NONE;
	PlaybackState.bPaused = true;
	PlaybackState.PausedPosition = 0.f;
	MarkPlaybackChanged();
}

void ABBPPlaylistSubsystem::Advance(bool bAutomatic)
{
	const int32 NextEntryIdToPlay = ChooseNextEntry(bAutomatic);
	if (NextEntryIdToPlay == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: end of queue"));
		StopPlayback();
		return;
	}
	StartEntry(NextEntryIdToPlay, 0.f);
}

int32 ABBPPlaylistSubsystem::ChooseNextEntry(bool bAutomatic)
{
	if (Queue.Num() == 0)
	{
		return INDEX_NONE;
	}
	const int32 CurrentId = PlaybackState.CurrentEntryId;

	if (bAutomatic && PlaybackState.RepeatMode == EBBPRepeatMode::One && CurrentId != INDEX_NONE)
	{
		return CurrentId;
	}

	if (PlaybackState.bShuffle)
	{
		TArray<int32> Candidates;
		for (const FBBPQueueEntry& Entry : Queue)
		{
			if (Entry.EntryId != CurrentId && !ShufflePlayed.Contains(Entry.EntryId))
			{
				Candidates.Add(Entry.EntryId);
			}
		}
		if (Candidates.Num() == 0)
		{
			if (PlaybackState.RepeatMode == EBBPRepeatMode::Off)
			{
				return INDEX_NONE;
			}
			ShufflePlayed.Reset();
			for (const FBBPQueueEntry& Entry : Queue)
			{
				if (Entry.EntryId != CurrentId || Queue.Num() == 1)
				{
					Candidates.Add(Entry.EntryId);
				}
			}
		}
		return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
	}

	const int32 NextIndex = FindEntryIndex(CurrentId) + 1;
	if (Queue.IsValidIndex(NextIndex))
	{
		return Queue[NextIndex].EntryId;
	}
	return PlaybackState.RepeatMode == EBBPRepeatMode::Off ? INDEX_NONE : Queue[0].EntryId;
}

void ABBPPlaylistSubsystem::RefreshActiveBoomBoxes()
{
	TArray<FBBPActiveBoomBox> Found;
	for (TActorIterator<AFGBoomBoxPlayer> It(GetWorld()); It; ++It)
	{
		if (UBBPCustomMusicTape::IsCustomMusicTape(It->GetCurrentTape()))
		{
			FBBPActiveBoomBox& Active = Found.AddDefaulted_GetRef();
			Active.BoomBox = *It;
			Active.Volume = It->GetmState().mVolume;
		}
	}
	if (Found != ActiveBoomBoxes)
	{
		if (Found.Num() != ActiveBoomBoxes.Num())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %d Boom Box(es) now have Custom Music loaded"), Found.Num());
		}
		ActiveBoomBoxes = MoveTemp(Found);
		ForceNetUpdate();
	}
}

void ABBPPlaylistSubsystem::MarkQueueChanged()
{
	ForceNetUpdate();
	OnQueueChanged.Broadcast();
}

void ABBPPlaylistSubsystem::MarkPlaybackChanged()
{
	++PlaybackState.Revision;
	ForceNetUpdate();
	OnPlaybackChanged.Broadcast();
}

void ABBPPlaylistSubsystem::OnRep_Queue()
{
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playlist (client): queue replicated, %d entries"), Queue.Num());
	OnQueueChanged.Broadcast();
}

void ABBPPlaylistSubsystem::OnRep_PlaybackState()
{
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Playlist (client): playback state rev %d, entry %d, %s"),
		PlaybackState.Revision, PlaybackState.CurrentEntryId, PlaybackState.bPaused ? TEXT("paused") : TEXT("playing"));
	OnPlaybackChanged.Broadcast();
}

#undef BBP_REQUIRE_AUTHORITY
