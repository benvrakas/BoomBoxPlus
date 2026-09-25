#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "Engine/GameInstance.h"
#include "FGPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Lyrics/BBPLyricsSubsystem.h"
#include "Network/BBPRemoteCallObject.h"
#include "FGBoomBoxPlayer.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"

UBBPRemoteCallObject* UBBPBlueprintLibrary::GetLocalRCO(const UObject* WorldContext, const TCHAR* RequestName)
{
	if (!WorldContext)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("%s: no Boom Box given"), RequestName);
		return nullptr;
	}
	AFGPlayerController* Controller = Cast<AFGPlayerController>(UGameplayStatics::GetPlayerController(WorldContext, 0));
	if (!Controller)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("%s: no local FG player controller"), RequestName);
		return nullptr;
	}
	UBBPRemoteCallObject* RCO = Controller->GetRemoteCallObjectOfClass<UBBPRemoteCallObject>();
	if (!RCO)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("%s: BoomBoxPlus RCO not registered on the local player (is the mod installed on the server?)"), RequestName);
		return nullptr;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Sending %s"), RequestName);
	return RCO;
}

void UBBPBlueprintLibrary::RequestAddTrack(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track, bool bFront)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("AddTrack")))
	{
		RCO->Server_AddTrack(BoomBox, Track, bFront);
	}
}

void UBBPBlueprintLibrary::ReportTrackLoaded(ABBPMusicChannel* Channel, int32 LoadGeneration)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(Channel, TEXT("ReportLoaded")))
	{
		RCO->Server_ReportLoaded(Channel, LoadGeneration);
	}
}

void UBBPBlueprintLibrary::RequestPlayTrackNow(AFGBoomBoxPlayer* BoomBox, const FBBPTrack& Track)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("PlayTrackNow")))
	{
		RCO->Server_PlayTrackNow(BoomBox, Track);
	}
}

void UBBPBlueprintLibrary::RequestAddTracks(AFGBoomBoxPlayer* BoomBox, const TArray<FBBPTrack>& Tracks)
{
	if (Tracks.Num() == 0)
	{
		return;
	}
	UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("AddTracks"));
	if (!RCO)
	{
		return;
	}
	for (int32 Start = 0; Start < Tracks.Num(); Start += UBBPRemoteCallObject::MaxTracksPerBatch)
	{
		const int32 Count = FMath::Min(UBBPRemoteCallObject::MaxTracksPerBatch, Tracks.Num() - Start);
		RCO->Server_AddTracks(BoomBox, TArray<FBBPTrack>(Tracks.GetData() + Start, Count));
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Sent %d tracks for %s in %d batch(es)"), Tracks.Num(), *GetNameSafe(BoomBox),
		FMath::DivideAndRoundUp(Tracks.Num(), UBBPRemoteCallObject::MaxTracksPerBatch));
}

void UBBPBlueprintLibrary::RequestRemoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("RemoveEntry")))
	{
		RCO->Server_RemoveEntry(BoomBox, EntryId);
	}
}

void UBBPBlueprintLibrary::RequestMoveEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId, int32 NewIndex)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("MoveEntry")))
	{
		RCO->Server_MoveEntry(BoomBox, EntryId, NewIndex);
	}
}

void UBBPBlueprintLibrary::RequestClearQueue(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("ClearQueue")))
	{
		RCO->Server_ClearQueue(BoomBox);
	}
}

void UBBPBlueprintLibrary::RequestSetPlaying(AFGBoomBoxPlayer* BoomBox, bool bPlay)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("SetPlaying")))
	{
		RCO->Server_SetPlaying(BoomBox, bPlay);
	}
}

void UBBPBlueprintLibrary::RequestTogglePlaying(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("TogglePlaying")))
	{
		RCO->Server_TogglePlaying(BoomBox);
	}
}

void UBBPBlueprintLibrary::RequestSkip(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("Skip")))
	{
		RCO->Server_Skip(BoomBox);
	}
}

void UBBPBlueprintLibrary::RequestPrevious(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("Previous")))
	{
		RCO->Server_Previous(BoomBox);
	}
}

void UBBPBlueprintLibrary::RequestPlayEntry(AFGBoomBoxPlayer* BoomBox, int32 EntryId)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("PlayEntry")))
	{
		RCO->Server_PlayEntry(BoomBox, EntryId);
	}
}

void UBBPBlueprintLibrary::RequestSeekTo(AFGBoomBoxPlayer* BoomBox, float PositionSeconds)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("SeekTo")))
	{
		RCO->Server_SeekTo(BoomBox, PositionSeconds);
	}
}

void UBBPBlueprintLibrary::RequestSetShuffle(AFGBoomBoxPlayer* BoomBox, bool bEnabled)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("SetShuffle")))
	{
		RCO->Server_SetShuffle(BoomBox, bEnabled);
	}
}

void UBBPBlueprintLibrary::RequestSetRepeatMode(AFGBoomBoxPlayer* BoomBox, EBBPRepeatMode Mode)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("SetRepeatMode")))
	{
		RCO->Server_SetRepeatMode(BoomBox, Mode);
	}
}

void UBBPBlueprintLibrary::RequestLinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("LinkBoomBox")))
	{
		RCO->Server_LinkBoomBox(BoomBox, Code);
	}
}

void UBBPBlueprintLibrary::RequestUnlinkBoomBox(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("UnlinkBoomBox")))
	{
		RCO->Server_UnlinkBoomBox(BoomBox);
	}
}

void UBBPBlueprintLibrary::RequestChannel(AFGBoomBoxPlayer* BoomBox)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(BoomBox, TEXT("EnsureChannel")))
	{
		RCO->Server_EnsureChannel(BoomBox);
	}
}

ABBPMusicChannel* UBBPBlueprintLibrary::GetChannel(const AFGBoomBoxPlayer* BoomBox)
{
	return ABBPPlaylistSubsystem::FindChannelFor(BoomBox);
}

EBBPEntryAvailability UBBPBlueprintLibrary::GetEntryAvailability(const ABBPMusicChannel* Channel, const FBBPQueueEntry& Entry)
{
	if (Channel && Channel->GetPlaybackState().CurrentEntryId == Entry.EntryId)
	{
		return EBBPEntryAvailability::Playing;
	}
	const UWorld* World = Channel ? Channel->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	if (Entry.Track.Source == EBBPTrackSource::Local && Library && !Library->HasTrack(Entry.Track.Id))
	{
		return EBBPEntryAvailability::NotInLibrary;
	}
	return EBBPEntryAvailability::Queued;
}

FString UBBPBlueprintLibrary::GetCurrentLyricLine(const ABBPMusicChannel* Channel)
{
	FBBPQueueEntry Entry;
	if (!Channel || !Channel->GetCurrentEntry(Entry))
	{
		return FString();
	}
	const UWorld* World = Channel->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPLyricsSubsystem* Lyrics = GameInstance ? GameInstance->GetSubsystem<UBBPLyricsSubsystem>() : nullptr;
	return Lyrics ? Lyrics->GetLineAt(Entry.Track.Id, Channel->GetPlaybackPosition()) : FString();
}

FString UBBPBlueprintLibrary::FormatDuration(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}
