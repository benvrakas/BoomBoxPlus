#pragma once

#include "CoreMinimal.h"
#include "Audio/BBPStreamState.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include <atomic>

// One live stream's audio on this machine, delivered to every sound wave playing it (the Boom Boxes of a group, or
// any Boom Boxes on the same station): each listener gets identical samples and is kept at the same playback point,
// so they play in step instead of each connecting and buffering on its own.
class FBBPLiveFanOut
{
public:
	FBBPLiveFanOut()
	{
		WakeEvent = FPlatformProcess::GetSynchEventFromPool(false);
	}

	~FBBPLiveFanOut()
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
	}

	// Adds a listener. It starts with a copy of what a playing listener has buffered, so it plays from the same point.
	void Subscribe(const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& State)
	{
		FScopeLock ScopeLock(&Lock);
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Other : Listeners)
		{
			if (!Other->IsBuffering())
			{
				State->CopyBufferFrom(*Other);
				break;
			}
		}
		State->SetLiveTitle(Title);
		if (bFinished)
		{
			State->bFailed = bFailed;
			State->bEndOfStream = true;
		}
		Listeners.Add(State);
	}

	void Unsubscribe(const FBBPStreamState* State)
	{
		FScopeLock ScopeLock(&Lock);
		Listeners.RemoveAll([State](const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener) { return &Listener.Get() == State; });
	}

	// Appends up to NumSamples to every listener and returns how many were taken: at most what the listener with the
	// most room can hold, so the rest waits in the worker. Listeners with less room (not being played) drop their oldest
	// audio to keep the newest. Then brings any that fell behind back in step.
	int32 Write(const int16* Src, int32 NumSamples)
	{
		FScopeLock ScopeLock(&Lock);
		if (Listeners.Num() == 0)
		{
			return NumSamples;
		}
		int32 Room = 0;
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			Room = FMath::Max(Room, Listener->GetFreeSamples());
		}
		NumSamples = FMath::Min(NumSamples, Room);
		NumSamples -= NumSamples % Listeners[0]->Channels;
		if (NumSamples <= 0)
		{
			return 0;
		}
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			Listener->WriteDroppingOldest(Src, NumSamples);
		}
		Align();
		return NumSamples;
	}

	void SetTitle(const FString& InTitle)
	{
		FScopeLock ScopeLock(&Lock);
		Title = InTitle;
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			Listener->SetLiveTitle(InTitle);
		}
	}

	// Marks the stream over for every listener (bInFailed: it never delivered audio).
	void Finish(bool bInFailed)
	{
		FScopeLock ScopeLock(&Lock);
		bFinished = true;
		bFailed = bInFailed;
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			Listener->bFailed = bInFailed;
			Listener->bEndOfStream = true;
		}
	}

	// Returns true once the worker has given up on the stream.
	bool IsFinished()
	{
		FScopeLock ScopeLock(&Lock);
		return bFinished;
	}

	std::atomic<bool> bStopRequested{false};

	// Wakes the worker early for stop requests.
	FEvent* WakeEvent = nullptr;

private:
	// Every listener gets the same writes, so the playing one with the least buffered is furthest ahead. A listener
	// nobody is playing (not read for StalledSeconds) more than StalledToleranceSamples behind it is trimmed to match.
	// Playing listeners are only trimmed beyond PlayingToleranceSamples: they differ by where each audio callback happens
	// to be and by each Boom Box's Doppler pitch, and trimming that would click.
	void Align()
	{
		const double Now = FPlatformTime::Seconds();
		auto IsStalled = [Now](const FBBPStreamState& Listener) { return Now - Listener.LastReadSeconds.load() > StalledSeconds; };
		int32 Least = MAX_int32;
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			if (!Listener->IsBuffering() && !IsStalled(*Listener))
			{
				Least = FMath::Min(Least, Listener->GetNumBuffered());
			}
		}
		if (Least == MAX_int32)
		{
			return;
		}
		for (const TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>& Listener : Listeners)
		{
			const int32 Tolerance = IsStalled(*Listener) ? StalledToleranceSamples : PlayingToleranceSamples;
			if (!Listener->IsBuffering() && Listener->GetNumBuffered() > Least + Tolerance)
			{
				Listener->TrimTo(Least);
			}
		}
	}

	static constexpr double StalledSeconds = 0.25;

	// 60 ms and 1 s of 48 kHz stereo.
	static constexpr int32 StalledToleranceSamples = 48000 * 2 * 60 / 1000;
	static constexpr int32 PlayingToleranceSamples = 48000 * 2;

	FCriticalSection Lock;
	TArray<TSharedRef<FBBPStreamState, ESPMode::ThreadSafe>> Listeners;
	FString Title;
	bool bFinished = false;
	bool bFailed = false;
};
