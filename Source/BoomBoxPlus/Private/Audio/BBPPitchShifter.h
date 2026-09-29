#pragma once

#include "CoreMinimal.h"

// Shifts the pitch of interleaved 16-bit audio without changing its speed. Two taps sweep through a short delay line,
// each faded out where it jumps back. Unshifted, the output is half the window (25 ms) late.
class FBBPPitchShifter
{
public:
	FBBPPitchShifter(int32 InSampleRate, int32 InChannels)
		: SampleRate(InSampleRate)
		, Channels(InChannels)
		, WindowFrames(FMath::Max(2, FMath::RoundToInt(InSampleRate * WindowSeconds)))
	{
		History.SetNumZeroed((WindowFrames + 1) * InChannels);
	}

	// Processes NumFrames frames of Samples in place, played at Ratio times their pitch (1 unchanged, 2 an octave up).
	void Process(int16* Samples, int32 NumFrames, float Ratio)
	{
		const int32 HistoryFrames = WindowFrames + 1;
		const bool bShifting = FMath::Abs(Ratio - 1.f) > SettleThreshold;
		const float ShiftStep = (1.f - Ratio) / WindowFrames;
		const float MaxSettleStep = SettleRatio / WindowFrames;
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			int16* FrameSamples = Samples + Frame * Channels;
			float* Slot = History.GetData() + WriteFrame * Channels;
			for (int32 Channel = 0; Channel < Channels; ++Channel)
			{
				Slot[Channel] = FrameSamples[Channel];
			}

			float OtherPhase = Phase + 0.5f;
			OtherPhase -= FMath::FloorToFloat(OtherPhase);
			const float Gain = FMath::Sin(PI * Phase);
			const float OtherGain = FMath::Sin(PI * OtherPhase);
			for (int32 Channel = 0; Channel < Channels; ++Channel)
			{
				const float Out = Gain * ReadDelayed(Phase * WindowFrames, Channel) + OtherGain * ReadDelayed(OtherPhase * WindowFrames, Channel);
				FrameSamples[Channel] = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Out), -32768, 32767));
			}

			if (bShifting)
			{
				Phase += ShiftStep;
			}
			else
			{
				// Unshifted, the taps rest where one is silent (phase 0 or 0.5); a frozen mix of both would sound hollow.
				const float Rest = FMath::RoundToFloat(Phase * 2.f) / 2.f;
				Phase -= FMath::Clamp(Phase - Rest, -MaxSettleStep, MaxSettleStep);
			}
			Phase -= FMath::FloorToFloat(Phase);
			WriteFrame = (WriteFrame + 1) % HistoryFrames;
		}
	}

	const int32 SampleRate;
	const int32 Channels;

private:
	// Returns Channel's sample DelayFrames (0 to WindowFrames) behind the frame just written, interpolated.
	float ReadDelayed(float DelayFrames, int32 Channel) const
	{
		const int32 HistoryFrames = WindowFrames + 1;
		const int32 Whole = FMath::Min(FMath::FloorToInt(DelayFrames), WindowFrames - 1);
		const float Fraction = DelayFrames - Whole;
		const int32 Newer = (WriteFrame - Whole + HistoryFrames) % HistoryFrames;
		const int32 Older = (Newer - 1 + HistoryFrames) % HistoryFrames;
		return FMath::Lerp(History[Newer * Channels + Channel], History[Older * Channels + Channel], Fraction);
	}

	// Length of the delay line the taps sweep.
	static constexpr float WindowSeconds = 0.05f;

	// Ratios this close to 1 count as unshifted.
	static constexpr float SettleThreshold = 0.0001f;

	// Largest pitch change used to bring the taps to rest (0.5%, too small to hear).
	static constexpr float SettleRatio = 0.005f;

	const int32 WindowFrames;
	TArray<float> History;
	int32 WriteFrame = 0;
	float Phase = 0.f;
};
