#include "Playback/BBPAdaChatCommand.h"
#include "BoomBoxPlus.h"
#include "Command/CommandSender.h"
#include "FGPlayerController.h"
#include "Network/BBPRemoteCallObject.h"
#include "Playback/BBPAdaEasterEggs.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPPlaylistSubsystem.h"

ABBPAdaChatCommand::ABBPAdaChatCommand()
{
	CommandName = TEXT("ada");
	MinNumberOfArguments = 1;
	bOnlyUsableByPlayer = true;
	Usage = NSLOCTEXT("BoomBoxPlus", "AdaCommandUsage", "/ada <line> | /ada list | /ada volume <0-10>");
}

EExecutionStatus ABBPAdaChatCommand::ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label)
{
	AFGPlayerController* Player = Sender->GetPlayer();
	if (Player && !Player->IsLocalController())
	{
		UBBPRemoteCallObject* RCO = Player->GetRemoteCallObjectOfClass<UBBPRemoteCallObject>();
		if (!RCO)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: /ada from %s: no remote call object to send it on"), *GetNameSafe(Player));
			Sender->SendChatMessage(TEXT("Couldn't send that to your game."), FLinearColor::Red);
			return EExecutionStatus::UNCOMPLETED;
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: sending /ada %s on to %s's game"), *FString::Join(Arguments, TEXT(" ")), *GetNameSafe(Player));
		RCO->Client_AdaCommand(Arguments);
		return EExecutionStatus::COMPLETED;
	}
	FString Reply;
	FLinearColor Color = FLinearColor::White;
	const EExecutionStatus Status = RunLocally(this, Arguments, Reply, Color);
	if (!Reply.IsEmpty())
	{
		Sender->SendChatMessage(Reply, Color);
	}
	return Status;
}

EExecutionStatus ABBPAdaChatCommand::RunLocally(const UObject* WorldContext, const TArray<FString>& Arguments, FString& OutReply, FLinearColor& OutColor)
{
	OutColor = FLinearColor::White;
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(WorldContext);
	const UBBPPlaybackController* Controller = Playlist ? Playlist->GetPlaybackController() : nullptr;
	UBBPAdaEasterEggs* Ada = Controller ? Controller->GetAdaEasterEggs() : nullptr;
	if (!Ada || Arguments.Num() == 0)
	{
		OutReply = TEXT("ADA easter eggs aren't running on this machine.");
		OutColor = FLinearColor::Red;
		return EExecutionStatus::UNCOMPLETED;
	}
	const FString& What = Arguments[0];
	if (What.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		OutReply = TEXT("ADA lines: ") + FString::Join(Ada->GetLineNames(), TEXT(", "));
		return EExecutionStatus::COMPLETED;
	}
	if (What.Equals(TEXT("volume"), ESearchCase::IgnoreCase))
	{
		if (Arguments.Num() < 2 || !Arguments[1].IsNumeric())
		{
			OutReply = FString::Printf(TEXT("ADA voice volume is %.2f (only used when her lines can't play through Wwise). Use /ada volume <0-10>."), UBBPAdaEasterEggs::VoiceGain);
			return EExecutionStatus::COMPLETED;
		}
		UBBPAdaEasterEggs::VoiceGain = FMath::Clamp(FCString::Atof(*Arguments[1]), 0.f, 10.f);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: voice volume set to %.2f by command"), UBBPAdaEasterEggs::VoiceGain);
		OutReply = FString::Printf(TEXT("ADA voice volume set to %.2f for this session."), UBBPAdaEasterEggs::VoiceGain);
		return EExecutionStatus::COMPLETED;
	}
	if (!Ada->GetLineNames().Contains(What))
	{
		OutReply = TEXT("No ADA line called '") + What + TEXT("'. Try /ada list.");
		OutColor = FLinearColor::Red;
		return EExecutionStatus::BAD_ARGUMENTS;
	}
	if (!Ada->PlayNow(What))
	{
		OutReply = TEXT("ADA can't say that right now (already talking, the game is in the background, or ADA easter eggs are off).");
		OutColor = FLinearColor::Yellow;
		return EExecutionStatus::UNCOMPLETED;
	}
	return EExecutionStatus::COMPLETED;
}
