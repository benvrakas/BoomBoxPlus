#pragma once

#include "CoreMinimal.h"
#include "BBPTrack.generated.h"

// Where a track's audio comes from.
UENUM(BlueprintType)
enum class EBBPTrackSource : uint8
{
	Local,
	YouTube,
	SoundCloud,
	// A live internet radio stream: no length, never downloaded, plays until skipped.
	Radio
};

// A track as shared between players. Contains nothing specific to one machine.
USTRUCT(BlueprintType)
struct BOOMBOXPLUS_API FBBPTrack
{
	GENERATED_BODY()

	// Identifies the same audio on every machine: a content hash for local files, the source id for network tracks.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FString Id;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FString Title;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FString Artist;

	// Length in seconds; 0 for radio.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	float Duration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	EBBPTrackSource Source = EBBPTrackSource::Local;

	// Video or track reference used to fetch network tracks, or the stream URL for radio; empty for local files.
	UPROPERTY(BlueprintReadOnly, Category = "BoomBoxPlus")
	FString SourceRef;

	bool IsValid() const { return !Id.IsEmpty(); }

	bool IsLive() const { return Source == EBBPTrackSource::Radio; }
};

// A track available on this machine, with the details needed to play it.
USTRUCT()
struct BOOMBOXPLUS_API FBBPLocalTrack
{
	GENERATED_BODY()

	UPROPERTY()
	FBBPTrack Track;

	UPROPERTY()
	FString FilePath;

	UPROPERTY()
	int32 SampleRate = 0;

	UPROPERTY()
	int32 Channels = 0;

	// File size and modification time the entry was built from, used to reuse cached scan results.
	UPROPERTY()
	int64 FileSize = 0;

	UPROPERTY()
	int64 ModifiedTicks = 0;
};
