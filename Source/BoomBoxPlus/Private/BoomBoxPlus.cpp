#include "BoomBoxPlus.h"
#include "Audio/BBPAudioDevice.h"
#include "Hooks/BBPHooks.h"

DEFINE_LOG_CATEGORY(LogBoomBoxPlus);

void FBoomBoxPlusModule::StartupModule()
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("BoomBoxPlus module starting"));
	BBPAudioDevice::ConfigureBeforeEngineInit();
	RegisterBBPHooks();
}

void FBoomBoxPlusModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBoomBoxPlusModule, BoomBoxPlus)
