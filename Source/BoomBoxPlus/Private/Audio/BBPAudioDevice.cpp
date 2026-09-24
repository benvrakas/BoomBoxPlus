#include "Audio/BBPAudioDevice.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "BoomBoxPlus.h"
#include "Engine/Engine.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* AudioSection = TEXT("Audio");
	const TCHAR* MixerModuleKey = TEXT("AudioMixerModuleName");
	const TCHAR* MixerModule = TEXT("AudioMixerXAudio2");

	// Calls the engine's protected audio setup on an existing engine.
	struct FBBPEngineAudioAccess : UEngine
	{
		static void InitializeAudio(UEngine* Engine)
		{
			void (UEngine::*Initialize)() = &FBBPEngineAudioAccess::InitializeAudioDeviceManager;
			(Engine->*Initialize)();
		}
	};

	bool ShouldUseUnrealAudio()
	{
		return !GIsEditor && !IsRunningDedicatedServer() && FApp::CanEverRenderAudio();
	}

	// Sets the mixer module in the engine config if the game left it empty; returns false if it was already set.
	bool SetMixerModuleIfEmpty()
	{
		FString Current;
		GConfig->GetString(AudioSection, MixerModuleKey, Current, GEngineIni);
		if (!Current.IsEmpty())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Audio: engine audio mixer module already set to '%s'"), *Current);
			return false;
		}
		GConfig->SetString(AudioSection, MixerModuleKey, MixerModule, GEngineIni);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Audio: set engine audio mixer module to '%s' (the game leaves it empty)"), MixerModule);
		return true;
	}
}

void BBPAudioDevice::ConfigureBeforeEngineInit()
{
	if (!ShouldUseUnrealAudio())
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Audio: not configuring Unreal audio (editor %d, dedicated server %d, audio allowed %d)"),
			GIsEditor, IsRunningDedicatedServer(), FApp::CanEverRenderAudio());
		return;
	}
	SetMixerModuleIfEmpty();
}

bool BBPAudioDevice::EnsureAvailable()
{
	if (!GEngine)
	{
		return false;
	}
	if (GEngine->GetMainAudioDeviceRaw())
	{
		return true;
	}
	if (!ShouldUseUnrealAudio())
	{
		return false;
	}

	static bool bAttempted = false;
	if (bAttempted)
	{
		return false;
	}
	bAttempted = true;

	UE_LOG(LogBoomBoxPlus, Warning, TEXT("Audio: the engine has no Unreal audio device; creating one now"));
	SetMixerModuleIfEmpty();
	FBBPEngineAudioAccess::InitializeAudio(GEngine);

	FAudioDevice* Device = GEngine->GetMainAudioDeviceRaw();
	if (!Device)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Audio: could not create an Unreal audio device; Custom Music will be silent. See 'LogAudio' lines above for the cause."));
		return false;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Audio: created Unreal audio device %u (%d Hz)"), Device->DeviceID, static_cast<int32>(Device->GetSampleRate()));
	return true;
}
