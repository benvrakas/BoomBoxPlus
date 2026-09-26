#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Library/BBPTrack.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "BBPBlueprintLibrary.generated.h"

class ABBPMusicChannel;
class AFGBoomBoxPlayer;
class UBBPRemoteCallObject;

// Row state of a queue entry as seen by the local player.
UENUM(BlueprintType)
enum class EBBPEntryAvailability : uint8
{
	Playing,
	Queued,
	NotInLibrary
};

// The queue actions available to UI and hooks, each aimed at one Boom Box. Requests go to the server through the local player's RCO.
UCLASS()
class BOOMBOXPLUS_API UBBPBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestAddTrack(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront);

	// Tells the server this player has Channel's current track ready (or can't play it), ending its load wait for this player.
	static void ReportTrackLoaded(ABBPMusicChannel* Channel, int32 LoadGeneration);

	// Adds a track right after BoomBox's current one and starts playing it.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestPlayTrackNow(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track);

	// Adds tracks to the end of BoomBox's queue in order, sent in small batches.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestAddTracks(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestRemoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestMoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestClearQueue(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestSetPlaying(AFGBoomBoxPlayer* BoomBox, bool bPlay);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestTogglePlaying(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestSkip(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestPrevious(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestPlayEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestSeekTo(AFGBoomBoxPlayer* BoomBox, float PositionSeconds);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestSetShuffle(AFGBoomBoxPlayer* BoomBox, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestSetRepeatMode(AFGBoomBoxPlayer* BoomBox, EBBPRepeatMode Mode);

	// Links BoomBox to the Boom Box whose 4-digit code is Code, so both share one queue.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestLinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code);

	// Gives BoomBox its own queue again, starting from a copy of the shared one.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus")
	static void RequestUnlinkBoomBox(AFGBoomBoxPlayer* BoomBox);

	// Asks the server to give BoomBox a channel (and link code) if it has none yet.
	static void RequestChannel(AFGBoomBoxPlayer* BoomBox);

	// Returns the channel (queue and transport) BoomBox plays, or null if it has none yet.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static ABBPMusicChannel* GetChannel(const AFGBoomBoxPlayer* BoomBox);

	// Returns how a queue entry of Channel should be shown to the local player.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static EBBPEntryAvailability GetEntryAvailability(const ABBPMusicChannel* Channel, const FBBPQueueEntry& Entry);

	// Returns the lyric line for Channel's current track at its current position, or an empty string.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static FString GetCurrentLyricLine(const ABBPMusicChannel* Channel);

	// Formats seconds as m:ss.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static FString FormatDuration(float Seconds);

	// Where Track comes from, as "Kind: name" ("YouTube: Monstercat", "Radio: <station>"), or just the kind when
	// the name isn't known.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static FString GetSourceName(const FBBPTrack& Track);

private:
	// Returns the local player's RCO, logging why if it isn't available.
	static UBBPRemoteCallObject* GetLocalRCO(const UObject* WorldContext, const TCHAR* RequestName);
};
