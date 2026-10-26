#include "Playlist/BBPPlaylistSubsystem.h"
#include "BoomBoxPlus.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Subsystem/SubsystemActorManager.h"
#include "Tape/BBPCustomMusicTape.h"
#include "FGCharacterPlayer.h"
#include "GameFramework/PlayerState.h"
#include "UI/BBPMusicPage.h"

namespace
{
	constexpr float ActiveBoomBoxRefreshInterval = 0.5f;

	// After a load, how long a saved channel waits for all of its Boom Boxes before it's rebuilt with those found.
	constexpr double RestoreGraceSeconds = 5.0;

	// How often SavedChannels is refreshed besides PreSaveGame.
	constexpr float SaveSyncInterval = 10.f;

	// BeginChangeTapeSequence only starts the eject/insert animation; the tape counts as loaded once it finishes.
	// Asking again before then restarts the animation, so a Boom Box is asked at most once per this many seconds.
	constexpr double TapeLoadRetrySeconds = 10.0;
	constexpr int32 MinLinkCode = 1000;
	constexpr int32 MaxLinkCode = 9999;
}

ABBPPlaylistSubsystem::ABBPPlaylistSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// After every actor tick and timer, so the vanilla Boom Box page's position is ours by the time it is drawn
	// (the Boom Box's own tick reports its Wwise position, always 0 for Custom Music; see UBBPPlaybackController).
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

ABBPPlaylistSubsystem* ABBPPlaylistSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	USubsystemActorManager* Manager = World ? World->GetSubsystem<USubsystemActorManager>() : nullptr;
	return Manager ? Manager->GetSubsystemActor<ABBPPlaylistSubsystem>() : nullptr;
}

ABBPMusicChannel* ABBPPlaylistSubsystem::FindChannelFor(const AFGBoomBoxPlayer* BoomBox)
{
	const ABBPPlaylistSubsystem* Subsystem = BoomBox ? Get(BoomBox) : nullptr;
	return Subsystem ? Subsystem->FindChannel(BoomBox) : nullptr;
}

void ABBPPlaylistSubsystem::BeginPlay()
{
	Super::BeginPlay();

	// Channels are rebuilt from SavedChannels by RestoreSavedChannels (see MaintainChannels) as their Boom Boxes
	// turn up; link codes and pending link requests start fresh.
	if (HasAuthority())
	{
		for (const FString& Json : SavedChannels)
		{
			FBBPSavedChannel Saved;
			if (FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Saved) && Saved.Members.Num() > 0)
			{
				PendingRestores.Add(MoveTemp(Saved));
			}
			else
			{
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: skipping an unreadable saved channel (%d chars)"), Json.Len());
			}
		}
		RestoreDeadline = GetWorld()->GetTimeSeconds() + RestoreGraceSeconds;
		for (const FString& Json : SavedRanges)
		{
			FBBPSavedRange Saved;
			if (FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Saved) && !Saved.Key.IsEmpty())
			{
				Ranges.Add(Saved.Key, FMath::Clamp(Saved.Range, BBPRange::Min, BBPRange::Max));
			}
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist subsystem started (%s); %d saved channel(s) to restore, %d Boom Box range(s)"),
		HasAuthority() ? TEXT("server") : TEXT("client"), PendingRestores.Num(), Ranges.Num());

	if (GetNetMode() != NM_DedicatedServer)
	{
		PlaybackController = NewObject<UBBPPlaybackController>(this);
		PlaybackController->Initialize(this);
	}
}

void ABBPPlaylistSubsystem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PlaybackController)
	{
		PlaybackController->Shutdown();
		PlaybackController = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ABBPPlaylistSubsystem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABBPPlaylistSubsystem, ActiveBoomBoxes);
	DOREPLIFETIME(ABBPPlaylistSubsystem, Channels);
	DOREPLIFETIME(ABBPPlaylistSubsystem, LinkRequests);
}

void ABBPPlaylistSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		ActiveBoomBoxRefreshTimer -= DeltaSeconds;
		if (ActiveBoomBoxRefreshTimer <= 0.f)
		{
			ActiveBoomBoxRefreshTimer = ActiveBoomBoxRefreshInterval;
			RefreshActiveBoomBoxes();
			MaintainChannels();
		}
	}

	if (PlaybackController)
	{
		PlaybackController->Tick(DeltaSeconds);
	}
}

double ABBPPlaylistSubsystem::GetServerTime() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

ABBPMusicChannel* ABBPPlaylistSubsystem::FindChannel(const AFGBoomBoxPlayer* BoomBox) const
{
	if (!BoomBox)
	{
		return nullptr;
	}
	for (ABBPMusicChannel* Channel : Channels)
	{
		if (Channel && Channel->HasMember(BoomBox))
		{
			return Channel;
		}
	}
	return nullptr;
}

ABBPMusicChannel* ABBPPlaylistSubsystem::FindChannelByCode(int32 Code) const
{
	for (ABBPMusicChannel* Channel : Channels)
	{
		if (Channel && Channel->GetLinkCode() == Code)
		{
			return Channel;
		}
	}
	return nullptr;
}

ABBPMusicChannel* ABBPPlaylistSubsystem::GetOrCreateChannel(AFGBoomBoxPlayer* BoomBox)
{
	if (!HasAuthority() || !BoomBox)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: GetOrCreateChannel needs authority and a Boom Box (authority %d, Boom Box %s)"), HasAuthority(), *GetNameSafe(BoomBox));
		return nullptr;
	}
	if (ABBPMusicChannel* Existing = FindChannel(BoomBox))
	{
		return Existing;
	}
	// A Boom Box whose saved channel hasn't been rebuilt yet gets that one, not an empty new one.
	RestoreSavedChannels(BoomBox);
	if (ABBPMusicChannel* Restored = FindChannel(BoomBox))
	{
		return Restored;
	}
	ABBPMusicChannel* Channel = SpawnChannel();
	if (Channel)
	{
		Channel->AddMember(BoomBox);
	}
	return Channel;
}

bool ABBPPlaylistSubsystem::LinkBoomBox(AFGBoomBoxPlayer* BoomBox, int32 Code, FString& OutMessage)
{
	if (!HasAuthority() || !BoomBox)
	{
		OutMessage = TEXT("Linking failed.");
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: LinkBoomBox needs authority and a Boom Box"));
		return false;
	}
	ABBPMusicChannel* Own = GetOrCreateChannel(BoomBox);
	ABBPMusicChannel* Target = FindChannelByCode(Code);
	if (!Own)
	{
		OutMessage = TEXT("Linking failed.");
		return false;
	}
	if (!Target)
	{
		OutMessage = FString::Printf(TEXT("No Boom Box uses code %04d."), Code);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s tried unknown code %04d"), *GetNameSafe(BoomBox), Code);
		return false;
	}
	if (Target == Own)
	{
		OutMessage = TEXT("That's this Boom Box's own code (or it's already linked to it).");
		return false;
	}

	PruneLinkRequests();
	const int32 OwnCode = Own->GetLinkCode();
	const int32 Matching = LinkRequests.IndexOfByPredicate([OwnCode, Code](const FBBPLinkRequest& Request)
	{
		return Request.FromCode == Code && Request.ToCode == OwnCode && !Request.bDeclined;
	});

	if (Matching == INDEX_NONE)
	{
		// First side: wait for the other Boom Box to enter this one's code.
		LinkRequests.RemoveAll([OwnCode](const FBBPLinkRequest& Request) { return Request.FromCode == OwnCode; });
		FBBPLinkRequest& Request = LinkRequests.AddDefaulted_GetRef();
		Request.FromCode = OwnCode;
		Request.ToCode = Code;
		Request.ExpiresAt = GetServerTime() + LinkWindowSeconds;
		ForceNetUpdate();
		OutMessage = FString::Printf(TEXT("Request sent. Someone at Boom Box %04d must enter %04d within 3 minutes."), Code, OwnCode);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d asked to link with %04d; waiting up to %.0f s"), OwnCode, Code, LinkWindowSeconds);
		return true;
	}

	// Second side: both have entered each other's code in time. The side that asked first keeps its channel.
	LinkRequests.RemoveAll([OwnCode, Code](const FBBPLinkRequest& Request)
	{
		return Request.FromCode == OwnCode || Request.FromCode == Code || Request.ToCode == OwnCode || Request.ToCode == Code;
	});
	ForceNetUpdate();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channels %04d and %04d entered each other's codes; linking"), OwnCode, Code);
	MergeChannels(Target, Own);
	OutMessage = FString::Printf(TEXT("Linked with Boom Box %04d. %d Boom Boxes now share one merged queue."), Code, Target->GetMembers().Num());
	return true;
}

bool ABBPPlaylistSubsystem::DeclineLinkRequest(AFGBoomBoxPlayer* BoomBox, int32 FromCode, FString& OutMessage)
{
	const ABBPMusicChannel* Own = HasAuthority() ? FindChannel(BoomBox) : nullptr;
	const int32 OwnCode = Own ? Own->GetLinkCode() : 0;
	FBBPLinkRequest* Request = Own ? LinkRequests.FindByPredicate([OwnCode, FromCode](const FBBPLinkRequest& R)
	{
		return R.FromCode == FromCode && R.ToCode == OwnCode && !R.bDeclined;
	}) : nullptr;
	if (!Request)
	{
		OutMessage = TEXT("That link request is no longer waiting.");
		return false;
	}
	Request->bDeclined = true;
	Request->ExpiresAt = GetServerTime() + DeclinedShownSeconds;
	ForceNetUpdate();
	OutMessage = FString::Printf(TEXT("Declined Boom Box %04d's link request."), FromCode);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d declined the link request from %04d"), OwnCode, FromCode);
	return true;
}

void ABBPPlaylistSubsystem::MergeChannels(ABBPMusicChannel* Kept, ABBPMusicChannel* Absorbed)
{
	Kept->MergeFrom(*Absorbed);
	const TArray<TObjectPtr<AFGBoomBoxPlayer>> Moving = Absorbed->GetMembers();
	for (AFGBoomBoxPlayer* Member : Moving)
	{
		Absorbed->RemoveMember(Member);
		Kept->AddMember(Member);
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d merged into %04d (%d Boom Box(es))"), Absorbed->GetLinkCode(), Kept->GetLinkCode(), Kept->GetMembers().Num());
	Channels.Remove(Absorbed);
	Absorbed->Destroy();

	// Linking is meant to make every Boom Box in the group play along right away, rather than waiting for the next
	// MaintainChannels tick (EnsureMembersLoaded also runs there, and covers every other way a Boom Box ends up on a
	// channel that's already playing: it was regrouped after a reload, or someone else on its channel started music
	// while its own tape had been changed away).
	EnsureMembersLoaded(Kept);
	ForceNetUpdate();
}

void ABBPPlaylistSubsystem::EnsureMembersLoaded(ABBPMusicChannel* Channel)
{
	if (!Channel || Channel->GetPlaybackState().CurrentEntryId == INDEX_NONE)
	{
		return;
	}
	const double Now = GetServerTime();
	for (AFGBoomBoxPlayer* Member : Channel->GetMembers())
	{
		if (!IsValid(Member))
		{
			continue;
		}
		if (UBBPCustomMusicTape::IsCustomMusicTape(Member->GetCurrentTape()))
		{
			TapeLoadRequests.Remove(Member);
			continue;
		}
		if (const double* RequestedAt = TapeLoadRequests.Find(Member))
		{
			if (Now - *RequestedAt < TapeLoadRetrySeconds)
			{
				continue;
			}
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: Custom Music still not loaded in %s %.0f s after asking; asking again"), *GetNameSafe(Member), Now - *RequestedAt);
		}
		AFGCharacterPlayer* TapeInstigator = FindTapeChangeInstigator(Member);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: loading Custom Music into %s (channel %04d already playing; instigator %s)"),
			*GetNameSafe(Member), Channel->GetLinkCode(), *GetNameSafe(TapeInstigator));
		TapeLoadRequests.Add(Member, Now);
		// This load is the mod's own doing, so it mustn't pull open Boom Box windows onto the music page.
		TGuardValue<bool> SuppressShow(UBBPMusicPage::bSuppressShowOnTapeChange, true);
		Member->BeginChangeTapeSequence(UBBPCustomMusicTape::StaticClass(), TapeInstigator);
	}
}

AFGCharacterPlayer* ABBPPlaylistSubsystem::FindTapeChangeInstigator(AFGBoomBoxPlayer* BoomBox) const
{
	if (AFGCharacterPlayer* Carrier = BoomBox->GetmOwningCharacter())
	{
		return Carrier;
	}
	// A placed Boom Box has no carrier; use the nearest player, as if they had changed the tape themselves.
	AFGCharacterPlayer* Nearest = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (GameState)
	{
		for (const APlayerState* Player : GameState->PlayerArray)
		{
			AFGCharacterPlayer* Character = Player ? Cast<AFGCharacterPlayer>(Player->GetPawn()) : nullptr;
			if (!Character)
			{
				continue;
			}
			const double DistanceSquared = FVector::DistSquared(Character->GetActorLocation(), BoomBox->GetActorLocation());
			if (DistanceSquared < NearestDistanceSquared)
			{
				NearestDistanceSquared = DistanceSquared;
				Nearest = Character;
			}
		}
	}
	return Nearest;
}

void ABBPPlaylistSubsystem::PruneLinkRequests()
{
	const double Now = GetServerTime();
	const int32 Removed = LinkRequests.RemoveAll([this, Now](const FBBPLinkRequest& Request)
	{
		const bool bExpired = Request.ExpiresAt <= Now;
		if (bExpired)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: link request %04d -> %04d expired"), Request.FromCode, Request.ToCode);
		}
		return bExpired || !FindChannelByCode(Request.FromCode) || !FindChannelByCode(Request.ToCode);
	});
	if (Removed > 0)
	{
		ForceNetUpdate();
	}
}

bool ABBPPlaylistSubsystem::UnlinkBoomBox(AFGBoomBoxPlayer* BoomBox, FString& OutMessage)
{
	if (!HasAuthority() || !BoomBox)
	{
		OutMessage = TEXT("Unlinking failed.");
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: UnlinkBoomBox needs authority and a Boom Box"));
		return false;
	}
	ABBPMusicChannel* Current = FindChannel(BoomBox);
	if (!Current || Current->GetMembers().Num() <= 1)
	{
		OutMessage = TEXT("This Boom Box isn't linked to another one.");
		return false;
	}
	ABBPMusicChannel* Own = SpawnChannel();
	if (!Own)
	{
		OutMessage = TEXT("Unlinking failed.");
		return false;
	}
	Own->CopyFrom(*Current);
	LeaveChannel(Current, BoomBox);
	Own->AddMember(BoomBox);
	OutMessage = FString::Printf(TEXT("Unlinked. This Boom Box has its own queue again (code %04d)."), Own->GetLinkCode());
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s unlinked from channel %04d onto its own channel %04d"), *GetNameSafe(BoomBox), Current->GetLinkCode(), Own->GetLinkCode());
	return true;
}

ABBPMusicChannel* ABBPPlaylistSubsystem::SpawnChannel()
{
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABBPMusicChannel* Channel = GetWorld()->SpawnActor<ABBPMusicChannel>(ABBPMusicChannel::StaticClass(), Params);
	if (!Channel)
	{
		UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: could not spawn a music channel"));
		return nullptr;
	}
	Channel->SetLinkCode(MakeUnusedCode());
	Channels.Add(Channel);
	ForceNetUpdate();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: created channel %04d (%d channels)"), Channel->GetLinkCode(), Channels.Num());
	return Channel;
}

int32 ABBPPlaylistSubsystem::MakeUnusedCode() const
{
	for (int32 Attempt = 0; Attempt < 100; ++Attempt)
	{
		const int32 Code = FMath::RandRange(MinLinkCode, MaxLinkCode);
		if (!FindChannelByCode(Code))
		{
			return Code;
		}
	}
	for (int32 Code = MinLinkCode; Code <= MaxLinkCode; ++Code)
	{
		if (!FindChannelByCode(Code))
		{
			return Code;
		}
	}
	UE_LOG(LogBoomBoxPlus, Error, TEXT("Playlist: every link code is in use"));
	return 0;
}

void ABBPPlaylistSubsystem::LeaveChannel(ABBPMusicChannel* Channel, AFGBoomBoxPlayer* BoomBox)
{
	Channel->RemoveMember(BoomBox);
	if (Channel->GetMembers().Num() == 0)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d has no Boom Boxes left; removing it"), Channel->GetLinkCode());
		Channels.Remove(Channel);
		Channel->Destroy();
		ForceNetUpdate();
	}
}

void ABBPPlaylistSubsystem::MaintainChannels()
{
	PruneLinkRequests();

	for (int32 i = Channels.Num() - 1; i >= 0; --i)
	{
		ABBPMusicChannel* Channel = Channels[i];
		if (!IsValid(Channel))
		{
			Channels.RemoveAt(i);
			ForceNetUpdate();
			continue;
		}
		Channel->PruneMembers();
		for (const AFGBoomBoxPlayer* Member : Channel->GetMembers())
		{
			if (const APlayerState* Carrier = GetCarrier(Member))
			{
				Channel->LastCarrier = Carrier;
				break;
			}
		}
		// A carried Boom Box is destroyed and made again when its player dies (and respawns, or picks it up from their
		// crate). Its queue is kept for that player until they carry a Boom Box again.
		if (Channel->GetMembers().Num() == 0 && Channel->LastCarrier.IsValid() && Channel->GetQueue().Num() > 0)
		{
			if (!Channel->bKeptForCarrier)
			{
				Channel->bKeptForCarrier = true;
				Channel->bResumeWhenAdopted = Channel->IsPlaying();
				if (Channel->bResumeWhenAdopted)
				{
					Channel->SetPlaying(false);
				}
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d lost its Boom Box; keeping its queue for %s until they carry a Boom Box again"),
					Channel->GetLinkCode(), *Channel->LastCarrier->GetPlayerName());
			}
			continue;
		}
		if (Channel->GetMembers().Num() == 0)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d has no Boom Boxes left; removing it"), Channel->GetLinkCode());
			Channels.RemoveAt(i);
			Channel->Destroy();
			ForceNetUpdate();
			continue;
		}
		EnsureMembersLoaded(Channel);
		// When the last Boom Box playing a channel switches to another tape, pause rather than run through the queue in silence.
		// A channel that never had one (queue built before the tape went in) keeps its state.
		const bool bHeard = Channel->GetMembers().ContainsByPredicate([this](const TObjectPtr<AFGBoomBoxPlayer>& Member)
		{
			return ActiveBoomBoxes.ContainsByPredicate([&Member](const FBBPActiveBoomBox& Active) { return Active.BoomBox == Member; });
		});
		if (Channel->bWasHeard && !bHeard && Channel->IsPlaying())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: Custom Music was taken out of the last Boom Box on channel %04d; pausing it"), Channel->GetLinkCode());
			Channel->SetPlaying(false);
		}
		Channel->bWasHeard = bHeard;
	}

	// Saved channels first, so their Boom Boxes get their saved queue back instead of an empty channel below.
	RestoreSavedChannels();
	AdoptKeptChannels();

	for (const FBBPActiveBoomBox& Active : ActiveBoomBoxes)
	{
		if (Active.BoomBox && !FindChannel(Active.BoomBox))
		{
			GetOrCreateChannel(Active.BoomBox);
		}
	}

	SaveSyncTimer -= ActiveBoomBoxRefreshInterval;
	if (SaveSyncTimer <= 0.f)
	{
		SaveSyncTimer = SaveSyncInterval;
		SaveChannels();
	}
}

const APlayerState* ABBPPlaylistSubsystem::GetCarrier(const AFGBoomBoxPlayer* BoomBox)
{
	const APawn* Pawn = IsValid(BoomBox) && BoomBox->IsInEquipmentMode() ? Cast<APawn>(BoomBox->GetmOwningCharacter()) : nullptr;
	return Pawn ? Pawn->GetPlayerState() : nullptr;
}

void ABBPPlaylistSubsystem::AdoptKeptChannels()
{
	TArray<ABBPMusicChannel*> Kept;
	for (ABBPMusicChannel* Channel : Channels)
	{
		if (IsValid(Channel) && Channel->bKeptForCarrier)
		{
			Kept.Add(Channel);
		}
	}
	for (ABBPMusicChannel* Channel : Kept)
	{
		const APlayerState* KeptFor = Channel->LastCarrier.Get();
		if (!KeptFor)
		{
			continue;
		}
		for (const FBBPActiveBoomBox& Active : ActiveBoomBoxes)
		{
			if (!IsValid(Active.BoomBox) || GetCarrier(Active.BoomBox) != KeptFor)
			{
				continue;
			}
			// Only a Boom Box with nothing of its own takes the kept queue; one with music on it keeps it.
			ABBPMusicChannel* Current = FindChannel(Active.BoomBox);
			if (Current && (Current->GetMembers().Num() > 1 || Current->GetQueue().Num() > 0))
			{
				continue;
			}
			if (Current)
			{
				LeaveChannel(Current, Active.BoomBox);
			}
			Channel->AddMember(Active.BoomBox);
			Channel->bKeptForCarrier = false;
			if (Channel->bResumeWhenAdopted)
			{
				Channel->SetPlaying(true);
			}
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s carries %s now; it gets back channel %04d's queue (%d track(s))"),
				*KeptFor->GetPlayerName(), *GetNameSafe(Active.BoomBox), Channel->GetLinkCode(), Channel->GetQueue().Num());
			EnsureMembersLoaded(Channel);
			ForceNetUpdate();
			break;
		}
	}
}

void ABBPPlaylistSubsystem::PreSaveGame_Implementation(int32 SaveVersion, int32 GameVersion)
{
	if (HasAuthority())
	{
		SaveChannels();
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: saving %d channel(s)"), SavedChannels.Num());
	}
}

FString ABBPPlaylistSubsystem::GetBoomBoxKey(const AFGBoomBoxPlayer* BoomBox)
{
	return BoomBox ? BoomBox->GetPathName() : FString();
}

void ABBPPlaylistSubsystem::RestoreSavedChannels(const AFGBoomBoxPlayer* Needed)
{
	if (PendingRestores.Num() == 0)
	{
		return;
	}
	TMap<FString, AFGBoomBoxPlayer*> BoomBoxesByKey;
	for (TActorIterator<AFGBoomBoxPlayer> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It))
		{
			BoomBoxesByKey.Add(GetBoomBoxKey(*It), *It);
		}
	}
	const bool bGraceOver = GetWorld()->GetTimeSeconds() >= RestoreDeadline;
	for (int32 i = PendingRestores.Num() - 1; i >= 0; --i)
	{
		const FBBPSavedChannel& Saved = PendingRestores[i];
		TArray<AFGBoomBoxPlayer*> Found;
		bool bHasNeeded = false;
		for (const FString& Key : Saved.Members)
		{
			AFGBoomBoxPlayer* const* BoomBox = BoomBoxesByKey.Find(Key);
			if (BoomBox && !FindChannel(*BoomBox))
			{
				Found.Add(*BoomBox);
				bHasNeeded |= *BoomBox == Needed;
			}
		}
		if (Found.Num() < Saved.Members.Num() && !bGraceOver && !bHasNeeded)
		{
			continue;
		}
		const FBBPSavedChannel Restoring = PendingRestores[i];
		PendingRestores.RemoveAt(i);
		if (Found.Num() == 0)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: none of a saved channel's %d Boom Box(es) exist any more; dropping its %d track(s)"),
				Restoring.Members.Num(), Restoring.Queue.Num());
			continue;
		}
		ABBPMusicChannel* Channel = SpawnChannel();
		if (!Channel)
		{
			continue;
		}
		for (AFGBoomBoxPlayer* Member : Found)
		{
			Channel->AddMember(Member);
		}
		Channel->RestoreFrom(Restoring);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: restored channel %04d from the save with %d of %d Boom Box(es)"),
			Channel->GetLinkCode(), Found.Num(), Restoring.Members.Num());
	}
}

void ABBPPlaylistSubsystem::SaveChannels()
{
	TArray<FString> NewSaved;
	NewSaved.Reserve(Channels.Num() + PendingRestores.Num());
	auto Add = [&NewSaved](const FBBPSavedChannel& Saved)
	{
		FString Json;
		if (FJsonObjectConverter::UStructToJsonObjectString(Saved, Json, 0, 0, 0, nullptr, false))
		{
			NewSaved.Add(MoveTemp(Json));
		}
		else
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Playlist: could not write a channel with %d track(s) to the save"), Saved.Queue.Num());
		}
	};
	for (const TObjectPtr<ABBPMusicChannel>& Channel : Channels)
	{
		if (!IsValid(Channel))
		{
			continue;
		}
		FBBPSavedChannel Saved = Channel->MakeSaveData();
		for (const TObjectPtr<AFGBoomBoxPlayer>& Member : Channel->GetMembers())
		{
			if (IsValid(Member))
			{
				Saved.Members.Add(GetBoomBoxKey(Member));
			}
		}
		// A lone Boom Box with an empty queue has nothing worth keeping.
		if (Saved.Members.Num() > 1 || (Saved.Members.Num() == 1 && Saved.Queue.Num() > 0))
		{
			Add(Saved);
		}
	}
	// Saved channels still waiting for their Boom Boxes are kept as they were.
	for (const FBBPSavedChannel& Pending : PendingRestores)
	{
		Add(Pending);
	}
	SavedChannels = MoveTemp(NewSaved);

	TArray<FString> NewRanges;
	for (const TPair<FString, float>& Pair : Ranges)
	{
		FBBPSavedRange Saved;
		Saved.Key = Pair.Key;
		Saved.Range = Pair.Value;
		FString Json;
		if (FJsonObjectConverter::UStructToJsonObjectString(Saved, Json, 0, 0, 0, nullptr, false))
		{
			NewRanges.Add(MoveTemp(Json));
		}
	}
	SavedRanges = MoveTemp(NewRanges);
}

float ABBPPlaylistSubsystem::GetRange(const AFGBoomBoxPlayer* BoomBox) const
{
	if (!BoomBox)
	{
		return BBPRange::Default;
	}
	if (const FBBPActiveBoomBox* Active = ActiveBoomBoxes.FindByPredicate([BoomBox](const FBBPActiveBoomBox& A) { return A.BoomBox == BoomBox; }))
	{
		return Active->Range;
	}
	const float* Found = Ranges.Find(GetBoomBoxKey(BoomBox));
	return Found ? *Found : BBPRange::Default;
}

void ABBPPlaylistSubsystem::SetRange(AFGBoomBoxPlayer* BoomBox, float Range)
{
	if (!HasAuthority() || !BoomBox || !FMath::IsFinite(Range))
	{
		return;
	}
	const float Clamped = FMath::Clamp(Range, BBPRange::Min, BBPRange::Max);
	if (FMath::IsNearlyEqual(Clamped, BBPRange::Default))
	{
		Ranges.Remove(GetBoomBoxKey(BoomBox));
	}
	else
	{
		Ranges.Add(GetBoomBoxKey(BoomBox), Clamped);
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s is now heard out to %.0f m"), *GetNameSafe(BoomBox), Clamped / 100.f);
	RefreshActiveBoomBoxes();
}

void ABBPPlaylistSubsystem::RefreshActiveBoomBoxes()
{
	TArray<FBBPActiveBoomBox> Found;
	for (TActorIterator<AFGBoomBoxPlayer> It(GetWorld()); It; ++It)
	{
		if (UBBPCustomMusicTape::IsCustomMusicTape(It->GetCurrentTape()))
		{
			FBBPActiveBoomBox& Active = Found.AddDefaulted_GetRef();
			Active.BoomBox = *It;
			Active.Volume = It->GetmState().mVolume;
			Active.Key = GetBoomBoxKey(*It);
			const float* Range = Ranges.Find(Active.Key);
			Active.Range = Range ? *Range : BBPRange::Default;
		}
	}
	if (Found != ActiveBoomBoxes)
	{
		if (Found.Num() != ActiveBoomBoxes.Num())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %d Boom Box(es) now have Custom Music loaded"), Found.Num());
		}
		ActiveBoomBoxes = MoveTemp(Found);
		ForceNetUpdate();
	}
}
