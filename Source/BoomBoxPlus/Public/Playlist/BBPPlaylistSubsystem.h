#pragma once

#include "CoreMinimal.h"
#include "FGSaveInterface.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "Subsystem/ModSubsystem.h"
#include "BBPPlaylistSubsystem.generated.h"

class ABBPMusicChannel;
class AFGBoomBoxPlayer;
class UBBPPlaybackController;

// Tracks which Boom Boxes play Custom Music and which channel (queue) each one plays. The server owns it; clients get a replicated copy.
// Also persists which Boom Boxes are linked together (IFGSaveInterface), so a group stays paired across a save reload.
UCLASS()
class BOOMBOXPLUS_API ABBPPlaylistSubsystem : public AModSubsystem, public IFGSaveInterface
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

	//~ Begin IFGSaveInterface. SavedChannels (each channel's Boom Boxes, queue and transport) is what this mod keeps
	// in the save; channels themselves, link codes and pending link requests are rebuilt or start fresh on load.
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override;
	virtual void PostSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override {}
	virtual void PreLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override {}
	virtual void PostLoadGame_Implementation(int32 SaveVersion, int32 GameVersion) override {}
	virtual void GatherDependencies_Implementation(TArray<UObject*>& out_dependentObjects) override {}
	virtual bool NeedTransform_Implementation() override { return false; }
	virtual bool ShouldSave_Implementation() const override { return true; }
	//~ End IFGSaveInterface

	// Returns the channel BoomBox plays, or null if it has none yet.
	ABBPMusicChannel* FindChannel(const AFGBoomBoxPlayer* BoomBox) const;

	// Returns the channel with the given link code, or null.
	ABBPMusicChannel* FindChannelByCode(int32 Code) const;

	// Server only. Returns BoomBox's channel, giving it a new one of its own if it has none.
	ABBPMusicChannel* GetOrCreateChannel(AFGBoomBoxPlayer* BoomBox);

	// Server only. Asks to link BoomBox's channel with the channel using Code. The link happens once both sides have entered
	// each other's code within LinkWindowSeconds; the two queues are then zipper-merged. Returns false with a reason if it can't.
	bool LinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code, FString& OutMessage);

	// Seconds both sides have to enter each other's code.
	static constexpr double LinkWindowSeconds = 180.0;

	// Link requests waiting for the other side.
	const TArray<FBBPLinkRequest>& GetLinkRequests() const { return LinkRequests; }

	// Returns the server's clock, in seconds.
	double GetServerTime() const;

	// Server only. Leaves BoomBox's group: gives it its own channel again (keeping a copy of the queue it was
	// playing), while the rest of the group stays together and stays paired across future saves. Returns false
	// with a reason if it can't.
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

	// Server only. Moves every Boom Box on Absorbed onto Kept, merges Absorbed's queue into Kept's and destroys Absorbed.
	void MergeChannels(ABBPMusicChannel* Kept, ABBPMusicChannel* Absorbed);

	// Server only. While Channel has a current track, loads Custom Music onto any of its Boom Boxes that don't have
	// it, so every Boom Box that shares a playing channel plays along without someone selecting the tape by hand.
	void EnsureMembersLoaded(ABBPMusicChannel* Channel);

	// Returns the character to name as changing BoomBox's tape: its carrier, else the nearest player, else null.
	class AFGCharacterPlayer* FindTapeChangeInstigator(AFGBoomBoxPlayer* BoomBox) const;

	// Server only. When EnsureMembersLoaded last asked each Boom Box to load Custom Music (server time).
	TMap<TWeakObjectPtr<AFGBoomBoxPlayer>, double> TapeLoadRequests;

	// Server only. Drops expired requests and requests naming codes no channel uses any more.
	void PruneLinkRequests();

	// Server only. Rebuilds SavedChannels from the live channels (and any saved ones not restored yet).
	void SaveChannels();

	// Server only. Rebuilds channels from PendingRestores once all of a saved channel's Boom Boxes have turned up
	// (or RestoreGraceSeconds after start, with whichever have). Needed forces the channel of that Boom Box now.
	void RestoreSavedChannels(const AFGBoomBoxPlayer* Needed = nullptr);

	// Stable identifier for BoomBox that survives a save/reload, or empty if BoomBox is null.
	static FString GetBoomBoxKey(const AFGBoomBoxPlayer* BoomBox);

	UPROPERTY(Replicated)
	TArray<FBBPActiveBoomBox> ActiveBoomBoxes;

	UPROPERTY(Replicated)
	TArray<TObjectPtr<ABBPMusicChannel>> Channels;

	UPROPERTY(Replicated)
	TArray<FBBPLinkRequest> LinkRequests;

	// Plays audio on this machine; null on dedicated servers.
	UPROPERTY(Transient)
	TObjectPtr<UBBPPlaybackController> PlaybackController;

	// Server only.
	float ActiveBoomBoxRefreshTimer = 0.f;

	// Kept in the save: one FBBPSavedChannel per channel, as JSON (so the struct can change without the save
	// system needing SaveGame flags on every field). Written by SaveChannels, read at BeginPlay.
	UPROPERTY(SaveGame)
	TArray<FString> SavedChannels;

	// Server only. Saved channels whose Boom Boxes haven't all turned up yet, and when to stop waiting for them.
	TArray<FBBPSavedChannel> PendingRestores;
	double RestoreDeadline = 0.0;

	// Server only. Seconds until SavedChannels is refreshed again (in case the game saves without PreSaveGame).
	float SaveSyncTimer = 0.f;
};
