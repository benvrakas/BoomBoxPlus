#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "BBPStreamingSoundWave.generated.h"

class FBBPStreamState;
class FRunnable;
class FRunnableThread;

// Procedural sound wave that plays a music file decoded on a background thread, or a live internet radio stream.
UCLASS()
class BOOMBOXPLUS_API UBBPStreamingSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UBBPStreamingSoundWave(const FObjectInitializer& ObjectInitializer);

	// Begins decoding FilePath from StartSeconds. SampleRate and Channels must match the file.
	bool StartStream(const FString& FilePath, int32 InSampleRate, int32 InChannels, float StartSeconds);

	// Begins playing the internet radio stream at Url through the bundled ffmpeg (48 kHz stereo); YouTube live pages are
	// looked up with yt-dlp first. Live streams can't seek.
	bool StartLiveStream(const FString& FfmpegPath, const FString& YtDlpPath, const FString& Url);

	// Continues the stream from PositionSeconds. Does nothing for live streams.
	void Seek(float PositionSeconds);

	// Returns true for a live internet radio stream.
	bool IsLive() const { return bLive; }

	// Returns true while playback waits for enough audio to arrive (live streams only).
	bool IsBuffering() const;

	// Returns the song title the radio station currently announces, or empty.
	FString GetLiveTitle() const;

	// Stops decoding and discards buffered audio.
	void StopStream();

	// Returns the file position of the next audio handed to the mixer, in seconds (live streams: seconds played).
	float GetPlaybackSeconds() const;

	// Returns true once the file has been fully decoded and all of it has been played.
	bool IsFinished() const;

	// Returns true if the file could not be opened or did not match the expected format.
	bool HasFailed() const;

	// Returns how many audio callbacks ran out of decoded audio since the stream started (a live stream refilling its prebuffer does not count).
	int32 GetUnderrunCount() const;

	//~ Begin USoundWaveProcedural interface
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;
	virtual Audio::EAudioMixerStreamDataFormat::Type GetGeneratedPCMDataFormat() const override;
	//~ End USoundWaveProcedural interface

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	// File being streamed, for log messages.
	FString StreamPath;

	TSharedPtr<FBBPStreamState, ESPMode::ThreadSafe> State;
	FRunnable* Worker = nullptr;
	bool bLive = false;
	FRunnableThread* WorkerThread = nullptr;
};
