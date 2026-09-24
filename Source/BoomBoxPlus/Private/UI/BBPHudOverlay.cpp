#include "UI/BBPHudOverlay.h"
#include "BBPBlueprintLibrary.h"
#include "BBPConfig.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "FGBoomBoxPlayer.h"
#include "Playback/BBPPlaybackController.h"
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

	LyricText = BBPWidgetStyle::MakeText(WidgetTree, 20, BBPWidgetStyle::TextColor);
	LyricText->SetJustification(ETextJustify::Center);
	LyricText->SetShadowOffset(FVector2D(1.f, 1.f));
	LyricText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));
	UCanvasPanelSlot* LyricSlot = Canvas->AddChildToCanvas(LyricText);
	LyricSlot->SetAnchors(FAnchors(0.5f, 0.82f));
	LyricSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	LyricSlot->SetAutoSize(true);

	UBorder* Box = WidgetTree->ConstructWidget<UBorder>();
	Box->SetBrushColor(BBPWidgetStyle::PanelColor);
	Box->SetPadding(FMargin(14.f, 8.f));
	NowPlayingBox = Box;
	UVerticalBox* Texts = WidgetTree->ConstructWidget<UVerticalBox>();
	Box->SetContent(Texts);
	Texts->AddChildToVerticalBox(BBPWidgetStyle::MakeText(WidgetTree, 10, BBPWidgetStyle::AccentColor, NSLOCTEXT("BoomBoxPlus", "NowPlaying", "NOW PLAYING")));
	NowPlayingTitleText = BBPWidgetStyle::MakeText(WidgetTree, 15, BBPWidgetStyle::TextColor);
	Texts->AddChildToVerticalBox(NowPlayingTitleText);
	NowPlayingArtistText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor);
	Texts->AddChildToVerticalBox(NowPlayingArtistText);
	UCanvasPanelSlot* BoxSlot = Canvas->AddChildToCanvas(Box);
	BoxSlot->SetAnchors(FAnchors(1.f, 0.f));
	BoxSlot->SetAlignment(FVector2D(1.f, 0.f));
	BoxSlot->SetPosition(FVector2D(-24.f, 120.f));
	BoxSlot->SetAutoSize(true);
}

bool UBBPHudOverlay::IsInEarshot() const
{
	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	const APawn* Pawn = GetOwningPlayerPawn();
	if (!Playlist || !Pawn)
	{
		return false;
	}
	const float RangeSquared = FMath::Square(UBBPPlaybackController::GetAudibleRange());
	for (const FBBPActiveBoomBox& Active : Playlist->GetActiveBoomBoxes())
	{
		if (Active.BoomBox && FVector::DistSquared(Active.BoomBox->GetActorLocation(), Pawn->GetActorLocation()) <= RangeSquared)
		{
			return true;
		}
	}
	return false;
}

void UBBPHudOverlay::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(this);
	FBBPQueueEntry Current;
	const bool bPlaying = Playlist && Playlist->IsPlaying() && Playlist->GetCurrentEntry(Current);
	const bool bAudible = bPlaying && IsInEarshot();
	bShowLyrics = UBBPConfig::GetBool(this, UBBPConfig::ShowLyricsKey, true);
	bShowNowPlaying = UBBPConfig::GetBool(this, UBBPConfig::ShowNowPlayingKey, true);

	if (LyricText)
	{
		const FString Line = (bAudible && bShowLyrics) ? UBBPBlueprintLibrary::GetCurrentLyricLine(this) : FString();
		LyricText->SetText(FText::FromString(Line));
	}

	const int32 EntryId = bPlaying ? Current.EntryId : INDEX_NONE;
	if (EntryId != LastEntryId)
	{
		LastEntryId = EntryId;
		if (bAudible && bShowNowPlaying)
		{
			NowPlayingTimeLeft = NowPlayingSeconds;
			if (NowPlayingTitleText) NowPlayingTitleText->SetText(FText::FromString(Current.Track.Title));
			if (NowPlayingArtistText) NowPlayingArtistText->SetText(FText::FromString(Current.Track.Artist));
		}
	}

	if (NowPlayingBox)
	{
		NowPlayingTimeLeft = FMath::Max(0.f, NowPlayingTimeLeft - InDeltaTime);
		BBPWidgetStyle::SetShown(NowPlayingBox, NowPlayingTimeLeft > 0.f);
		NowPlayingBox->SetRenderOpacity(FMath::Clamp(NowPlayingTimeLeft / NowPlayingFadeSeconds, 0.f, 1.f));
	}
}
