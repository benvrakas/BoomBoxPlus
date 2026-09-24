#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "BBPMusicChannel.generated.h"

class AFGBoomBoxPlayer;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBBPOnChannelChanged);

// One queue and transport, played in sync by every Boom Box linked to it. The server owns it; clients get a replicated copy.
UCLASS(NotPlaceable, Transient)
class BOOMBOXPLUS_API ABBPMusicChannel : public AInfo
{
	GENERATED_BODY()

public:
	ABBPMusicChannel();

	//~ Begin AActor interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	// Server only. Adds a track right after the current one (bFront) or at the end. Starts playback if nothing is playing.
	bool AddTrack(const FBBPTrack& Track, bool bFront, const FString& AddedBy);

	// Server only. Removes a queue entry; skips ahead if it is playing.
	bool RemoveEntry(int32 EntryId);

	// Server only. Moves a queue entry to NewIndex.
	bool MoveEntry(int32 EntryId, int32 NewIndex);

	// Server only. Empties the queue and stops playback.
	void ClearQueue();

	// Server only. Resumes (true) or pauses (false) the current track.
	void SetPlaying(bool bPlay);

	// Server only. Moves to the next track.
	void Skip();

	// Server only. Restarts the current track, or goes to the previous one if near its start.
	void Previous();

	// Server only. Starts playing a specific queue entry from the beginning.
	bool PlayEntry(int32 EntryId);

	// Server only. Jumps to a position in the current track.
	void SeekTo(float PositionSeconds);

	// Server only.
	void SetShuffle(bool bEnabled);

	// Server only.
	void SetRepeatMode(EBBPRepeatMode Mode);

	// Server only. Takes over another channel's queue and playback, so a Boom Box leaving it keeps playing the same music.
	void CopyFrom(const ABBPMusicChannel& Other);

	// Server only. Interleaves Other's queue with this one's upcoming tracks (A1, B1, A2, B2...), for Boom Boxes linking up.
	void MergeFrom(const ABBPMusicChannel& Other);

	// Server only. Sets the code other Boom Boxes enter to link to this channel.
	void SetLinkCode(int32 Code);

	// Server only. Adds or removes a Boom Box that plays this channel.
	void AddMember(AFGBoomBoxPlayer* BoomBox);
	void RemoveMember(const AFGBoomBoxPlayer* BoomBox);

	// Server only. Drops Boom Boxes that no longer exist.
	void PruneMembers();

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	const TArray<FBBPQueueEntry>& GetQueue() const { return Queue; }

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	const FBBPPlaybackState& GetPlaybackState() const { return PlaybackState; }

	// Returns the entry being played. False if nothing is playing.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	bool GetCurrentEntry(FBBPQueueEntry& OutEntry) const;

	// Returns where the current track should be right now, in seconds, from the server's clock.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	float GetPlaybackPosition() const;

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	bool IsPlaying() const { return PlaybackState.CurrentEntryId != INDEX_NONE && !PlaybackState.bPaused; }

	// The 4-digit code that links another Boom Box to this channel.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	int32 GetLinkCode() const { return LinkCode; }

	// Boom Boxes playing this channel. Entries can be null on clients for Boom Boxes that aren't replicated to them.
	const TArray<TObjectPtr<AFGBoomBoxPlayer>>& GetMembers() const { return Members; }

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Playlist")
	bool HasMember(const AFGBoomBoxPlayer* BoomBox) const;

	// Returns the server's clock, in seconds.
	double GetServerTime() const;

	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Playlist")
	FBBPOnChannelChanged OnQueueChanged;

	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Playlist")
	FBBPOnChannelChanged OnPlaybackChanged;

	// Fires when the link code or the linked Boom Boxes change.
	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Playlist")
	FBBPOnChannelChanged OnMembersChanged;

private:
	UFUNCTION()
	void OnRep_Queue();

	UFUNCTION()
	void OnRep_PlaybackState();

	UFUNCTION()
	void OnRep_Members();

	// Server only. Makes EntryId the current track, starting at StartPosition.
	void StartEntry(int32 EntryId, float StartPosition);

	// Server only. Stops playback and clears the current entry.
	void StopPlayback();

	// Server only. Picks and starts the next track. bAutomatic is true when the previous track ended by itself.
	void Advance(bool bAutomatic);

	// Server only. Returns the entry to play after the current one, or INDEX_NONE if playback should stop.
	int32 ChooseNextEntry(bool bAutomatic);

	// Notifies listeners and pushes the change to clients.
	void MarkQueueChanged();
	void MarkPlaybackChanged();
	void MarkMembersChanged();

	int32 FindEntryIndex(int32 EntryId) const;

	UPROPERTY(ReplicatedUsing = OnRep_Queue)
	TArray<FBBPQueueEntry> Queue;

	UPROPERTY(ReplicatedUsing = OnRep_PlaybackState)
	FBBPPlaybackState PlaybackState;

	UPROPERTY(ReplicatedUsing = OnRep_Members)
	int32 LinkCode = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Members)
	TArray<TObjectPtr<AFGBoomBoxPlayer>> Members;

	// Server only.
	int32 NextEntryId = 1;
	TArray<int32> PlayHistory;
	TSet<int32> ShufflePlayed;
	double LastSkipTime = -1.0;
	bool bWarnedMissingDuration = false;
};
