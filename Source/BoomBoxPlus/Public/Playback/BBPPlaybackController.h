#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Playback/BBPGameMusicFader.h"
#include "BBPPlaybackController.generated.h"

class ABBPMusicChannel;
class ABBPPlaylistSubsystem;
class AFGBoomBoxPlayer;
class UAudioComponent;
class UBBPStreamingSoundWave;
class UBBPHudOverlay;
struct FBBPQueueEntry;

// How a channel's current entry is shown everywhere (the Custom Music page, the vanilla Boom Box page and the
// now-playing toast): a title, and "Artist, Source" under it. Never empty strings: the vanilla page shows both as
// FNames, and an empty FName reads "None".
struct FBBPNowPlaying
{
	FString Title;
	// "Koven, YouTube: Monstercat Uncaged": the artist (left out when it's just the uploader again), then
	// UBBPBlueprintLibrary::GetSourceName.
	FString Subtitle;
	float Duration = 0.f;
};

// Audio playing from one Boom Box on this machine.
USTRUCT()
struct FBBPEmitter
{
	GENERATED_BODY()

	TWeakObjectPtr<AFGBoomBoxPlayer> BoomBox;

	// Channel the Boom Box played when EntryId was started.
	TWeakObjectPtr<ABBPMusicChannel> Channel;

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

	// True while EntryId is a live radio stream.
	bool bLive = false;

	// Last value written to the Boom Box's own (vanilla, unreplicated) "is playing" flag; see
	// UBBPPlaybackController::ApplyPaused. Lets Turbo Bass and anything else reading it work with Custom Music.
	bool bAppliedVanillaPlaying = false;
};

// Plays each Custom Music Boom Box's channel through that Boom Box, kept in sync with the server clock.
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

	// Returns the channel playing on the nearest Custom Music Boom Box the local player can hear, or null.
	// With bRequireSound, only counts Boom Boxes actually producing sound on this machine.
	const ABBPMusicChannel* GetAudibleChannel(bool bRequireSound) const;

	// Returns true if this machine is playing Channel's current track as a live radio stream, with the song title the
	// station announces (may be empty) and whether it is still waiting for audio.
	bool GetLiveStatus(const ABBPMusicChannel* Channel, FString& OutSongTitle, bool& bOutBuffering) const;

	// Fills how Channel's current entry is shown; false when nothing is queued. A radio entry uses the song the
	// station announces ("Artist - Title"), which needs Controller (null on a dedicated server).
	static bool DescribeNowPlaying(const ABBPMusicChannel* Channel, const UBBPPlaybackController* Controller, FBBPNowPlaying& Out);

private:
	// Adds emitters for newly active Boom Boxes and removes emitters for ones that are gone.
	void SyncEmitters();

	// Brings one emitter in line with the current playback state.
	void UpdateEmitter(FBBPEmitter& Emitter, float DeltaSeconds);

	// Starts the channel's current track on an emitter from the given position.
	void StartTrack(FBBPEmitter& Emitter, const ABBPMusicChannel& Channel, int32 EntryId, float Position);

	// Starts the live radio stream of Entry on an emitter.
	void StartLiveStream(FBBPEmitter& Emitter, const FBBPQueueEntry& Entry);

	// Plays a started wave on an emitter's audio component.
	void PlayWave(FBBPEmitter& Emitter, UBBPStreamingSoundWave* Wave);

	// Stops and releases an emitter's wave, keeping the entry it plays.
	void ReleaseWave(FBBPEmitter& Emitter);

	// Stops and releases an emitter's wave.
	void StopTrack(FBBPEmitter& Emitter);

	// Pauses or resumes an emitter's audio component, and keeps the Boom Box's own (vanilla) "is playing" flag in
	// step, since the game's own turbo bass check reads it and Custom Music never runs the code path that would
	// normally set it.
	void ApplyPaused(FBBPEmitter& Emitter, bool bPaused);

	// Sets the Boom Box's own "is playing" flag (mState.mPlaybackState's PlaybackEnabled bit) for this machine.
	void SetVanillaPlaybackFlag(FBBPEmitter& Emitter, bool bPlaying);

	UAudioComponent* CreateAudioComponent(AFGBoomBoxPlayer* BoomBox);

	// Creates the lyric/now-playing overlay once a local player controller exists.
	void EnsureHudOverlay();

	// Starts downloads for the current and next few network tracks in the queue.
	void PrefetchNetworkTracks();

	// Tells the server when this machine has the current track of a loading channel ready (or can't play it).
	void ReportLoadedTracks();

	// Load generation last reported per channel.
	TMap<TWeakObjectPtr<const ABBPMusicChannel>, int32> ReportedLoads;

	// Lowers or restores the game's music depending on whether Custom Music is audible.
	void UpdateGameMusic(float DeltaSeconds);

	// Re-reads the game's Master and Boom Box volume sliders.
	void UpdateGameVolumeScale();

	// Sends the current song, play state and position to open vanilla Boom Box pages, which otherwise only follow Wwise playback.
	void UpdateVanillaPages();

	// Position and duration the vanilla Boom Box page shows for Channel, matching the Custom Music page's seek bar
	// (0 / 0 for live streams and when nothing is queued).
	static void GetVanillaPosition(const ABBPMusicChannel* Channel, float& OutPosition, float& OutDuration);

	// Game volume sliders (Master x Boom Box), 0..1.
	float GameVolumeScale = 1.f;
	float GameVolumeTimer = 0.f;

	// What each vanilla page was last told, so song and state are only re-sent on change.
	struct FBBPVanillaPageState
	{
		TWeakObjectPtr<const ABBPMusicChannel> Channel;
		int32 EntryId = INDEX_NONE - 1;
		bool bPlaying = false;
		// Text lines the page was last given, so a new radio announcement or album line is re-sent.
		FString Title;
		FString Subtitle;
		FString Album;
	};
	TMap<TWeakObjectPtr<UObject>, FBBPVanillaPageState> VanillaPages;

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
