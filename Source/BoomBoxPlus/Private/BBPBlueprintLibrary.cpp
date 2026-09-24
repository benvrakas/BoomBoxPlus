#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "Engine/GameInstance.h"
#include "FGPlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Network/BBPRemoteCallObject.h"
#include "Playlist/BBPPlaylistSubsystem.h"

UBBPRemoteCallObject* UBBPBlueprintLibrary::GetLocalRCO(const UObject* WorldContext, const TCHAR* RequestName)
{
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

void UBBPBlueprintLibrary::RequestAddTrack(const UObject* WorldContext, const FBBPTrack& Track, bool bFront)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("AddTrack")))
	{
		RCO->Server_AddTrack(Track, bFront);
	}
}

void UBBPBlueprintLibrary::RequestRemoveEntry(const UObject* WorldContext, int32 EntryId)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("RemoveEntry")))
	{
		RCO->Server_RemoveEntry(EntryId);
	}
}

void UBBPBlueprintLibrary::RequestMoveEntry(const UObject* WorldContext, int32 EntryId, int32 NewIndex)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("MoveEntry")))
	{
		RCO->Server_MoveEntry(EntryId, NewIndex);
	}
}

void UBBPBlueprintLibrary::RequestClearQueue(const UObject* WorldContext)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("ClearQueue")))
	{
		RCO->Server_ClearQueue();
	}
}

void UBBPBlueprintLibrary::RequestSetPlaying(const UObject* WorldContext, bool bPlay)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("SetPlaying")))
	{
		RCO->Server_SetPlaying(bPlay);
	}
}

void UBBPBlueprintLibrary::RequestTogglePlaying(const UObject* WorldContext)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("TogglePlaying")))
	{
		RCO->Server_TogglePlaying();
	}
}

void UBBPBlueprintLibrary::RequestSkip(const UObject* WorldContext)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("Skip")))
	{
		RCO->Server_Skip();
	}
}

void UBBPBlueprintLibrary::RequestPrevious(const UObject* WorldContext)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("Previous")))
	{
		RCO->Server_Previous();
	}
}

void UBBPBlueprintLibrary::RequestPlayEntry(const UObject* WorldContext, int32 EntryId)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("PlayEntry")))
	{
		RCO->Server_PlayEntry(EntryId);
	}
}

void UBBPBlueprintLibrary::RequestSeekTo(const UObject* WorldContext, float PositionSeconds)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("SeekTo")))
	{
		RCO->Server_SeekTo(PositionSeconds);
	}
}

void UBBPBlueprintLibrary::RequestSetShuffle(const UObject* WorldContext, bool bEnabled)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("SetShuffle")))
	{
		RCO->Server_SetShuffle(bEnabled);
	}
}

void UBBPBlueprintLibrary::RequestSetRepeatMode(const UObject* WorldContext, EBBPRepeatMode Mode)
{
	if (UBBPRemoteCallObject* RCO = GetLocalRCO(WorldContext, TEXT("SetRepeatMode")))
	{
		RCO->Server_SetRepeatMode(Mode);
	}
}

EBBPEntryAvailability UBBPBlueprintLibrary::GetEntryAvailability(const UObject* WorldContext, const FBBPQueueEntry& Entry)
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(WorldContext);
	if (Playlist && Playlist->GetPlaybackState().CurrentEntryId == Entry.EntryId)
	{
		return EBBPEntryAvailability::Playing;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;
	if (Entry.Track.Source == EBBPTrackSource::Local && Library && !Library->HasTrack(Entry.Track.Id))
	{
		return EBBPEntryAvailability::NotInLibrary;
	}
	return EBBPEntryAvailability::Queued;
}

FString UBBPBlueprintLibrary::FormatDuration(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::FloorToInt(Seconds));
	return FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60);
}
