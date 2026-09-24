#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPNetSubsystem.generated.h"

class UBBPLibrarySubsystem;

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

	// Finds a YouTube match for every track of a Spotify playlist or album. Calls back on the game thread.
	void ResolveSpotifyCollection(const FString& Url, FBBPOnNetTracks OnDone);

	// Downloads a network track if it isn't cached yet, then makes it available in the library.
	void EnsureDownloaded(const FBBPTrack& Track);

	// Returns true while Track is queued for download or downloading.
	bool IsDownloadPending(const FString& TrackId) const;

private:
	// Downloads the next queued track, if any and none is in progress.
	void StartNextDownload();

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
};
