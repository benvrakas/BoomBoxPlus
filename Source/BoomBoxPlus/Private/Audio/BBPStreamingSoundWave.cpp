#include "Audio/BBPStreamingSoundWave.h"
#include "Audio/BBPDecoder.h"
#include "Audio/BBPLiveStreamWorker.h"
#include "Audio/BBPStreamState.h"
#include "BoomBoxPlus.h"
#include "Net/BBPRadio.h"
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

	// Live streams keep more audio in hand, and wait for LivePrebufferSeconds of it before playing, to ride out network hiccups.
	constexpr float LiveBufferSeconds = 6.0f;
	constexpr float LivePrebufferSeconds = 2.0f;
}

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

		UE_LOG(LogBoomBoxPlus, Log, TEXT("Decode thread opened '%s': %d Hz, %d ch, %.1f s"), *FilePath,
			Decoder.GetSampleRate(), Decoder.GetChannels(), Decoder.GetDurationSeconds());

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
				if (!bSeeked)
				{
					UE_LOG(LogBoomBoxPlus, Warning, TEXT("Seek to frame %lld failed in '%s' (file has %llu frames); treating as end of track"),
						SeekFrame, *FilePath, Decoder.GetTotalFrames());
				}
				UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Decode thread seeked '%s' to %.2f s"), *FilePath, (double)SeekFrame / State->SampleRate);
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
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Decode thread reached end of '%s'"), *FilePath);
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
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Could not decode '%s': %s"), *FilePath, *Decoder.GetLastError());
			return false;
		}
		if (Decoder.GetSampleRate() != State->SampleRate || FMath::Min(Decoder.GetChannels(), MaxOutputChannels) != State->Channels)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("'%s' changed format since it was scanned (expected %d Hz/%d ch, file is %d Hz/%d ch); rescan the library"),
				*FilePath, State->SampleRate, State->Channels, Decoder.GetSampleRate(), Decoder.GetChannels());
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
	if (State.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("StartStream called twice on the same wave ('%s' then '%s'); create a new wave per track"), *StreamPath, *FilePath);
		return false;
	}
	if (InSampleRate <= 0 || InChannels <= 0)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("StartStream('%s') given invalid format: %d Hz, %d ch"), *FilePath, InSampleRate, InChannels);
		return false;
	}
	StreamPath = FilePath;

	const int32 OutChannels = FMath::Min(InChannels, MaxOutputChannels);
	NumChannels = OutChannels;
	SetSampleRate((uint32)InSampleRate);

	const int64 StartFrame = FMath::Max<int64>(0, (int64)(StartSeconds * InSampleRate));
	State = MakeShared<FBBPStreamState, ESPMode::ThreadSafe>(InSampleRate, OutChannels, StartFrame, BufferAheadSeconds, 0.f);
	Worker = new FBBPDecodeWorker(State.ToSharedRef(), FilePath);
	WorkerThread = FRunnableThread::Create(Worker, TEXT("BBPDecode"), 0, TPri_AboveNormal);
	if (!WorkerThread)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Could not create decode thread for '%s'"), *FilePath);
		State->bFailed = true;
		return false;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Streaming '%s' from %.2f s (%d Hz, %d ch out)"), *FilePath, StartSeconds, InSampleRate, OutChannels);
	return true;
}

bool UBBPStreamingSoundWave::StartLiveStream(const FString& FfmpegPath, const FString& YtDlpPath, const FString& Url)
{
	if (State.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("StartLiveStream called on a wave that is already streaming ('%s' then '%s')"), *StreamPath, *Url);
		return false;
	}
	if (!BBPRadio::IsStreamUrl(Url))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("StartLiveStream refused '%s': not an http(s) stream URL"), *Url);
		return false;
	}
	StreamPath = Url;
	bLive = true;
	NumChannels = BBPRadio::Channels;
	SetSampleRate((uint32)BBPRadio::SampleRate);

	State = MakeShared<FBBPStreamState, ESPMode::ThreadSafe>(BBPRadio::SampleRate, BBPRadio::Channels, 0, LiveBufferSeconds, LivePrebufferSeconds);
	State->PendingSeekFrame = -1;
	Worker = new FBBPLiveStreamWorker(State.ToSharedRef(), FfmpegPath, YtDlpPath, Url);
	WorkerThread = FRunnableThread::Create(Worker, TEXT("BBPRadio"), 0, TPri_AboveNormal);
	if (!WorkerThread)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Could not create radio thread for '%s'"), *Url);
		State->bFailed = true;
		return false;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Streaming radio '%s'"), *Url);
	return true;
}

void UBBPStreamingSoundWave::Seek(float PositionSeconds)
{
	if (bLive)
	{
		return;
	}
	if (!State.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Seek(%.2f) on a wave that was never started"), PositionSeconds);
		return;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Seek '%s' to %.2f s (was at %.2f s)"), *StreamPath, PositionSeconds, GetPlaybackSeconds());
	State->PendingSeekFrame = FMath::Max<int64>(0, (int64)(PositionSeconds * State->SampleRate));
	State->WakeEvent->Trigger();
}

void UBBPStreamingSoundWave::StopStream()
{
	if (WorkerThread)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Stopping stream '%s' at %.2f s (%d underruns)"), *StreamPath, GetPlaybackSeconds(), GetUnderrunCount());
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

bool UBBPStreamingSoundWave::IsBuffering() const
{
	return State.IsValid() && State->IsBuffering();
}

FString UBBPStreamingSoundWave::GetLiveTitle() const
{
	return State.IsValid() ? State->GetLiveTitle() : FString();
}

int32 UBBPStreamingSoundWave::GetUnderrunCount() const
{
	return State.IsValid() ? State->UnderrunCount.load() : 0;
}

int32 UBBPStreamingSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * sizeof(int16), EAllowShrinking::No);
	int16* Dst = reinterpret_cast<int16*>(OutAudio.GetData());
	const int32 Read = State.IsValid() ? State->Read(Dst, NumSamples) : 0;
	if (Read < NumSamples)
	{
		FMemory::Memzero(Dst + Read, (NumSamples - Read) * sizeof(int16));
		if (State.IsValid() && !State->bEndOfStream && (Read > 0 || !State->IsBuffering()))
		{
			++State->UnderrunCount;
		}
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
