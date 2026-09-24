#include "Audio/BBPStreamingSoundWave.h"
#include "Audio/BBPDecoder.h"
#include "BoomBoxPlus.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include <atomic>

namespace
{
	constexpr float BufferAheadSeconds = 1.0f;
	constexpr int32 DecodeChunkFrames = 2048;
	constexpr int32 MaxOutputChannels = 2;
	constexpr uint32 IdleWaitMs = 10;
}

// Audio shared between the game thread, the decode thread and the audio render thread.
class FBBPStreamState
{
public:
	FBBPStreamState(int32 InSampleRate, int32 InChannels, int64 InStartFrame)
		: SampleRate(InSampleRate)
		, Channels(InChannels)
		, PlaybackFrame(InStartFrame)
		, PendingSeekFrame(InStartFrame)
	{
		Ring.SetNumZeroed(FMath::CeilToInt(BufferAheadSeconds * InSampleRate) * InChannels);
		WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
	}

	~FBBPStreamState()
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
	}

	// Returns the number of samples that can be written without overwriting unplayed audio.
	int32 GetFreeSamples()
	{
		FScopeLock ScopeLock(&Lock);
		return Ring.Num() - NumBuffered;
	}

	// Appends interleaved samples after the buffered audio.
	void Write(const int16* Src, int32 NumSamples)
	{
		FScopeLock ScopeLock(&Lock);
		const int32 Capacity = Ring.Num();
		NumSamples = FMath::Min(NumSamples, Capacity - NumBuffered);
		int32 WriteIndex = (ReadIndex + NumBuffered) % Capacity;
		for (int32 Remaining = NumSamples; Remaining > 0;)
		{
			const int32 Span = FMath::Min(Remaining, Capacity - WriteIndex);
			FMemory::Memcpy(Ring.GetData() + WriteIndex, Src, Span * sizeof(int16));
			Src += Span;
			Remaining -= Span;
			WriteIndex = (WriteIndex + Span) % Capacity;
		}
		NumBuffered += NumSamples;
	}

	// Moves up to NumSamples buffered samples (whole frames only) into Dst and returns how many were moved.
	int32 Read(int16* Dst, int32 NumSamples)
	{
		FScopeLock ScopeLock(&Lock);
		const int32 Capacity = Ring.Num();
		const int32 ToRead = (FMath::Min(NumSamples, NumBuffered) / Channels) * Channels;
		for (int32 Remaining = ToRead; Remaining > 0;)
		{
			const int32 Span = FMath::Min(Remaining, Capacity - ReadIndex);
			FMemory::Memcpy(Dst, Ring.GetData() + ReadIndex, Span * sizeof(int16));
			Dst += Span;
			Remaining -= Span;
			ReadIndex = (ReadIndex + Span) % Capacity;
		}
		NumBuffered -= ToRead;
		PlaybackFrame += ToRead / Channels;
		return ToRead;
	}

	// Discards buffered audio and sets the file frame that playback continues from.
	void Reset(int64 NewPlaybackFrame)
	{
		FScopeLock ScopeLock(&Lock);
		ReadIndex = 0;
		NumBuffered = 0;
		PlaybackFrame = NewPlaybackFrame;
	}

	// Returns true when the decoder has reached the end and nothing is left to play.
	bool IsDrained()
	{
		FScopeLock ScopeLock(&Lock);
		return bEndOfStream && NumBuffered == 0;
	}

	const int32 SampleRate;
	const int32 Channels;

	// File frame index of the next frame handed to the mixer.
	std::atomic<int64> PlaybackFrame;

	// Frame to seek to on the decode thread, or -1 if none is pending.
	std::atomic<int64> PendingSeekFrame;

	std::atomic<bool> bStopRequested{false};
	std::atomic<bool> bEndOfStream{false};
	std::atomic<bool> bFailed{false};

	// Wakes the decode thread early for seeks and stop requests.
	FEvent* WakeEvent = nullptr;

private:
	FCriticalSection Lock;
	TArray<int16> Ring;
	int32 ReadIndex = 0;
	int32 NumBuffered = 0;
};

// Decodes a file into an FBBPStreamState, keeping BufferAheadSeconds of audio ready.
class FBBPDecodeWorker : public FRunnable
{
public:
	FBBPDecodeWorker(TSharedRef<FBBPStreamState, ESPMode::ThreadSafe> InState, FString InFilePath)
		: State(InState)
		, FilePath(MoveTemp(InFilePath))
	{
	}

	virtual uint32 Run() override
	{
		FBBPDecoder Decoder;
		if (!OpenDecoder(Decoder))
		{
			State->bFailed = true;
			return 0;
		}

		const int32 FileChannels = Decoder.GetChannels();
		const int32 OutChannels = State->Channels;
		TArray<int16> Scratch;
		Scratch.SetNumUninitialized(DecodeChunkFrames * FileChannels);

		while (!State->bStopRequested)
		{
			const int64 SeekFrame = State->PendingSeekFrame.exchange(-1);
			if (SeekFrame >= 0)
			{
				const bool bSeeked = Decoder.SeekToFrame((uint64)SeekFrame);
				State->Reset(SeekFrame);
				State->bEndOfStream = !bSeeked;
				continue;
			}

			if (State->bEndOfStream || State->GetFreeSamples() < DecodeChunkFrames * OutChannels)
			{
				State->WakeEvent->Wait(IdleWaitMs);
				continue;
			}

			const int32 Frames = Decoder.ReadFrames(Scratch.GetData(), DecodeChunkFrames);
			if (Frames <= 0)
			{
				State->bEndOfStream = true;
				continue;
			}

			// Keeps only the first OutChannels of each frame.
			if (FileChannels > OutChannels)
			{
				for (int32 Frame = 0; Frame < Frames; ++Frame)
				{
					for (int32 Ch = 0; Ch < OutChannels; ++Ch)
					{
						Scratch[Frame * OutChannels + Ch] = Scratch[Frame * FileChannels + Ch];
					}
				}
			}

			State->Write(Scratch.GetData(), Frames * OutChannels);
		}
		return 0;
	}

	virtual void Stop() override
	{
		State->bStopRequested = true;
		State->WakeEvent->Trigger();
	}

private:
	// Loads the file and opens it, rejecting files whose format differs from what the stream was configured for.
	bool OpenDecoder(FBBPDecoder& Decoder)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Could not read '%s'"), *FilePath);
			return false;
		}
		if (!Decoder.Open(MoveTemp(Bytes)))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Could not decode '%s'"), *FilePath);
			return false;
		}
		if (Decoder.GetSampleRate() != State->SampleRate || FMath::Min(Decoder.GetChannels(), MaxOutputChannels) != State->Channels)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("'%s' changed format since it was scanned"), *FilePath);
			return false;
		}
		return true;
	}

	TSharedRef<FBBPStreamState, ESPMode::ThreadSafe> State;
	FString FilePath;
};

UBBPStreamingSoundWave::UBBPStreamingSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	bCanProcessAsync = true;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
}

bool UBBPStreamingSoundWave::StartStream(const FString& FilePath, int32 InSampleRate, int32 InChannels, float StartSeconds)
{
	if (State.IsValid() || InSampleRate <= 0 || InChannels <= 0)
	{
		return false;
	}

	const int32 OutChannels = FMath::Min(InChannels, MaxOutputChannels);
	NumChannels = OutChannels;
	SetSampleRate((uint32)InSampleRate);

	const int64 StartFrame = FMath::Max<int64>(0, (int64)(StartSeconds * InSampleRate));
	State = MakeShared<FBBPStreamState, ESPMode::ThreadSafe>(InSampleRate, OutChannels, StartFrame);
	Worker = new FBBPDecodeWorker(State.ToSharedRef(), FilePath);
	WorkerThread = FRunnableThread::Create(Worker, TEXT("BBPDecode"), 0, TPri_AboveNormal);
	return WorkerThread != nullptr;
}

void UBBPStreamingSoundWave::Seek(float PositionSeconds)
{
	if (State.IsValid())
	{
		State->PendingSeekFrame = FMath::Max<int64>(0, (int64)(PositionSeconds * State->SampleRate));
		State->WakeEvent->Trigger();
	}
}

void UBBPStreamingSoundWave::StopStream()
{
	if (WorkerThread)
	{
		WorkerThread->Kill(true);
		delete WorkerThread;
		WorkerThread = nullptr;
	}
	delete Worker;
	Worker = nullptr;
	if (State.IsValid())
	{
		State->Reset(State->PlaybackFrame);
	}
}

float UBBPStreamingSoundWave::GetPlaybackSeconds() const
{
	return State.IsValid() ? (float)State->PlaybackFrame / (float)State->SampleRate : 0.f;
}

bool UBBPStreamingSoundWave::IsFinished() const
{
	return State.IsValid() && State->IsDrained();
}

bool UBBPStreamingSoundWave::HasFailed() const
{
	return State.IsValid() && State->bFailed;
}

int32 UBBPStreamingSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * sizeof(int16), EAllowShrinking::No);
	int16* Dst = reinterpret_cast<int16*>(OutAudio.GetData());
	const int32 Read = State.IsValid() ? State->Read(Dst, NumSamples) : 0;
	if (Read < NumSamples)
	{
		FMemory::Memzero(Dst + Read, (NumSamples - Read) * sizeof(int16));
	}
	return NumSamples;
}

Audio::EAudioMixerStreamDataFormat::Type UBBPStreamingSoundWave::GetGeneratedPCMDataFormat() const
{
	return Audio::EAudioMixerStreamDataFormat::Int16;
}

void UBBPStreamingSoundWave::BeginDestroy()
{
	StopStream();
	Super::BeginDestroy();
}
