#include "Network/BBPRemoteCallObject.h"
#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "FGBoomBoxPlayer.h"
#include "FGPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Algo/AllOf.h"
#include "Net/BBPRadio.h"
#include "Net/UnrealNetwork.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "UI/BBPMusicPage.h"

namespace
{
	constexpr int32 MaxTextLength = 512;
}

void UBBPRemoteCallObject::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBBPRemoteCallObject, DummyReplicatedField);
}

bool UBBPRemoteCallObject::MayControl(const TCHAR* RequestName) const
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!Playlist)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: %s from %s ignored; playlist subsystem not spawned"), RequestName, *GetRequesterName());
		return false;
	}
	const AFGPlayerController* Controller = GetOwnerPlayerController();
	const bool bIsHost = Controller && Controller->IsLocalController();
	if (!bIsHost && Playlist->GetNetMode() == NM_ListenServer && UBBPConfig::GetBool(this, UBBPConfig::HostOnlyControlKey, false))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("RCO: %s from %s rejected; host-only control is on"), RequestName, *GetRequesterName());
		return false;
	}
	return true;
}

ABBPMusicChannel* UBBPRemoteCallObject::GetChannelForRequest(AFGBoomBoxPlayer* BoomBox, const TCHAR* RequestName) const
{
	if (!BoomBox)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: %s from %s ignored; no Boom Box given"), RequestName, *GetRequesterName());
		return nullptr;
	}
	if (!MayControl(RequestName))
	{
		return nullptr;
	}
	ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	ABBPMusicChannel* Channel = Playlist->GetOrCreateChannel(BoomBox);
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("RCO: %s from %s on %s (channel %04d)"), RequestName, *GetRequesterName(), *GetNameSafe(BoomBox), Channel ? Channel->GetLinkCode() : 0);
	return Channel;
}

FString UBBPRemoteCallObject::GetRequesterName() const
{
	const AFGPlayerController* Controller = GetOwnerPlayerController();
	const APlayerState* State = Controller ? Controller->PlayerState : nullptr;
	return State ? State->GetPlayerName() : FString(TEXT("unknown player"));
}

void UBBPRemoteCallObject::Server_AddTrack_Implementation(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("AddTrack")))
	{
		Channel->AddTrack(Track, bFront, GetRequesterName());
	}
}

namespace
{
	bool IsTrackValid(const FBBPTrack& Track)
	{
		return Track.Id.Len() <= MaxTextLength && Track.Title.Len() <= MaxTextLength
			&& Track.Artist.Len() <= MaxTextLength && Track.SourceRef.Len() <= MaxTextLength
			&& Track.Duration >= 0.f && Track.Duration < 24.f * 3600.f
			// Every player's game opens a radio entry's URL, so only plain web stream URLs are accepted.
			&& (!Track.IsLive() || BBPRadio::IsStreamUrl(Track.SourceRef));
	}
}

void UBBPRemoteCallObject::Server_PlayTrackNow_Implementation(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("PlayTrackNow")))
	{
		Channel->PlayTrackNow(Track, GetRequesterName());
	}
}

bool UBBPRemoteCallObject::Server_PlayTrackNow_Validate(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track)
{
	const bool bValid = IsTrackValid(Track);
	if (!bValid)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: PlayTrackNow failed validation (source %d, ref '%s')"), (int32)Track.Source, *Track.SourceRef.Left(80));
	}
	return bValid;
}

void UBBPRemoteCallObject::Server_AddTracks_Implementation(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("AddTracks")))
	{
		const FString Requester = GetRequesterName();
		for (const FBBPTrack& Track : Tracks)
		{
			Channel->AddTrack(Track, false, Requester);
		}
	}
}

bool UBBPRemoteCallObject::Server_AddTracks_Validate(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks)
{
	const bool bValid = Tracks.Num() <= MaxTracksPerBatch && Algo::AllOf(Tracks, IsTrackValid);
	if (!bValid)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: AddTracks failed validation (%d tracks)"), Tracks.Num());
	}
	return bValid;
}

bool UBBPRemoteCallObject::Server_AddTrack_Validate(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront)
{
	const bool bValid = IsTrackValid(Track);
	if (!bValid)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: AddTrack failed validation (id len %d, duration %.1f)"), Track.Id.Len(), Track.Duration);
	}
	return bValid;
}

void UBBPRemoteCallObject::Server_RemoveEntry_Implementation(AFGBoomBoxPlayer* BoomBox, int32 EntryId)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("RemoveEntry")))
	{
		Channel->RemoveEntry(EntryId);
	}
}

bool UBBPRemoteCallObject::Server_RemoveEntry_Validate(AFGBoomBoxPlayer* BoomBox, int32 EntryId)
{
	return EntryId >= 0;
}

void UBBPRemoteCallObject::Server_MoveEntry_Implementation(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("MoveEntry")))
	{
		Channel->MoveEntry(EntryId, NewIndex);
	}
}

bool UBBPRemoteCallObject::Server_MoveEntry_Validate(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex)
{
	return EntryId >= 0 && NewIndex >= 0;
}

void UBBPRemoteCallObject::Server_ClearQueue_Implementation(AFGBoomBoxPlayer* BoomBox)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("ClearQueue")))
	{
		Channel->ClearQueue();
	}
}

void UBBPRemoteCallObject::Server_SetPlaying_Implementation(AFGBoomBoxPlayer* BoomBox, bool bPlay)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, bPlay ? TEXT("Play") : TEXT("Pause")))
	{
		Channel->SetPlaying(bPlay);
	}
}

void UBBPRemoteCallObject::Server_TogglePlaying_Implementation(AFGBoomBoxPlayer* BoomBox)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("TogglePlaying")))
	{
		Channel->SetPlaying(!Channel->IsPlaying());
	}
}

void UBBPRemoteCallObject::Server_Skip_Implementation(AFGBoomBoxPlayer* BoomBox)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("Skip")))
	{
		Channel->Skip();
	}
}

void UBBPRemoteCallObject::Server_Previous_Implementation(AFGBoomBoxPlayer* BoomBox)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("Previous")))
	{
		Channel->Previous();
	}
}

void UBBPRemoteCallObject::Server_PlayEntry_Implementation(AFGBoomBoxPlayer* BoomBox, int32 EntryId)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("PlayEntry")))
	{
		Channel->PlayEntry(EntryId);
	}
}

void UBBPRemoteCallObject::Server_SeekTo_Implementation(AFGBoomBoxPlayer* BoomBox, float PositionSeconds)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("SeekTo")))
	{
		Channel->SeekTo(PositionSeconds);
	}
}

bool UBBPRemoteCallObject::Server_SeekTo_Validate(AFGBoomBoxPlayer* BoomBox, float PositionSeconds)
{
	return FMath::IsFinite(PositionSeconds) && PositionSeconds >= 0.f;
}

void UBBPRemoteCallObject::Server_SetShuffle_Implementation(AFGBoomBoxPlayer* BoomBox, bool bEnabled)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("SetShuffle")))
	{
		Channel->SetShuffle(bEnabled);
	}
}

void UBBPRemoteCallObject::Server_SetRepeatMode_Implementation(AFGBoomBoxPlayer* BoomBox, EBBPRepeatMode Mode)
{
	if (ABBPMusicChannel* Channel = GetChannelForRequest(BoomBox, TEXT("SetRepeatMode")))
	{
		Channel->SetRepeatMode(Mode);
	}
}

void UBBPRemoteCallObject::Server_LinkBoomBox_Implementation(AFGBoomBoxPlayer* BoomBox, int32 Code)
{
	FString Message;
	bool bSuccess = false;
	ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!BoomBox || !Playlist)
	{
		Message = TEXT("Linking failed.");
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: LinkBoomBox from %s ignored (Boom Box %s, subsystem %d)"), *GetRequesterName(), *GetNameSafe(BoomBox), Playlist != nullptr);
	}
	else if (!MayControl(TEXT("LinkBoomBox")))
	{
		Message = TEXT("Only the host can link Boom Boxes.");
	}
	else
	{
		bSuccess = Playlist->LinkBoomBox(BoomBox, Code, Message);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("RCO: %s linking %s to %04d: %s"), *GetRequesterName(), *GetNameSafe(BoomBox), Code, *Message);
	}
	Client_LinkResult(BoomBox, bSuccess, Message);
}

bool UBBPRemoteCallObject::Server_LinkBoomBox_Validate(AFGBoomBoxPlayer* BoomBox, int32 Code)
{
	return Code >= 0 && Code <= 9999;
}

void UBBPRemoteCallObject::Server_UnlinkBoomBox_Implementation(AFGBoomBoxPlayer* BoomBox)
{
	FString Message;
	bool bSuccess = false;
	ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!BoomBox || !Playlist)
	{
		Message = TEXT("Unlinking failed.");
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("RCO: UnlinkBoomBox from %s ignored (Boom Box %s, subsystem %d)"), *GetRequesterName(), *GetNameSafe(BoomBox), Playlist != nullptr);
	}
	else if (!MayControl(TEXT("UnlinkBoomBox")))
	{
		Message = TEXT("Only the host can unlink Boom Boxes.");
	}
	else
	{
		bSuccess = Playlist->UnlinkBoomBox(BoomBox, Message);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("RCO: %s unlinking %s: %s"), *GetRequesterName(), *GetNameSafe(BoomBox), *Message);
	}
	Client_LinkResult(BoomBox, bSuccess, Message);
}

void UBBPRemoteCallObject::Client_LinkResult_Implementation(AFGBoomBoxPlayer* BoomBox, bool bSuccess, const FString& Message)
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Link result for %s: %s (%s)"), *GetNameSafe(BoomBox), *Message, bSuccess ? TEXT("ok") : TEXT("failed"));
	UBBPMusicPage::NotifyLinkResult(BoomBox, Message);
}
