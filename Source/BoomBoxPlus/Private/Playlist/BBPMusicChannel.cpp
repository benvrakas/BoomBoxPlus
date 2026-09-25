#include "Playlist/BBPMusicChannel.h"
#include "BoomBoxPlus.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Net/UnrealNetwork.h"

// Returns early (with the given value) and logs when a server-only function is called on a client.
#define BBP_REQUIRE_AUTHORITY(ReturnValue) \
	if (!HasAuthority()) \
	{ \
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Channel %04d: %s called without authority; ignored"), LinkCode, ANSI_TO_TCHAR(__FUNCTION__)); \
		return ReturnValue; \
	}

namespace
{
	constexpr int32 MaxQueueLength = 1000;
	constexpr float PreviousRestartThreshold = 3.f;
	constexpr double SkipDebounceSeconds = 0.25;

	// With more than one player, a track waits at most this long for everyone to load it.
	constexpr double MultiplayerLoadGraceSeconds = 10.0;

	// A load wait ends if no Boom Box has played the channel for this long (nobody to wait for).
	constexpr double NobodyListeningGraceSeconds = 3.0;
}

ABBPMusicChannel::ABBPMusicChannel()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ABBPMusicChannel::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABBPMusicChannel, Queue);
	DOREPLIFETIME(ABBPMusicChannel, PlaybackState);
	DOREPLIFETIME(ABBPMusicChannel, LinkCode);
	DOREPLIFETIME(ABBPMusicChannel, Members);
}

void ABBPMusicChannel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return;
	}
	UpdateLoading();
	FBBPQueueEntry Current;
	if (IsPlaying() && GetCurrentEntry(Current))
	{
		if (Current.Track.IsLive())
		{
			// Radio plays until someone skips it.
		}
		else if (Current.Track.Duration <= 0.f)
		{
			if (!bWarnedMissingDuration)
			{
				bWarnedMissingDuration = true;
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: '%s' has no duration; it will not advance on its own"), LinkCode, *Current.Track.Title);
			}
		}
		else if (GetPlaybackPosition() >= Current.Track.Duration)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: '%s' finished"), LinkCode, *Current.Track.Title);
			Advance(true);
		}
	}
}

double ABBPMusicChannel::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float ABBPMusicChannel::GetPlaybackPosition() const
{
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return 0.f;
	}
	if (PlaybackState.bPaused || PlaybackState.bLoading)
	{
		return PlaybackState.PausedPosition;
	}
	return FMath::Max(0.f, (float)(GetServerTime() - PlaybackState.TrackStartServerTime));
}

bool ABBPMusicChannel::GetCurrentEntry(FBBPQueueEntry& OutEntry) const
{
	const int32 Index = FindEntryIndex(PlaybackState.CurrentEntryId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	OutEntry = Queue[Index];
	return true;
}

bool ABBPMusicChannel::HasMember(const AFGBoomBoxPlayer* BoomBox) const
{
	return BoomBox && Members.Contains(BoomBox);
}

int32 ABBPMusicChannel::FindEntryIndex(int32 EntryId) const
{
	if (EntryId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	return Queue.IndexOfByPredicate([EntryId](const FBBPQueueEntry& Entry) { return Entry.EntryId == EntryId; });
}

bool ABBPMusicChannel::AddTrack(const FBBPTrack& Track, bool bFront, const FString& AddedBy)
{
	BBP_REQUIRE_AUTHORITY(false)
	if (!Track.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: rejected track with no id from %s"), LinkCode, *AddedBy);
		return false;
	}
	if (Queue.Num() >= MaxQueueLength)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: queue full (%d); rejected '%s' from %s"), LinkCode, MaxQueueLength, *Track.Title, *AddedBy);
		return false;
	}

	FBBPQueueEntry Entry;
	Entry.EntryId = NextEntryId++;
	Entry.Track = Track;
	Entry.AddedBy = AddedBy;

	const int32 CurrentIndex = FindEntryIndex(PlaybackState.CurrentEntryId);
	const int32 InsertIndex = bFront ? CurrentIndex + 1 : Queue.Num();
	Queue.Insert(Entry, InsertIndex);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: %s added '%s' (entry %d) at %s, position %d of %d"),
		LinkCode, *AddedBy, *Track.Title, Entry.EntryId, bFront ? TEXT("front") : TEXT("end"), InsertIndex + 1, Queue.Num());
	MarkQueueChanged();

	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		StartEntry(Entry.EntryId, 0.f);
	}
	return true;
}

bool ABBPMusicChannel::PlayTrackNow(const FBBPTrack& Track, const FString& AddedBy)
{
	BBP_REQUIRE_AUTHORITY(false)
	const int32 NewEntryId = NextEntryId;
	if (!AddTrack(Track, true, AddedBy))
	{
		return false;
	}
	if (PlaybackState.CurrentEntryId != NewEntryId)
	{
		StartEntry(NewEntryId, 0.f);
	}
	return true;
}

bool ABBPMusicChannel::RemoveEntry(int32 EntryId)
{
	BBP_REQUIRE_AUTHORITY(false)
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: remove failed, no entry %d"), LinkCode, EntryId);
		return false;
	}

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: removing '%s' (entry %d)"), LinkCode, *Queue[Index].Track.Title, EntryId);
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

bool ABBPMusicChannel::MoveEntry(int32 EntryId, int32 NewIndex)
{
	BBP_REQUIRE_AUTHORITY(false)
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: move failed, no entry %d"), LinkCode, EntryId);
		return false;
	}
	const FBBPQueueEntry Entry = Queue[Index];
	Queue.RemoveAt(Index);
	NewIndex = FMath::Clamp(NewIndex, 0, Queue.Num());
	Queue.Insert(Entry, NewIndex);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: moved '%s' (entry %d) from %d to %d"), LinkCode, *Entry.Track.Title, EntryId, Index, NewIndex);
	MarkQueueChanged();
	return true;
}

void ABBPMusicChannel::ClearQueue()
{
	BBP_REQUIRE_AUTHORITY()
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: clearing %d entries"), LinkCode, Queue.Num());
	Queue.Empty();
	PlayHistory.Empty();
	ShufflePlayed.Empty();
	StopPlayback();
	MarkQueueChanged();
}

void ABBPMusicChannel::SetPlaying(bool bPlay)
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
			UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d: %s ignored, nothing queued"), LinkCode, bPlay ? TEXT("play") : TEXT("pause"));
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
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: resumed at %.1f s"), LinkCode, PlaybackState.PausedPosition);
	}
	else
	{
		PlaybackState.PausedPosition = GetPlaybackPosition();
		PlaybackState.bPaused = true;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: paused at %.1f s"), LinkCode, PlaybackState.PausedPosition);
	}
	MarkPlaybackChanged();
}

void ABBPMusicChannel::Skip()
{
	BBP_REQUIRE_AUTHORITY()
	const double Now = GetServerTime();
	if (Now - LastSkipTime < SkipDebounceSeconds)
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d: duplicate skip ignored"), LinkCode);
		return;
	}
	LastSkipTime = Now;
	// Next on the last song does nothing unless the queue repeats; the song keeps playing.
	const int32 NextEntry = ChooseNextEntry(false);
	if (NextEntry == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: next ignored, already at the last song"), LinkCode);
		return;
	}
	StartEntry(NextEntry, 0.f);
}

void ABBPMusicChannel::Previous()
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
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: back to entry %d"), LinkCode, PreviousEntryId);
	const int32 CurrentBefore = PlaybackState.CurrentEntryId;
	StartEntry(PreviousEntryId, 0.f);
	// StartEntry pushed the track we just left; going back should not be undone by the next Previous.
	if (PlayHistory.Num() > 0 && PlayHistory.Last() == CurrentBefore)
	{
		PlayHistory.Pop();
	}
}

bool ABBPMusicChannel::PlayEntry(int32 EntryId)
{
	BBP_REQUIRE_AUTHORITY(false)
	if (FindEntryIndex(EntryId) == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Channel %04d: play failed, no entry %d"), LinkCode, EntryId);
		return false;
	}
	StartEntry(EntryId, 0.f);
	return true;
}

void ABBPMusicChannel::SeekTo(float PositionSeconds)
{
	BBP_REQUIRE_AUTHORITY()
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	PositionSeconds = FMath::Max(0.f, PositionSeconds);
	if (PlaybackState.bPaused || PlaybackState.bLoading)
	{
		PlaybackState.PausedPosition = PositionSeconds;
	}
	else
	{
		PlaybackState.TrackStartServerTime = GetServerTime() - PositionSeconds;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: seek to %.1f s"), LinkCode, PositionSeconds);
	MarkPlaybackChanged();
}

void ABBPMusicChannel::SetShuffle(bool bEnabled)
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
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: shuffle %s"), LinkCode, bEnabled ? TEXT("on") : TEXT("off"));
	MarkPlaybackChanged();
}

void ABBPMusicChannel::SetRepeatMode(EBBPRepeatMode Mode)
{
	BBP_REQUIRE_AUTHORITY()
	PlaybackState.RepeatMode = Mode;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: repeat mode %d"), LinkCode, (int32)Mode);
	MarkPlaybackChanged();
}

FBBPSavedChannel ABBPMusicChannel::MakeSaveData() const
{
	FBBPSavedChannel Saved;
	Saved.Queue = Queue;
	Saved.CurrentEntryId = PlaybackState.CurrentEntryId;
	Saved.Position = GetPlaybackPosition();
	Saved.bPaused = PlaybackState.bPaused;
	Saved.bShuffle = PlaybackState.bShuffle;
	Saved.RepeatMode = PlaybackState.RepeatMode;
	return Saved;
}

void ABBPMusicChannel::RestoreFrom(const FBBPSavedChannel& Saved)
{
	BBP_REQUIRE_AUTHORITY()
	Queue = Saved.Queue;
	if (Queue.Num() > MaxQueueLength)
	{
		Queue.SetNum(MaxQueueLength);
	}
	NextEntryId = 1;
	for (const FBBPQueueEntry& Entry : Queue)
	{
		NextEntryId = FMath::Max(NextEntryId, Entry.EntryId + 1);
	}
	PlayHistory.Reset();
	ShufflePlayed.Reset();
	PlaybackState.bShuffle = Saved.bShuffle;
	PlaybackState.RepeatMode = Saved.RepeatMode;
	MarkQueueChanged();

	const int32 Index = FindEntryIndex(Saved.CurrentEntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: restored %d saved track(s), nothing current"), LinkCode, Queue.Num());
		MarkPlaybackChanged();
		return;
	}
	const FBBPTrack& Track = Queue[Index].Track;
	const float Position = Track.IsLive() ? 0.f
		: FMath::Clamp(Saved.Position, 0.f, Track.Duration > 0.f ? FMath::Max(0.f, Track.Duration - 1.f) : Saved.Position);
	StartEntry(Saved.CurrentEntryId, Position);
	if (Saved.bPaused)
	{
		PlaybackState.bPaused = true;
		PlaybackState.bLoading = false;
		PlaybackState.PausedPosition = Position;
		MarkPlaybackChanged();
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: restored %d saved track(s); '%s' %s at %.1f s"), LinkCode, Queue.Num(),
		*Track.Title, Saved.bPaused ? TEXT("paused") : TEXT("playing"), Position);
}

void ABBPMusicChannel::CopyFrom(const ABBPMusicChannel& Other)
{
	BBP_REQUIRE_AUTHORITY()
	Queue = Other.Queue;
	PlaybackState = Other.PlaybackState;
	NextEntryId = Other.NextEntryId;
	PlayHistory = Other.PlayHistory;
	ShufflePlayed = Other.ShufflePlayed;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: copied %d entries and playback from channel %04d"), LinkCode, Queue.Num(), Other.LinkCode);
	MarkQueueChanged();
	MarkPlaybackChanged();
}

void ABBPMusicChannel::MergeFrom(const ABBPMusicChannel& Other)
{
	BBP_REQUIRE_AUTHORITY()

	// Other's tracks from its current one onward, then the ones it had already played.
	TArray<FBBPQueueEntry> Incoming;
	const int32 OtherStart = FMath::Max(0, Other.FindEntryIndex(Other.PlaybackState.CurrentEntryId));
	for (int32 i = OtherStart; i < Other.Queue.Num(); ++i)
	{
		Incoming.Add(Other.Queue[i]);
	}
	for (int32 i = 0; i < OtherStart; ++i)
	{
		Incoming.Add(Other.Queue[i]);
	}
	if (Incoming.Num() == 0)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: nothing to merge from channel %04d"), LinkCode, Other.LinkCode);
		return;
	}
	for (FBBPQueueEntry& Entry : Incoming)
	{
		Entry.EntryId = NextEntryId++;
	}

	// Everything up to and including the current track stays put; only what comes next is interleaved.
	const int32 CurrentIndex = FindEntryIndex(PlaybackState.CurrentEntryId);
	TArray<FBBPQueueEntry> Merged;
	for (int32 i = 0; i <= CurrentIndex; ++i)
	{
		Merged.Add(Queue[i]);
	}
	const int32 UpcomingStart = CurrentIndex + 1;
	const int32 UpcomingCount = Queue.Num() - UpcomingStart;
	for (int32 k = 0; k < FMath::Max(UpcomingCount, Incoming.Num()) && Merged.Num() < MaxQueueLength; ++k)
	{
		if (k < UpcomingCount)
		{
			Merged.Add(Queue[UpcomingStart + k]);
		}
		if (k < Incoming.Num() && Merged.Num() < MaxQueueLength)
		{
			Merged.Add(Incoming[k]);
		}
	}
	const int32 Dropped = Queue.Num() + Incoming.Num() - Merged.Num();
	Queue = MoveTemp(Merged);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: zipper-merged %d track(s) from channel %04d with %d upcoming; queue now %d%s"),
		LinkCode, Incoming.Num(), Other.LinkCode, UpcomingCount, Queue.Num(), Dropped > 0 ? TEXT(" (queue full, some dropped)") : TEXT(""));
	MarkQueueChanged();

	if (PlaybackState.CurrentEntryId == INDEX_NONE && Other.IsPlaying())
	{
		// Other was already partway through this song; picking it up here should continue it, not restart it.
		StartEntry(Incoming[0].EntryId, Other.GetPlaybackPosition());
	}
}

void ABBPMusicChannel::SetLinkCode(int32 Code)
{
	BBP_REQUIRE_AUTHORITY()
	LinkCode = Code;
	MarkMembersChanged();
}

void ABBPMusicChannel::AddMember(AFGBoomBoxPlayer* BoomBox)
{
	BBP_REQUIRE_AUTHORITY()
	if (BoomBox && !Members.Contains(BoomBox))
	{
		Members.Add(BoomBox);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: %s joined (%d Boom Box(es))"), LinkCode, *GetNameSafe(BoomBox), Members.Num());
		MarkMembersChanged();
	}
}

void ABBPMusicChannel::RemoveMember(const AFGBoomBoxPlayer* BoomBox)
{
	BBP_REQUIRE_AUTHORITY()
	if (Members.Remove(const_cast<AFGBoomBoxPlayer*>(BoomBox)) > 0)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: %s left (%d Boom Box(es))"), LinkCode, *GetNameSafe(BoomBox), Members.Num());
		MarkMembersChanged();
	}
}

void ABBPMusicChannel::PruneMembers()
{
	BBP_REQUIRE_AUTHORITY()
	const int32 Removed = Members.RemoveAll([](const TObjectPtr<AFGBoomBoxPlayer>& BoomBox) { return !IsValid(BoomBox); });
	if (Removed > 0)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: %d Boom Box(es) no longer exist; %d left"), LinkCode, Removed, Members.Num());
		MarkMembersChanged();
	}
}

void ABBPMusicChannel::StartEntry(int32 EntryId, float StartPosition)
{
	const int32 Index = FindEntryIndex(EntryId);
	if (Index == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Channel %04d: StartEntry given unknown entry %d"), LinkCode, EntryId);
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
	// Radio is live: there's nothing to load ahead, and waiting would only make players hear it later.
	PlaybackState.bLoading = !Queue[Index].Track.IsLive();
	if (PlaybackState.bLoading)
	{
		++PlaybackState.LoadGeneration;
		LoadedPlayers.Reset();
		LoadStartTime = GetServerTime();
		NobodyListeningSince = -1.0;
	}
	ShufflePlayed.Add(EntryId);
	bWarnedMissingDuration = false;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: now playing '%s' - '%s' (entry %d, %.1f s)"),
		LinkCode, *Queue[Index].Track.Artist, *Queue[Index].Track.Title, EntryId, Queue[Index].Track.Duration);
	MarkPlaybackChanged();
}

void ABBPMusicChannel::ReportLoaded(const APlayerState* Player, int32 LoadGeneration)
{
	BBP_REQUIRE_AUTHORITY()
	if (!Player || !PlaybackState.bLoading || LoadGeneration != PlaybackState.LoadGeneration)
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d: stale load report from %s (generation %d, current %d%s)"), LinkCode,
			Player ? *Player->GetPlayerName() : TEXT("?"), LoadGeneration, PlaybackState.LoadGeneration, PlaybackState.bLoading ? TEXT("") : TEXT(", not loading"));
		return;
	}
	LoadedPlayers.Add(Player);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: %s has the track loaded (%.1f s after it started loading)"), LinkCode,
		*Player->GetPlayerName(), GetServerTime() - LoadStartTime);
	UpdateLoading();
}

void ABBPMusicChannel::UpdateLoading()
{
	if (!PlaybackState.bLoading)
	{
		return;
	}
	const double Now = GetServerTime();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	int32 NumPlayers = 0;
	int32 NumLoaded = 0;
	if (GameState)
	{
		for (const APlayerState* Player : GameState->PlayerArray)
		{
			if (Player && !Player->IsInactive())
			{
				++NumPlayers;
				NumLoaded += LoadedPlayers.Contains(Player) ? 1 : 0;
			}
		}
	}
	if (IsHeard())
	{
		NobodyListeningSince = -1.0;
	}
	else if (NobodyListeningSince < 0.0)
	{
		NobodyListeningSince = Now;
	}

	// A single player always waits for their own download; with more, one slow player can't hold everyone for long.
	const TCHAR* Reason = nullptr;
	if (NumLoaded >= NumPlayers)
	{
		Reason = TEXT("everyone has it loaded");
	}
	else if (NobodyListeningSince >= 0.0 && Now - NobodyListeningSince >= NobodyListeningGraceSeconds)
	{
		Reason = TEXT("no Boom Box is playing this channel");
	}
	else if (NumPlayers > 1 && Now - LoadStartTime >= MultiplayerLoadGraceSeconds)
	{
		Reason = TEXT("the grace period ran out");
	}
	if (!Reason)
	{
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: starting the track after %.1f s of loading; %s (%d of %d players loaded)"),
		LinkCode, Now - LoadStartTime, Reason, NumLoaded, NumPlayers);
	PlaybackState.bLoading = false;
	PlaybackState.TrackStartServerTime = Now - PlaybackState.PausedPosition;
	MarkPlaybackChanged();
}

bool ABBPMusicChannel::IsHeard() const
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!Playlist)
	{
		return false;
	}
	return Playlist->GetActiveBoomBoxes().ContainsByPredicate([this](const FBBPActiveBoomBox& Active) { return Members.Contains(Active.BoomBox); });
}

void ABBPMusicChannel::StopPlayback()
{
	if (PlaybackState.CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: stopped"), LinkCode);
	PlaybackState.CurrentEntryId = INDEX_NONE;
	PlaybackState.bPaused = true;
	PlaybackState.bLoading = false;
	PlaybackState.PausedPosition = 0.f;
	MarkPlaybackChanged();
}

void ABBPMusicChannel::Advance(bool bAutomatic)
{
	const int32 NextEntryIdToPlay = ChooseNextEntry(bAutomatic);
	if (NextEntryIdToPlay == INDEX_NONE)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Channel %04d: end of queue"), LinkCode);
		StopPlayback();
		return;
	}
	StartEntry(NextEntryIdToPlay, 0.f);
}

int32 ABBPMusicChannel::ChooseNextEntry(bool bAutomatic)
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
			// Only "repeat all" starts a new shuffled round.
			if (PlaybackState.RepeatMode != EBBPRepeatMode::All)
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
	// Only "repeat all" goes from the last song back to the first.
	return PlaybackState.RepeatMode == EBBPRepeatMode::All ? Queue[0].EntryId : INDEX_NONE;
}

void ABBPMusicChannel::MarkQueueChanged()
{
	ForceNetUpdate();
	OnQueueChanged.Broadcast();
}

void ABBPMusicChannel::MarkPlaybackChanged()
{
	++PlaybackState.Revision;
	ForceNetUpdate();
	OnPlaybackChanged.Broadcast();
}

void ABBPMusicChannel::MarkMembersChanged()
{
	ForceNetUpdate();
	OnMembersChanged.Broadcast();
}

void ABBPMusicChannel::OnRep_Queue()
{
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d (client): queue replicated, %d entries"), LinkCode, Queue.Num());
	OnQueueChanged.Broadcast();
}

void ABBPMusicChannel::OnRep_PlaybackState()
{
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d (client): playback state rev %d, entry %d, %s"),
		LinkCode, PlaybackState.Revision, PlaybackState.CurrentEntryId, PlaybackState.bPaused ? TEXT("paused") : TEXT("playing"));
	OnPlaybackChanged.Broadcast();
}

void ABBPMusicChannel::OnRep_Members()
{
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Channel %04d (client): %d Boom Box(es)"), LinkCode, Members.Num());
	OnMembersChanged.Broadcast();
}

#undef BBP_REQUIRE_AUTHORITY
