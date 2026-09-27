#include "UI/BBPNearbyBoomBoxRow.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "FGBoomBoxPlayer.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "UI/BBPWidgetStyle.h"

namespace
{
	constexpr float SliderWidth = 110.f;
	constexpr float PercentWidth = 40.f;

	UBBPPlaybackController* GetController(const UObject* WorldContext)
	{
		const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(WorldContext);
		return Playlist ? Playlist->GetPlaybackController() : nullptr;
	}
}

void UBBPNearbyBoomBoxRow::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		BuildDefaultLayout();
	}
	if (VolumeSlider)
	{
		VolumeSlider->OnValueChanged.AddDynamic(this, &UBBPNearbyBoomBoxRow::HandleVolumeChanged);
		VolumeSlider->OnMouseCaptureBegin.AddDynamic(this, &UBBPNearbyBoomBoxRow::HandleCaptureBegin);
		VolumeSlider->OnMouseCaptureEnd.AddDynamic(this, &UBBPNearbyBoomBoxRow::HandleCaptureEnd);
		VolumeSlider->OnControllerCaptureBegin.AddDynamic(this, &UBBPNearbyBoomBoxRow::HandleCaptureBegin);
		VolumeSlider->OnControllerCaptureEnd.AddDynamic(this, &UBBPNearbyBoomBoxRow::HandleCaptureEnd);
	}
}

void UBBPNearbyBoomBoxRow::BuildDefaultLayout()
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	WidgetTree->RootWidget = Row;

	LabelText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::TextColor);
	BBPWidgetStyle::Truncate(LabelText);
	UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelText);
	LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	LabelSlot->SetVerticalAlignment(VAlign_Center);
	LabelSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));

	VolumeSlider = WidgetTree->ConstructWidget<USlider>();
	BBPWidgetStyle::StyleVolumeSlider(VolumeSlider);
	USizeBox* SliderSize = WidgetTree->ConstructWidget<USizeBox>();
	SliderSize->SetWidthOverride(SliderWidth);
	SliderSize->SetContent(VolumeSlider);
	UHorizontalBoxSlot* SliderSlot = Row->AddChildToHorizontalBox(SliderSize);
	SliderSlot->SetVerticalAlignment(VAlign_Center);
	SliderSlot->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));

	VolumeText = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::DimTextColor, FText::GetEmpty(), BBPWidgetStyle::EFontWeight::SemiBold);
	USizeBox* PercentSize = WidgetTree->ConstructWidget<USizeBox>();
	PercentSize->SetWidthOverride(PercentWidth);
	PercentSize->SetContent(VolumeText);
	Row->AddChildToHorizontalBox(PercentSize)->SetVerticalAlignment(VAlign_Center);
}

void UBBPNearbyBoomBoxRow::Setup(AFGBoomBoxPlayer* InBoomBox, const FText& Label, float MyVolume)
{
	BoomBox = InBoomBox;
	if (LabelText)
	{
		LabelText->SetText(Label);
	}
	if (!bDragging)
	{
		if (VolumeSlider)
		{
			VolumeSlider->SetValue(MyVolume);
		}
		ShowVolume(MyVolume);
	}
}

void UBBPNearbyBoomBoxRow::ShowVolume(float Value)
{
	if (VolumeText)
	{
		VolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.f))));
	}
}

void UBBPNearbyBoomBoxRow::HandleVolumeChanged(float Value)
{
	Value = BBPWidgetStyle::SnapVolume(Value);
	// Only this player's volume for this one Boom Box: nothing is sent to the server.
	if (UBBPPlaybackController* Controller = GetController(this))
	{
		Controller->SetMyVolume(BoomBox.Get(), Value);
	}
	ShowVolume(Value);
}

void UBBPNearbyBoomBoxRow::HandleCaptureBegin()
{
	bDragging = true;
}

void UBBPNearbyBoomBoxRow::HandleCaptureEnd()
{
	bDragging = false;
}
