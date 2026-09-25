#include "Audio/BBPLiveStreamWorker.h"
#include "Audio/BBPStreamState.h"
#include "BoomBoxPlus.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Net/BBPProcess.h"
#include "Net/BBPRadio.h"

namespace
{
	constexpr uint32 PollMs = 5;
	constexpr int32 MaxPendingBytes = 256 * 1024;

	// ffmpeg restarts allowed in a row before the stream is given up; a run that played for GoodRunSeconds resets the count.
	constexpr int32 MaxRestarts = 3;
	constexpr double GoodRunSeconds = 30.0;
	constexpr uint32 RestartDelayMs = 3000;

	constexpr int32 RecentLogLines = 6;

	// Longest a yt-dlp lookup of a YouTube live stream may take.
	constexpr double ResolveTimeoutSeconds = 60.0;
	constexpr uint32 ResolvePollMs = 50;
}

FBBPLiveStreamWorker::FBBPLiveStreamWorker(TSharedRef<FBBPStreamState, ESPMode::ThreadSafe> InState, FString InFfmpegPath, FString InYtDlpPath, FString InUrl)
	: State(InState)
	, FfmpegPath(MoveTemp(InFfmpegPath))
	, YtDlpPath(MoveTemp(InYtDlpPath))
	, Url(MoveTemp(InUrl))
{
}

uint32 FBBPLiveStreamWorker::Run()
{
	int32 RestartsInARow = 0;
	bool bEverGotAudio = false;
	while (!State->bStopRequested)
	{
		if (!Launch())
		{
			if (++RestartsInARow > MaxRestarts || State->bStopRequested)
			{
				State->bFailed = !bEverGotAudio;
				State->bEndOfStream = true;
				break;
			}
			State->WakeEvent->Wait(RestartDelayMs);
			continue;
		}
		const double StartedAt = FPlatformTime::Seconds();
		bool bGotAudio = false;
		while (!State->bStopRequested)
		{
			PumpLog();
			const bool bMoved = PumpAudio();
			if (bMoved && !bGotAudio)
			{
				bGotAudio = true;
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Radio: receiving audio from '%s' (station '%s')"), *Url, *StationName);
			}
			if (bMoved)
			{
				continue;
			}
			// ffmpeg has exited once its output is fully read; leftover audio waits for room in the ring first.
			if (!FPlatformProcess::IsProcRunning(Process) && Pending.Num() < BBPRadio::Channels * (int32)sizeof(int16))
			{
				TArray<uint8> Tail;
				if (!FPlatformProcess::ReadPipeToArray(StdOutRead, Tail) || Tail.Num() == 0)
				{
					break;
				}
				Pending.Append(Tail);
				continue;
			}
			State->WakeEvent->Wait(PollMs);
		}
		PumpLog();
		int32 ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(Process, &ReturnCode);
		Shutdown();
		if (State->bStopRequested)
		{
			break;
		}

		bEverGotAudio |= bGotAudio;
		if (bGotAudio && FPlatformTime::Seconds() - StartedAt >= GoodRunSeconds)
		{
			RestartsInARow = 0;
		}
		FString LastOutput;
		for (const FString& Line : RecentLog)
		{
			LastOutput += LastOutput.IsEmpty() ? Line : TEXT(" | ") + Line;
		}
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Radio: ffmpeg stopped (code %d) after %.0f s on '%s'. Last output: %s"),
			ReturnCode, FPlatformTime::Seconds() - StartedAt, *Url, *LastOutput);
		if (++RestartsInARow > MaxRestarts)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Radio: giving up on '%s' after %d attempts"), *Url, RestartsInARow);
			State->bFailed = !bEverGotAudio;
			State->bEndOfStream = true;
			break;
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Radio: reconnecting to '%s' (attempt %d of %d)"), *Url, RestartsInARow, MaxRestarts);
		State->WakeEvent->Wait(RestartDelayMs);
	}
	Shutdown();
	return 0;
}

void FBBPLiveStreamWorker::Stop()
{
	State->bStopRequested = true;
	State->WakeEvent->Trigger();
}

FString FBBPLiveStreamWorker::ResolveStreamUrl()
{
	if (!BBPRadio::IsYouTubeUrl(Url))
	{
		return Url;
	}
	if (YtDlpPath.IsEmpty())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Radio: can't play YouTube live '%s': the bundled yt-dlp is missing"), *Url);
		return FString();
	}
	// Run here rather than through BBPRunProcess so a stop request (skip, pause, leaving) can kill it instead of waiting.
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Radio: could not create a pipe for yt-dlp"));
		return FString();
	}
	const TArray<FString> Args = { TEXT("--ignore-config"), TEXT("--no-warnings"), TEXT("--socket-timeout"), TEXT("15"),
		TEXT("-g"), TEXT("-f"), TEXT("bestaudio/best"), TEXT("--"), Url };
	FString Params;
	for (const FString& Arg : Args)
	{
		Params += (Params.IsEmpty() ? TEXT("") : TEXT(" ")) + BBPQuoteArg(Arg);
	}
	FProcHandle Lookup = FPlatformProcess::CreateProc(*YtDlpPath, *Params, false, true, true, nullptr, 0, nullptr, WritePipe, nullptr);
	if (!Lookup.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Radio: could not launch '%s'"), *YtDlpPath);
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		return FString();
	}
	FString Output;
	const double StartedAt = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(Lookup))
	{
		if (State->bStopRequested || FPlatformTime::Seconds() - StartedAt > ResolveTimeoutSeconds)
		{
			FPlatformProcess::TerminateProc(Lookup, true);
			break;
		}
		Output += FPlatformProcess::ReadPipe(ReadPipe);
		State->WakeEvent->Wait(ResolvePollMs);
	}
	Output += FPlatformProcess::ReadPipe(ReadPipe);
	int32 ReturnCode = -1;
	FPlatformProcess::GetProcReturnCode(Lookup, &ReturnCode);
	FPlatformProcess::CloseProc(Lookup);
	FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
	if (State->bStopRequested)
	{
		return FString();
	}

	// stdout and stderr share the pipe; the address is the line that starts with http.
	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines);
	FString StreamUrl;
	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("http")))
		{
			StreamUrl = Line.TrimStartAndEnd();
			break;
		}
	}
	if (!BBPRadio::IsResolvedStreamUrl(StreamUrl))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Radio: yt-dlp found no live stream for '%s' (code %d): %s"), *Url, ReturnCode, *Output.Right(300));
		return FString();
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Radio: YouTube live '%s' streams from %s"), *Url, *StreamUrl.Left(60));
	return StreamUrl;
}

bool FBBPLiveStreamWorker::Launch()
{
	const FString StreamUrl = ResolveStreamUrl();
	if (StreamUrl.IsEmpty())
	{
		return false;
	}
	if (!FPlatformProcess::CreatePipe(StdOutRead, StdOutWrite) || !FPlatformProcess::CreatePipe(StdErrRead, StdErrWrite))
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Radio: could not create pipes for ffmpeg"));
		Shutdown();
		return false;
	}

	TArray<FString> Args = { TEXT("-hide_banner"), TEXT("-nostdin"), TEXT("-nostats"), TEXT("-loglevel"), TEXT("verbose") };
	Args.Append(BBPRadio::MakeInputArgs(StreamUrl, true));
	Args.Append({
		TEXT("-vn"),
		TEXT("-ac"), FString::FromInt(BBPRadio::Channels),
		TEXT("-ar"), FString::FromInt(BBPRadio::SampleRate),
		TEXT("-f"), TEXT("s16le"),
		TEXT("pipe:1"),
	});
	FString Params;
	for (const FString& Arg : Args)
	{
		if (!Params.IsEmpty())
		{
			Params.AppendChar(TEXT(' '));
		}
		Params += BBPQuoteArg(Arg);
	}

	Process = FPlatformProcess::CreateProc(*FfmpegPath, *Params, false, true, true, nullptr, 0, nullptr, StdOutWrite, nullptr, StdErrWrite);
	if (!Process.IsValid())
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Radio: could not launch '%s'"), *FfmpegPath);
		Shutdown();
		return false;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Radio: connecting to '%s'"), *Url);
	return true;
}

void FBBPLiveStreamWorker::Shutdown()
{
	if (Process.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Process))
		{
			FPlatformProcess::TerminateProc(Process, true);
		}
		FPlatformProcess::CloseProc(Process);
		Process.Reset();
	}
	if (StdOutRead || StdOutWrite)
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
	}
	if (StdErrRead || StdErrWrite)
	{
		FPlatformProcess::ClosePipe(StdErrRead, StdErrWrite);
	}
	StdOutRead = StdOutWrite = StdErrRead = StdErrWrite = nullptr;
	Pending.Reset();
	LogCarry.Reset();
}

bool FBBPLiveStreamWorker::PumpAudio()
{
	if (Pending.Num() < MaxPendingBytes)
	{
		TArray<uint8> Chunk;
		if (FPlatformProcess::ReadPipeToArray(StdOutRead, Chunk) && Chunk.Num() > 0)
		{
			Pending.Append(Chunk);
		}
	}
	int32 Samples = FMath::Min(Pending.Num() / (int32)sizeof(int16), State->GetFreeSamples());
	Samples -= Samples % BBPRadio::Channels;
	if (Samples <= 0)
	{
		return false;
	}
	State->Write(reinterpret_cast<const int16*>(Pending.GetData()), Samples);
	Pending.RemoveAt(0, Samples * sizeof(int16), EAllowShrinking::No);
	return true;
}

void FBBPLiveStreamWorker::PumpLog()
{
	if (!StdErrRead)
	{
		return;
	}
	LogCarry += FPlatformProcess::ReadPipe(StdErrRead);
	int32 LineEnd = INDEX_NONE;
	while (LogCarry.FindChar(TEXT('\n'), LineEnd))
	{
		FString Line = LogCarry.Left(LineEnd).TrimStartAndEnd();
		LogCarry.RightChopInline(LineEnd + 1);
		if (Line.IsEmpty())
		{
			continue;
		}
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Radio ffmpeg: %s"), *Line);
		RecentLog.Add(Line);
		if (RecentLog.Num() > RecentLogLines)
		{
			RecentLog.RemoveAt(0);
		}

		const FString OldTitle = SongTitle;
		BBPRadio::ParseLogLine(Line, StationName, SongTitle);
		if (SongTitle != OldTitle)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Radio: '%s' now playing '%s'"), *StationName, *SongTitle);
			State->SetLiveTitle(SongTitle);
		}
	}
}
