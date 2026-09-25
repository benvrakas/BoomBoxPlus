#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"

class FBBPStreamState;

// Plays an internet radio stream: runs the bundled ffmpeg to connect and convert the station to 48 kHz stereo PCM,
// and feeds its output into an FBBPStreamState. Restarts ffmpeg a few times if the station drops.
// For a YouTube live page, yt-dlp first looks up the broadcast's stream address (again on every restart, since it expires).
class FBBPLiveStreamWorker : public FRunnable
{
public:
	FBBPLiveStreamWorker(TSharedRef<FBBPStreamState, ESPMode::ThreadSafe> InState, FString InFfmpegPath, FString InYtDlpPath, FString InUrl);

	virtual uint32 Run() override;
	virtual void Stop() override;

private:
	// Starts ffmpeg with its stdout and stderr piped to this worker. Returns false if it couldn't be launched.
	bool Launch();

	// Returns the address ffmpeg should open: Url itself, or for a YouTube page the live stream yt-dlp finds (empty on failure).
	FString ResolveStreamUrl();

	// Kills ffmpeg if it is still running and closes the pipes.
	void Shutdown();

	// Moves whatever ffmpeg has written to stdout into the ring buffer. Returns true if any audio arrived.
	bool PumpAudio();

	// Reads ffmpeg's log output, picking up the station name and song titles.
	void PumpLog();

	TSharedRef<FBBPStreamState, ESPMode::ThreadSafe> State;
	FString FfmpegPath;
	FString YtDlpPath;
	FString Url;

	FProcHandle Process;
	void* StdOutRead = nullptr;
	void* StdOutWrite = nullptr;
	void* StdErrRead = nullptr;
	void* StdErrWrite = nullptr;

	// Bytes read from ffmpeg that don't fit in the ring yet (or are part of an incomplete frame).
	TArray<uint8> Pending;

	// Unfinished last line of ffmpeg's log output.
	FString LogCarry;

	// Recent log lines, reported if ffmpeg exits.
	TArray<FString> RecentLog;

	FString StationName;
	FString SongTitle;
};
