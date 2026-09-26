#pragma once

#include "CoreMinimal.h"
#include "Resources/FGTapeData.h"
#include "BBPCustomMusicTape.generated.h"

// The "Custom Music" entry in the Boom Box tape list. Selecting it switches the Boom Box to BoomBoxPlus playback.
UCLASS()
class BOOMBOXPLUS_API UBBPCustomMusicTape : public UFGTapeData
{
	GENERATED_BODY()

public:
	UBBPCustomMusicTape();

	// Returns true if Tape is the Custom Music tape.
	static bool IsCustomMusicTape(TSubclassOf<UFGTapeData> Tape);

	// The description shown when nothing is playing (also the tape's built-in default); BBPHooks overwrites
	// mDescription with the current track's artist while something plays, and restores this when it stops.
	static FText GetIdleDescription();
};
