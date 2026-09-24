#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPNetSubsystem.generated.h"

class AFGBoomBoxPlayer;
class UBBPLibrarySubsystem;
struct FBBPMatchJob;

// Kinds of links the search box understands.
enum class EBBPLinkKind : uint8
{
	None,
	YouTubeVideo,
	YouTubePlaylist,
	SoundCloudTrack,
	SoundCloudSet,
	SpotifyTrack,
	SpotifyCollection
};

DECLARE_DELEGATE_TwoParams(FBBPOnNetTracks, const TArray<FBBPTrack>& /*Tracks*/, const FString& /*Error*/);
DECLARE_DELEGATE_TwoParams(FBBPOnNetText, const FString& /*Text*/, const FString& /*Error*/);

// Progress of matching a Spotify playlist or album on YouTube.
struct FBBPMatchProgress
{
	int32 JobId = 0;

	// Matches so far, in playlist order. Only grows from the front: track N appears once tracks 0..N are settled.
	TArray<FBBPTrack> Matched;

	// Playlist tracks settled (matched or not found), counted from the start.
	int32 Processed = 0;
	int32 Total = 0;
	bool bFinished = false;
	FString Error;
};

DECLARE_DELEGATE_OneParam(FBBPOnMatchProgress, const FBBPMatchProgress&);

// A downloaded network track on disk.
USTRUCT()
struct FBBPNetCacheEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FBBPTrack Track;

	UPROPERTY()
	FString FilePath;

	UPROPERTY()
	int64 LastUsedTicks = 0;
};

// Download cache index written between sessions.
USTRUCT()
struct FBBPNetCache
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 0;

	UPROPERTY()
	TArray<FBBPNetCacheEntry> Entries;
};

// YouTube/SoundCloud search and downloads through the bundled yt-dlp and ffmpeg, and Spotify link resolution.
UCLASS()
class BOOMBOXPLUS_API UBBPNetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// Returns true if the bundled yt-dlp and ffmpeg were found.
	bool AreToolsAvailable() const { return bToolsAvailable; }

	// Identifies a supported link in the search box text.
	static EBBPLinkKind ClassifyLink(const FString& Text);

	// Searches YouTube and SoundCloud. Calls back on the game thread.
	void Search(const FString& Query, FBBPOnNetTracks OnDone);

	// Lists the track(s) behind a YouTube or SoundCloud link. Calls back on the game thread.
	void ResolveLink(const FString& Url, FBBPOnNetTracks OnDone);

	// Turns a Spotify track link into "Title Artist" search text. Calls back on the game thread.
	void ResolveSpotifyTrack(const FString& Url, FBBPOnNetText OnDone);

	// Starts matching every track of a Spotify playlist or album on YouTube in the background, several at a time.
	// OnProgress is called on the game thread as matches arrive (in playlist order) and once more when finished.
	// Returns the job id.
	int32 StartSpotifyCollection(const FString& Url, FBBPOnMatchProgress OnProgress);

	// Queues every match of a running job into BoomBox's queue now, and each later match as it arrives, in playlist order.
	// Keeps going after the caller goes away. Returns false if the job has already finished.
	bool QueueJobInto(int32 JobId, AFGBoomBoxPlayer* BoomBox);

	// Stops a running job unless it is queueing into a Boom Box.
	void CancelJob(int32 JobId);

	// Downloads a network track if it isn't cached yet, then makes it available in the library.
	void EnsureDownloaded(const FBBPTrack& Track);

	// Returns true while Track is queued for download or downloading.
	bool IsDownloadPending(const FString& TrackId) const;

private:
	// Downloads the next queued track, if any and none is in progress.
	void StartNextDownload();

	// Game thread. Records one job result (a match, or none for track Index) and publishes any newly settled prefix.
	void HandleMatchResult(int32 JobId, int32 Index, const TOptional<FBBPTrack>& Match);

	// Game thread. Marks a job finished once all its workers have stopped.
	void HandleMatchFinished(int32 JobId);

	// Sends matches the job hasn't queued yet to its Boom Box.
	void QueueNewMatches(FBBPMatchJob& Job);

	// Records a finished download and evicts old cache entries.
	void FinishDownload(const FBBPTrack& Track, const FString& FilePath, const FString& Error);

	// Deletes least-recently-used downloads beyond the configured limit, keeping tracks in the current queue.
	void EvictOldDownloads();

	void SaveCache() const;
	FString GetCacheDir() const;
	UBBPLibrarySubsystem* GetLibrary() const;

	FString YtDlpPath;
	FString FfmpegDir;
	bool bToolsAvailable = false;

	FBBPNetCache Cache;
	TArray<FBBPTrack> DownloadQueue;
	FString DownloadingId;

	TMap<int32, TSharedPtr<FBBPMatchJob>> MatchJobs;
	int32 NextJobId = 1;
};
