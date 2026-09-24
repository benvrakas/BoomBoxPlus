#include "UI/BBPWidgetStyle.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "UI/BBPGameButton.h"

namespace
{
	const TCHAR* GameFontPath = TEXT("/Game/FactoryGame/Interface/Font/DescriptionText.DescriptionText");
	const TCHAR* SearchIconPath = TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/SearchIcon.SearchIcon");
	const TCHAR* SliderHandlePath = TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/ConstructorWindows/ManufacutringMenu_Overclock_SliderHandle.ManufacutringMenu_Overclock_SliderHandle");

	constexpr float SearchIconSize = 18.f;
	constexpr float SliderHandleHeight = 20.f;

	// Loads an asset once per session; logs the first failure.
	template <typename T>
	T* LoadGameAsset(const TCHAR* Path)
	{
		T* Asset = LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
		{
			static TSet<FString> Reported;
			if (!Reported.Contains(Path))
			{
				Reported.Add(Path);
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: game asset %s not found; using a plain look for it"), Path);
			}
		}
		return Asset;
	}

	FSlateBrush MakeFlatBrush(const FLinearColor& Color)
	{
		FSlateBrush Brush = FSlateColorBrush(Color);
		return Brush;
	}

	FSlateBrush MakeOutlinedBrush(const FLinearColor& Fill, const FLinearColor& Outline, float Width)
	{
		FSlateBrush Brush = FSlateRoundedBoxBrush(Fill, 2.f);
		Brush.OutlineSettings = FSlateBrushOutlineSettings(2.f, FSlateColor(Outline), Width);
		return Brush;
	}
}

FSlateFontInfo BBPWidgetStyle::GetFont(int32 Size, EFontWeight Weight)
{
	const FName Typeface = Weight == EFontWeight::Bold ? FName(TEXT("Bold")) : Weight == EFontWeight::SemiBold ? FName(TEXT("SemiBold")) : FName(TEXT("Regular"));
	if (const UFont* Font = LoadGameAsset<UFont>(GameFontPath))
	{
		return FSlateFontInfo(Font, Size, Typeface);
	}
	FSlateFontInfo Fallback = FCoreStyle::GetDefaultFontStyle(Weight == EFontWeight::Regular ? "Regular" : "Bold", Size);
	return Fallback;
}

UTextBlock* BBPWidgetStyle::MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Color, const FText& Text, EFontWeight Weight)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
	Block->SetFont(GetFont(FontSize, Weight));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(Text);
	return Block;
}

UBBPGameButton* BBPWidgetStyle::MakeButton(UWidgetTree* Tree, const FText& Label)
{
	UBBPGameButton* Button = Tree->ConstructWidget<UBBPGameButton>(UBBPGameButton::StaticClass());
	Button->SetLabel(Label);
	return Button;
}

void BBPWidgetStyle::SetButtonLabel(UBBPGameButton* Button, const FText& Label)
{
	if (Button)
	{
		Button->SetLabel(Label);
	}
}

UBorder* BBPWidgetStyle::MakePanel(UWidgetTree* Tree, const FLinearColor& Color, const FMargin& Padding)
{
	UBorder* Panel = Tree->ConstructWidget<UBorder>();
	Panel->SetBrush(MakeFlatBrush(Color));
	Panel->SetPadding(Padding);
	return Panel;
}

UVerticalBox* BBPWidgetStyle::MakeSectionHeader(UWidgetTree* Tree, const FText& Title, UTextBlock*& OutTitle)
{
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>();
	OutTitle = MakeText(Tree, 13, AccentColor, Title.ToUpper(), EFontWeight::Bold);
	Box->AddChildToVerticalBox(OutTitle);
	UBorder* Rule = Tree->ConstructWidget<UBorder>();
	Rule->SetBrush(MakeFlatBrush(FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, 0.45f)));
	Rule->SetPadding(FMargin(0.f, 1.f));
	Box->AddChildToVerticalBox(Rule)->SetPadding(FMargin(0.f, 3.f, 0.f, 6.f));
	return Box;
}

UImage* BBPWidgetStyle::MakeSearchIcon(UWidgetTree* Tree)
{
	UTexture2D* Texture = LoadGameAsset<UTexture2D>(SearchIconPath);
	if (!Texture)
	{
		return nullptr;
	}
	UImage* Image = Tree->ConstructWidget<UImage>();
	Image->SetBrushFromTexture(Texture);
	Image->SetDesiredSizeOverride(FVector2D(SearchIconSize, SearchIconSize));
	Image->SetColorAndOpacity(DimTextColor);
	return Image;
}

void BBPWidgetStyle::StyleTextBox(UEditableTextBox* Box, int32 FontSize)
{
	if (!Box)
	{
		return;
	}
	FEditableTextBoxStyle& Style = Box->WidgetStyle;
	Style.SetBackgroundImageNormal(MakeOutlinedBrush(InputColor, OutlineColor, 1.f));
	Style.SetBackgroundImageHovered(MakeOutlinedBrush(InputColor, DimTextColor, 1.f));
	Style.SetBackgroundImageFocused(MakeOutlinedBrush(InputColor, AccentColor, 1.f));
	Style.SetBackgroundImageReadOnly(MakeOutlinedBrush(InputColor, OutlineColor, 1.f));
	Style.SetPadding(FMargin(8.f, 5.f));
	Style.SetForegroundColor(FSlateColor(TextColor));
	Style.SetFocusedForegroundColor(FSlateColor(TextColor));
	FTextBlockStyle TextStyle = Style.TextStyle;
	TextStyle.SetFont(GetFont(FontSize));
	TextStyle.SetColorAndOpacity(FSlateColor(TextColor));
	Style.SetTextStyle(TextStyle);
}

void BBPWidgetStyle::StyleScrollBox(UScrollBox* Box)
{
	if (!Box)
	{
		return;
	}
	FScrollBarStyle Bar = Box->GetWidgetBarStyle();
	const FSlateBrush Thumb = MakeFlatBrush(FLinearColor(0.12f, 0.12f, 0.12f, 1.f));
	Bar.SetNormalThumbImage(Thumb);
	Bar.SetHoveredThumbImage(MakeFlatBrush(AccentColor));
	Bar.SetDraggedThumbImage(MakeFlatBrush(AccentColor));
	Bar.SetVerticalBackgroundImage(MakeFlatBrush(FLinearColor(0.f, 0.f, 0.f, 0.35f)));
	Bar.SetVerticalTopSlotImage(FSlateNoResource());
	Bar.SetVerticalBottomSlotImage(FSlateNoResource());
	Box->SetWidgetBarStyle(Bar);
	Box->SetScrollbarThickness(FVector2D(4.f, 4.f));
	Box->SetScrollbarPadding(FMargin(4.f, 0.f, 0.f, 0.f));
}

void BBPWidgetStyle::StyleSlider(USlider* Slider)
{
	if (!Slider)
	{
		return;
	}
	FSliderStyle Style = Slider->GetWidgetStyle();
	Style.SetNormalBarImage(MakeFlatBrush(FLinearColor(0.03f, 0.03f, 0.03f, 1.f)));
	Style.SetHoveredBarImage(MakeFlatBrush(FLinearColor(0.05f, 0.05f, 0.05f, 1.f)));
	Style.SetDisabledBarImage(MakeFlatBrush(FLinearColor(0.02f, 0.02f, 0.02f, 1.f)));
	Style.SetBarThickness(4.f);

	FSlateBrush Handle = MakeFlatBrush(AccentColor);
	Handle.ImageSize = FVector2D(8.f, SliderHandleHeight);
	if (UTexture2D* Texture = LoadGameAsset<UTexture2D>(SliderHandlePath))
	{
		Handle = FSlateBrush();
		Handle.SetResourceObject(Texture);
		const float Aspect = Texture->GetSizeY() > 0 ? (float)Texture->GetSizeX() / Texture->GetSizeY() : 0.5f;
		Handle.ImageSize = FVector2D(SliderHandleHeight * Aspect, SliderHandleHeight);
	}
	FSlateBrush HoveredHandle = Handle;
	HoveredHandle.TintColor = FSlateColor(FLinearColor(1.f, 0.85f, 0.7f));
	FSlateBrush DisabledHandle = Handle;
	DisabledHandle.TintColor = FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f));
	Style.SetNormalThumbImage(Handle);
	Style.SetHoveredThumbImage(HoveredHandle);
	Style.SetDisabledThumbImage(DisabledHandle);
	Slider->SetWidgetStyle(Style);
	Slider->SetSliderBarColor(FLinearColor::White);
	Slider->SetSliderHandleColor(FLinearColor::White);
}

void BBPWidgetStyle::SetShown(UWidget* Widget, bool bShown)
{
	if (Widget)
	{
		Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}
