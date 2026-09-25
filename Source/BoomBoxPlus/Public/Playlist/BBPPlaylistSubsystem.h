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

	//~ Begin IFGSaveInterface. SavedGroups is the only thing this mod persists; everything else (channels,
	// queues, link codes, playback) is runtime-only and rebuilt from SavedGroups plus the Boom Boxes' own
	// (already-saved) mCurrentTape after a reload.
	virtual void PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion) override {}
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

	// Server only. Rebuilds SavedGroups from the current channels (one entry per channel with 2+ members).
	// Called every MaintainChannels tick, so leaving a group or a link completing is reflected within a fraction
	// of a second, and the next save always has an up-to-date picture.
	void SyncSavedGroupsFromChannels();

	// Server only. For each SavedGroups entry, gathers its currently-active, not-yet-channelled Boom Boxes and,
	// if 2 or more are present, gives them all one shared channel together - so a saved group reunites as soon
	// as its Boom Boxes are discovered, instead of each first getting its own lone channel.
	void RegroupSavedBoomBoxes();

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

	// Persisted (SaveGame) across saves. Each entry is one group: the GetBoomBoxKey() of every Boom Box that
	// was sharing a channel, joined with '|'. Rebuilt from the live channels every tick (SyncSavedGroupsFromChannels)
	// and consumed on the way back up (RegroupSavedBoomBoxes) - there is no other persisted mod state.
	UPROPERTY(SaveGame)
	TArray<FString> SavedGroups;
};
