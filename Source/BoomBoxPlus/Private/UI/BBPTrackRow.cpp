#include "UI/BBPTrackRow.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/BBPWidgetStyle.h"

void UBBPTrackRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	if (AddFrontButton) AddFrontButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandleAddFront);
	if (AddEndButton) AddEndButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandleAddEnd);
	if (PlayButton) PlayButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandlePlay);
	if (MoveUpButton) MoveUpButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandleMoveUp);
	if (MoveDownButton) MoveDownButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandleMoveDown);
	if (RemoveButton) RemoveButton->OnClicked.AddDynamic(this, &UBBPTrackRow::HandleRemove);
}

void UBBPTrackRow::BuildDefaultLayout()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	WidgetTree->RootWidget = Row;

	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TextsSlot = Row->AddChildToHorizontalBox(Texts);
	TextsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextsSlot->SetVerticalAlignment(VAlign_Center);

	TitleText = BBPWidgetStyle::MakeText(WidgetTree, 13, BBPWidgetStyle::TextColor);
	Texts->AddChildToVerticalBox(TitleText);
	DetailText = BBPWidgetStyle::MakeText(WidgetTree, 10, BBPWidgetStyle::DimTextColor);
	Texts->AddChildToVerticalBox(DetailText);

	auto AddButton = [this, Row](const TCHAR* Label) -> UButton*
	{
		UButton* Button = BBPWidgetStyle::MakeButton(WidgetTree, FText::FromString(Label));
		UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button);
		ButtonSlot->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
		return Button;
	};
	AddFrontButton = AddButton(TEXT("+ Front"));
	AddEndButton = AddButton(TEXT("+ End"));
	PlayButton = AddButton(TEXT("Play"));
	MoveUpButton = AddButton(TEXT("Up"));
	MoveDownButton = AddButton(TEXT("Down"));
	RemoveButton = AddButton(TEXT("X"));
}

void UBBPTrackRow::SetupAsResult(const FBBPTrack& InTrack)
{
	Track = InTrack;
	EntryId = INDEX_NONE;
	Index = INDEX_NONE;

	if (TitleText) TitleText->SetText(FText::FromString(Track.Title));
	if (DetailText) DetailText->SetText(FText::FromString(FString::Printf(TEXT("%s   %s"), *Track.Artist, *UBBPBlueprintLibrary::FormatDuration(Track.Duration))));

	BBPWidgetStyle::SetShown(AddFrontButton, true);
	BBPWidgetStyle::SetShown(AddEndButton, true);
	BBPWidgetStyle::SetShown(PlayButton, false);
	BBPWidgetStyle::SetShown(MoveUpButton, false);
	BBPWidgetStyle::SetShown(MoveDownButton, false);
	BBPWidgetStyle::SetShown(RemoveButton, false);
}

void UBBPTrackRow::SetupAsQueueEntry(const FBBPQueueEntry& Entry, int32 InIndex, int32 QueueLength, EBBPEntryAvailability Availability)
{
	Track = Entry.Track;
	EntryId = Entry.EntryId;
	Index = InIndex;

	const bool bPlaying = Availability == EBBPEntryAvailability::Playing;
	const bool bMissing = Availability == EBBPEntryAvailability::NotInLibrary;

	FString Title = FString::Printf(TEXT("%d. %s"), InIndex + 1, *Track.Title);
	if (bPlaying)
	{
		Title = TEXT("> ") + Title;
	}
	FString Detail = FString::Printf(TEXT("%s   %s"), *Track.Artist, *UBBPBlueprintLibrary::FormatDuration(Track.Duration));
	if (bMissing)
	{
		Detail += TEXT("   (not in your library)");
	}
	if (!Entry.AddedBy.IsEmpty())
	{
		Detail += FString::Printf(TEXT("   added by %s"), *Entry.AddedBy);
	}

	if (TitleText)
	{
		TitleText->SetText(FText::FromString(Title));
		TitleText->SetColorAndOpacity(bPlaying ? BBPWidgetStyle::AccentColor : bMissing ? BBPWidgetStyle::DimTextColor : BBPWidgetStyle::TextColor);
	}
	if (DetailText)
	{
		DetailText->SetText(FText::FromString(Detail));
	}

	BBPWidgetStyle::SetShown(AddFrontButton, false);
	BBPWidgetStyle::SetShown(AddEndButton, false);
	BBPWidgetStyle::SetShown(PlayButton, !bPlaying);
	BBPWidgetStyle::SetShown(MoveUpButton, InIndex > 0);
	BBPWidgetStyle::SetShown(MoveDownButton, InIndex < QueueLength - 1);
	BBPWidgetStyle::SetShown(RemoveButton, true);
}

void UBBPTrackRow::HandleAddFront()
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: +Front '%s'"), *Track.Title);
	UBBPBlueprintLibrary::RequestAddTrack(this, Track, true);
}

void UBBPTrackRow::HandleAddEnd()
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: +End '%s'"), *Track.Title);
	UBBPBlueprintLibrary::RequestAddTrack(this, Track, false);
}

void UBBPTrackRow::HandlePlay()
{
	UBBPBlueprintLibrary::RequestPlayEntry(this, EntryId);
}

void UBBPTrackRow::HandleMoveUp()
{
	UBBPBlueprintLibrary::RequestMoveEntry(this, EntryId, Index - 1);
}

void UBBPTrackRow::HandleMoveDown()
{
	UBBPBlueprintLibrary::RequestMoveEntry(this, EntryId, Index + 1);
}

void UBBPTrackRow::HandleRemove()
{
	UBBPBlueprintLibrary::RequestRemoveEntry(this, EntryId);
}
