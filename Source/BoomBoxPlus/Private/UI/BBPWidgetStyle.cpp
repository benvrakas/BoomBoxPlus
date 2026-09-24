#include "UI/BBPWidgetStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"

UTextBlock* BBPWidgetStyle::MakeText(UWidgetTree* Tree, int32 FontSize, const FLinearColor& Color, const FText& Text)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = FontSize;
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(Text);
	return Block;
}

UButton* BBPWidgetStyle::MakeButton(UWidgetTree* Tree, const FText& Label)
{
	UButton* Button = Tree->ConstructWidget<UButton>();
	Button->SetBackgroundColor(ButtonColor);
	UTextBlock* LabelText = MakeText(Tree, 11, TextColor, Label);
	if (UButtonSlot* Slot = Cast<UButtonSlot>(Button->AddChild(LabelText)))
	{
		Slot->SetPadding(FMargin(8.f, 2.f));
	}
	return Button;
}

void BBPWidgetStyle::SetButtonLabel(UButton* Button, const FText& Label)
{
	if (UTextBlock* LabelText = Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr)
	{
		LabelText->SetText(Label);
	}
}

void BBPWidgetStyle::SetShown(UWidget* Widget, bool bShown)
{
	if (Widget)
	{
		Widget->SetVisibility(bShown ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}
