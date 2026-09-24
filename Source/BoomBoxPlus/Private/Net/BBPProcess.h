#pragma once

#include "CoreMinimal.h"

// Output of a finished external process.
struct FBBPProcessResult
{
	bool bLaunched = false;
	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
};

// Quotes one command-line argument using the Windows CreateProcess / CommandLineToArgvW rules.
FString BBPQuoteArg(const FString& Arg);

// Runs Executable with Args (each quoted individually) and waits for it to exit. Blocking: never call on the game thread.
FBBPProcessResult BBPRunProcess(const FString& Executable, const TArray<FString>& Args);
