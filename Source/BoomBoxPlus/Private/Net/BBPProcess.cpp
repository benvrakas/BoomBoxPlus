#include "Net/BBPProcess.h"
#include "BoomBoxPlus.h"
#include "HAL/PlatformProcess.h"

FString BBPQuoteArg(const FString& Arg)
{
	if (!Arg.IsEmpty() && !Arg.Contains(TEXT(" ")) && !Arg.Contains(TEXT("\t")) && !Arg.Contains(TEXT("\"")))
	{
		return Arg;
	}

	// Backslashes are literal unless they precede a quote; then each must be doubled.
	FString Out = TEXT("\"");
	int32 PendingBackslashes = 0;
	for (const TCHAR Char : Arg)
	{
		if (Char == TEXT('\\'))
		{
			++PendingBackslashes;
			continue;
		}
		if (Char == TEXT('"'))
		{
			Out += FString::ChrN(PendingBackslashes * 2 + 1, TEXT('\\'));
			Out.AppendChar(TEXT('"'));
		}
		else
		{
			Out += FString::ChrN(PendingBackslashes, TEXT('\\'));
			Out.AppendChar(Char);
		}
		PendingBackslashes = 0;
	}
	// Backslashes before the closing quote must be doubled too.
	Out += FString::ChrN(PendingBackslashes * 2, TEXT('\\'));
	Out.AppendChar(TEXT('"'));
	return Out;
}

FBBPProcessResult BBPRunProcess(const FString& Executable, const TArray<FString>& Args)
{
	FBBPProcessResult Result;
	if (IsInGameThread())
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Process: refusing to run '%s' on the game thread"), *FPaths::GetCleanFilename(Executable));
		return Result;
	}

	FString Params;
	for (const FString& Arg : Args)
	{
		if (!Params.IsEmpty())
		{
			Params.AppendChar(TEXT(' '));
		}
		Params += BBPQuoteArg(Arg);
	}

	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Process: %s %s"), *FPaths::GetCleanFilename(Executable), *Params);
	const double StartTime = FPlatformTime::Seconds();
	Result.bLaunched = FPlatformProcess::ExecProcess(*Executable, *Params, &Result.ReturnCode, &Result.StdOut, &Result.StdErr, nullptr, true);
	if (!Result.bLaunched)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Process: could not launch '%s'"), *Executable);
	}
	else
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Process: %s exited %d after %.1f s"), *FPaths::GetCleanFilename(Executable),
			Result.ReturnCode, FPlatformTime::Seconds() - StartTime);
	}
	return Result;
}
