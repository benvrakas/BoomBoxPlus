#pragma once

#include "CoreMinimal.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include <atomic>

// Audio shared between the game thread, the decode thread and the audio render thread.
class FBBPStreamState
{
public:
	// BufferSeconds sets how much decoded audio can wait in the ring. With PrebufferSeconds > 0, playback holds back
	// (silence) until that much is buffered, at the start and again after the buffer runs dry.
	FBBPStreamState(int32 InSampleRate, int32 InChannels, int64 InStartFrame, float BufferSeconds, float PrebufferSeconds)
		: SampleRate(InSampleRate)
		, Channels(InChannels)
		, PlaybackFrame(InStartFrame)
		, PendingSeekFrame(InStartFrame)
		, PrebufferSamples(FMath::CeilToInt(PrebufferSeconds * InSampleRate) * InChannels)
		, bBuffering(PrebufferSamples > 0)
	{
		Ring.SetNumZeroed(FMath::CeilToInt(BufferSeconds * InSampleRate) * InChannels);
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
		if (bBuffering)
		{
			if (NumBuffered < PrebufferSamples && !bEndOfStream)
			{
				return 0;
			}
			bBuffering = false;
		}
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
		if (PrebufferSamples > 0 && NumBuffered == 0 && !bEndOfStream)
		{
			bBuffering = true;
		}
		return ToRead;
	}

	// Discards buffered audio and sets the file frame that playback continues from.
	void Reset(int64 NewPlaybackFrame)
	{
		FScopeLock ScopeLock(&Lock);
		ReadIndex = 0;
		NumBuffered = 0;
		PlaybackFrame = NewPlaybackFrame;
		bBuffering = PrebufferSamples > 0;
	}

	// Returns true when the decoder has reached the end and nothing is left to play.
	bool IsDrained()
	{
		FScopeLock ScopeLock(&Lock);
		return bEndOfStream && NumBuffered == 0;
	}

	// Returns true while playback is held back waiting for the prebuffer to fill.
	bool IsBuffering()
	{
		FScopeLock ScopeLock(&Lock);
		return bBuffering;
	}

	// Live streams: the song title the station currently announces, and the station's own name.
	void SetLiveTitle(const FString& Title)
	{
		FScopeLock ScopeLock(&Lock);
		LiveTitle = Title;
	}
	FString GetLiveTitle()
	{
		FScopeLock ScopeLock(&Lock);
		return LiveTitle;
	}

	const int32 SampleRate;
	const int32 Channels;

	// File frame index of the next frame handed to the mixer (for live streams: frames played since connecting).
	std::atomic<int64> PlaybackFrame;

	// Frame to seek to on the decode thread, or -1 if none is pending.
	std::atomic<int64> PendingSeekFrame;

	std::atomic<int32> UnderrunCount{0};
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
	const int32 PrebufferSamples;
	bool bBuffering;
	FString LiveTitle;
};
