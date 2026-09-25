#pragma once

#include "CoreMinimal.h"

class UBBPGameButton;
class UBorder;
class UEditableTextBox;
class UImage;
class UScrollBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UWidget;
class UWidgetTree;

// Satisfactory's UI look for the built-in layouts: the game's font, colours, textures and button widget.
namespace BBPWidgetStyle
{
	// Colours are linear values of the game's sRGB palette.
	const FLinearColor TextColor(0.831f, 0.831f, 0.831f);			// #EBEBEB
	const FLinearColor DimTextColor(0.305f, 0.305f, 0.305f);		// #969696
	const FLinearColor AccentColor(0.956f, 0.301f, 0.0666f);		// FICSIT orange #FA9549
	const FLinearColor PanelColor(0.0091f, 0.0091f, 0.0103f, 0.92f);	// window body #18181A
	const FLinearColor InsetColor(0.0044f, 0.0044f, 0.0052f, 0.85f);	// recessed panels #0E0E10
	const FLinearColor RowColor(0.0212f, 0.0212f, 0.0242f, 0.9f);	// list rows #28282B
	const FLinearColor PlayingRowColor(0.956f, 0.301f, 0.0666f, 0.16f);
	const FLinearColor InputColor(0.0024f, 0.0024f, 0.0027f, 1.f);	// text fields #080809
	const FLinearColor OutlineColor(0.0612f, 0.0612f, 0.0648f, 1.f);	// #464648
	const FLinearColor ButtonColor(0.1144f, 0.1144f, 0.1144f);		// fallback buttons #5F5F5F
	const FLinearColor FlatButtonColor(0.0395f, 0.0395f, 0.0452f, 1.f);	// flat list buttons #38383B
	const FLinearColor AccentPressedColor(0.604f, 0.162f, 0.0319f);	// pressed orange #CC7033

	enum class EFontWeight : uint8
	{
		Regular,
		SemiBold,
		Bold
	};

	// Returns the game's UI font at the given size and weight, or the engine font if it can't be loaded.
	FSlateFontInfo GetFont(int32 Size, EFontWeight Weight = EFontWeight::Regular);

	// Creates a text block in the game's font.
	UTextBlock* MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Color, const FText& Text = FText::GetEmpty(), EFontWeight Weight = EFontWeight::Regular);

	// Returns Text for a text block in the game's font, which has no emoji: drops emoji, pictographic symbols and their
	// joiners/variation selectors, and collapses the spaces they leave.
	FText ToDisplayText(const FString& Text);

	// Cuts text that doesn't fit its space off with "..." instead of letting it run over neighbouring widgets.
	void Truncate(UTextBlock* Text);

	// Creates a button drawn with the game's own button widget, or a light flat one when bCompact (for list rows).
	UBBPGameButton* MakeButton(UWidgetTree* Tree, const FText& Label, bool bCompact = false);

	// Replaces the text of a button made by MakeButton.
	void SetButtonLabel(UBBPGameButton* Button, const FText& Label);

	// Creates a flat panel with the given colour and padding.
	UBorder* MakePanel(UWidgetTree* Tree, const FLinearColor& Color, const FMargin& Padding);

	// Creates an orange, upper-case section title with a thin rule under it; returns the title text.
	UVerticalBox* MakeSectionHeader(UWidgetTree* Tree, const FText& Title, UTextBlock*& OutTitle);

	// Creates the game's magnifying-glass search icon, or null if it can't be loaded.
	UImage* MakeSearchIcon(UWidgetTree* Tree);

	// Styles a text field like the game's: dark inset box, game font, orange caret focus.
	void StyleTextBox(UEditableTextBox* Box, int32 FontSize);

	// Styles a scroll box's bar like the game's thin grey scrollbars.
	void StyleScrollBox(UScrollBox* Box);

	// Styles a slider like the game's overclock slider.
	void StyleSlider(USlider* Slider);

	// Shows or collapses a widget; ignores null.
	void SetShown(UWidget* Widget, bool bShown);
}
