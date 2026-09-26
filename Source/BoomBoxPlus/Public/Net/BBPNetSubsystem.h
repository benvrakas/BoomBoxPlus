#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPNetSubsystem.generated.h"

class AFGBoomBoxPlayer;
class UBBPLibrarySubsystem;
struct FBBPMatchJob;
struct FBBPWantedTrack;

// Kinds of links the search box understands.
enum class EBBPLinkKind : uint8
{
	None,
	YouTubeVideo,
	YouTubePlaylist,
	SoundCloudTrack,
	SoundCloudSet,
	SpotifyTrack,
	SpotifyCollection,
	// Any other http(s) address, treated as an internet radio stream (YouTube live links are YouTubeVideo).
	Stream
};

DECLARE_DELEGATE_TwoParams(FBBPOnNetTracks, const TArray<FBBPTrack>& /*Tracks*/, const FString& /*Error*/);
DECLARE_DELEGATE_TwoParams(FBBPOnNetText, const FString& /*Text*/, const FString& /*Error*/);
DECLARE_DELEGATE_TwoParams(FBBPOnRadioTuned, const FBBPTrack& /*Station*/, const FString& /*Error*/);

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

	// Information for the player that isn't an error, e.g. that only part of the playlist could be read.
	FString Note;
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

// Radio stations this player tuned in to recently, written between sessions.
USTRUCT()
struct FBBPRadioStations
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FBBPTrack> Stations;
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

	// Returns the bundled ffmpeg, which plays radio streams, or empty if it is missing.
	FString GetFfmpegPath() const { return bFfmpegAvailable ? FfmpegDir / TEXT("ffmpeg.exe") : FString(); }

	// Returns the bundled yt-dlp, which finds YouTube live streams, or empty if the tools are missing.
	FString GetYtDlpPath() const { return bToolsAvailable ? YtDlpPath : FString(); }

	// Checks that Text is a playable radio stream (following .pls/.m3u station playlists) and reads the station's name.
	// YouTube live links aren't handled here: they go through ResolveLink like any YouTube link.
	// Calls back on the game thread with a queueable track, or a full-sentence error for the player.
	void TuneRadio(const FString& Text, FBBPOnRadioTuned OnDone);

	// Stations tuned in to recently, newest first.
	const TArray<FBBPTrack>& GetRecentStations() const { return RadioStations.Stations; }

	// Moves Station to the top of the recent stations and saves the list.
	void RememberStation(const FBBPTrack& Station);

	// How many recent stations are kept (the music page has a button for each).
	static constexpr int32 MaxRecentStations = 4;

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

	// Downloads a network track if it isn't cached yet, then makes it available in the library. bFirst puts it ahead of
	// the other waiting downloads (for a track that is playing now).
	void EnsureDownloaded(const FBBPTrack& Track, bool bFirst = false);

	// Records that a downloaded track just started playing, so the cache deletes ones played longer ago first.
	void MarkPlayed(const FString& TrackId);

	// Drops queued downloads (not the one in progress) whose track id isn't in WantedIds.
	void SetWantedDownloads(const TSet<FString>& WantedIds);

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

	// Game thread. Starts the YouTube matching workers for a job's track list.
	void BeginMatching(int32 JobId, TArray<FBBPWantedTrack>&& Wanted, const FString& Note);

	// Game thread. Ends a job that couldn't read its playlist.
	void FailJob(int32 JobId, const FString& Error);

	// Records a finished download and evicts old cache entries.
	void FinishDownload(const FBBPTrack& Track, const FString& FilePath, const FString& Error);

	// Deletes least-recently-used downloads beyond the configured limit, keeping tracks in the current queue.
	void EvictOldDownloads();

	void SaveCache() const;
	void SaveRadioStations() const;
	FString GetRadioStationsPath() const;

	// Game thread. Runs ffmpeg briefly on a stream URL to confirm it plays and read its station name.
	void ProbeRadio(const FString& Url, FBBPOnRadioTuned OnDone);
	FString GetCacheDir() const;
	UBBPLibrarySubsystem* GetLibrary() const;

	FString YtDlpPath;
	FString FfmpegDir;
	bool bToolsAvailable = false;
	bool bFfmpegAvailable = false;

	FBBPRadioStations RadioStations;

	FBBPNetCache Cache;
	TArray<FBBPTrack> DownloadQueue;
	FString DownloadingId;

	// Tracks whose last download failed. Prefetching skips them; only starting to play one tries again.
	TSet<FString> FailedDownloads;

	TMap<int32, TSharedPtr<FBBPMatchJob>> MatchJobs;
	int32 NextJobId = 1;
};
