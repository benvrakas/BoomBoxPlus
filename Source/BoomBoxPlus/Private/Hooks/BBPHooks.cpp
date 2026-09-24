#include "Hooks/BBPHooks.h"
#include "BoomBoxPlus.h"
#include "FGUnlockSubsystem.h"
#include "Patching/NativeHookManager.h"
#include "Tape/BBPCustomMusicTape.h"

void RegisterBBPHooks()
{
#if !WITH_EDITOR
	// Adds the Custom Music tape to every unlocked-tapes query without saving it as an unlock.
	SUBSCRIBE_METHOD_AFTER(AFGUnlockSubsystem::GetUnlockedTapes, [](const AFGUnlockSubsystem* Self, TArray<TSubclassOf<UFGTapeData>>& OutTapes)
	{
		static bool bLoggedFirstCall = false;
		if (!bLoggedFirstCall)
		{
			bLoggedFirstCall = true;
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook: GetUnlockedTapes fired for the first time (%d vanilla tapes); adding Custom Music"), OutTapes.Num());
		}
		OutTapes.AddUnique(UBBPCustomMusicTape::StaticClass());
	});
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook installed: AFGUnlockSubsystem::GetUnlockedTapes"));
#else
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Editor build: native hooks not installed"));
#endif
}
