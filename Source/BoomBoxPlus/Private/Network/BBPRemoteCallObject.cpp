#include "Network/BBPRemoteCallObject.h"
#include "BoomBoxPlus.h"
#include "FGPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Playlist/BBPPlaylistSubsystem.h"

namespace
{
	constexpr int32 MaxTextLength = 512;
}

void UBBPRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBBPRemoteCallObject, DummyReplicatedField);
}

ABBPPlaylistSubsystem* UBBPRemoteCallObject::GetPlaylistForRequest(const TCHAR* RequestName) const
{
	ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!Playlist)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: %s from %s ignored; playlist subsystem not spawned"), RequestName, *GetRequesterName());
		return nullptr;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("RCO: %s from %s"), RequestName, *GetRequesterName());
	return Playlist;
}

FString UBBPRemoteCallObject::GetRequesterName() const
{
	const AFGPlayerController* Controller = GetOwnerPlayerController();
	const APlayerState* State = Controller ? Controller->PlayerState : nullptr;
	return State ? State->GetPlayerName() : FString(TEXT("unknown player"));
}

void UBBPRemoteCallObject::Server_AddTrack_Implementation(const FBBPTrack& Track, bool bFront)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("AddTrack")))
	{
		Playlist->AddTrack(Track, bFront, GetRequesterName());
	}
}

bool UBBPRemoteCallObject::Server_AddTrack_Validate(const FBBPTrack& Track, bool bFront)
{
	const bool bValid = Track.Id.Len() <= MaxTextLength && Track.Title.Len() <= MaxTextLength
		&& Track.Artist.Len() <= MaxTextLength && Track.SourceRef.Len() <= MaxTextLength
		&& Track.Duration >= 0.f && Track.Duration < 24.f * 3600.f;
	if (!bValid)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: AddTrack failed validation (id len %d, duration %.1f)"), Track.Id.Len(), Track.Duration);
	}
	return bValid;
}

void UBBPRemoteCallObject::Server_RemoveEntry_Implementation(int32 EntryId)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("RemoveEntry")))
	{
		Playlist->RemoveEntry(EntryId);
	}
}

bool UBBPRemoteCallObject::Server_RemoveEntry_Validate(int32 EntryId)
{
	return EntryId >= 0;
}

void UBBPRemoteCallObject::Server_MoveEntry_Implementation(int32 EntryId, int32 NewIndex)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("MoveEntry")))
	{
		Playlist->MoveEntry(EntryId, NewIndex);
	}
}

bool UBBPRemoteCallObject::Server_MoveEntry_Validate(int32 EntryId, int32 NewIndex)
{
	return EntryId >= 0 && NewIndex >= 0;
}

void UBBPRemoteCallObject::Server_ClearQueue_Implementation()
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("ClearQueue")))
	{
		Playlist->ClearQueue();
	}
}

void UBBPRemoteCallObject::Server_SetPlaying_Implementation(bool bPlay)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(bPlay ? TEXT("Play") : TEXT("Pause")))
	{
		Playlist->SetPlaying(bPlay);
	}
}

void UBBPRemoteCallObject::Server_TogglePlaying_Implementation()
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("TogglePlaying")))
	{
		Playlist->SetPlaying(!Playlist->IsPlaying());
	}
}

void UBBPRemoteCallObject::Server_Skip_Implementation()
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("Skip")))
	{
		Playlist->Skip();
	}
}

void UBBPRemoteCallObject::Server_Previous_Implementation()
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("Previous")))
	{
		Playlist->Previous();
	}
}

void UBBPRemoteCallObject::Server_PlayEntry_Implementation(int32 EntryId)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("PlayEntry")))
	{
		Playlist->PlayEntry(EntryId);
	}
}

void UBBPRemoteCallObject::Server_SeekTo_Implementation(float PositionSeconds)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("SeekTo")))
	{
		Playlist->SeekTo(PositionSeconds);
	}
}

bool UBBPRemoteCallObject::Server_SeekTo_Validate(float PositionSeconds)
{
	return FMath::IsFinite(PositionSeconds) && PositionSeconds >= 0.f;
}

void UBBPRemoteCallObject::Server_SetShuffle_Implementation(bool bEnabled)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("SetShuffle")))
	{
		Playlist->SetShuffle(bEnabled);
	}
}

void UBBPRemoteCallObject::Server_SetRepeatMode_Implementation(EBBPRepeatMode Mode)
{
	if (ABBPPlaylistSubsystem* Playlist = GetPlaylistForRequest(TEXT("SetRepeatMode")))
	{
		Playlist->SetRepeatMode(Mode);
	}
}
