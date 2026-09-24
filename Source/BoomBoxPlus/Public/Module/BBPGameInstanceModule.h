#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "BBPGameInstanceModule.generated.h"

// BoomBoxPlus's root game instance module; registers its remote call objects with SML.
UCLASS()
class BOOMBOXPLUS_API UBBPGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UBBPGameInstanceModule();
};
