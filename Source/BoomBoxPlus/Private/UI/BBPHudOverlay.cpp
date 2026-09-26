#include "UI/BBPHudOverlay.h"
#include "BBPBlueprintLibrary.h"
#include "BBPConfig.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FGBoomBoxPlayer.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "UI/BBPWidgetStyle.h"

namespace
{
	constexpr float NowPlayingSeconds = 4.f;
	constexpr float NowPlayingFadeSeconds = 0.5f;
}

void UBBPHudOverlay::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	SetVisibility(ESlateVisibility::HitTestInvisible);
	BBPWidgetStyle::SetShown(NowPlayingBox, false);
}

void UBBPHudOverlay::BuildDefaultLayout()
{
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;

	// Bottom-centre stack: the "now playing" card directly above the lyric line. The game's own HUD never draws
	// here, so both stay visible whatever the overlay's Z-order (see UBBPPlaybackController::EnsureHudOverlay).
	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>();
	UCanvasPanelSlot* StackSlot = Canvas->AddChildToCanvas(Stack);
	StackSlot->SetAnchors(FAnchors(0.5f, 0.84f));
	StackSlot->SetAlignment(FVector2D(0.5f, 1.f));
	StackSlot->SetAutoSize(true);

	UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
	Box->SetBrushColor(BBPWidgetStyle::PanelColor);
	Box->SetPadding(FMargin(12.f, 6.f));
	NowPlayingBox = Box;
	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
	Box->SetContent(Texts);
	UTextBlock* Label = BBPWidgetStyle::MakeText(WidgetTree, 10, BBPWidgetStyle::AccentColor, NSLOCTEXT("BoomBoxPlus", "NowPlaying", "NOW PLAYING"));
	Label->SetJustification(ETextJustify::Center);
	Texts->AddChildToVerticalBox(Label);
	NowPlayingTitleText = BBPWidgetStyle::MakeText(WidgetTree, 15, BBPWidgetStyle::TextColor);
	NowPlayingTitleText->SetJustification(ETextJustify::Center);
	Texts->AddChildToVerticalBox(NowPlayingTitleText);
	NowPlayingArtistText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor);
	NowPlayingArtistText->SetJustification(ETextJustify::Center);
	Texts->AddChildToVerticalBox(NowPlayingArtistText);
	UVerticalBoxSlot* BoxSlot = Stack->AddChildToVerticalBox(Box);
	BoxSlot->SetHorizontalAlignment(HAlign_Center);
	BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	LyricText = BBPWidgetStyle::MakeText(WidgetTree, 20, BBPWidgetStyle::TextColor);
	LyricText->SetJustification(ETextJustify::Center);
	LyricText->SetShadowOffset(FVector2D(1.f, 1.f));
	LyricText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	Stack->AddChildToVerticalBox(LyricText)->SetHorizontalAlignment(HAlign_Center);
}

const ABBPMusicChannel* UBBPHudOverlay::GetHeardChannel() const
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const UBBPPlaybackController* Controller = Playlist ? Playlist->GetPlaybackController() : nullptr;
	return Controller ? Controller->GetAudibleChannel(false) : nullptr;
}

void UBBPHudOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const ABBPMusicChannel* Channel = GetHeardChannel();
	FBBPQueueEntry Current;
	const bool bPlaying = Channel && Channel->GetCurrentEntry(Current);
	bShowLyrics = UBBPConfig::GetBool(this, UBBPConfig::ShowLyricsKey, true);
	bShowNowPlaying = UBBPConfig::GetBool(this, UBBPConfig::ShowNowPlayingKey, true);

	if (LyricText)
	{
		const FString Line = (bPlaying && bShowLyrics) ? UBBPBlueprintLibrary::GetCurrentLyricLine(Channel) : FString();
		LyricText->SetText(BBPWidgetStyle::ToDisplayText(Line));
	}

	// Shown for a new track, and again when a radio station announces a new song (its title changes). Entry ids are
	// per channel, so a different channel counts as a different track.
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	FBBPNowPlaying Described;
	UBBPPlaybackController::DescribeNowPlaying(Channel, Playlist ? Playlist->GetPlaybackController() : nullptr, Described);
	const int32 EntryId = bPlaying ? Current.EntryId : INDEX_NONE;
	if (EntryId != LastEntryId || Channel != LastChannel.Get() || !Described.Title.Equals(LastTitle, ESearchCase::CaseSensitive))
	{
		LastEntryId = EntryId;
		LastChannel = Channel;
		LastTitle = Described.Title;
		if (bPlaying && bShowNowPlaying)
		{
			NowPlayingTimeLeft = NowPlayingSeconds;
			if (NowPlayingTitleText) NowPlayingTitleText->SetText(BBPWidgetStyle::ToDisplayText(Described.Title));
			if (NowPlayingArtistText) NowPlayingArtistText->SetText(BBPWidgetStyle::ToDisplayText(Described.Subtitle));
		}
	}

	if (NowPlayingBox)
	{
		NowPlayingTimeLeft = FMath::Max(0.f, NowPlayingTimeLeft - InDeltaTime);
		BBPWidgetStyle::SetShown(NowPlayingBox, NowPlayingTimeLeft > 0.f);
		NowPlayingBox->SetRenderOpacity(FMath::Clamp(NowPlayingTimeLeft / NowPlayingFadeSeconds, 0.f, 1.f));
	}
}
