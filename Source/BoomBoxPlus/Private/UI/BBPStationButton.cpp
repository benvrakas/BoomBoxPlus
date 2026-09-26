#include "UI/BBPStationButton.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

namespace
{
	constexpr int32 MaxLabelLength = 24;
}

void UBBPStationButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (PlayButton) PlayButton->OnClicked.AddDynamic(this, &UBBPStationButton::HandlePlay);
	if (RemoveButton) RemoveButton->OnClicked.AddDynamic(this, &UBBPStationButton::HandleRemove);
}

void UBBPStationButton::BuildDefaultLayout()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	WidgetTree->RootWidget = Row;
	PlayButton = BBPWidgetStyle::MakeButton(WidgetTree, FText::GetEmpty(), true);
	Row->AddChildToHorizontalBox(PlayButton)->SetPadding(FMargin(0.f, 0.f, 1.f, 0.f));
	RemoveButton = BBPWidgetStyle::MakeButton(WidgetTree, FText::FromString(TEXT("x")), true);
	RemoveButton->SetToolTipText(NSLOCTEXT("BoomBoxPlus", "RemoveStation", "Remove this station from the list"));
	Row->AddChildToHorizontalBox(RemoveButton);
}

void UBBPStationButton::SetStation(const FBBPTrack& InStation)
{
	Station = InStation;
	if (PlayButton)
	{
		const FString Title = BBPWidgetStyle::ToDisplayText(Station.Title).ToString();
		PlayButton->SetLabel(FText::FromString(Title.Len() > MaxLabelLength ? Title.Left(MaxLabelLength - 1) + TEXT("...") : Title));
		PlayButton->SetToolTipText(FText::FromString(Station.SourceRef));
	}
}

void UBBPStationButton::HandlePlay()
{
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>())
	{
		Page->PlayStation(Station);
	}
}

void UBBPStationButton::HandleRemove()
{
	if (UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>())
	{
		Page->ForgetStation(Station.Id);
	}
}
