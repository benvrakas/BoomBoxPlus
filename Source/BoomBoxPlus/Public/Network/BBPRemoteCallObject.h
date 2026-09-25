#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "BBPRemoteCallObject.generated.h"

class ABBPMusicChannel;
class AFGBoomBoxPlayer;

// Carries a player's requests for a Boom Box's queue to the server, and link results back.
UCLASS(NotBlueprintable)
class BOOMBOXPLUS_API UBBPRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable, WithValidation = Server_AddTrack_Validate)
	void Server_AddTrack(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront);
	bool Server_AddTrack_Validate(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront);

	// Tells the server this player has Channel's current track ready (or can't play it) for its load wait.
	// Not subject to HostOnlyControl: it's a status report, not a request.
	UFUNCTION(Server, Reliable)
	void Server_ReportLoaded(ABBPMusicChannel* Channel, int32 LoadGeneration);

	// Adds a track right after the current one and starts playing it (used to tune in to a radio station).
	UFUNCTION(Server, Reliable, WithValidation = Server_PlayTrackNow_Validate)
	void Server_PlayTrackNow(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track);
	bool Server_PlayTrackNow_Validate(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track);

	// Adds several tracks to the end of the queue, in order. At most MaxTracksPerBatch per call.
	UFUNCTION(Server, Reliable, WithValidation = Server_AddTracks_Validate)
	void Server_AddTracks(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks);
	bool Server_AddTracks_Validate(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks);

	// Largest batch Server_AddTracks accepts; bigger lists are split by the sender.
	static constexpr int32 MaxTracksPerBatch = 25;

	UFUNCTION(Server, Reliable, WithValidation = Server_RemoveEntry_Validate)
	void Server_RemoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId);
	bool Server_RemoveEntry_Validate(AFGBoomBoxPlayer* BoomBox, int32 EntryId);

	UFUNCTION(Server, Reliable, WithValidation = Server_MoveEntry_Validate)
	void Server_MoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex);
	bool Server_MoveEntry_Validate(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex);

	UFUNCTION(Server, Reliable)
	void Server_ClearQueue(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(Server, Reliable)
	void Server_SetPlaying(AFGBoomBoxPlayer* BoomBox, bool bPlay);

	UFUNCTION(Server, Reliable)
	void Server_TogglePlaying(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(Server, Reliable)
	void Server_Skip(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(Server, Reliable)
	void Server_Previous(AFGBoomBoxPlayer* BoomBox);

	UFUNCTION(Server, Reliable)
	void Server_PlayEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId);

	UFUNCTION(Server, Reliable, WithValidation = Server_SeekTo_Validate)
	void Server_SeekTo(AFGBoomBoxPlayer* BoomBox, float PositionSeconds);
	bool Server_SeekTo_Validate(AFGBoomBoxPlayer* BoomBox, float PositionSeconds);

	UFUNCTION(Server, Reliable)
	void Server_SetShuffle(AFGBoomBoxPlayer* BoomBox, bool bEnabled);

	UFUNCTION(Server, Reliable)
	void Server_SetRepeatMode(AFGBoomBoxPlayer* BoomBox, EBBPRepeatMode Mode);

	// Links BoomBox to the channel with the given 4-digit code.
	UFUNCTION(Server, Reliable, WithValidation = Server_LinkBoomBox_Validate)
	void Server_LinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code);
	bool Server_LinkBoomBox_Validate(AFGBoomBoxPlayer* BoomBox, int32 Code);

	// Gives BoomBox its own channel again.
	UFUNCTION(Server, Reliable)
	void Server_UnlinkBoomBox(AFGBoomBoxPlayer* BoomBox);

	// Tells the requesting player how a link or unlink went.
	UFUNCTION(Client, Reliable)
	void Client_LinkResult(AFGBoomBoxPlayer* BoomBox, bool bSuccess, const FString& Message);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Returns BoomBox's channel if the calling player may control it; logs why not otherwise.
	ABBPMusicChannel* GetChannelForRequest(AFGBoomBoxPlayer* BoomBox, const TCHAR* RequestName) const;

	// Returns true if the calling player may control music; logs why not otherwise.
	bool MayControl(const TCHAR* RequestName) const;

	// Returns the calling player's display name.
	FString GetRequesterName() const;

	// Required for the RCO to replicate.
	UPROPERTY(Replicated)
	int32 DummyReplicatedField = 0;
};
