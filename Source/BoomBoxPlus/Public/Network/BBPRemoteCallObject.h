#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "BBPRemoteCallObject.generated.h"

// Carries a player's playlist requests to the server.
UCLASS(NotBlueprintable)
class BOOMBOXPLUS_API UBBPRemoteCallObject : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	UFUNCTION(Server, Reliable, WithValidation = Server_AddTrack_Validate)
	void Server_AddTrack(const FBBPTrack& Track, bool bFront);
	bool Server_AddTrack_Validate(const FBBPTrack& Track, bool bFront);

	UFUNCTION(Server, Reliable, WithValidation = Server_RemoveEntry_Validate)
	void Server_RemoveEntry(int32 EntryId);
	bool Server_RemoveEntry_Validate(int32 EntryId);

	UFUNCTION(Server, Reliable, WithValidation = Server_MoveEntry_Validate)
	void Server_MoveEntry(int32 EntryId, int32 NewIndex);
	bool Server_MoveEntry_Validate(int32 EntryId, int32 NewIndex);

	UFUNCTION(Server, Reliable)
	void Server_ClearQueue();

	UFUNCTION(Server, Reliable)
	void Server_SetPlaying(bool bPlay);

	UFUNCTION(Server, Reliable)
	void Server_TogglePlaying();

	UFUNCTION(Server, Reliable)
	void Server_Skip();

	UFUNCTION(Server, Reliable)
	void Server_Previous();

	UFUNCTION(Server, Reliable)
	void Server_PlayEntry(int32 EntryId);

	UFUNCTION(Server, Reliable, WithValidation = Server_SeekTo_Validate)
	void Server_SeekTo(float PositionSeconds);
	bool Server_SeekTo_Validate(float PositionSeconds);

	UFUNCTION(Server, Reliable)
	void Server_SetShuffle(bool bEnabled);

	UFUNCTION(Server, Reliable)
	void Server_SetRepeatMode(EBBPRepeatMode Mode);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Returns the playlist if the calling player may control it; logs why not otherwise.
	class ABBPPlaylistSubsystem* GetPlaylistForRequest(const TCHAR* RequestName) const;

	// Returns the calling player's display name.
	FString GetRequesterName() const;

	// Required for the RCO to replicate.
	UPROPERTY(Replicated)
	int32 DummyReplicatedField = 0;
};
