#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Playback/BBPGameMusicFader.h"
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

	// True while EntryId has no local file yet; playback starts at the synced position once it appears.
	bool bWaitingForFile = false;
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

	// Starts downloads for the current and next few network tracks in the queue.
	void PrefetchNetworkTracks();

	// Returns true if Custom Music is playing on a Boom Box within earshot of the local player.
	bool IsCustomMusicAudible() const;

	// Lowers or restores the game's music depending on whether Custom Music is audible.
	void UpdateGameMusic(float DeltaSeconds);

	FBBPGameMusicFader GameMusicFader;

	// True once the missing audio device has been reported.
	bool bReportedNoAudioDevice = false;

	float PrefetchTimer = 0.f;

	UPROPERTY()
	TObjectPtr<ABBPPlaylistSubsystem> Playlist;

	UPROPERTY()
	TArray<FBBPEmitter> Emitters;

	UPROPERTY()
	TObjectPtr<UBBPHudOverlay> HudOverlay;

};
