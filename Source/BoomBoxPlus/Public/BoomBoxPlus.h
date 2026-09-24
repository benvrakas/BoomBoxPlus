#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBoomBoxPlus, Log, All);

// Module entry point for BoomBoxPlus.
class FBoomBoxPlusModule : public IModuleInterface
{
public:
	// Registers native hooks when the module loads.
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
