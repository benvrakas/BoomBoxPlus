#include "UI/BBPTrackRow.h"
#include "BoomBoxPlus.h"
#include "FGBoomBoxPlayer.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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
	RowBackground = BBPWidgetStyle::MakePanel(WidgetTree, BBPWidgetStyle::RowColor, FMargin(0.f));
	WidgetTree->RootWidget = RowBackground;

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	RowBackground->SetContent(Row);

	UBorder* Marker = BBPWidgetStyle::MakePanel(WidgetTree, BBPWidgetStyle::AccentColor, FMargin(2.f, 0.f));
	PlayingMarker = Marker;
	Row->AddChildToHorizontalBox(Marker)->SetVerticalAlignment(VAlign_Fill);

	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TextsSlot = Row->AddChildToHorizontalBox(Texts);
	TextsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextsSlot->SetVerticalAlignment(VAlign_Center);
	TextsSlot->SetPadding(FMargin(10.f, 6.f, 8.f, 6.f));
	Texts->SetClipping(EWidgetClipping::ClipToBounds);

	TitleText = BBPWidgetStyle::MakeText(WidgetTree, 13, BBPWidgetStyle::TextColor, FText::GetEmpty(), BBPWidgetStyle::EFontWeight::SemiBold);
	BBPWidgetStyle::Truncate(TitleText);
	Texts->AddChildToVerticalBox(TitleText);
	UHorizontalBox* DetailRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Texts->AddChildToVerticalBox(DetailRow)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));
	SourceText = BBPWidgetStyle::MakeText(WidgetTree, 9, BBPWidgetStyle::AccentColor, FText::GetEmpty(), BBPWidgetStyle::EFontWeight::Bold);
	DetailRow->AddChildToHorizontalBox(SourceText)->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	DetailText = BBPWidgetStyle::MakeText(WidgetTree, 10, BBPWidgetStyle::DimTextColor);
	BBPWidgetStyle::Truncate(DetailText);
	DetailRow->AddChildToHorizontalBox(DetailText);

	auto AddButton = [this, Row](const FText& Label) -> UBBPGameButton*
	{
		UBBPGameButton* Button = BBPWidgetStyle::MakeButton(WidgetTree, Label, true);
		UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button);
		ButtonSlot->SetPadding(FMargin(0.f, 4.f, 4.f, 4.f));
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
		return Button;
	};
	AddFrontButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowPlayNext", "Play Next"));
	AddEndButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowAdd", "Add"));
	PlayButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowPlay", "Play"));
	MoveUpButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowUp", "Up"));
	MoveDownButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowDown", "Down"));
	RemoveButton = AddButton(NSLOCTEXT("BoomBoxPlus", "RowRemove", "Remove"));
}

void UBBPTrackRow::SetupAsResult(const FBBPTrack& InTrack)
{
	Track = InTrack;
	EntryId = INDEX_NONE;
	Index = INDEX_NONE;

	if (TitleText) TitleText->SetText(FText::FromString(Track.Title));
	ShowSourceTag();
	if (DetailText) DetailText->SetText(FText::FromString(Track.IsLive() ? Track.Artist
		: FString::Printf(TEXT("%s   %s"), *Track.Artist, *UBBPBlueprintLibrary::FormatDuration(Track.Duration))));
	if (RowBackground) RowBackground->SetBrushColor(BBPWidgetStyle::RowColor);
	BBPWidgetStyle::SetShown(PlayingMarker, false);

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

	const FString Title = FString::Printf(TEXT("%d.  %s"), InIndex + 1, *Track.Title);
	ShowSourceTag();
	if (RowBackground) RowBackground->SetBrushColor(bPlaying ? BBPWidgetStyle::PlayingRowColor : BBPWidgetStyle::RowColor);
	BBPWidgetStyle::SetShown(PlayingMarker, bPlaying);
	FString Detail = Track.IsLive() ? Track.Artist : FString::Printf(TEXT("%s   %s"), *Track.Artist, *UBBPBlueprintLibrary::FormatDuration(Track.Duration));
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
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>()) Page->EnsureCustomMusicLoaded();
	UBBPBlueprintLibrary::RequestAddTrack(BoomBox.Get(), Track, true);
}

void UBBPTrackRow::HandleAddEnd()
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: +End '%s'"), *Track.Title);
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>()) Page->EnsureCustomMusicLoaded();
	UBBPBlueprintLibrary::RequestAddTrack(BoomBox.Get(), Track, false);
}

void UBBPTrackRow::HandlePlay()
{
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>()) Page->EnsureCustomMusicLoaded();
	UBBPBlueprintLibrary::RequestPlayEntry(BoomBox.Get(), EntryId);
}

void UBBPTrackRow::HandleMoveUp()
{
	UBBPBlueprintLibrary::RequestMoveEntry(BoomBox.Get(), EntryId, Index - 1);
}

void UBBPTrackRow::HandleMoveDown()
{
	UBBPBlueprintLibrary::RequestMoveEntry(BoomBox.Get(), EntryId, Index + 1);
}

void UBBPTrackRow::HandleRemove()
{
	UBBPBlueprintLibrary::RequestRemoveEntry(BoomBox.Get(), EntryId);
}

void UBBPTrackRow::SetBoomBox(AFGBoomBoxPlayer* InBoomBox)
{
	BoomBox = InBoomBox;
}

void UBBPTrackRow::ShowSourceTag()
{
	const TCHAR* Tag = Track.Source == EBBPTrackSource::YouTube ? TEXT("YOUTUBE") : Track.Source == EBBPTrackSource::SoundCloud ? TEXT("SOUNDCLOUD")
		: Track.IsLive() ? TEXT("LIVE") : TEXT("");
	if (SourceText)
	{
		SourceText->SetText(FText::FromString(Tag));
		BBPWidgetStyle::SetShown(SourceText, *Tag != 0);
	}
}
