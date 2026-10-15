#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Library/BBPTrack.h"
#include "BBPSongHistory.generated.h"

class AFGBoomBoxPlayer;
class UBBPPlaybackController;

// One song in a Boom Box's history.
USTRUCT()
struct FBBPHistoryEntry
{
	GENERATED_BODY()

	// The queue entry's track: for a radio station, the station itself.
	UPROPERTY()
	FBBPTrack Track;

	// For a song a radio station announced: its title as announced ("Artist - Title"). Empty otherwise.
	UPROPERTY()
	FString RadioSong;

	// When it started playing, Unix seconds (UTC).
	UPROPERTY()
	int64 PlayedAt = 0;
};

USTRUCT()
struct FBBPHistoryList
{
	GENERATED_BODY()

	// Newest first.
	UPROPERTY()
	TArray<FBBPHistoryEntry> Entries;
};

// What's saved in Saved/BoomBoxPlus/History.json: each Boom Box's history, under the same key as its My Volume.
USTRUCT()
struct FBBPHistoryFile
{
	GENERATED_BODY()

	UPROPERTY()
	TMap<FString, FBBPHistoryList> BoomBoxes;
};

// The songs each Boom Box played, as this player's game saw them, including the songs radio stations announced. Kept
// on this machine only. Linked Boom Boxes get the same entries, so a group shares one history.
UCLASS()
class BOOMBOXPLUS_API UBBPSongHistory : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UBBPPlaybackController* InController);
	void Shutdown();
	void Tick(float DeltaSeconds);

	// BoomBox's history, newest first (empty if it has none).
	const TArray<FBBPHistoryEntry>& GetHistory(const AFGBoomBoxPlayer* BoomBox) const;

	// Goes up whenever any history changes, so pages know to refresh.
	int32 GetRevision() const { return Revision; }

	// Entries kept per Boom Box.
	static constexpr int32 MaxEntries = 100;

private:
	FString GetKey(const AFGBoomBoxPlayer* BoomBox) const;
	void Add(const FString& Key, const FBBPHistoryEntry& Entry);
	void Load();
	void Save();
	static FString GetPath();

	UPROPERTY()
	TObjectPtr<UBBPPlaybackController> Controller;

	FBBPHistoryFile History;

	// What each Boom Box (by key) last recorded: its queue entry, and the radio song the station last announced.
	struct FSeen
	{
		int32 EntryId = INDEX_NONE;
		FString RadioSong;
	};
	TMap<FString, FSeen> Seen;

	int32 Revision = 0;
	bool bDirty = false;
	float SaveTimer = 0.f;
	float CheckTimer = 0.f;
};
