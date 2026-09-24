#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include "BBPStreamingSoundWave.generated.h"

class FBBPStreamState;
class FBBPDecodeWorker;
class FRunnableThread;

// Procedural sound wave that plays a music file decoded on a background thread.
UCLASS()
class BOOMBOXPLUS_API UBBPStreamingSoundWave : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UBBPStreamingSoundWave(const FObjectInitializer& ObjectInitializer);

	// Begins decoding FilePath from StartSeconds. SampleRate and Channels must match the file.
	bool StartStream(const FString& FilePath, int32 InSampleRate, int32 InChannels, float StartSeconds);

	// Continues the stream from PositionSeconds.
	void Seek(float PositionSeconds);

	// Stops decoding and discards buffered audio.
	void StopStream();

	// Returns the file position of the next audio handed to the mixer, in seconds.
	float GetPlaybackSeconds() const;

	// Returns true once the file has been fully decoded and all of it has been played.
	bool IsFinished() const;

	// Returns true if the file could not be opened or did not match the expected format.
	bool HasFailed() const;

	// Returns how many audio callbacks ran out of decoded audio since the stream started.
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
	FBBPDecodeWorker* Worker = nullptr;
	FRunnableThread* WorkerThread = nullptr;
};
