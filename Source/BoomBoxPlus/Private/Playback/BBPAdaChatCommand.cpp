#include "Playback/BBPAdaChatCommand.h"
#include "BoomBoxPlus.h"
#include "Command/CommandSender.h"
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
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const UBBPPlaybackController* Controller = Playlist ? Playlist->GetPlaybackController() : nullptr;
	UBBPAdaEasterEggs* Ada = Controller ? Controller->GetAdaEasterEggs() : nullptr;
	if (!Ada)
	{
		Sender->SendChatMessage(TEXT("ADA easter eggs aren't running on this machine."), FLinearColor::Red);
		return EExecutionStatus::UNCOMPLETED;
	}
	const FString& What = Arguments[0];
	if (What.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		Sender->SendChatMessage(TEXT("ADA lines: ") + FString::Join(Ada->GetLineNames(), TEXT(", ")));
		return EExecutionStatus::COMPLETED;
	}
	if (What.Equals(TEXT("volume"), ESearchCase::IgnoreCase))
	{
		if (Arguments.Num() < 2 || !Arguments[1].IsNumeric())
		{
			Sender->SendChatMessage(FString::Printf(TEXT("ADA voice volume is %.2f (only used when her lines can't play through Wwise). Use /ada volume <0-10>."), UBBPAdaEasterEggs::VoiceGain));
			return EExecutionStatus::COMPLETED;
		}
		UBBPAdaEasterEggs::VoiceGain = FMath::Clamp(FCString::Atof(*Arguments[1]), 0.f, 10.f);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: voice volume set to %.2f by command"), UBBPAdaEasterEggs::VoiceGain);
		Sender->SendChatMessage(FString::Printf(TEXT("ADA voice volume set to %.2f for this session."), UBBPAdaEasterEggs::VoiceGain));
		return EExecutionStatus::COMPLETED;
	}
	if (!Ada->GetLineNames().Contains(What))
	{
		Sender->SendChatMessage(TEXT("No ADA line called '") + What + TEXT("'. Try /ada list."), FLinearColor::Red);
		return EExecutionStatus::BAD_ARGUMENTS;
	}
	if (!Ada->PlayNow(What))
	{
		Sender->SendChatMessage(TEXT("ADA can't say that right now (already talking, the game is in the background, or ADA easter eggs are off)."), FLinearColor::Yellow);
		return EExecutionStatus::UNCOMPLETED;
	}
	return EExecutionStatus::COMPLETED;
}
