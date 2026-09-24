#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Library/BBPTrack.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "BBPBlueprintLibrary.generated.h"

class UBBPRemoteCallObject;

// Row state of a queue entry as seen by the local player.
UENUM(BlueprintType)
enum class EBBPEntryAvailability : uint8
{
	Playing,
	Queued,
	NotInLibrary
};

// The playlist actions available to UI and hooks. Requests go to the server through the local player's RCO.
UCLASS()
class BOOMBOXPLUS_API UBBPBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestAddTrack(const UObject* WorldContext, const FBBPTrack& Track, bool bFront);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestRemoveEntry(const UObject* WorldContext, int32 EntryId);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestMoveEntry(const UObject* WorldContext, int32 EntryId, int32 NewIndex);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestClearQueue(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestSetPlaying(const UObject* WorldContext, bool bPlay);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestTogglePlaying(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestSkip(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestPrevious(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestPlayEntry(const UObject* WorldContext, int32 EntryId);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestSeekTo(const UObject* WorldContext, float PositionSeconds);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestSetShuffle(const UObject* WorldContext, bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static void RequestSetRepeatMode(const UObject* WorldContext, EBBPRepeatMode Mode);

	// Returns how a queue entry should be shown to the local player.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus", meta = (WorldContext = "WorldContext"))
	static EBBPEntryAvailability GetEntryAvailability(const UObject* WorldContext, const FBBPQueueEntry& Entry);

	// Formats seconds as m:ss.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus")
	static FString FormatDuration(float Seconds);

private:
	// Returns the local player's RCO, logging why if it isn't available.
	static UBBPRemoteCallObject* GetLocalRCO(const UObject* WorldContext, const TCHAR* RequestName);
};
