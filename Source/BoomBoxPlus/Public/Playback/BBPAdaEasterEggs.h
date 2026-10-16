#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "BBPAdaEasterEggs.generated.h"

class ABBPMusicChannel;
class UAudioComponent;
class UBBPPlaybackController;
class UBBPStreamingSoundWave;
class UFGAudioMessage;
class UFGMessage;
class UFGMessageSender;

// Now and then ADA comments on what the local player is listening to (the AdaEasterEggs setting). Lines and subtitles
// come from Resources/Audio/ADA (lines.json and <name>.ogg). Each line plays at most once a session, never within
// MinGapSeconds of another, only on this machine.
//
// A line is shown like ADA's own messages: queued in the game's message box as a runtime UFGMessage from ADA's sender
// (which shows her box and plays her jingle), with the voice started when the box starts playback
// (NotifyAudioMessageStarted). The voice is a Wwise event from the mod's own bank (BBP_ADA.bnk), routed to ADA's voice
// bus, so the game processes and mixes it like her own lines. Without the bank, the .ogg clip plays through Unreal. If
// the box doesn't show the line in time, it plays with ADA's jingle and the mod's own subtitle instead.
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

	// Called (from a hook) whenever the game's message box starts playing a message.
	static void NotifyAudioMessageStarted(UFGAudioMessage* AudioMessage);

	// Plays Name now, ignoring the once-a-session rule and the gap between lines (the /ada chat command).
	bool PlayNow(const FString& Name);

	// Names of the loaded lines.
	TArray<FString> GetLineNames() const;

	// Multiplier on ADA's voice volume when it plays through Unreal (no bank, or the fallback), on top of the game's
	// Master x Dialogue sliders. Default 5: matched by ear against the game's own ADA lines (clips at -23 LUFS). Through
	// Wwise the game's own bus sets the volume.
	static float VoiceGain;

	// What the mod's own subtitle shows (only when the game's message box couldn't show the line); empty otherwise.
	const FString& GetSubtitle() const { return Subtitle; }

	// Multiplier on Custom Music while ADA speaks (1 when she doesn't).
	float GetMusicDuck() const { return MusicDuck; }

private:
	// Plays the line called Name, with SubtitleText if given instead of its own. Returns false if eggs are off, the line
	// already played this session (unless bForce), is missing, another line is too recent (unless bIgnoreGap or bForce),
	// a line or a game message is already playing, or the game window is in the background.
	bool TryPlay(const FString& Name, const FString& SubtitleText = FString(), bool bIgnoreGap = false, bool bForce = false);

	// Starts the queued line's voice. bOwnSubtitle shows the mod's subtitle, bJingle plays ADA's jingle first (and the
	// voice through Unreal).
	void StartVoice(bool bOwnSubtitle, bool bJingle);

	// Posts the line's event from BBP_ADA.bnk on the mod's Wwise game object. False if it can't.
	bool PostWwiseVoice();

	// Loads BBP_ADA.bnk into Wwise, once per game session.
	void LoadBank();

	// Takes the mod's lines out of the game's lists of played messages, which are saved: the lines are made at runtime,
	// so a save holding them warns about unknown objects on every load.
	void RemoveFromPlayedMessages();

	// True while a line's voice plays, through either path.
	bool IsSpeaking() const { return Wave != nullptr || WwisePlayingId != 0; }
	void StopLine();
	void LoadLines();

	// The game's message box, or null (no HUD yet, or a dedicated server).
	class UFGGameUI* GetGameUI() const;

	// ADA's message sender class, loaded on first use.
	TSubclassOf<UFGMessageSender> GetAdaSender();

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

	// The message queued in the game's message box for the line about to play, until its voice starts.
	UPROPERTY()
	TObjectPtr<UFGMessage> PendingMessage;

	UPROPERTY()
	TSubclassOf<UFGMessageSender> AdaSender;

	// The message box widget showing our line, to log when it closes.
	TWeakObjectPtr<UFGAudioMessage> ShownBox;
	double ShownBoxSince = 0.0;

	// Line name -> subtitle, from lines.json.
	TMap<FString, FString> Subtitles;
	FString AudioDir;
	TSet<FString> Played;

	// The line queued or playing: its name, subtitle, clip and clip format.
	FString LineName;
	FString LineText;
	FString LinePath;
	int32 LineSampleRate = 0;
	int32 LineChannels = 0;
	float LineDuration = 0.f;
	double PendingSince = 0.0;

	// FPlatformTime::Seconds() when the last line was queued or started.
	double LastLineTime = 0.0;

	FString Subtitle;
	float LineTimeLeft = 0.f;
	// Seconds before the voice starts after the jingle (fallback only).
	float VoiceDelay = 0.f;
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
	float PlayedCleanupTimer = 0.f;

	// Wwise playing id of the voice posted from the bank, or 0.
	uint32 WwisePlayingId = 0;

	static bool bBankTried;
	static bool bBankLoaded;

	// Every live instance, for NotifyAudioMessageStarted.
	static TArray<TWeakObjectPtr<UBBPAdaEasterEggs>> Instances;
};
