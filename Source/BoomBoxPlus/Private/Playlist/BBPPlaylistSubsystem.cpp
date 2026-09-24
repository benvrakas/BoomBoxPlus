#include "Playlist/BBPPlaylistSubsystem.h"
#include "BoomBoxPlus.h"
#include "EngineUtils.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Subsystem/SubsystemActorManager.h"
#include "Tape/BBPCustomMusicTape.h"

namespace
{
	constexpr float ActiveBoomBoxRefreshInterval = 0.5f;
	constexpr int32 MinLinkCode = 1000;
	constexpr int32 MaxLinkCode = 9999;
}

ABBPPlaylistSubsystem::ABBPPlaylistSubsystem()
{
	ReplicationPolicy = ESubsystemReplicationPolicy::SpawnOnServer_Replicate;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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

	// Channels, link codes and pending link requests are runtime-only (no SaveGame properties anywhere in this
	// class or ABBPMusicChannel), so this array starts empty on every BeginPlay - a fresh session, a reloaded
	// save or a world restart all reset every Boom Box to its own unlinked channel with a freshly rolled code.
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist subsystem started (%s); %d channel(s), %d pending link request(s)"),
		HasAuthority() ? TEXT("server") : TEXT("client"), Channels.Num(), LinkRequests.Num());

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
		return Request.FromCode == Code && Request.ToCode == OwnCode;
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
	ForceNetUpdate();
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
		if (Channel->GetMembers().Num() == 0)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: channel %04d has no Boom Boxes left; removing it"), Channel->GetLinkCode());
			Channels.RemoveAt(i);
			Channel->Destroy();
			ForceNetUpdate();
			continue;
		}
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

	for (const FBBPActiveBoomBox& Active : ActiveBoomBoxes)
	{
		if (Active.BoomBox && !FindChannel(Active.BoomBox))
		{
			GetOrCreateChannel(Active.BoomBox);
		}
	}
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
