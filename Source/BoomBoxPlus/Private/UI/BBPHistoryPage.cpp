#include "UI/BBPHistoryPage.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Playback/BBPPlaybackController.h"
#include "Playback/BBPSongHistory.h"
#include "UI/BBPHistoryRow.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

namespace
{
	// Seconds between checks for new history entries.
	constexpr float CheckInterval = 0.5f;
}

void UBBPHistoryPage::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
}

void UBBPHistoryPage::BuildDefaultLayout()
{
	using namespace BBPWidgetStyle;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Column;

	UTextBlock* Title = nullptr;
	Column->AddChildToVerticalBox(MakeSectionHeader(WidgetTree, LOCTEXT("HistoryHeader", "Played on this Boom Box"), Title));
	TitleText = Title;
	EmptyText = MakeText(WidgetTree, 11, DimTextColor,
		LOCTEXT("HistoryEmpty", "Nothing yet. Songs this Boom Box plays show up here, including the songs radio stations announce."));
	EmptyText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(EmptyText);

	UBorder* ListPanel = MakePanel(WidgetTree, InsetColor, FMargin(6.f));
	UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(ListPanel);
	ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ListSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	HistoryList = WidgetTree->ConstructWidget<UScrollBox>();
	StyleScrollBox(HistoryList);
	ListPanel->SetContent(HistoryList);
}

void UBBPHistoryPage::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	CheckTimer -= InDeltaTime;
	if (CheckTimer > 0.f)
	{
		return;
	}
	CheckTimer = CheckInterval;
	const UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>();
	const UBBPPlaybackController* Controller = Page ? Page->GetPlaybackController() : nullptr;
	const UBBPSongHistory* History = Controller ? Controller->GetSongHistory() : nullptr;
	if (History && History->GetRevision() != ShownRevision)
	{
		Refresh();
	}
}

void UBBPHistoryPage::Refresh()
{
	const UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>();
	const UBBPPlaybackController* Controller = Page ? Page->GetPlaybackController() : nullptr;
	const UBBPSongHistory* History = Controller ? Controller->GetSongHistory() : nullptr;
	static const TArray<FBBPHistoryEntry> None;
	const TArray<FBBPHistoryEntry>& Entries = History ? History->GetHistory(Page->GetBoomBox()) : None;
	ShownRevision = History ? History->GetRevision() : -1;

	if (TitleText)
	{
		TitleText->SetText(FText::Format(LOCTEXT("HistoryHeaderCount", "Played on this Boom Box ({0})"), Entries.Num()));
	}
	BBPWidgetStyle::SetShown(EmptyText, Entries.Num() == 0);
	if (!HistoryList)
	{
		return;
	}
	while (Rows.Num() < Entries.Num())
	{
		UBBPHistoryRow* Row = CreateWidget<UBBPHistoryRow>(this, UBBPHistoryRow::StaticClass());
		if (!Row)
		{
			break;
		}
		HistoryList->AddChild(Row);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(Row->Slot))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
		Rows.Add(Row);
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const bool bShow = Entries.IsValidIndex(i);
		if (bShow)
		{
			Rows[i]->SetEntry(Entries[i]);
		}
		BBPWidgetStyle::SetShown(Rows[i], bShow);
	}
}

#undef LOCTEXT_NAMESPACE
