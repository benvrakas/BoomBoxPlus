#include "UI/BBPHistoryRow.h"
#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

void UBBPHistoryRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (PlayButton) PlayButton->OnClicked.AddDynamic(this, &UBBPHistoryRow::HandlePlay);
	if (CopyButton) CopyButton->OnClicked.AddDynamic(this, &UBBPHistoryRow::HandleCopy);
}

void UBBPHistoryRow::BuildDefaultLayout()
{
	using namespace BBPWidgetStyle;

	UBorder* Background = MakePanel(WidgetTree, RowColor, FMargin(8.f, 5.f));
	WidgetTree->RootWidget = Background;
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Background->SetContent(Row);

	UVerticalBox* Text = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(Text);
	TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TextSlot->SetVerticalAlignment(VAlign_Center);
	TextSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
	TitleText = MakeText(WidgetTree, 12, TextColor, FText::GetEmpty(), EFontWeight::SemiBold);
	Truncate(TitleText);
	Text->AddChildToVerticalBox(TitleText);
	SourceText = MakeText(WidgetTree, 10, DimTextColor);
	Truncate(SourceText);
	Text->AddChildToVerticalBox(SourceText)->SetPadding(FMargin(0.f, 1.f, 0.f, 0.f));

	TimeText = MakeText(WidgetTree, 10, DimTextColor);
	UHorizontalBoxSlot* TimeSlot = Row->AddChildToHorizontalBox(TimeText);
	TimeSlot->SetVerticalAlignment(VAlign_Center);
	TimeSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));

	PlayButton = MakeButton(WidgetTree, LOCTEXT("HistoryPlay", "Play"), true);
	UHorizontalBoxSlot* PlaySlot = Row->AddChildToHorizontalBox(PlayButton);
	PlaySlot->SetVerticalAlignment(VAlign_Center);
	PlaySlot->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
	CopyButton = MakeButton(WidgetTree, LOCTEXT("HistoryCopyLink", "Copy Link"), true);
	Row->AddChildToHorizontalBox(CopyButton)->SetVerticalAlignment(VAlign_Center);
}

FString UBBPHistoryRow::GetSongName(const FBBPHistoryEntry& InEntry)
{
	if (!InEntry.RadioSong.IsEmpty())
	{
		return InEntry.RadioSong;
	}
	return InEntry.Track.Artist.IsEmpty() ? InEntry.Track.Title : InEntry.Track.Artist + TEXT(" - ") + InEntry.Track.Title;
}

FString UBBPHistoryRow::GetLink() const
{
	if (!Entry.RadioSong.IsEmpty() || Entry.Track.Source == EBBPTrackSource::Local)
	{
		return FString();
	}
	return Entry.Track.SourceRef;
}

void UBBPHistoryRow::SetEntry(const FBBPHistoryEntry& InEntry)
{
	Entry = InEntry;
	const bool bRadioSong = !Entry.RadioSong.IsEmpty();
	if (TitleText)
	{
		TitleText->SetText(BBPWidgetStyle::ToDisplayText(bRadioSong ? Entry.RadioSong : Entry.Track.Title));
	}
	if (SourceText)
	{
		FString Source;
		if (Entry.Track.IsLive())
		{
			Source = FString::Printf(TEXT("Radio: %s"), *Entry.Track.Title);
		}
		else
		{
			const FString SourceName = UBBPBlueprintLibrary::GetSourceName(Entry.Track);
			Source = Entry.Track.Artist.IsEmpty() ? SourceName : Entry.Track.Artist + TEXT(", ") + SourceName;
		}
		SourceText->SetText(BBPWidgetStyle::ToDisplayText(Source));
	}
	if (TimeText)
	{
		const FDateTime PlayedAt = FDateTime::FromUnixTimestamp(Entry.PlayedAt);
		const bool bToday = (FDateTime::UtcNow() - PlayedAt).GetTotalHours() < 20.0;
		TimeText->SetText(bToday ? FText::AsTime(PlayedAt, EDateTimeStyle::Short) : FText::AsDateTime(PlayedAt, EDateTimeStyle::Short, EDateTimeStyle::Short));
	}
	if (PlayButton)
	{
		PlayButton->SetLabel(bRadioSong ? LOCTEXT("HistorySearch", "Search") : LOCTEXT("HistoryPlay", "Play"));
		PlayButton->SetToolTipText(bRadioSong ? LOCTEXT("HistorySearchTip", "Search YouTube and SoundCloud for this song")
			: LOCTEXT("HistoryPlayTip", "Play this again now"));
	}
	if (CopyButton)
	{
		const bool bLink = !GetLink().IsEmpty();
		CopyButton->SetLabel(bLink ? LOCTEXT("HistoryCopyLink", "Copy Link") : LOCTEXT("HistoryCopyName", "Copy Name"));
		CopyButton->SetToolTipText(bLink ? FText::FromString(GetLink()) : FText::FromString(GetSongName(Entry)));
	}
}

void UBBPHistoryRow::HandlePlay()
{
	UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>();
	if (!Page)
	{
		return;
	}
	if (Entry.RadioSong.IsEmpty())
	{
		Page->PlayFromHistory(Entry.Track);
	}
	else
	{
		Page->SearchFromHistory(Entry.RadioSong);
	}
}

void UBBPHistoryRow::HandleCopy()
{
	const FString Link = GetLink();
	const FString Text = Link.IsEmpty() ? GetSongName(Entry) : Link;
	FPlatformApplicationMisc::ClipboardCopy(*Text);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: copied from history: %s"), *Text);
}

#undef LOCTEXT_NAMESPACE
