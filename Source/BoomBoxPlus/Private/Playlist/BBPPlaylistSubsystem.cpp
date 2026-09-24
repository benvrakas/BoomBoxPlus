#include "Playlist/BBPPlaylistSubsystem.h"
#include "BoomBoxPlus.h"
#include "EngineUtils.h"
#include "FGBoomBoxPlayer.h"
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

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist subsystem started (%s)"), HasAuthority() ? TEXT("server") : TEXT("client"));

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
	ABBPMusicChannel* Target = FindChannelByCode(Code);
	if (!Target)
	{
		OutMessage = FString::Printf(TEXT("No Boom Box uses code %04d."), Code);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s tried unknown code %04d"), *GetNameSafe(BoomBox), Code);
		return false;
	}
	ABBPMusicChannel* Current = FindChannel(BoomBox);
	if (Current == Target)
	{
		OutMessage = TEXT("This Boom Box already uses that code.");
		return false;
	}
	if (Current)
	{
		// Only the last Boom Box on a channel brings its queue along; others keep playing it.
		if (Current->GetMembers().Num() == 1)
		{
			Target->MergeFrom(*Current);
		}
		LeaveChannel(Current, BoomBox);
	}
	Target->AddMember(BoomBox);
	OutMessage = FString::Printf(TEXT("Linked. %d Boom Boxes now share one queue; both queues were merged."), Target->GetMembers().Num());
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: %s linked to channel %04d"), *GetNameSafe(BoomBox), Code);
	return true;
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
		// A channel no Boom Box is playing would otherwise run through its queue in silence.
		const bool bHeard = Channel->GetMembers().ContainsByPredicate([this](const TObjectPtr<AFGBoomBoxPlayer>& Member)
		{
			return ActiveBoomBoxes.ContainsByPredicate([&Member](const FBBPActiveBoomBox& Active) { return Active.BoomBox == Member; });
		});
		if (!bHeard && Channel->IsPlaying())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Playlist: no Boom Box on channel %04d has Custom Music loaded; pausing it"), Channel->GetLinkCode());
			Channel->SetPlaying(false);
		}
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
