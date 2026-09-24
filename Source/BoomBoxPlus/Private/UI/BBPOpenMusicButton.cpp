#include "UI/BBPOpenMusicButton.h"
#include "BoomBoxPlus.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPWidgetStyle.h"

namespace
{
	const FName SwitcherPropertyName(TEXT("mWidgetSwitcher"));
}

void UBBPOpenMusicButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		OpenButton = BBPWidgetStyle::MakeButton(WidgetTree, NSLOCTEXT("BoomBoxPlus", "OpenCustomMusic", "Custom Music"));
		WidgetTree->RootWidget = OpenButton;
	}
	if (OpenButton)
	{
		OpenButton->OnClicked.AddDynamic(this, &UBBPOpenMusicButton::HandleOpen);
	}
}

void UBBPOpenMusicButton::HandleOpen()
{
	// Walks out to the Boom Box window and finds the music page among its switcher's pages.
	for (UObject* Outer = GetOuter(); Outer; Outer = Outer->GetOuter())
	{
		const FObjectProperty* Property = FindFProperty<FObjectProperty>(Outer->GetClass(), SwitcherPropertyName);
		UWidgetSwitcher* Switcher = Property ? Cast<UWidgetSwitcher>(Property->GetObjectPropertyValue_InContainer(Outer)) : nullptr;
		if (!Switcher)
		{
			continue;
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
		return;
	}
	UE_LOG(LogBoomBoxPlus, Warning, TEXT("UI: Custom Music button could not find its Boom Box window"));
}
