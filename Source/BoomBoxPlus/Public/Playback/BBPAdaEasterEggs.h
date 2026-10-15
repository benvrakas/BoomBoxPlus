#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BBPAdaEasterEggs.generated.h"

class ABBPMusicChannel;
class UAudioComponent;
class UBBPPlaybackController;
class UBBPStreamingSoundWave;

// Now and then ADA comments on what the local player is listening to (the AdaEasterEggs setting). Lines and subtitles
// come from Resources/Audio/ADA (lines.json and <name>.ogg). Each line plays at most once a session, never within
// MinGapSeconds of another, only on this machine.
UCLASS()
class BOOMBOXPLUS_API UBBPAdaEasterEggs : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UBBPPlaybackController* InController);
	void Shutdown();
	void Tick(float DeltaSeconds);

	// Called when this player opens a Custom Music page.
	void NotifyMusicPageOpened();

	// What ADA is saying, for the subtitle; empty when she isn't speaking.
	const FString& GetSubtitle() const { return Subtitle; }

	// Multiplier on Custom Music while ADA speaks (1 when she doesn't).
	float GetMusicDuck() const { return MusicDuck; }

private:
	// Plays the line called Name, with SubtitleText if given instead of its own. Returns false if eggs are off, the line
	// already played this session, is missing, or another line is too recent (unless bIgnoreGap) or still speaking.
	bool TryPlay(const FString& Name, const FString& SubtitleText = FString(), bool bIgnoreGap = false);
	void StopLine();
	void LoadLines();

	// Idle while listening (idle, then idle2 if still idle).
	void CheckIdle(float DeltaSeconds, const ABBPMusicChannel* Channel);

	// Song starts and restarts on the channel heard: loops, keywords, copyright.
	void CheckTrack(const ABBPMusicChannel* Channel);

	// A Boom Box at full volume with another player near it.
	void CheckVolume();

	UPROPERTY()
	TObjectPtr<UBBPPlaybackController> Controller;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Component;

	UPROPERTY()
	TObjectPtr<UBBPStreamingSoundWave> Wave;

	// Line name -> subtitle, from lines.json.
	TMap<FString, FString> Subtitles;
	FString AudioDir;
	TSet<FString> Played;

	// FPlatformTime::Seconds() when the last line started.
	double LastLineTime = 0.0;

	FString Subtitle;
	float LineTimeLeft = 0.f;
	float MusicDuck = 1.f;

	FVector IdleLocation = FVector::ZeroVector;
	FRotator IdleRotation = FRotator::ZeroRotator;
	float IdleSeconds = 0.f;
	// True once "idle" played during the current idle stretch, so "idle2" can follow it.
	bool bIdleSaidThisStretch = false;

	TWeakObjectPtr<const ABBPMusicChannel> TrackChannel;
	int32 TrackEntryId = INDEX_NONE;
	FString LastTrackId;
	float LastTrackPosition = 0.f;
	// Plays in a row of the same track on the channel heard.
	int32 ConsecutivePlays = 0;

	float VolumeCheckTimer = 0.f;
};
