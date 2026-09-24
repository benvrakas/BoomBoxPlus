#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BBPPlaybackController.generated.h"

class ABBPPlaylistSubsystem;
class AFGBoomBoxPlayer;
class UAudioComponent;
class UBBPStreamingSoundWave;
class UBBPHudOverlay;

// Audio playing from one Boom Box on this machine.
USTRUCT()
struct FBBPEmitter
{
	GENERATED_BODY()

	TWeakObjectPtr<AFGBoomBoxPlayer> BoomBox;

	UPROPERTY()
	TObjectPtr<UAudioComponent> Component;

	UPROPERTY()
	TObjectPtr<UBBPStreamingSoundWave> Wave;

	// Queue entry the wave is playing, or INDEX_NONE.
	int32 EntryId = INDEX_NONE;

	// Playback state revision last applied.
	int32 AppliedRevision = -1;

	// Volume last applied to Component.
	float AppliedVolume = -1.f;

	// Seconds until the next drift check.
	float DriftCheckTimer = 0.f;

	// True once the missing-track or failure warning has been logged for EntryId.
	bool bReportedProblem = false;
};

// Plays the shared queue through every Boom Box that has Custom Music loaded, kept in sync with the server clock.
UCLASS()
class BOOMBOXPLUS_API UBBPPlaybackController : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(ABBPPlaylistSubsystem* InPlaylist);
	void Shutdown();
	void Tick(float DeltaSeconds);

	// Returns the distance in centimetres at which Boom Box music becomes inaudible.
	static float GetAudibleRange();

private:
	// Adds emitters for newly active Boom Boxes and removes emitters for ones that are gone.
	void SyncEmitters();

	// Brings one emitter in line with the current playback state.
	void UpdateEmitter(FBBPEmitter& Emitter, float DeltaSeconds);

	// Starts the current track on an emitter from the given position.
	void StartTrack(FBBPEmitter& Emitter, int32 EntryId, float Position);

	// Stops and releases an emitter's wave.
	void StopTrack(FBBPEmitter& Emitter);

	UAudioComponent* CreateAudioComponent(AFGBoomBoxPlayer* BoomBox);

	// Creates the lyric/now-playing overlay once a local player controller exists.
	void EnsureHudOverlay();

	UPROPERTY()
	TObjectPtr<ABBPPlaylistSubsystem> Playlist;

	UPROPERTY()
	TArray<FBBPEmitter> Emitters;

	UPROPERTY()
	TObjectPtr<UBBPHudOverlay> HudOverlay;

};
