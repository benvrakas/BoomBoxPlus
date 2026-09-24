#pragma once

#include "CoreMinimal.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "Subsystem/ModSubsystem.h"
#include "BBPPlaylistSubsystem.generated.h"

class AFGBoomBoxPlayer;
class UBBPPlaybackController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBBPOnQueueChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBBPOnPlaybackChanged);

// The session's shared music queue and transport. The server owns it; every client receives a replicated copy.
UCLASS()
class BOOMBOXPLUS_API ABBPPlaylistSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ABBPPlaylistSubsystem();

	// Returns the subsystem for the world WorldContext belongs to, or null if it hasn't spawned yet.
	static ABBPPlaylistSubsystem* Get(const UObject* WorldContext);

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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

	// Boom Boxes that currently have the Custom Music tape loaded.
	const TArray<FBBPActiveBoomBox>& GetActiveBoomBoxes() const { return ActiveBoomBoxes; }

	// Returns the server's clock, in seconds.
	double GetServerTime() const;

	// Returns this machine's audio playback, or null on a dedicated server.
	UBBPPlaybackController* GetPlaybackController() const { return PlaybackController; }

	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Playlist")
	FBBPOnQueueChanged OnQueueChanged;

	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Playlist")
	FBBPOnPlaybackChanged OnPlaybackChanged;

private:
	UFUNCTION()
	void OnRep_Queue();

	UFUNCTION()
	void OnRep_PlaybackState();

	// Server only. Makes EntryId the current track, starting at StartPosition.
	void StartEntry(int32 EntryId, float StartPosition);

	// Server only. Stops playback and clears the current entry.
	void StopPlayback();

	// Server only. Picks and starts the next track. bAutomatic is true when the previous track ended by itself.
	void Advance(bool bAutomatic);

	// Server only. Returns the entry to play after the current one, or INDEX_NONE if playback should stop.
	int32 ChooseNextEntry(bool bAutomatic);

	// Server only. Rebuilds ActiveBoomBoxes from the Boom Boxes in the world.
	void RefreshActiveBoomBoxes();

	// Notifies listeners and pushes the change to clients.
	void MarkQueueChanged();
	void MarkPlaybackChanged();

	int32 FindEntryIndex(int32 EntryId) const;

	UPROPERTY(ReplicatedUsing = OnRep_Queue)
	TArray<FBBPQueueEntry> Queue;

	UPROPERTY(ReplicatedUsing = OnRep_PlaybackState)
	FBBPPlaybackState PlaybackState;

	UPROPERTY(Replicated)
	TArray<FBBPActiveBoomBox> ActiveBoomBoxes;

	// Plays audio on this machine; null on dedicated servers.
	UPROPERTY(Transient)
	TObjectPtr<UBBPPlaybackController> PlaybackController;

	// Server only.
	int32 NextEntryId = 1;
	TArray<int32> PlayHistory;
	TSet<int32> ShufflePlayed;
	float ActiveBoomBoxRefreshTimer = 0.f;
	double LastSkipTime = -1.0;
	bool bWarnedMissingDuration = false;
};
