#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "BBPGameWorldModule.generated.h"

// BoomBoxPlus's root game world module; registers its mod subsystems with SML.
UCLASS()
class BOOMBOXPLUS_API UBBPGameWorldModule : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UBBPGameWorldModule();
};
