#pragma once

#include "CoreMinimal.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "Subsystem/ModSubsystem.h"
#include "BBPPlaylistSubsystem.generated.h"

class ABBPMusicChannel;
class AFGBoomBoxPlayer;
class UBBPPlaybackController;

// Tracks which Boom Boxes play Custom Music and which channel (queue) each one plays. The server owns it; clients get a replicated copy.
UCLASS()
class BOOMBOXPLUS_API ABBPPlaylistSubsystem : public AModSubsystem
{
	GENERATED_BODY()

public:
	ABBPPlaylistSubsystem();

	// Returns the subsystem for the world WorldContext belongs to, or null if it hasn't spawned yet.
	static ABBPPlaylistSubsystem* Get(const UObject* WorldContext);

	// Returns the channel BoomBox plays, or null if it has none yet.
	static ABBPMusicChannel* FindChannelFor(const AFGBoomBoxPlayer* BoomBox);

	//~ Begin AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor interface

	// Returns the channel BoomBox plays, or null if it has none yet.
	ABBPMusicChannel* FindChannel(const AFGBoomBoxPlayer* BoomBox) const;

	// Returns the channel with the given link code, or null.
	ABBPMusicChannel* FindChannelByCode(int32 Code) const;

	// Server only. Returns BoomBox's channel, giving it a new one of its own if it has none.
	ABBPMusicChannel* GetOrCreateChannel(AFGBoomBoxPlayer* BoomBox);

	// Server only. Moves BoomBox onto the channel with the given code, zipper-merging its queue in. Returns false with a reason if it can't.
	bool LinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code, FString& OutMessage);

	// Server only. Gives BoomBox its own channel again, keeping a copy of the queue it was playing. Returns false with a reason if it can't.
	bool UnlinkBoomBox(AFGBoomBoxPlayer* BoomBox, FString& OutMessage);

	// Boom Boxes that currently have the Custom Music tape loaded.
	const TArray<FBBPActiveBoomBox>& GetActiveBoomBoxes() const { return ActiveBoomBoxes; }

	// Every channel in the session.
	const TArray<TObjectPtr<ABBPMusicChannel>>& GetChannels() const { return Channels; }

	// Returns this machine's audio playback, or null on a dedicated server.
	UBBPPlaybackController* GetPlaybackController() const { return PlaybackController; }

private:
	// Server only. Rebuilds ActiveBoomBoxes from the Boom Boxes in the world.
	void RefreshActiveBoomBoxes();

	// Server only. Gives active Boom Boxes channels, drops destroyed Boom Boxes and empty channels, and pauses channels nobody can hear.
	void MaintainChannels();

	// Server only. Spawns an empty channel with an unused link code.
	ABBPMusicChannel* SpawnChannel();

	// Server only. Returns a random 4-digit code no channel uses.
	int32 MakeUnusedCode() const;

	// Server only. Removes BoomBox from Channel, destroying the channel if nobody is left on it.
	void LeaveChannel(ABBPMusicChannel* Channel, AFGBoomBoxPlayer* BoomBox);

	UPROPERTY(Replicated)
	TArray<FBBPActiveBoomBox> ActiveBoomBoxes;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<ABBPMusicChannel>> Channels;

	// Plays audio on this machine; null on dedicated servers.
	UPROPERTY(Transient)
	TObjectPtr<UBBPPlaybackController> PlaybackController;

	// Server only.
	float ActiveBoomBoxRefreshTimer = 0.f;
};
