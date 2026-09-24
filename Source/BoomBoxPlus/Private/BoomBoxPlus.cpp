#include "BoomBoxPlus.h"
#include "Hooks/BBPHooks.h"

DEFINE_LOG_CATEGORY(LogBoomBoxPlus);

void FBoomBoxPlusModule::StartupModule()
{
	RegisterBBPHooks();
}

void FBoomBoxPlusModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBoomBoxPlusModule, BoomBoxPlus)
