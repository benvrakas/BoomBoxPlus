#include "Playlist/BBPBoomBoxChatCommand.h"
#include "BoomBoxPlus.h"
#include "Command/CommandSender.h"
#include "FGPlayerController.h"
#include "Network/BBPRemoteCallObject.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"

ABBPBoomBoxChatCommand::ABBPBoomBoxChatCommand()
{
	CommandName = TEXT("bbp");
	MinNumberOfArguments = 1;
	bOnlyUsableByPlayer = false;
	Usage = NSLOCTEXT("BoomBoxPlus", "BbpCommandUsage", "/bbp list - Boom Boxes and what your game streams | /bbp stopall - pause every Custom Music Boom Box (host only)");
}

EExecutionStatus ABBPBoomBoxChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	if (Arguments[0].Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		return List(Sender);
	}
	if (Arguments[0].Equals(TEXT("stopall"), ESearchCase::IgnoreCase))
	{
		return StopAll(Sender);
	}
	Sender->SendChatMessage(TEXT("Unknown option. Use /bbp list or /bbp stopall."), FLinearColor::Red);
	return EExecutionStatus::BAD_ARGUMENTS;
}

EExecutionStatus ABBPBoomBoxChatCommand::List(UCommandSender* Sender)
{
	AFGPlayerController* Player = Sender->IsPlayerSender() ? Sender->GetPlayer() : nullptr;
	if (Player && !Player->IsLocalController())
	{
		UBBPRemoteCallObject* RCO = Player->GetRemoteCallObjectOfClass<UBBPRemoteCallObject>();
		if (!RCO)
		{
			Sender->SendChatMessage(TEXT("Couldn't ask your game."), FLinearColor::Red);
			return EExecutionStatus::UNCOMPLETED;
		}
		RCO->Client_ListStreams();
		return EExecutionStatus::COMPLETED;
	}
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const UBBPPlaybackController* Controller = Playlist ? Playlist->GetPlaybackController() : nullptr;
	if (!Controller)
	{
		Sender->SendChatMessage(TEXT("Nothing plays on this machine (no playback here, e.g. a dedicated server)."), FLinearColor::Yellow);
		return EExecutionStatus::UNCOMPLETED;
	}
	TArray<FString> Lines;
	Controller->DescribeStreams(Lines);
	for (const FString& Line : Lines)
	{
		Sender->SendChatMessage(Line);
	}
	return EExecutionStatus::COMPLETED;
}

EExecutionStatus ABBPBoomBoxChatCommand::StopAll(UCommandSender* Sender)
{
	// The host's own player controller is local on the server; other players' aren't.
	const AFGPlayerController* Player = Sender->IsPlayerSender() ? Sender->GetPlayer() : nullptr;
	if (Sender->IsPlayerSender() && (!Player || !Player->IsLocalController()))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Command: /bbp stopall refused for %s (not the host)"), *GetNameSafe(Player));
		Sender->SendChatMessage(TEXT("Only the host can stop every Boom Box."), FLinearColor::Red);
		return EExecutionStatus::INSUFFICIENT_PERMISSIONS;
	}
	ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	if (!Playlist)
	{
		Sender->SendChatMessage(TEXT("BoomBoxPlus isn't running in this world."), FLinearColor::Red);
		return EExecutionStatus::UNCOMPLETED;
	}
	int32 Stopped = 0;
	for (ABBPMusicChannel* Channel : Playlist->GetChannels())
	{
		if (IsValid(Channel) && Channel->IsPlaying())
		{
			Channel->SetPlaying(false);
			++Stopped;
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Command: /bbp stopall paused %d channel(s)"), Stopped);
	Sender->SendChatMessage(Stopped == 0 ? FString(TEXT("No Boom Box was playing Custom Music."))
		: FString::Printf(TEXT("Paused %d Custom Music queue%s. Press Play on a Boom Box to start it again."), Stopped, Stopped == 1 ? TEXT("") : TEXT("s")));
	return EExecutionStatus::COMPLETED;
}
