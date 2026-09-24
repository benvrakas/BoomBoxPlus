#include "Playback/BBPGameMusicFader.h"
#include "AkGameplayStatics.h"
#include "BoomBoxPlus.h"
#include "FGGameUserSettings.h"

namespace
{
	// Wwise parameter behind the game's Music volume slider, and that slider's option id.
	const FName MusicVolumeRtpc(TEXT("Music_Bus_Volume"));
	const TCHAR* MusicVolumeOption = TEXT("RTPC.Music_Bus_Volume");

	constexpr float CheckInterval = 1.f;

	// Relative difference above which the parameter is treated as changed by the game.
	constexpr float ChangeTolerance = 0.01f;
}

void FBBPGameMusicFader::Update(bool bLower, float Level, float FadeSeconds, float DeltaSeconds)
{
	Level = FMath::Clamp(Level, 0.f, 1.f);
	FadeSeconds = FMath::Max(0.f, FadeSeconds);

	if (RestoreTimer >= 0.f && !bLower)
	{
		RestoreTimer -= DeltaSeconds;
		if (RestoreTimer < 0.f)
		{
			ReapplyPlayerSetting();
		}
	}

	if (bLower && !bLowered)
	{
		// Mid fade-up the parameter is between values, so keep the setting captured last time.
		if (RestoreTimer < 0.f)
		{
			BaseValue = GetMusicVolume();
		}
		RestoreTimer = -1.f;
		AppliedValue = BaseValue * Level;
		SetMusicVolume(AppliedValue, FadeSeconds);
		bLowered = true;
		CheckTimer = FadeSeconds + CheckInterval;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("GameMusic: lowering '%s' from %.2f to %.2f over %.1f s"), *MusicVolumeRtpc.ToString(), BaseValue, AppliedValue, FadeSeconds);
		return;
	}

	if (!bLower && bLowered)
	{
		bLowered = false;
		SetMusicVolume(BaseValue, FadeSeconds);
		RestoreTimer = FadeSeconds + CheckInterval;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("GameMusic: raising '%s' back to %.2f over %.1f s"), *MusicVolumeRtpc.ToString(), BaseValue, FadeSeconds);
		return;
	}

	if (!bLowered)
	{
		return;
	}

	const float Wanted = BaseValue * Level;
	if (!FMath::IsNearlyEqual(Wanted, AppliedValue))
	{
		AppliedValue = Wanted;
		SetMusicVolume(AppliedValue, FadeSeconds);
		CheckTimer = FadeSeconds + CheckInterval;
		UE_LOG(LogBoomBoxPlus, Log, TEXT("GameMusic: level changed; now %.2f"), AppliedValue);
		return;
	}

	// The game sets the parameter itself when the player moves the Music slider; take that as the new setting.
	CheckTimer -= DeltaSeconds;
	if (CheckTimer <= 0.f)
	{
		CheckTimer = CheckInterval;
		const float Current = GetMusicVolume();
		if (FMath::Abs(Current - AppliedValue) > ChangeTolerance * FMath::Max(1.f, FMath::Abs(BaseValue)))
		{
			BaseValue = Current;
			AppliedValue = BaseValue * Level;
			SetMusicVolume(AppliedValue, FadeSeconds);
			CheckTimer = FadeSeconds + CheckInterval;
			UE_LOG(LogBoomBoxPlus, Log, TEXT("GameMusic: music setting changed to %.2f while lowered; lowering to %.2f"), BaseValue, AppliedValue);
		}
	}
}

void FBBPGameMusicFader::Restore()
{
	if (bLowered || RestoreTimer >= 0.f)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("GameMusic: restoring the player's music volume"));
		ReapplyPlayerSetting();
	}
	bLowered = false;
	RestoreTimer = -1.f;
}

void FBBPGameMusicFader::SetMusicVolume(float Value, float FadeSeconds) const
{
	UAkGameplayStatics::SetGlobalRTPCValue(nullptr, Value, FMath::RoundToInt(FadeSeconds * 1000.f), MusicVolumeRtpc);
}

float FBBPGameMusicFader::GetMusicVolume() const
{
	return UAkGameplayStatics::GetGlobalRTPCValue(nullptr, MusicVolumeRtpc);
}

void FBBPGameMusicFader::ReapplyPlayerSetting() const
{
	UFGGameUserSettings* Settings = UFGGameUserSettings::GetFGGameUserSettings();
	if (!Settings)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("GameMusic: game settings unavailable; setting '%s' back to %.2f directly"), *MusicVolumeRtpc.ToString(), BaseValue);
		SetMusicVolume(BaseValue, 0.f);
		return;
	}
	Settings->UpdateAudioOption(MusicVolumeOption);
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("GameMusic: game re-applied its music setting (%.2f)"), GetMusicVolume());
}
