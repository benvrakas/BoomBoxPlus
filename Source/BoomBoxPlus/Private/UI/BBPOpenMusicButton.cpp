#include "UI/BBPOpenMusicButton.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/TextBlock.h"
#include "UI/BBPGameButton.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

#define LOCTEXT_NAMESPACE "BoomBoxPlus"

namespace
{
	const FName SwitcherPropertyName(TEXT("mWidgetSwitcher"));
	const FName ChangeTapePropertyName(TEXT("mChangeTape"));
	const FName InnerButtonName(TEXT("mButton"));

	// Look properties copied from the vanilla Change Tape button so both buttons match.
	const TCHAR* CopiedStyleProperties[] = { TEXT("mButtonStyle"), TEXT("mOverrideWidth"), TEXT("mOverrideHeight"), TEXT("mIconSize"), TEXT("mWrapTextAt") };

	// Returns the value of an object property named Name on the first outer of Widget that has one.
	UObject* FindOuterProperty(const UObject* Widget, FName Name)
	{
		for (UObject* Outer = Widget->GetOuter(); Outer; Outer = Outer->GetOuter())
		{
			if (const FObjectProperty* Property = FindFProperty<FObjectProperty>(Outer->GetClass(), Name))
			{
				return Property->GetObjectPropertyValue_InContainer(Outer);
			}
		}
		return nullptr;
	}
}

void UBBPOpenMusicButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		if (OpenButton)
		{
			OpenButton->OnClicked.AddDynamic(this, &UBBPOpenMusicButton::HandleOpen);
		}
		return;
	}

	UClass* TileableClass = LoadClass<UUserWidget>(nullptr, UBBPGameButton::GameButtonClassPath);
	UUserWidget* Tileable = TileableClass ? WidgetTree->ConstructWidget<UUserWidget>(TileableClass) : nullptr;
	UButton* Inner = Tileable ? Cast<UButton>(Tileable->GetWidgetFromName(InnerButtonName)) : nullptr;
	if (!Inner)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: vanilla tileable button unavailable (class %s); using a plain Custom Music button"), *GetNameSafe(TileableClass));
		OpenButton = WidgetTree->ConstructWidget<UButton>();
		OpenButton->SetBackgroundColor(BBPWidgetStyle::ButtonColor);
		OpenButton->AddChild(BBPWidgetStyle::MakeText(WidgetTree, 12, BBPWidgetStyle::TextColor, LOCTEXT("OpenCustomMusic", "Custom Music"), BBPWidgetStyle::EFontWeight::SemiBold));
		WidgetTree->RootWidget = OpenButton;
		OpenButton->OnClicked.AddDynamic(this, &UBBPOpenMusicButton::HandleOpen);
		return;
	}

	TileableButton = Tileable;
	WidgetTree->RootWidget = Tileable;
	SetTileableText(LOCTEXT("OpenCustomMusic", "Custom Music"));
	OpenButton = Inner;
	OpenButton->OnClicked.AddDynamic(this, &UBBPOpenMusicButton::HandleOpen);
}

void UBBPOpenMusicButton::NativeConstruct()
{
	Super::NativeConstruct();
	MatchChangeTapeButton();
}

void UBBPOpenMusicButton::SetTileableText(const FText& Text)
{
	UBBPGameButton::SetGameButtonText(TileableButton, Text);
}

void UBBPOpenMusicButton::MatchChangeTapeButton()
{
	if (bMatchedChangeTape)
	{
		return;
	}
	UUserWidget* ChangeTape = Cast<UUserWidget>(FindOuterProperty(this, ChangeTapePropertyName));
	if (!ChangeTape)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: Change Tape button not found; Custom Music button keeps its default look"));
		return;
	}
	bMatchedChangeTape = true;

	if (TileableButton && TileableButton->GetClass() == ChangeTape->GetClass())
	{
		for (const TCHAR* Name : CopiedStyleProperties)
		{
			if (const FProperty* Property = FindFProperty<FProperty>(ChangeTape->GetClass(), Name))
			{
				Property->CopyCompleteValue_InContainer(TileableButton, ChangeTape);
			}
		}
		TileableButton->SynchronizeProperties();
		SetTileableText(LOCTEXT("OpenCustomMusic", "Custom Music"));
	}

	// Sit beside Change Tape with the same spacing and sizing.
	if (UHorizontalBoxSlot* Mine = Cast<UHorizontalBoxSlot>(Slot))
	{
		if (const UHorizontalBoxSlot* Theirs = Cast<UHorizontalBoxSlot>(ChangeTape->Slot))
		{
			Mine->SetPadding(Theirs->GetPadding());
			Mine->SetSize(Theirs->GetSize());
			Mine->SetHorizontalAlignment(Theirs->GetHorizontalAlignment());
			Mine->SetVerticalAlignment(Theirs->GetVerticalAlignment());
		}
	}
	else if (UVerticalBoxSlot* MineV = Cast<UVerticalBoxSlot>(Slot))
	{
		if (const UVerticalBoxSlot* TheirsV = Cast<UVerticalBoxSlot>(ChangeTape->Slot))
		{
			MineV->SetPadding(TheirsV->GetPadding());
			MineV->SetSize(TheirsV->GetSize());
			MineV->SetHorizontalAlignment(TheirsV->GetHorizontalAlignment());
			MineV->SetVerticalAlignment(TheirsV->GetVerticalAlignment());
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Custom Music button matched to Change Tape (slot %s, parent %s)"),
		*GetNameSafe(Slot), *GetNameSafe(GetParent()));
}

void UBBPOpenMusicButton::HandleOpen()
{
	UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(FindOuterProperty(this, SwitcherPropertyName));
	if (!Switcher)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: Custom Music button could not find its Boom Box window"));
		return;
	}
	for (int32 i = 0; i < Switcher->GetNumWidgets(); ++i)
	{
		if (UBBPMusicPage* Page = Cast<UBBPMusicPage>(Switcher->GetWidgetAtIndex(i)))
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("UI: Custom Music button pressed"));
			Page->RequestShow();
			return;
		}
	}
	UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: Custom Music button found the Boom Box window but no music page in it"));
}

#undef LOCTEXT_NAMESPACE
