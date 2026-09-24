#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPLyricsSubsystem.generated.h"

// One timed lyric line.
struct FBBPLyricLine
{
	float Time = 0.f;
	FString Text;
};

// Lyrics for one track: synced lines, or none if the track has no lyrics.
struct FBBPLyrics
{
	TArray<FBBPLyricLine> Lines;
};

// Finds synced lyrics for tracks: a .lrc file next to a local track, then LRCLIB. Results are cached.
UCLASS()
class BOOMBOXPLUS_API UBBPLyricsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Starts loading lyrics for Track if they aren't loaded or loading already. LocalFilePath may be empty.
	void RequestLyrics(const FBBPTrack& Track, const FString& LocalFilePath);

	// Returns the line being sung at PositionSeconds, or an empty string if there is none.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Lyrics")
	FString GetLineAt(const FString& TrackId, float PositionSeconds) const;

	// Returns true if lyrics for TrackId were found.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Lyrics")
	bool HasLyrics(const FString& TrackId) const;

	// Parses LRC text into time-sorted lines.
	static FBBPLyrics ParseLrc(const FString& LrcText);

private:
	// Queries LRCLIB for Track.
	void FetchFromLrclib(const FBBPTrack& Track);

	// Stores lyrics for a track and writes them to the disk cache when they came from the network.
	void StoreLyrics(const FString& TrackId, FBBPLyrics&& Lyrics, const FString& LrcTextToCache);

	FString GetCachePath(const FString& TrackId) const;

	TMap<FString, FBBPLyrics> LyricsByTrack;
	TSet<FString> PendingTracks;
};
