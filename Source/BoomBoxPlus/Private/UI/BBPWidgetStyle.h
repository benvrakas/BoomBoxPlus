#pragma once

#include "CoreMinimal.h"

class UButton;
class UTextBlock;
class UWidget;
class UWidgetTree;

// Colours and constructors for the built-in fallback layouts.
namespace BBPWidgetStyle
{
	const FLinearColor TextColor(0.92f, 0.92f, 0.92f);
	const FLinearColor DimTextColor(0.55f, 0.55f, 0.55f);
	const FLinearColor AccentColor(0.98f, 0.58f, 0.29f);
	const FLinearColor PanelColor(0.03f, 0.03f, 0.035f, 0.92f);
	const FLinearColor ButtonColor(0.16f, 0.16f, 0.18f);

	// Creates a text block with the given font size and colour.
	UTextBlock* MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Color, const FText& Text = FText::GetEmpty());

	// Creates a button containing a text label.
	UButton* MakeButton(UWidgetTree* Tree, const FText& Label);

	// Replaces the text label of a button made by MakeButton.
	void SetButtonLabel(UButton* Button, const FText& Label);

	// Shows or collapses a widget; ignores null.
	void SetShown(UWidget* Widget, bool bShown);
}
