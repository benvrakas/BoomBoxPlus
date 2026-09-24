#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "BBPPlaylistTypes.generated.h"

class AFGBoomBoxPlayer;

// What happens when the current track ends.
UENUM(BlueprintType)
enum class EBBPRepeatMode : uint8
{
	Off,
	All,
	One
};

// One item in the shared queue.
USTRUCT(BlueprintType)
struct BOOMBOXPLUS_API FBBPQueueEntry
{
	GENERATED_BODY()

	// Unique within the session; identifies this queue item even if the same track is queued twice.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	int32 EntryId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FBBPTrack Track;

	// Name of the player who queued it.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FString AddedBy;
};

// A Boom Box that has the Custom Music tape loaded.
USTRUCT(BlueprintType)
struct BOOMBOXPLUS_API FBBPActiveBoomBox
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	TObjectPtr<AFGBoomBoxPlayer> BoomBox = nullptr;

	// The Boom Box's own volume setting (0..1), read from the server's copy of its state.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	float Volume = 1.f;

	bool operator==(const FBBPActiveBoomBox& Other) const { return BoomBox == Other.BoomBox && Volume == Other.Volume; }
};

// Transport state shared by every player. Replicated as one unit so clients never see a half-applied change.
USTRUCT(BlueprintType)
struct BOOMBOXPLUS_API FBBPPlaybackState
{
	GENERATED_BODY()

	// Queue entry being played, or INDEX_NONE when stopped.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	int32 CurrentEntryId = INDEX_NONE;

	// Server world time at which the current track's position was 0.
	UPROPERTY()
	double TrackStartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	bool bPaused = true;

	// Track position in seconds while paused.
	UPROPERTY()
	float PausedPosition = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	bool bShuffle = false;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	EBBPRepeatMode RepeatMode = EBBPRepeatMode::Off;

	// Increments on every change so clients can tell a restart of the same track from no change.
	UPROPERTY()
	int32 Revision = 0;
};

// A Boom Box's request to link with another; completes when the other enters this one's code before it expires.
USTRUCT(BlueprintType)
struct BOOMBOXPLUS_API FBBPLinkRequest
{
	GENERATED_BODY()

	// Link code of the requesting channel.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	int32 FromCode = 0;

	// Link code the requester entered.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	int32 ToCode = 0;

	// Server time after which the request lapses.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	double ExpiresAt = 0.0;
};
