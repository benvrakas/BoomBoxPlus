#include "UI/BBPMusicPage.h"
#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "FGBoomBoxPlayer.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Net/BBPNetSubsystem.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Tape/BBPCustomMusicTape.h"
#include "UI/BBPTrackRow.h"
#include "UI/BBPWidgetStyle.h"
#include "UI/FGInteractWidget.h"
#include "FGCharacterPlayer.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

namespace
{
	constexpr float SearchDebounceSeconds = 0.25f;
	constexpr float ShowEnforceSeconds = 0.5f;
	constexpr int32 MaxResults = 100;
	const FName BoomBoxPropertyName(TEXT("mBoomBox"));
}

TArray<TWeakObjectPtr<UBBPMusicPage>> UBBPMusicPage::LivePages;

void UBBPMusicPage::NotifyTapeChanged(AFGBoomBoxPlayer* BoomBox, TSubclassOf<UFGTapeData> NewTape)
{
	if (!UBBPCustomMusicTape::IsCustomMusicTape(NewTape))
	{
		return;
	}
	int32 NumShown = 0;
	for (const TWeakObjectPtr<UBBPMusicPage>& Page : LivePages)
	{
		if (Page.IsValid() && Page->GetBoomBox() == BoomBox)
		{
			Page->RequestShow();
			++NumShown;
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Custom Music loaded into %s; bringing %d open page(s) forward"), *GetNameSafe(BoomBox), NumShown);
}

void UBBPMusicPage::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!TrackRowClass)
	{
		TrackRowClass = UBBPTrackRow::StaticClass();
	}
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}

	if (SearchBox)
	{
		SearchBox->OnTextChanged.AddDynamic(this, &UBBPMusicPage::HandleSearchChanged);
		SearchBox->OnTextCommitted.AddDynamic(this, &UBBPMusicPage::HandleSearchCommitted);
	}
	if (PlayPauseButton) PlayPauseButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandlePlayPause);
	if (NextButton) NextButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleNext);
	if (PreviousButton) PreviousButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandlePrevious);
	if (ShuffleButton) ShuffleButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleShuffle);
	if (RepeatButton) RepeatButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleRepeat);
	if (ClearQueueButton) ClearQueueButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleClearQueue);
	if (TapesButton) TapesButton->OnClicked.AddDynamic(this, &UBBPMusicPage::ShowTapeList);
	if (BackButton) BackButton->OnClicked.AddDynamic(this, &UBBPMusicPage::ShowPlayerPage);
	if (UseBoomBoxButton) UseBoomBoxButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleUseBoomBox);
	if (OpenFolderButton) OpenFolderButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleOpenFolder);
	if (RescanButton) RescanButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleRescan);
}

void UBBPMusicPage::BuildDefaultLayout()
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>();
	Panel->SetBrushColor(BBPWidgetStyle::PanelColor);
	Panel->SetPadding(FMargin(12.f));
	WidgetTree->RootWidget = Panel;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Panel->SetContent(Column);

	auto AddRow = [this, Column](float TopPadding) -> UHorizontalBox*
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, TopPadding, 0.f, 0.f));
		return Row;
	};
	auto AddToRow = [](UHorizontalBox* Row, UWidget* Widget, bool bFill)
	{
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Widget);
		Slot->SetVerticalAlignment(VAlign_Center);
		Slot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	};

	UHorizontalBox* Header = AddRow(0.f);
	BackButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Back", "< Back"));
	AddToRow(Header, BackButton, false);
	TapesButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Tapes", "Tapes"));
	AddToRow(Header, TapesButton, false);
	AddToRow(Header, BBPWidgetStyle::MakeText(WidgetTree, 16, BBPWidgetStyle::AccentColor, LOCTEXT("PageTitle", "Custom Music")), true);
	LibraryStatusText = BBPWidgetStyle::MakeText(WidgetTree, 10, BBPWidgetStyle::DimTextColor);
	AddToRow(Header, LibraryStatusText, false);
	OpenFolderButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("OpenFolder", "Open Folder"));
	AddToRow(Header, OpenFolderButton, false);
	RescanButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Rescan", "Rescan"));
	AddToRow(Header, RescanButton, false);

	UHorizontalBox* NotLoadedRow = AddRow(8.f);
	NotLoadedText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::AccentColor,
		LOCTEXT("NotLoaded", "This Boom Box isn't playing Custom Music. You can still manage the queue."));
	AddToRow(NotLoadedRow, NotLoadedText, true);
	UseBoomBoxButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("UseBoomBox", "Play through this Boom Box"));
	AddToRow(NotLoadedRow, UseBoomBoxButton, false);

	UHorizontalBox* NowPlayingRow = AddRow(10.f);
	NowPlayingText = BBPWidgetStyle::MakeText(WidgetTree, 14, BBPWidgetStyle::TextColor);
	AddToRow(NowPlayingRow, NowPlayingText, true);
	PositionText = BBPWidgetStyle::MakeText(WidgetTree, 12, BBPWidgetStyle::DimTextColor);
	AddToRow(NowPlayingRow, PositionText, false);

	LyricText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor);
	Column->AddChildToVerticalBox(LyricText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	UHorizontalBox* Transport = AddRow(8.f);
	PreviousButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Previous", "Prev"));
	AddToRow(Transport, PreviousButton, false);
	PlayPauseButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Play", "Play"));
	AddToRow(Transport, PlayPauseButton, false);
	NextButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Next", "Next"));
	AddToRow(Transport, NextButton, false);
	ShuffleButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Shuffle", "Shuffle: Off"));
	AddToRow(Transport, ShuffleButton, false);
	RepeatButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("Repeat", "Repeat: All"));
	AddToRow(Transport, RepeatButton, false);

	SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	SearchBox->SetHintText(LOCTEXT("SearchHint", "Search your music, or press Enter to search YouTube / SoundCloud. Links work too."));
	Column->AddChildToVerticalBox(SearchBox)->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));

	Column->AddChildToVerticalBox(BBPWidgetStyle::MakeText(WidgetTree, 12, BBPWidgetStyle::AccentColor, LOCTEXT("Results", "Results")))
		->SetPadding(FMargin(0.f, 8.f, 0.f, 2.f));
	ResultsList = WidgetTree->ConstructWidget<UScrollBox>();
	Column->AddChildToVerticalBox(ResultsList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	UHorizontalBox* QueueHeader = AddRow(8.f);
	QueueHeaderText = BBPWidgetStyle::MakeText(WidgetTree, 12, BBPWidgetStyle::AccentColor, LOCTEXT("Queue", "Queue"));
	AddToRow(QueueHeader, QueueHeaderText, true);
	ClearQueueButton = BBPWidgetStyle::MakeButton(WidgetTree, LOCTEXT("ClearQueue", "Clear Queue"));
	AddToRow(QueueHeader, ClearQueueButton, false);

	QueueList = WidgetTree->ConstructWidget<UScrollBox>();
	Column->AddChildToVerticalBox(QueueList)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}

void UBBPMusicPage::NativeConstruct()
{
	Super::NativeConstruct();
	LivePages.RemoveAll([](const TWeakObjectPtr<UBBPMusicPage>& Page) { return !Page.IsValid(); });
	LivePages.AddUnique(this);

	AFGBoomBoxPlayer* BoomBox = GetBoomBox();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: music page constructed for %s"), BoomBox ? *GetNameSafe(BoomBox) : TEXT("<Boom Box not found>"));
	if (BoomBox && UBBPCustomMusicTape::IsCustomMusicTape(BoomBox->GetCurrentTape()))
	{
		RequestShow();
	}

	TryBindSources();
	RefreshResults();
	RefreshQueue();
	RefreshTransport();
}

void UBBPMusicPage::NativeDestruct()
{
	CancelShowRequest();
	LivePages.Remove(this);
	Super::NativeDestruct();
}

void UBBPMusicPage::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bBoundLibrary || !bBoundPlaylist)
	{
		TryBindSources();
	}

	if (SearchDebounceTimer >= 0.f)
	{
		SearchDebounceTimer -= InDeltaTime;
		if (SearchDebounceTimer < 0.f)
		{
			RefreshResults();
		}
	}

	RefreshTransport();
}

void UBBPMusicPage::TryBindSources()
{
	if (!bBoundLibrary)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		if (UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr)
		{
			Library->OnLibraryChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandleLibraryChanged);
			bBoundLibrary = true;
			RefreshResults();
		}
	}
	if (!bBoundPlaylist)
	{
		if (ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this))
		{
			Playlist->OnQueueChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandleQueueChanged);
			Playlist->OnPlaybackChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandlePlaybackChanged);
			bBoundPlaylist = true;
			RefreshQueue();
		}
	}
}

AFGBoomBoxPlayer* UBBPMusicPage::GetBoomBox() const
{
	for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
	{
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(Outer->GetClass(), BoomBoxPropertyName);
		if (Property)
		{
			return Cast<AFGBoomBoxPlayer>(Property->GetObjectPropertyValue_InContainer(Outer));
		}
	}
	return nullptr;
}

void UBBPMusicPage::RequestShow()
{
	ShowRequestTimeLeft = ShowEnforceSeconds;
	if (!ShowTickerHandle.IsValid())
	{
		TWeakObjectPtr<UBBPMusicPage> WeakThis(this);
		ShowTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float DeltaTime)
		{
			UBBPMusicPage* This = WeakThis.Get();
			return This && This->TickShowRequest(DeltaTime);
		}));
	}
	ShowPage();
}

bool UBBPMusicPage::TickShowRequest(float DeltaTime)
{
	const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(GetParent());
	if (Switcher && Switcher->GetActiveWidget() != this)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Boom Box window switched away from the music page; switching back"));
		ShowPage();
	}
	ShowRequestTimeLeft -= DeltaTime;
	if (ShowRequestTimeLeft <= 0.f)
	{
		ShowTickerHandle.Reset();
		return false;
	}
	return true;
}

void UBBPMusicPage::CancelShowRequest()
{
	ShowRequestTimeLeft = 0.f;
	if (ShowTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ShowTickerHandle);
		ShowTickerHandle.Reset();
	}
}

void UBBPMusicPage::ShowPage()
{
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(GetParent());
	if (!Switcher)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: music page parent is %s, not a WidgetSwitcher; cannot show it"), *GetNameSafe(GetParent()));
		return;
	}
	Switcher->SetActiveWidget(this);

	// Gives gamepad navigation a starting point on this page.
	UWidget* FocusTarget = PlayPauseButton ? static_cast<UWidget*>(PlayPauseButton) : static_cast<UWidget*>(SearchBox);
	if (UFGInteractWidget* Window = GetTypedOuter<UFGInteractWidget>())
	{
		Window->SetDefaultFocusWidget(FocusTarget);
	}
	if (FocusTarget)
	{
		FocusTarget->SetFocus();
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: music page shown"));
}

void UBBPMusicPage::ShowTapeList()
{
	ShowSiblingPage(TEXT("TapeSelect"));
}

void UBBPMusicPage::ShowPlayerPage()
{
	ShowSiblingPage(TEXT("BoomBox_Player"));
}

bool UBBPMusicPage::ShowSiblingPage(const TCHAR* NameFragment)
{
	CancelShowRequest();
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(GetParent());
	if (!Switcher)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: cannot leave the music page, parent is not a WidgetSwitcher"));
		return false;
	}
	for (int32 i = 0; i < Switcher->GetNumWidgets(); ++i)
	{
		UWidget* Child = Switcher->GetWidgetAtIndex(i);
		if (Child && Child != this && Child->GetClass()->GetName().Contains(NameFragment))
		{
			Switcher->SetActiveWidget(Child);
			return true;
		}
	}
	UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: no page matching '%s' among %d switcher pages"), NameFragment, Switcher->GetNumWidgets());
	return false;
}

void UBBPMusicPage::HandleUseBoomBox()
{
	AFGBoomBoxPlayer* BoomBox = GetBoomBox();
	AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(GetOwningPlayerPawn());
	if (!BoomBox || !Character)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: cannot load Custom Music (Boom Box %s, character %s)"), *GetNameSafe(BoomBox), *GetNameSafe(Character));
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: loading Custom Music into %s"), *GetNameSafe(BoomBox));
	BoomBox->BeginChangeTapeSequence(UBBPCustomMusicTape::StaticClass(), Character);
}

UBBPTrackRow* UBBPMusicPage::MakeRow()
{
	return CreateWidget<UBBPTrackRow>(this, TrackRowClass);
}

void UBBPMusicPage::RefreshResults()
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;

	if (LibraryStatusText)
	{
		const FText Status = !Library ? LOCTEXT("NoLibrary", "Library unavailable")
			: Library->IsScanning() ? LOCTEXT("Scanning", "Scanning...")
			: FText::Format(LOCTEXT("TrackCount", "{0} tracks"), Library->GetTrackCount());
		LibraryStatusText->SetText(Status);
	}

	if (!ResultsList)
	{
		return;
	}
	ResultsList->ClearChildren();
	if (!Library)
	{
		return;
	}

	if (Library->GetTrackCount() == 0)
	{
		const FText Empty = FText::Format(LOCTEXT("EmptyLibrary", "No music found. Put .mp3, .ogg or .wav files in:\n{0}"), FText::FromString(Library->GetMusicFolder()));
		ResultsList->AddChild(BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor, Empty));
		return;
	}

	const TArray<FBBPTrack> Results = Library->Search(SearchQuery, MaxResults);
	if (Results.Num() == 0 && OnlineResults.Num() == 0 && OnlineStatus.IsEmpty())
	{
		ResultsList->AddChild(BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor, LOCTEXT("NoResults", "No matches. Press Enter to search YouTube and SoundCloud.")));
		return;
	}
	for (const FBBPTrack& Track : Results)
	{
		if (UBBPTrackRow* Row = MakeRow())
		{
			Row->SetupAsResult(Track);
			ResultsList->AddChild(Row);
		}
	}

	if (!OnlineStatus.IsEmpty())
	{
		ResultsList->AddChild(BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::AccentColor, FText::FromString(OnlineStatus)));
	}
	if (OnlineResults.Num() > 0)
	{
		if (bOnlineIsCollection)
		{
			UButton* AddAll = BBPWidgetStyle::MakeButton(WidgetTree, FText::Format(LOCTEXT("AddAll", "Add all {0} to queue"), OnlineResults.Num()));
			AddAll->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleAddAllOnline);
			ResultsList->AddChild(AddAll);
		}
		for (const FBBPTrack& Track : OnlineResults)
		{
			if (UBBPTrackRow* Row = MakeRow())
			{
				Row->SetupAsResult(Track);
				ResultsList->AddChild(Row);
			}
		}
	}
}

void UBBPMusicPage::RefreshQueue()
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const int32 QueueLength = Playlist ? Playlist->GetQueue().Num() : 0;

	if (QueueHeaderText)
	{
		QueueHeaderText->SetText(FText::Format(LOCTEXT("QueueHeader", "Queue ({0})"), QueueLength));
	}
	if (!QueueList)
	{
		return;
	}
	QueueList->ClearChildren();
	if (!Playlist)
	{
		return;
	}
	if (QueueLength == 0)
	{
		QueueList->AddChild(BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor, LOCTEXT("EmptyQueue", "The queue is empty. Add tracks with + Front or + End.")));
		return;
	}
	const TArray<FBBPQueueEntry>& Queue = Playlist->GetQueue();
	for (int32 i = 0; i < Queue.Num(); ++i)
	{
		if (UBBPTrackRow* Row = MakeRow())
		{
			Row->SetupAsQueueEntry(Queue[i], i, QueueLength, UBBPBlueprintLibrary::GetEntryAvailability(this, Queue[i]));
			QueueList->AddChild(Row);
		}
	}
}

void UBBPMusicPage::RefreshTransport()
{
	const AFGBoomBoxPlayer* ViewedBoomBox = GetBoomBox();
	const bool bLoaded = ViewedBoomBox && UBBPCustomMusicTape::IsCustomMusicTape(ViewedBoomBox->GetCurrentTape());
	BBPWidgetStyle::SetShown(NotLoadedText, !bLoaded);
	BBPWidgetStyle::SetShown(UseBoomBoxButton, !bLoaded);

	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	FBBPQueueEntry Current;
	const bool bHasCurrent = Playlist && Playlist->GetCurrentEntry(Current);

	if (NowPlayingText)
	{
		NowPlayingText->SetText(bHasCurrent
			? FText::FromString(FString::Printf(TEXT("%s - %s"), *Current.Track.Artist, *Current.Track.Title))
			: LOCTEXT("NothingPlaying", "Nothing playing"));
	}
	if (PositionText)
	{
		PositionText->SetText(bHasCurrent
			? FText::FromString(FString::Printf(TEXT("%s / %s"), *UBBPBlueprintLibrary::FormatDuration(Playlist->GetPlaybackPosition()), *UBBPBlueprintLibrary::FormatDuration(Current.Track.Duration)))
			: FText::GetEmpty());
	}
	if (LyricText)
	{
		LyricText->SetText(FText::FromString(UBBPBlueprintLibrary::GetCurrentLyricLine(this)));
	}
	if (Playlist)
	{
		const FBBPPlaybackState& State = Playlist->GetPlaybackState();
		BBPWidgetStyle::SetButtonLabel(PlayPauseButton, Playlist->IsPlaying() ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"));
		BBPWidgetStyle::SetButtonLabel(ShuffleButton, State.bShuffle ? LOCTEXT("ShuffleOn", "Shuffle: On") : LOCTEXT("ShuffleOff", "Shuffle: Off"));
		const FText Repeat = State.RepeatMode == EBBPRepeatMode::One ? LOCTEXT("RepeatOne", "Repeat: One")
			: State.RepeatMode == EBBPRepeatMode::All ? LOCTEXT("RepeatAll", "Repeat: All")
			: LOCTEXT("RepeatOff", "Repeat: Off");
		BBPWidgetStyle::SetButtonLabel(RepeatButton, Repeat);
	}
}

void UBBPMusicPage::HandleSearchChanged(const FText& Text)
{
	SearchQuery = Text.ToString();
	SearchDebounceTimer = SearchDebounceSeconds;
	if (UBBPNetSubsystem::ClassifyLink(SearchQuery) != EBBPLinkKind::None)
	{
		// A pasted link would match nothing locally; searching waits for Enter.
		SearchQuery.Reset();
	}
}

void UBBPMusicPage::HandleSearchCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		RunOnlineSearch(Text.ToString().TrimStartAndEnd());
	}
}

void UBBPMusicPage::RunOnlineSearch(const FString& Text)
{
	const UGameInstance* GameInstance = GetGameInstance();
	UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	if (!Net || Text.Len() < 2)
	{
		return;
	}

	const int32 Generation = ++SearchGeneration;
	OnlineResults.Reset();
	bOnlineIsCollection = false;
	TWeakObjectPtr<UBBPMusicPage> WeakThis(this);

	switch (UBBPNetSubsystem::ClassifyLink(Text))
	{
	case EBBPLinkKind::SpotifyTrack:
		OnlineStatus = TEXT("Reading Spotify link...");
		Net->ResolveSpotifyTrack(Text, FBBPOnNetText::CreateLambda([WeakThis, Generation](const FString& SearchText, const FString& Error)
		{
			UBBPMusicPage* This = WeakThis.Get();
			if (!This || Generation != This->SearchGeneration)
			{
				return;
			}
			if (!Error.IsEmpty())
			{
				This->ShowOnlineResults(Generation, {}, Error, false);
				return;
			}
			if (This->SearchBox)
			{
				This->SearchBox->SetText(FText::FromString(SearchText));
			}
			This->RunOnlineSearch(SearchText);
		}));
		break;
	case EBBPLinkKind::SpotifyCollection:
		OnlineStatus = TEXT("Matching the Spotify playlist on YouTube (can take a minute)...");
		Net->ResolveSpotifyCollection(Text, FBBPOnNetTracks::CreateLambda([WeakThis, Generation](const TArray<FBBPTrack>& Tracks, const FString& Error)
		{
			if (UBBPMusicPage* This = WeakThis.Get())
			{
				This->ShowOnlineResults(Generation, Tracks, Error, true);
			}
		}));
		break;
	case EBBPLinkKind::YouTubeVideo:
	case EBBPLinkKind::YouTubePlaylist:
	case EBBPLinkKind::SoundCloudTrack:
	case EBBPLinkKind::SoundCloudSet:
	{
		const EBBPLinkKind Kind = UBBPNetSubsystem::ClassifyLink(Text);
		const bool bCollection = Kind == EBBPLinkKind::YouTubePlaylist || Kind == EBBPLinkKind::SoundCloudSet;
		OnlineStatus = TEXT("Reading link...");
		Net->ResolveLink(Text, FBBPOnNetTracks::CreateLambda([WeakThis, Generation, bCollection](const TArray<FBBPTrack>& Tracks, const FString& Error)
		{
			if (UBBPMusicPage* This = WeakThis.Get())
			{
				This->ShowOnlineResults(Generation, Tracks, Error, bCollection);
			}
		}));
		break;
	}
	default:
		OnlineStatus = Net->AreToolsAvailable() ? TEXT("Searching YouTube and SoundCloud...") : TEXT("YouTube/SoundCloud unavailable: the mod's download tools are missing.");
		if (Net->AreToolsAvailable())
		{
			Net->Search(Text, FBBPOnNetTracks::CreateLambda([WeakThis, Generation](const TArray<FBBPTrack>& Tracks, const FString& Error)
			{
				if (UBBPMusicPage* This = WeakThis.Get())
				{
					This->ShowOnlineResults(Generation, Tracks, Error, false);
				}
			}));
		}
		break;
	}
	RefreshResults();
}

void UBBPMusicPage::ShowOnlineResults(int32 Generation, const TArray<FBBPTrack>& Tracks, const FString& Error, bool bIsCollection)
{
	if (Generation != SearchGeneration)
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("UI: dropping results of an older search"));
		return;
	}
	OnlineResults = Tracks;
	bOnlineIsCollection = bIsCollection && Tracks.Num() > 1;
	OnlineStatus = !Error.IsEmpty() ? FString::Printf(TEXT("Online search failed: %s"), *Error)
		: Tracks.Num() == 0 ? FString(TEXT("Nothing found online."))
		: FString(TEXT("From YouTube / SoundCloud:"));
	RefreshResults();
}

void UBBPMusicPage::HandleAddAllOnline()
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: adding %d online tracks to the queue"), OnlineResults.Num());
	for (const FBBPTrack& Track : OnlineResults)
	{
		UBBPBlueprintLibrary::RequestAddTrack(this, Track, false);
	}
}

void UBBPMusicPage::HandleLibraryChanged()
{
	RefreshResults();
	RefreshQueue();
}

void UBBPMusicPage::HandleQueueChanged()
{
	RefreshQueue();
}

void UBBPMusicPage::HandlePlaybackChanged()
{
	RefreshQueue();
	RefreshTransport();
}

void UBBPMusicPage::HandlePlayPause()
{
	UBBPBlueprintLibrary::RequestTogglePlaying(this);
}

void UBBPMusicPage::HandleNext()
{
	UBBPBlueprintLibrary::RequestSkip(this);
}

void UBBPMusicPage::HandlePrevious()
{
	UBBPBlueprintLibrary::RequestPrevious(this);
}

void UBBPMusicPage::HandleShuffle()
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	UBBPBlueprintLibrary::RequestSetShuffle(this, !(Playlist && Playlist->GetPlaybackState().bShuffle));
}

void UBBPMusicPage::HandleRepeat()
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const EBBPRepeatMode Current = Playlist ? Playlist->GetPlaybackState().RepeatMode : EBBPRepeatMode::All;
	const EBBPRepeatMode Next = Current == EBBPRepeatMode::All ? EBBPRepeatMode::One
		: Current == EBBPRepeatMode::One ? EBBPRepeatMode::Off
		: EBBPRepeatMode::All;
	UBBPBlueprintLibrary::RequestSetRepeatMode(this, Next);
}

void UBBPMusicPage::HandleClearQueue()
{
	UBBPBlueprintLibrary::RequestClearQueue(this);
}

void UBBPMusicPage::HandleOpenFolder()
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr)
	{
		Library->OpenMusicFolder();
	}
}

void UBBPMusicPage::HandleRescan()
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr)
	{
		Library->Rescan();
		RefreshResults();
	}
}

#undef LOCTEXT_NAMESPACE
