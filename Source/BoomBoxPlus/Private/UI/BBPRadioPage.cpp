#include "UI/BBPRadioPage.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Misc/Paths.h"
#include "Net/BBPImport.h"
#include "Net/BBPNetSubsystem.h"
#include "Net/BBPRadio.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "UI/BBPFileDialog.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPStationRow.h"
#include "UI/BBPWidgetStyle.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

namespace
{
	// Seconds between checks of which station plays.
	constexpr float PlayingCheckInterval = 0.5f;

	UBBPNetSubsystem* GetNet(const UUserWidget* Widget)
	{
		const UGameInstance* GameInstance = Widget ? Widget->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UBBPNetSubsystem>() : nullptr;
	}
}

void UBBPRadioPage::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (AddButton) AddButton->OnClicked.AddDynamic(this, &UBBPRadioPage::HandleAdd);
	if (AddressBox) AddressBox->OnTextCommitted.AddDynamic(this, &UBBPRadioPage::HandleAddressCommitted);
	if (ImportButton) ImportButton->OnClicked.AddDynamic(this, &UBBPRadioPage::HandleImport);
}

void UBBPRadioPage::BuildDefaultLayout()
{
	using namespace BBPWidgetStyle;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Column;

	UTextBlock* AddTitle = nullptr;
	Column->AddChildToVerticalBox(MakeSectionHeader(WidgetTree, LOCTEXT("AddStation", "Add a station"), AddTitle));
	UHorizontalBox* AddRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	Column->AddChildToVerticalBox(AddRow);
	AddressBox = WidgetTree->ConstructWidget<UEditableTextBox>();
	StyleTextBox(AddressBox, 12);
	AddressBox->SetHintText(LOCTEXT("StationAddressHint", "Paste a station's stream address (http://... , or a .pls / .m3u link) and press Enter."));
	UHorizontalBoxSlot* AddressSlot = AddRow->AddChildToHorizontalBox(AddressBox);
	AddressSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	AddressSlot->SetVerticalAlignment(VAlign_Center);
	AddressSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	AddButton = MakeButton(WidgetTree, LOCTEXT("AddStationButton", "Add"));
	UHorizontalBoxSlot* AddButtonSlot = AddRow->AddChildToHorizontalBox(AddButton);
	AddButtonSlot->SetVerticalAlignment(VAlign_Center);
	AddButtonSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
	ImportButton = MakeButton(WidgetTree, LOCTEXT("ImportStations", "Import List"));
	ImportButton->SetToolTipText(LOCTEXT("ImportStationsTip", "Add every station in a file: a .pls or .m3u playlist, or a text file with one station per line (a name and its stream address, e.g. separated by a tab)."));
	AddRow->AddChildToHorizontalBox(ImportButton)->SetVerticalAlignment(VAlign_Center);
	AddStatusText = MakeText(WidgetTree, 11, DimTextColor,
		LOCTEXT("AddStationHelp", "A stream address is the link the station's own player plays, often ending in .mp3, .aac, .pls or .m3u. The station is checked, then saved to your list."));
	AddStatusText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(AddStatusText)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

	UTextBlock* ListTitle = nullptr;
	Column->AddChildToVerticalBox(MakeSectionHeader(WidgetTree, LOCTEXT("YourStations", "Your stations"), ListTitle))->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
	StationsTitle = ListTitle;
	EmptyText = MakeText(WidgetTree, 11, DimTextColor,
		LOCTEXT("NoStations", "No stations yet. Add one above. Stations you tune in from the search box, and YouTube live streams you queue, are saved here too."));
	EmptyText->SetAutoWrapText(true);
	Column->AddChildToVerticalBox(EmptyText);

	UBorder* ListPanel = MakePanel(WidgetTree, InsetColor, FMargin(6.f));
	UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(ListPanel);
	ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ListSlot->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
	StationList = WidgetTree->ConstructWidget<UScrollBox>();
	StyleScrollBox(StationList);
	ListPanel->SetContent(StationList);
}

void UBBPRadioPage::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	PlayingCheckTimer -= InDeltaTime;
	if (PlayingCheckTimer <= 0.f)
	{
		PlayingCheckTimer = PlayingCheckInterval;
		if (GetPlayingStationId() != ShownPlayingId)
		{
			Refresh();
		}
	}
}

void UBBPRadioPage::Refresh()
{
	const UBBPNetSubsystem* Net = GetNet(this);
	const TArray<FBBPTrack> Stations = Net ? Net->GetStations() : TArray<FBBPTrack>();
	ShownPlayingId = GetPlayingStationId();

	if (StationsTitle)
	{
		StationsTitle->SetText(FText::Format(LOCTEXT("YourStationsCount", "Your stations ({0} of {1})"), Stations.Num(), UBBPNetSubsystem::MaxStations));
	}
	BBPWidgetStyle::SetShown(EmptyText, Stations.Num() == 0);
	if (!StationList)
	{
		return;
	}
	while (Rows.Num() < Stations.Num())
	{
		UBBPStationRow* Row = CreateWidget<UBBPStationRow>(this, UBBPStationRow::StaticClass());
		if (!Row)
		{
			break;
		}
		StationList->AddChild(Row);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(Row->Slot))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
		Rows.Add(Row);
	}
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		const bool bShow = Stations.IsValidIndex(i);
		if (bShow)
		{
			Rows[i]->SetStation(Stations[i], !ShownPlayingId.IsEmpty() && Stations[i].Id == ShownPlayingId);
		}
		BBPWidgetStyle::SetShown(Rows[i], bShow);
	}
}

void UBBPRadioPage::HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleAdd();
	}
}

void UBBPRadioPage::HandleAdd()
{
	UBBPNetSubsystem* Net = GetNet(this);
	const FString Address = AddressBox ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!Net || Address.IsEmpty())
	{
		return;
	}
	switch (UBBPNetSubsystem::ClassifyLink(Address))
	{
	case EBBPLinkKind::Stream:
		break;
	case EBBPLinkKind::YouTubeVideo:
	case EBBPLinkKind::YouTubePlaylist:
		SetAddStatus(LOCTEXT("AddYouTubeLive", "YouTube live streams are added by pasting them into the search box on the music page and queuing them."), true);
		return;
	default:
		SetAddStatus(LOCTEXT("AddNotStream", "That isn't a stream address. Paste the link the station plays, starting with http:// or https://."), true);
		return;
	}
	if (Net->GetStations().Num() >= UBBPNetSubsystem::MaxStations && !Net->HasStation(BBPRadio::MakeTrackId(BBPRadio::NormalizeUrl(Address))))
	{
		SetAddStatus(FText::Format(LOCTEXT("StationsFull", "Your list is full ({0} stations). Remove one first."), UBBPNetSubsystem::MaxStations), true);
		return;
	}

	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: adding station '%s'"), *Address);
	SetAddStatus(LOCTEXT("CheckingStation", "Checking the station..."), false);
	if (AddButton)
	{
		AddButton->SetIsEnabled(false);
	}
	const int32 Generation = ++AddGeneration;
	TWeakObjectPtr<UBBPRadioPage> WeakThis(this);
	Net->TuneRadio(Address, FBBPOnRadioTuned::CreateLambda([WeakThis, Generation](const FBBPTrack& Station, const FString& Error)
	{
		UBBPRadioPage* This = WeakThis.Get();
		if (!This || Generation != This->AddGeneration)
		{
			return;
		}
		if (This->AddButton)
		{
			This->AddButton->SetIsEnabled(true);
		}
		UBBPNetSubsystem* Net = GetNet(This);
		if (!Error.IsEmpty() || !Net)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: station not added: %s"), *Error);
			This->SetAddStatus(FText::FromString(Error), true);
			return;
		}
		const bool bAlreadySaved = Net->HasStation(Station.Id);
		Net->RememberStation(Station);
		if (This->AddressBox)
		{
			This->AddressBox->SetText(FText::GetEmpty());
		}
		const FText Name = BBPWidgetStyle::ToDisplayText(Station.Title);
		This->SetAddStatus(bAlreadySaved ? FText::Format(LOCTEXT("StationAlreadySaved", "{0} is already in your list."), Name)
			: FText::Format(LOCTEXT("StationAdded", "Added {0}. Click its name to rename it."), Name), false);
		This->Refresh();
	}));
}

void UBBPRadioPage::HandleImport()
{
	TWeakObjectPtr<UBBPRadioPage> WeakThis(this);
	const bool bShown = BBPFileDialog::OpenListFile(TEXT("Import radio stations"), [WeakThis](const FString& Path)
	{
		UBBPRadioPage* This = WeakThis.Get();
		if (This && !Path.IsEmpty())
		{
			This->ImportFile(Path);
		}
	});
	if (bShown)
	{
		SetAddStatus(LOCTEXT("ImportPicking", "Choose a station list in the window that opened (it may be behind the game)."), false);
	}
}

void UBBPRadioPage::ImportFile(const FString& Path)
{
	UBBPNetSubsystem* Net = GetNet(this);
	FString Text, Error;
	if (!Net || !BBPImport::LoadListFile(Path, Text, Error))
	{
		SetAddStatus(FText::FromString(Error), true);
		return;
	}
	const TArray<BBPImport::FStation> Stations = BBPImport::ParseStations(Text);
	const FString FileName = FPaths::GetCleanFilename(Path);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: importing %d stations from '%s'"), Stations.Num(), *Path);
	if (Stations.Num() == 0)
	{
		SetAddStatus(FText::Format(LOCTEXT("ImportNoStations", "No stream addresses found in {0}. Put one station per line: a name, then its http:// address."), FText::FromString(FileName)), true);
		return;
	}
	SetAddStatus(FText::Format(LOCTEXT("ImportingStations", "Importing {0} stations from {1}..."), Stations.Num(), FText::FromString(FileName)), false);
	if (ImportButton)
	{
		ImportButton->SetIsEnabled(false);
	}
	TWeakObjectPtr<UBBPRadioPage> WeakThis(this);
	Net->ImportStations(Stations, FBBPOnStationsImported::CreateLambda([WeakThis, FileName](const FBBPStationImportResult& Result)
	{
		UBBPRadioPage* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		if (This->ImportButton)
		{
			This->ImportButton->SetIsEnabled(true);
		}
		FString Message = FString::Printf(TEXT("Added %d station%s from %s."), Result.Added, Result.Added == 1 ? TEXT("") : TEXT("s"), *FileName);
		if (Result.AlreadySaved > 0)
		{
			Message += FString::Printf(TEXT(" %d %s already in your list."), Result.AlreadySaved, Result.AlreadySaved == 1 ? TEXT("was") : TEXT("were"));
		}
		if (Result.NoRoom > 0)
		{
			Message += FString::Printf(TEXT(" %d didn't fit (the list holds %d)."), Result.NoRoom, UBBPNetSubsystem::MaxStations);
		}
		if (Result.Unusable > 0)
		{
			Message += FString::Printf(TEXT(" %d %s skipped (not a stream address, or its playlist couldn't be read)."), Result.Unusable, Result.Unusable == 1 ? TEXT("was") : TEXT("were"));
		}
		This->SetAddStatus(FText::FromString(Message), Result.Added == 0);
		This->Refresh();
	}));
}

void UBBPRadioPage::SetAddStatus(const FText& Status, bool bError)
{
	if (AddStatusText)
	{
		AddStatusText->SetText(Status);
		AddStatusText->SetColorAndOpacity(FSlateColor(bError ? BBPWidgetStyle::AccentColor : BBPWidgetStyle::TextColor));
	}
}

FString UBBPRadioPage::GetPlayingStationId() const
{
	const UBBPMusicPage* Page = GetTypedOuter<UBBPMusicPage>();
	const ABBPMusicChannel* Channel = Page ? Page->GetChannel() : nullptr;
	FBBPQueueEntry Entry;
	if (!Channel || Channel->GetPlaybackState().bPaused || !Channel->GetCurrentEntry(Entry) || !Entry.Track.IsLive())
	{
		return FString();
	}
	return Entry.Track.Id;
}

#undef LOCTEXT_NAMESPACE
