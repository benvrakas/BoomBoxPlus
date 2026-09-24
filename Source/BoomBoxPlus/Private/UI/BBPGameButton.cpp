#include "UI/BBPGameButton.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "UI/BBPWidgetStyle.h"

const TCHAR* UBBPGameButton::GameButtonClassPath = TEXT("/Game/FactoryGame/Interface/UI/InGame/-Shared/BPW_TileableButton.BPW_TileableButton_C");

namespace
{
	const FName InnerButtonName(TEXT("mButton"));
	const FName IconWidgetName(TEXT("mIconObject"));
	const FName IconPropertyName(TEXT("mIcon"));
}

void UBBPGameButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}

	static bool bLoggedFallback = false;
	UClass* GameButtonClass = LoadClass<UUserWidget>(nullptr, GameButtonClassPath);
	UUserWidget* Game = GameButtonClass ? WidgetTree->ConstructWidget<UUserWidget>(GameButtonClass) : nullptr;
	UButton* Inner = Game ? Cast<UButton>(Game->GetWidgetFromName(InnerButtonName)) : nullptr;
	if (Inner)
	{
		GameButton = Game;
		InnerButton = Inner;
		WidgetTree->RootWidget = Game;
		if (FObjectProperty* Icon = FindFProperty<FObjectProperty>(Game->GetClass(), IconPropertyName))
		{
			Icon->SetObjectPropertyValue_InContainer(Game, nullptr);
		}
	}
	else
	{
		if (!bLoggedFallback)
		{
			bLoggedFallback = true;
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: game button widget unavailable (class %s); using plain buttons"), *GetNameSafe(GameButtonClass));
		}
		InnerButton = WidgetTree->ConstructWidget<UButton>();
		InnerButton->SetBackgroundColor(BBPWidgetStyle::ButtonColor);
		FallbackLabel = BBPWidgetStyle::MakeText(WidgetTree, 11, BBPWidgetStyle::TextColor, FText::GetEmpty(), BBPWidgetStyle::EFontWeight::SemiBold);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(InnerButton->AddChild(FallbackLabel)))
		{
			LabelSlot->SetPadding(FMargin(10.f, 3.f));
		}
		WidgetTree->RootWidget = InnerButton;
	}
	InnerButton->OnClicked.AddDynamic(this, &UBBPGameButton::HandleClicked);
}

void UBBPGameButton::NativeConstruct()
{
	Super::NativeConstruct();
	HideGameButtonIcon();
}

void UBBPGameButton::HideGameButtonIcon()
{
	UWidget* Icon = GameButton ? GameButton->GetWidgetFromName(IconWidgetName) : nullptr;
	if (Icon)
	{
		Icon->SetVisibility(ESlateVisibility::Collapsed);
	}
	else if (GameButton)
	{
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: game button has no '%s' widget; its icon can't be hidden"), *IconWidgetName.ToString());
		}
	}
}

void UBBPGameButton::SetLabel(const FText& InLabel)
{
	if (GameButton)
	{
		SetGameButtonText(GameButton, InLabel);
	}
	else if (FallbackLabel)
	{
		FallbackLabel->SetText(InLabel);
	}
}

UWidget* UBBPGameButton::GetFocusTarget() const
{
	return InnerButton;
}

void UBBPGameButton::HandleClicked()
{
	OnClicked.Broadcast();
}

void UBBPGameButton::SetGameButtonText(UUserWidget* Game, const FText& Text)
{
	if (!Game)
	{
		return;
	}
	if (FTextProperty* TextProperty = FindFProperty<FTextProperty>(Game->GetClass(), TEXT("mText")))
	{
		TextProperty->SetPropertyValue_InContainer(Game, Text);
	}
	UFunction* SetText = Game->FindFunction(TEXT("SetText"));
	if (!SetText)
	{
		return;
	}
	// SetText takes a single FText parameter.
	if (SetText->NumParms == 1 && CastField<FTextProperty>(SetText->PropertyLink))
	{
		FText Param = Text;
		Game->ProcessEvent(SetText, &Param);
	}
	else
	{
		static bool bLogged = false;
		if (!bLogged)
		{
			bLogged = true;
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: game button SetText has an unexpected signature; label set through mText only"));
		}
	}
}
