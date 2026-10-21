#include "Playlist/BBPStopAllChatCommand.h"
#include "BoomBoxPlus.h"
#include "Command/CommandSender.h"
#include "FGPlayerController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"

ABBPStopAllChatCommand::ABBPStopAllChatCommand()
{
	CommandName = TEXT("bbp");
	MinNumberOfArguments = 1;
	bOnlyUsableByPlayer = false;
	Usage = NSLOCTEXT("BoomBoxPlus", "BbpCommandUsage", "/bbp stopall - pause every Custom Music Boom Box in the world (host only)");
}

EExecutionStatus ABBPStopAllChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	if (!Arguments[0].Equals(TEXT("stopall"), ESearchCase::IgnoreCase))
	{
		Sender->SendChatMessage(TEXT("Unknown option. Use /bbp stopall."), FLinearColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}
	// Chat commands run on the server: the host's own player controller is local there; other players' aren't.
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
