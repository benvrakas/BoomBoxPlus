#include "UI/BBPMusicPage.h"
#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "UI/BBPGameButton.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Engine/GameInstance.h"
#include "FGBoomBoxPlayer.h"
#include "GameFramework/GameStateBase.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Net/BBPNetSubsystem.h"
#include "Net/BBPSpotifyAuth.h"
#include "Playlist/BBPMusicChannel.h"
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
	constexpr double LinkMessageSeconds = 8.0;

	// Formats seconds left as m:ss.
	FText FormatTimeLeft(double Seconds)
	{
		const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60));
	}
	constexpr int32 MaxResults = 100;
	constexpr int32 MaxResultRows = 100;
	constexpr int32 MaxQueueRows = 100;
	constexpr int32 QueueRowsBeforeCurrent = 2;
	constexpr float RebuildInterval = 0.3f;
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

void UBBPMusicPage::NotifyLinkResult(AFGBoomBoxPlayer* BoomBox, const FString& Message)
{
	for (const TWeakObjectPtr<UBBPMusicPage>& Page : LivePages)
	{
		if (Page.IsValid() && Page->GetBoomBox() == BoomBox)
		{
			Page->SetLinkMessage(FText::FromString(Message));
		}
	}
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
	if (LinkButton) LinkButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleLink);
	if (UnlinkButton) UnlinkButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleUnlink);
	if (LinkCodeBox) LinkCodeBox->OnTextCommitted.AddDynamic(this, &UBBPMusicPage::HandleLinkCodeCommitted);
	if (AddAllButton) AddAllButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleAddAllOnline);
	if (SpotifyButton) SpotifyButton->OnClicked.AddDynamic(this, &UBBPMusicPage::HandleConnectSpotify);
	if (SeekSlider)
	{
		SeekSlider->OnMouseCaptureBegin.AddDynamic(this, &UBBPMusicPage::HandleSeekBegin);
		SeekSlider->OnMouseCaptureEnd.AddDynamic(this, &UBBPMusicPage::HandleSeekEnd);
		SeekSlider->OnControllerCaptureBegin.AddDynamic(this, &UBBPMusicPage::HandleSeekBegin);
		SeekSlider->OnControllerCaptureEnd.AddDynamic(this, &UBBPMusicPage::HandleSeekEnd);
	}
}

void UBBPMusicPage::BuildDefaultLayout()
{
	using namespace BBPWidgetStyle;

	UBorder* Window = MakePanel(WidgetTree, PanelColor, FMargin(18.f, 14.f));
	WidgetTree->RootWidget = Window;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	Window->SetContent(Column);

	auto AddRow = [this](UVerticalBox* Parent, float TopPadding) -> UHorizontalBox*
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Parent->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, TopPadding, 0.f, 0.f));
		return Row;
	};
	auto AddToRow = [](UHorizontalBox* Row, UWidget* Widget, bool bFill, float RightPadding = 6.f)
	{
		UHorizontalBoxSlot* Slot = Row->AddChildToHorizontalBox(Widget);
		Slot->SetVerticalAlignment(VAlign_Center);
		Slot->SetPadding(FMargin(0.f, 0.f, RightPadding, 0.f));
		if (bFill)
		{
			Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	};
	auto AddFill = [this](UVerticalBox* Parent, UWidget* Widget, float TopPadding)
	{
		UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Widget);
		Slot->SetPadding(FMargin(0.f, TopPadding, 0.f, 0.f));
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	};

	// Title bar: back, title, library status and folder buttons.
	UHorizontalBox* Header = AddRow(Column, 0.f);
	BackButton = MakeButton(WidgetTree, LOCTEXT("Back", "Back"));
	AddToRow(Header, BackButton, false, 12.f);
	AddToRow(Header, MakeText(WidgetTree, 22, AccentColor, LOCTEXT("PageTitle", "CUSTOM MUSIC"), EFontWeight::Bold), true);
	LibraryStatusText = MakeText(WidgetTree, 11, DimTextColor);
	AddToRow(Header, LibraryStatusText, false, 12.f);
	OpenFolderButton = MakeButton(WidgetTree, LOCTEXT("OpenFolder", "Open Folder"));
	AddToRow(Header, OpenFolderButton, false);
	RescanButton = MakeButton(WidgetTree, LOCTEXT("Rescan", "Rescan"));
	AddToRow(Header, RescanButton, false);
	TapesButton = MakeButton(WidgetTree, LOCTEXT("Tapes", "Tapes"));
	AddToRow(Header, TapesButton, false, 0.f);

	UBorder* HeaderRule = MakePanel(WidgetTree, AccentColor, FMargin(0.f, 1.f));
	Column->AddChildToVerticalBox(HeaderRule)->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));

	// Shown while this Boom Box has another tape in.
	UHorizontalBox* NotLoadedRow = AddRow(Column, 10.f);
	NotLoadedText = MakeText(WidgetTree, 12, AccentColor,
		LOCTEXT("NotLoaded", "This Boom Box isn't playing Custom Music. You can still manage its queue."), EFontWeight::SemiBold);
	AddToRow(NotLoadedRow, NotLoadedText, true);
	UseBoomBoxButton = MakeButton(WidgetTree, LOCTEXT("UseBoomBox", "Play Custom Music"));
	AddToRow(NotLoadedRow, UseBoomBoxButton, false, 0.f);

	// Now playing: track, position, lyric and transport.
	UBorder* NowPlayingPanel = MakePanel(WidgetTree, InsetColor, FMargin(14.f, 10.f));
	Column->AddChildToVerticalBox(NowPlayingPanel)->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
	UVerticalBox* NowPlaying = WidgetTree->ConstructWidget<UVerticalBox>();
	NowPlayingPanel->SetContent(NowPlaying);

	NowPlaying->AddChildToVerticalBox(MakeText(WidgetTree, 10, DimTextColor, LOCTEXT("NowPlayingLabel", "NOW PLAYING"), EFontWeight::Bold));
	NowPlayingText = MakeText(WidgetTree, 18, TextColor, FText::GetEmpty(), EFontWeight::Bold);
	NowPlaying->AddChildToVerticalBox(NowPlayingText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));
	LyricText = MakeText(WidgetTree, 12, AccentColor);
	NowPlaying->AddChildToVerticalBox(LyricText)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

	UHorizontalBox* SeekRow = AddRow(NowPlaying, 8.f);
	SeekSlider = WidgetTree->ConstructWidget<USlider>();
	StyleSlider(SeekSlider);
	AddToRow(SeekRow, SeekSlider, true, 10.f);
	PositionText = MakeText(WidgetTree, 12, DimTextColor, FText::GetEmpty(), EFontWeight::SemiBold);
	AddToRow(SeekRow, PositionText, false, 0.f);

	UHorizontalBox* Transport = AddRow(NowPlaying, 8.f);
	PreviousButton = MakeButton(WidgetTree, LOCTEXT("Previous", "Previous"));
	AddToRow(Transport, PreviousButton, false);
	PlayPauseButton = MakeButton(WidgetTree, LOCTEXT("Play", "Play"));
	AddToRow(Transport, PlayPauseButton, false);
	NextButton = MakeButton(WidgetTree, LOCTEXT("Next", "Next"));
	AddToRow(Transport, NextButton, false, 18.f);
	ShuffleButton = MakeButton(WidgetTree, LOCTEXT("Shuffle", "Shuffle: Off"));
	AddToRow(Transport, ShuffleButton, false);
	RepeatButton = MakeButton(WidgetTree, LOCTEXT("Repeat", "Repeat: All"));
	AddToRow(Transport, RepeatButton, false);

	// Two columns: search and results on the left, the queue on the right.
	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>();
	AddFill(Column, Columns, 12.f);

	UVerticalBox* SearchColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* SearchSlot = Columns->AddChildToHorizontalBox(SearchColumn);
	SearchSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SearchSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	UTextBlock* ResultsTitle = nullptr;
	SearchColumn->AddChildToVerticalBox(MakeSectionHeader(WidgetTree, LOCTEXT("Results", "Search"), ResultsTitle));
	UHorizontalBox* SearchRow = AddRow(SearchColumn, 0.f);
	if (UImage* Icon = MakeSearchIcon(WidgetTree))
	{
		AddToRow(SearchRow, Icon, false, 8.f);
	}
	SearchBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	StyleTextBox(SearchBox, 12);
	SearchBox->SetHintText(LOCTEXT("SearchHint", "Search your music. Press Enter for YouTube / SoundCloud, or paste a link."));
	AddToRow(SearchRow, SearchBox, true, 0.f);

	ResultsMessageText = MakeText(WidgetTree, 11, AccentColor);
	ResultsMessageText->SetAutoWrapText(true);
	SearchColumn->AddChildToVerticalBox(ResultsMessageText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	SpotifyButton = MakeButton(WidgetTree, LOCTEXT("ConnectSpotify", "Connect Spotify"));
	SpotifyButton->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBoxSlot* SpotifySlot = SearchColumn->AddChildToVerticalBox(SpotifyButton);
	SpotifySlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	SpotifySlot->SetHorizontalAlignment(HAlign_Left);

	AddAllButton = MakeButton(WidgetTree, LOCTEXT("AddAllDefault", "Add all to queue"));
	AddAllButton->SetVisibility(ESlateVisibility::Collapsed);
	UVerticalBoxSlot* AddAllSlot = SearchColumn->AddChildToVerticalBox(AddAllButton);
	AddAllSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	AddAllSlot->SetHorizontalAlignment(HAlign_Left);

	UBorder* ResultsPanel = MakePanel(WidgetTree, InsetColor, FMargin(6.f));
	AddFill(SearchColumn, ResultsPanel, 8.f);
	ResultsList = WidgetTree->ConstructWidget<UScrollBox>();
	StyleScrollBox(ResultsList);
	ResultsPanel->SetContent(ResultsList);

	UVerticalBox* QueueColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	UHorizontalBoxSlot* QueueSlot = Columns->AddChildToHorizontalBox(QueueColumn);
	QueueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	QueueSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));

	UHorizontalBox* QueueHeader = AddRow(QueueColumn, 0.f);
	UTextBlock* QueueTitle = nullptr;
	AddToRow(QueueHeader, MakeSectionHeader(WidgetTree, LOCTEXT("Queue", "Queue"), QueueTitle), true);
	QueueHeaderText = QueueTitle;
	ClearQueueButton = MakeButton(WidgetTree, LOCTEXT("ClearQueue", "Clear Queue"));
	AddToRow(QueueHeader, ClearQueueButton, false, 0.f);

	QueueMessageText = MakeText(WidgetTree, 11, DimTextColor);
	QueueMessageText->SetAutoWrapText(true);
	QueueColumn->AddChildToVerticalBox(QueueMessageText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	UBorder* QueuePanel = MakePanel(WidgetTree, InsetColor, FMargin(6.f));
	AddFill(QueueColumn, QueuePanel, 8.f);
	QueueList = WidgetTree->ConstructWidget<UScrollBox>();
	StyleScrollBox(QueueList);
	QueuePanel->SetContent(QueueList);

	// Linking: this Boom Box's code, and a field for another Boom Box's code.
	UBorder* LinkPanel = MakePanel(WidgetTree, InsetColor, FMargin(14.f, 8.f));
	Column->AddChildToVerticalBox(LinkPanel)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
	UVerticalBox* LinkColumn = WidgetTree->ConstructWidget<UVerticalBox>();
	LinkPanel->SetContent(LinkColumn);
	UHorizontalBox* LinkRow = AddRow(LinkColumn, 0.f);
	LinkCodeText = MakeText(WidgetTree, 13, TextColor, FText::GetEmpty(), EFontWeight::SemiBold);
	AddToRow(LinkRow, LinkCodeText, true, 10.f);
	LinkCodeBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	StyleTextBox(LinkCodeBox, 13);
	LinkCodeBox->SetHintText(LOCTEXT("LinkHint", "0000"));
	USizeBox* CodeSize = WidgetTree->ConstructWidget<USizeBox>();
	CodeSize->SetWidthOverride(90.f);
	CodeSize->SetContent(LinkCodeBox);
	AddToRow(LinkRow, CodeSize, false);
	LinkButton = MakeButton(WidgetTree, LOCTEXT("Link", "Link"));
	AddToRow(LinkRow, LinkButton, false);
	UnlinkButton = MakeButton(WidgetTree, LOCTEXT("Unlink", "Unlink"));
	AddToRow(LinkRow, UnlinkButton, false, 0.f);
	LinkMessageText = MakeText(WidgetTree, 11, DimTextColor);
	LinkColumn->AddChildToVerticalBox(LinkMessageText)->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
}

void UBBPMusicPage::AddToList(UScrollBox* List, UWidget* Widget)
{
	if (List && Widget)
	{
		if (UScrollBoxSlot* ListSlot = Cast<UScrollBoxSlot>(List->AddChild(Widget)))
		{
			ListSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
	}
}

void UBBPMusicPage::NativeConstruct()
{
	Super::NativeConstruct();
	LivePages.RemoveAll([](const TWeakObjectPtr<UBBPMusicPage>& Page) { return !Page.IsValid(); });
	LivePages.AddUnique(this);

	AFGBoomBoxPlayer* BoomBox = GetBoomBox();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: music page constructed for %s"), BoomBox ? *GetNameSafe(BoomBox) : TEXT("<Boom Box not found>"));

	TryBindSources();
	UpdateChannelBinding();
	RebuildResults();
	RebuildQueue();
	RefreshTransport();
	RefreshLink();
}

void UBBPMusicPage::NativeDestruct()
{
	CancelShowRequest();
	CancelMatchJob();
	LivePages.Remove(this);
	if (ABBPMusicChannel* Channel = BoundChannel.Get())
	{
		Channel->OnQueueChanged.RemoveAll(this);
		Channel->OnPlaybackChanged.RemoveAll(this);
		Channel->OnMembersChanged.RemoveAll(this);
	}
	BoundChannel = nullptr;
	Super::NativeDestruct();
}

void UBBPMusicPage::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bBoundLibrary)
	{
		TryBindSources();
	}
	UpdateChannelBinding();

	RebuildCooldown -= InDeltaTime;
	if ((bResultsDirty || bQueueDirty) && RebuildCooldown <= 0.f)
	{
		RebuildCooldown = RebuildInterval;
		if (bResultsDirty)
		{
			RebuildResults();
		}
		if (bQueueDirty)
		{
			RebuildQueue();
		}
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
	RefreshLink();
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
}

void UBBPMusicPage::UpdateChannelBinding()
{
	ABBPMusicChannel* Channel = GetChannel();
	if (Channel == BoundChannel.Get())
	{
		return;
	}
	if (ABBPMusicChannel* Old = BoundChannel.Get())
	{
		Old->OnQueueChanged.RemoveAll(this);
		Old->OnPlaybackChanged.RemoveAll(this);
		Old->OnMembersChanged.RemoveAll(this);
	}
	BoundChannel = Channel;
	if (Channel)
	{
		Channel->OnQueueChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandleQueueChanged);
		Channel->OnPlaybackChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandlePlaybackChanged);
		Channel->OnMembersChanged.AddUniqueDynamic(this, &UBBPMusicPage::HandleMembersChanged);
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: music page for %s now shows channel %04d"), *GetNameSafe(GetBoomBox()), Channel ? Channel->GetLinkCode() : 0);
	RefreshQueue();
	RefreshTransport();
	RefreshLink();
}

ABBPMusicChannel* UBBPMusicPage::GetChannel() const
{
	return ABBPPlaylistSubsystem::FindChannelFor(GetBoomBox());
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
	UWidget* FocusTarget = PlayPauseButton ? PlayPauseButton->GetFocusTarget() : static_cast<UWidget*>(SearchBox);
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

void UBBPMusicPage::EnsureCustomMusicLoaded()
{
	const AFGBoomBoxPlayer* BoomBox = GetBoomBox();
	if (BoomBox && !UBBPCustomMusicTape::IsCustomMusicTape(BoomBox->GetCurrentTape()))
	{
		HandleUseBoomBox();
	}
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
	UBBPTrackRow* Row = CreateWidget<UBBPTrackRow>(this, TrackRowClass);
	if (Row)
	{
		Row->SetBoomBox(GetBoomBox());
	}
	return Row;
}

void UBBPMusicPage::RefreshResults()
{
	bResultsDirty = true;
}

void UBBPMusicPage::RefreshQueue()
{
	bQueueDirty = true;
}

void UBBPMusicPage::SyncRows(UScrollBox* List, TArray<TObjectPtr<UBBPTrackRow>>& Pool, int32 Count, TFunctionRef<void(UBBPTrackRow& Row, int32 Index)> Setup)
{
	if (!List)
	{
		return;
	}
	while (Pool.Num() < Count)
	{
		UBBPTrackRow* Row = MakeRow();
		if (!Row)
		{
			break;
		}
		Pool.Add(Row);
		AddToList(List, Row);
	}
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		if (i < Count)
		{
			Setup(*Pool[i], i);
			Pool[i]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Pool[i]->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UBBPMusicPage::RebuildResults()
{
	bResultsDirty = false;
	const UGameInstance* GameInstance = GetGameInstance();
	const UBBPLibrarySubsystem* Library = GameInstance ? GameInstance->GetSubsystem<UBBPLibrarySubsystem>() : nullptr;

	if (LibraryStatusText)
	{
		const FText Status = !Library ? LOCTEXT("NoLibrary", "Library unavailable")
			: Library->IsScanning() ? LOCTEXT("Scanning", "Scanning...")
			: FText::Format(LOCTEXT("TrackCount", "{0} tracks"), Library->GetTrackCount());
		LibraryStatusText->SetText(Status);
	}

	TArray<FBBPTrack> Tracks;
	FString Message;
	if (Library && Library->GetTrackCount() > 0)
	{
		Tracks = Library->Search(SearchQuery, MaxResults);
	}
	else if (Library && OnlineStatus.IsEmpty())
	{
		Message = FString::Printf(TEXT("No music files yet. Put .mp3, .ogg or .wav files in:\n%s\nor press Enter to search YouTube and SoundCloud."), *Library->GetMusicFolder());
	}
	Tracks.Append(OnlineResults);
	if (Tracks.Num() == 0 && Message.IsEmpty() && OnlineStatus.IsEmpty())
	{
		Message = TEXT("No matches. Press Enter to search YouTube and SoundCloud.");
	}
	if (!OnlineStatus.IsEmpty())
	{
		Message = OnlineStatus;
	}
	if (!OnlineNote.IsEmpty())
	{
		Message += (Message.IsEmpty() ? TEXT("") : TEXT("\n")) + OnlineNote;
	}
	const int32 Shown = FMath::Min(Tracks.Num(), MaxResultRows);
	if (Tracks.Num() > Shown)
	{
		Message += FString::Printf(TEXT("%sShowing the first %d of %d."), Message.IsEmpty() ? TEXT("") : TEXT("\n"), Shown, Tracks.Num());
	}
	if (ResultsMessageText)
	{
		ResultsMessageText->SetText(FText::FromString(Message));
		BBPWidgetStyle::SetShown(ResultsMessageText, !Message.IsEmpty());
	}

	const bool bMatching = MatchJobId != 0;
	const bool bShowAddAll = bOnlineIsCollection && !bMatchQueued && (OnlineResults.Num() > 0 || (bMatching && MatchTotal > 0));
	if (AddAllButton)
	{
		BBPWidgetStyle::SetShown(AddAllButton, bShowAddAll);
		if (bShowAddAll)
		{
			AddAllButton->SetLabel(bMatching
				? FText::Format(LOCTEXT("AddAllMatching", "Add all {0} (queues as they're found)"), MatchTotal)
				: FText::Format(LOCTEXT("AddAll", "Add all {0} to queue"), OnlineResults.Num()));
		}
	}

	RefreshSpotifyButton();

	SyncRows(ResultsList, ResultRows, Shown, [&Tracks](UBBPTrackRow& Row, int32 Index)
	{
		Row.SetupAsResult(Tracks[Index]);
	});
}

void UBBPMusicPage::RebuildQueue()
{
	bQueueDirty = false;
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	const int32 QueueLength = Channel ? Channel->GetQueue().Num() : 0;

	if (QueueHeaderText)
	{
		QueueHeaderText->SetText(FText::Format(LOCTEXT("QueueHeader", "QUEUE ({0})"), QueueLength));
	}

	// Long queues show a window starting just before the current track.
	int32 Start = 0;
	if (Channel)
	{
		const int32 CurrentEntryId = Channel->GetPlaybackState().CurrentEntryId;
		const int32 CurrentIndex = Channel->GetQueue().IndexOfByPredicate([CurrentEntryId](const FBBPQueueEntry& E) { return E.EntryId == CurrentEntryId; });
		Start = FMath::Clamp(CurrentIndex - QueueRowsBeforeCurrent, 0, FMath::Max(0, QueueLength - MaxQueueRows));
	}
	const int32 Shown = FMath::Min(MaxQueueRows, QueueLength - Start);

	if (QueueMessageText)
	{
		const FText Message = QueueLength == 0
			? LOCTEXT("EmptyQueue", "The queue is empty. Add tracks with Play Next or Add.")
			: QueueLength > Shown
				? FText::Format(LOCTEXT("QueueWindow", "Showing songs {0}-{1} of {2}."), Start + 1, Start + Shown, QueueLength)
				: FText::GetEmpty();
		QueueMessageText->SetText(Message);
		BBPWidgetStyle::SetShown(QueueMessageText, !Message.IsEmpty());
	}

	SyncRows(QueueList, QueueRows, Shown, [Channel, Start, QueueLength](UBBPTrackRow& Row, int32 Index)
	{
		const FBBPQueueEntry& Entry = Channel->GetQueue()[Start + Index];
		Row.SetupAsQueueEntry(Entry, Start + Index, QueueLength, UBBPBlueprintLibrary::GetEntryAvailability(Channel, Entry));
	});
}

void UBBPMusicPage::RefreshTransport()
{
	const AFGBoomBoxPlayer* ViewedBoomBox = GetBoomBox();
	const bool bLoaded = ViewedBoomBox && UBBPCustomMusicTape::IsCustomMusicTape(ViewedBoomBox->GetCurrentTape());
	BBPWidgetStyle::SetShown(NotLoadedText, !bLoaded);
	BBPWidgetStyle::SetShown(UseBoomBoxButton, !bLoaded);

	const ABBPMusicChannel* Channel = BoundChannel.Get();
	FBBPQueueEntry Current;
	const bool bHasCurrent = Channel && Channel->GetCurrentEntry(Current);

	if (NowPlayingText)
	{
		NowPlayingText->SetText(bHasCurrent
			? FText::FromString(FString::Printf(TEXT("%s - %s"), *Current.Track.Artist, *Current.Track.Title))
			: LOCTEXT("NothingPlaying", "Nothing playing"));
	}
	if (PositionText)
	{
		PositionText->SetText(bHasCurrent
			? FText::FromString(FString::Printf(TEXT("%s / %s"), *UBBPBlueprintLibrary::FormatDuration(Channel->GetPlaybackPosition()), *UBBPBlueprintLibrary::FormatDuration(Current.Track.Duration)))
			: FText::GetEmpty());
	}
	if (SeekSlider)
	{
		SeekSlider->SetIsEnabled(bHasCurrent && Current.Track.Duration > 0.f);
		if (!bSeeking)
		{
			SeekSlider->SetValue(bHasCurrent && Current.Track.Duration > 0.f ? FMath::Clamp(Channel->GetPlaybackPosition() / Current.Track.Duration, 0.f, 1.f) : 0.f);
		}
	}
	if (LyricText)
	{
		LyricText->SetText(FText::FromString(UBBPBlueprintLibrary::GetCurrentLyricLine(Channel)));
	}
	if (Channel)
	{
		const FBBPPlaybackState& State = Channel->GetPlaybackState();
		BBPWidgetStyle::SetButtonLabel(PlayPauseButton, Channel->IsPlaying() ? LOCTEXT("Pause", "Pause") : LOCTEXT("Play", "Play"));
		BBPWidgetStyle::SetButtonLabel(ShuffleButton, State.bShuffle ? LOCTEXT("ShuffleOn", "Shuffle: On") : LOCTEXT("ShuffleOff", "Shuffle: Off"));
		const FText Repeat = State.RepeatMode == EBBPRepeatMode::One ? LOCTEXT("RepeatOne", "Repeat: One")
			: State.RepeatMode == EBBPRepeatMode::All ? LOCTEXT("RepeatAll", "Repeat: All")
			: LOCTEXT("RepeatOff", "Repeat: Off");
		BBPWidgetStyle::SetButtonLabel(RepeatButton, Repeat);
	}
}

void UBBPMusicPage::RefreshLink()
{
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	const int32 Shared = Channel ? Channel->GetMembers().Num() : 0;
	if (LinkCodeText)
	{
		FText Text;
		if (!Channel)
		{
			Text = LOCTEXT("NoLinkCode", "Link code: appears once this Boom Box plays or queues music");
		}
		else if (Shared > 1)
		{
			Text = FText::Format(LOCTEXT("LinkCodeShared", "Link code: {0}   (queue shared by {1} Boom Boxes)"),
				FText::FromString(FString::Printf(TEXT("%04d"), Channel->GetLinkCode())), Shared);
		}
		else
		{
			Text = FText::Format(LOCTEXT("LinkCodeSolo", "Link code: {0}   (to share a queue, both Boom Boxes enter each other's code within 3 minutes)"),
				FText::FromString(FString::Printf(TEXT("%04d"), Channel->GetLinkCode())));
		}
		LinkCodeText->SetText(Text);
	}
	BBPWidgetStyle::SetShown(UnlinkButton, Shared > 1);

	if (!LinkMessageText)
	{
		return;
	}
	// Pending requests take priority: they carry a countdown both players need to see.
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (Channel && Playlist && GameState)
	{
		const double Now = GameState->GetServerWorldTimeSeconds();
		const int32 OwnCode = Channel->GetLinkCode();
		for (const FBBPLinkRequest& Request : Playlist->GetLinkRequests())
		{
			const FText Code = FText::FromString(FString::Printf(TEXT("%04d"), Request.FromCode == OwnCode ? Request.ToCode : Request.FromCode));
			if (Request.FromCode == OwnCode)
			{
				LinkMessageText->SetText(FText::Format(LOCTEXT("LinkWaiting", "Waiting for Boom Box {0} to enter {1}. {2} left."),
					Code, FText::FromString(FString::Printf(TEXT("%04d"), OwnCode)), FormatTimeLeft(Request.ExpiresAt - Now)));
				LinkMessageText->SetColorAndOpacity(BBPWidgetStyle::AccentColor);
				return;
			}
			if (Request.ToCode == OwnCode)
			{
				LinkMessageText->SetText(FText::Format(LOCTEXT("LinkIncoming", "Boom Box {0} wants to link. Enter {0} within {1} to merge queues."),
					Code, FormatTimeLeft(Request.ExpiresAt - Now)));
				LinkMessageText->SetColorAndOpacity(BBPWidgetStyle::AccentColor);
				return;
			}
		}
	}
	const bool bRecent = LinkMessageTime >= 0.0 && FPlatformTime::Seconds() - LinkMessageTime < LinkMessageSeconds;
	LinkMessageText->SetText(bRecent ? LinkMessage : FText::GetEmpty());
	LinkMessageText->SetColorAndOpacity(BBPWidgetStyle::DimTextColor);
}

void UBBPMusicPage::SetLinkMessage(const FText& Message)
{
	LinkMessage = Message;
	LinkMessageTime = FPlatformTime::Seconds();
	RefreshLink();
}

void UBBPMusicPage::HandleMembersChanged()
{
	RefreshLink();
}

void UBBPMusicPage::HandleLinkCodeCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleLink();
	}
}

void UBBPMusicPage::HandleLink()
{
	const FString Code = LinkCodeBox ? LinkCodeBox->GetText().ToString().TrimStartAndEnd() : FString();
	bool bFourDigits = Code.Len() == 4;
	for (const TCHAR Char : Code)
	{
		bFourDigits &= FChar::IsDigit(Char);
	}
	if (!bFourDigits)
	{
		SetLinkMessage(LOCTEXT("BadCode", "Enter the 4-digit code shown on the other Boom Box."));
		return;
	}
	SetLinkMessage(LOCTEXT("Linking", "Sending link request..."));
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: linking %s to code %s"), *GetNameSafe(GetBoomBox()), *Code);
	UBBPBlueprintLibrary::RequestLinkBoomBox(GetBoomBox(), FCString::Atoi(*Code));
	LinkCodeBox->SetText(FText::GetEmpty());
}

void UBBPMusicPage::HandleUnlink()
{
	SetLinkMessage(LOCTEXT("Unlinking", "Unlinking..."));
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: unlinking %s"), *GetNameSafe(GetBoomBox()));
	UBBPBlueprintLibrary::RequestUnlinkBoomBox(GetBoomBox());
}

void UBBPMusicPage::HandleSeekBegin()
{
	bSeeking = true;
}

void UBBPMusicPage::HandleSeekEnd()
{
	bSeeking = false;
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	FBBPQueueEntry Current;
	if (!SeekSlider || !Channel || !Channel->GetCurrentEntry(Current) || Current.Track.Duration <= 0.f)
	{
		return;
	}
	const float Target = SeekSlider->GetValue() * Current.Track.Duration;
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: seeking '%s' to %.1f s"), *Current.Track.Title, Target);
	UBBPBlueprintLibrary::RequestSeekTo(GetBoomBox(), Target);
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
	LastOnlineQuery = Text;
	CancelMatchJob();
	OnlineResults.Reset();
	OnlineNote.Reset();
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
		OnlineStatus = TEXT("Reading the Spotify playlist...");
		bOnlineIsCollection = true;
	{
		// -1 marks "starting"; a job that fails immediately reports back before the id is known and resets it to 0.
		MatchJobId = -1;
		const int32 NewJobId = Net->StartSpotifyCollection(Text, FBBPOnMatchProgress::CreateLambda([WeakThis, Generation](const FBBPMatchProgress& Progress)
		{
			if (UBBPMusicPage* This = WeakThis.Get())
			{
				This->HandleMatchProgress(Generation, Progress);
			}
		}));
		if (MatchJobId == -1)
		{
			MatchJobId = NewJobId;
		}
		break;
	}
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
	EnsureCustomMusicLoaded();
	const UGameInstance* GameInstance = GetGameInstance();
	UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	if (MatchJobId > 0 && Net && Net->QueueJobInto(MatchJobId, GetBoomBox()))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: queueing Spotify matches as they're found (%d ready)"), OnlineResults.Num());
		bMatchQueued = true;
		RefreshResults();
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: adding %d online tracks to the queue"), OnlineResults.Num());
	UBBPBlueprintLibrary::RequestAddTracks(GetBoomBox(), OnlineResults);
	bMatchQueued = true;
	OnlineStatus = FString::Printf(TEXT("Added %d tracks to the queue."), OnlineResults.Num());
	RefreshResults();
}

void UBBPMusicPage::RefreshSpotifyButton()
{
	if (!SpotifyButton)
	{
		return;
	}
	const UGameInstance* GameInstance = GetGameInstance();
	const UBBPSpotifyAuth* Auth = GameInstance ? GameInstance->GetSubsystem<UBBPSpotifyAuth>() : nullptr;
	const bool bShow = Auth && Auth->HasAppCredentials() && !Auth->IsConnected();
	BBPWidgetStyle::SetShown(SpotifyButton, bShow);
	if (bShow)
	{
		SpotifyButton->SetLabel(Auth->IsLoginInProgress()
			? LOCTEXT("SpotifyWaiting", "Waiting for Spotify sign-in in your browser...")
			: LOCTEXT("ConnectSpotify", "Connect Spotify"));
	}
}

void UBBPMusicPage::HandleConnectSpotify()
{
	const UGameInstance* GameInstance = GetGameInstance();
	UBBPSpotifyAuth* Auth = GameInstance ? GameInstance->GetSubsystem<UBBPSpotifyAuth>() : nullptr;
	if (!Auth)
	{
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Connect Spotify pressed"));
	TWeakObjectPtr<UBBPMusicPage> WeakThis(this);
	Auth->BeginLogin(FBBPOnSpotifyLogin::CreateLambda([WeakThis](const FString& Error)
	{
		UBBPMusicPage* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		if (!Error.IsEmpty())
		{
			This->OnlineStatus = Error;
			This->RefreshResults();
			return;
		}
		This->OnlineStatus = TEXT("Spotify connected.");
		// Read the playlist that prompted the sign-in again, now in full.
		if (UBBPNetSubsystem::ClassifyLink(This->LastOnlineQuery) == EBBPLinkKind::SpotifyCollection)
		{
			This->RunOnlineSearch(This->LastOnlineQuery);
		}
		else
		{
			This->RefreshResults();
		}
	}));
	RefreshSpotifyButton();
	OnlineStatus = TEXT("Sign in to Spotify in the browser window that just opened, then come back here.");
	RefreshResults();
}

void UBBPMusicPage::HandleMatchProgress(int32 Generation, const FBBPMatchProgress& Progress)
{
	if (Generation != SearchGeneration)
	{
		return;
	}
	OnlineResults = Progress.Matched;
	MatchTotal = Progress.Total;
	OnlineNote = Progress.Note;
	bOnlineIsCollection = true;
	if (!Progress.bFinished)
	{
		OnlineStatus = Progress.Total == 0 ? FString(TEXT("Reading the Spotify playlist..."))
			: FString::Printf(TEXT("Matching on YouTube: %d of %d done%s"), Progress.Processed, Progress.Total,
				bMatchQueued ? TEXT(". Adding each to the queue as it's found.") : TEXT("."));
	}
	else
	{
		MatchJobId = 0;
		OnlineStatus = !Progress.Error.IsEmpty() ? FString::Printf(TEXT("Spotify playlist: %s"), *Progress.Error)
			: FString::Printf(TEXT("Matched %d of %d Spotify tracks on YouTube%s"), Progress.Matched.Num(), Progress.Total,
				bMatchQueued ? TEXT(", all added to the queue.") : TEXT("."));
	}
	RefreshResults();
}

void UBBPMusicPage::CancelMatchJob()
{
	if (MatchJobId > 0)
	{
		const UGameInstance* GameInstance = GetGameInstance();
		if (UBBPNetSubsystem* Net = GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr)
		{
			Net->CancelJob(MatchJobId);
		}
	}
	MatchJobId = 0;
	MatchTotal = 0;
	bMatchQueued = false;
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
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	if (!(Channel && Channel->IsPlaying()))
	{
		EnsureCustomMusicLoaded();
	}
	UBBPBlueprintLibrary::RequestTogglePlaying(GetBoomBox());
}

void UBBPMusicPage::HandleNext()
{
	UBBPBlueprintLibrary::RequestSkip(GetBoomBox());
}

void UBBPMusicPage::HandlePrevious()
{
	UBBPBlueprintLibrary::RequestPrevious(GetBoomBox());
}

void UBBPMusicPage::HandleShuffle()
{
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	UBBPBlueprintLibrary::RequestSetShuffle(GetBoomBox(), !(Channel && Channel->GetPlaybackState().bShuffle));
}

void UBBPMusicPage::HandleRepeat()
{
	const ABBPMusicChannel* Channel = BoundChannel.Get();
	const EBBPRepeatMode Current = Channel ? Channel->GetPlaybackState().RepeatMode : EBBPRepeatMode::All;
	const EBBPRepeatMode Next = Current == EBBPRepeatMode::All ? EBBPRepeatMode::One
		: Current == EBBPRepeatMode::One ? EBBPRepeatMode::Off
		: EBBPRepeatMode::All;
	UBBPBlueprintLibrary::RequestSetRepeatMode(GetBoomBox(), Next);
}

void UBBPMusicPage::HandleClearQueue()
{
	UBBPBlueprintLibrary::RequestClearQueue(GetBoomBox());
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
