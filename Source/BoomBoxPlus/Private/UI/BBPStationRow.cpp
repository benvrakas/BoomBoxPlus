#include "UI/BBPStationRow.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

void UBBPStationRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (PlayButton) PlayButton->OnClicked.AddDynamic(this, &UBBPStationRow::HandlePlay);
	if (RemoveButton) RemoveButton->OnClicked.AddDynamic(this, &UBBPStationRow::HandleRemove);
	if (NameBox) NameBox->OnTextCommitted.AddDynamic(this, &UBBPStationRow::HandleNameCommitted);
}

void UBBPStationRow::BuildDefaultLayout()
{
	using namespace BBPWidgetStyle;

	RowBackground = MakePanel(WidgetTree, RowColor, FMargin(8.f, 5.f));
	WidgetTree->RootWidget = RowBackground;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	RowBackground->SetContent(Row);

	UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	TextSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	NameBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	StyleTextBox(NameBox, 12);
	NameBox->SetToolTipText(LOCTEXT("RenameStationTip", "Click to rename this station, then press Enter."));
	Text->AddChildToVerticalBox(NameBox);
	AddressText = MakeText(WidgetTree, 10, DimTextColor);
	Truncate(AddressText);
	Text->AddChildToVerticalBox(AddressText)->SetPadding(FMargin(2.f, 2.f, 0.f, 0.f));

	PlayButton = MakeButton(WidgetTree, LOCTEXT("PlayStation", "Play"), true);
	UHorizontalBoxSlot* PlaySlot = Row->AddChildToHorizontalBox(PlayButton);
	PlaySlot->SetVerticalAlignment(VAlign_Center);
	PlaySlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	RemoveButton = MakeButton(WidgetTree, LOCTEXT("RemoveStation", "Remove"), true);
	RemoveButton->SetToolTipText(LOCTEXT("RemoveStationTip", "Remove this station from your list"));
	Row->AddChildToHorizontalBox(RemoveButton)->SetVerticalAlignment(VAlign_Center);
}

void UBBPStationRow::SetStation(const FBBPTrack& InStation, bool bPlaying)
{
	Station = InStation;
	// Left alone while the player is typing a new name.
	if (NameBox && !NameBox->HasKeyboardFocus())
	{
		NameBox->SetText(BBPWidgetStyle::ToDisplayText(Station.Title));
	}
	if (AddressText)
	{
		AddressText->SetText(FText::FromString(Station.SourceRef));
		AddressText->SetToolTipText(FText::FromString(Station.SourceRef));
	}
	if (RowBackground)
	{
		RowBackground->SetBrushColor(bPlaying ? BBPWidgetStyle::PlayingRowColor : BBPWidgetStyle::RowColor);
	}
	if (PlayButton)
	{
		PlayButton->SetLabel(bPlaying ? LOCTEXT("StationPlaying", "Playing") : LOCTEXT("PlayStation", "Play"));
		PlayButton->SetIsEnabled(!bPlaying);
	}
}

void UBBPStationRow::HandlePlay()
{
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>())
	{
		Page->PlayStation(Station);
	}
}

void UBBPStationRow::HandleRemove()
{
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>())
	{
		Page->ForgetStation(Station.Id);
	}
}

void UBBPStationRow::HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	const FString Name = Text.ToString().TrimStartAndEnd();
	UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>();
	if (CommitMethod == ETextCommit::OnCleared || Name.IsEmpty() || Name == Station.Title || !Page || !Page->RenameStation(Station.Id, Name))
	{
		if (NameBox)
		{
			NameBox->SetText(BBPWidgetStyle::ToDisplayText(Station.Title));
		}
	}
}

#undef LOCTEXT_NAMESPACE
