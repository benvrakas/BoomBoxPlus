#include "Tape/BBPCustomMusicTape.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

UBBPCustomMusicTape::UBBPCustomMusicTape()
{
	mTitle = LOCTEXT("CustomMusicTitle", "Custom Music");
	mDescription = LOCTEXT("CustomMusicDescription", "Your own music, YouTube and SoundCloud.");

	const TSoftObjectPtr<UTexture2D> EmptyTapeIcon(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BoomBox/TXUI_Tape_EmptyTape.TXUI_Tape_EmptyTape")));
	mSmallIcon = EmptyTapeIcon;
	mBigIcon = EmptyTapeIcon;
}

bool UBBPCustomMusicTape::IsCustomMusicTape(TSubclassOf<UFGTapeData> Tape)
{
	return Tape && Tape->IsChildOf(UBBPCustomMusicTape::StaticClass());
}

#undef LOCTEXT_NAMESPACE
