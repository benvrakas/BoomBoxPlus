#include "Hooks/BBPHooks.h"
#include "FGUnlockSubsystem.h"
#include "Patching/NativeHookManager.h"
#include "Tape/BBPCustomMusicTape.h"

void RegisterBBPHooks()
{
#if !WITH_EDITOR
	// Adds the Custom Music tape to every unlocked-tapes query without saving it as an unlock.
	SUBSCRIBE_METHOD_AFTER(AFGUnlockSubsystem::GetUnlockedTapes, [](const AFGUnlockSubsystem* Self, TArray<TSubclassOf<UFGTapeData>>& OutTapes)
	{
		OutTapes.AddUnique(UBBPCustomMusicTape::StaticClass());
	});
#endif
}
