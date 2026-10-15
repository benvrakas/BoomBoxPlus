#pragma once

#include "CoreMinimal.h"

// The Windows "Open" dialog, for importing list files.
namespace BBPFileDialog
{
	// Shows an Open dialog for a text list over the game window, on its own thread so the game keeps running, and calls
	// OnChosen on the game thread with the chosen file, or empty if cancelled. Does nothing while a dialog is already
	// open. Returns false if no dialog was shown (one is already open, or this platform has none).
	bool OpenListFile(const FString& Title, TFunction<void(const FString& Path)> OnChosen);
}
