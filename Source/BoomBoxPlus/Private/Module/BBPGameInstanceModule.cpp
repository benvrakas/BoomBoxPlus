#include "Module/BBPGameInstanceModule.h"
#include "BBPConfig.h"
#include "Network/BBPRemoteCallObject.h"
#include "Patching/WidgetBlueprintHookManager.h"
#include "UI/BBPMusicPage.h"

UBBPGameInstanceModule::UBBPGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.Add(UBBPRemoteCallObject::StaticClass());
	ModConfigurations.Add(UBBPConfig::StaticClass());

	// Adds the Custom Music page as an extra page of the Boom Box window's page switcher.
	UWidgetBlueprintHookData* MusicPageHook = CreateDefaultSubobject<UWidgetBlueprintHookData>(TEXT("MusicPageHook"));
	MusicPageHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BoomBox/BPW_BoomBox.BPW_BoomBox_C")));
	MusicPageHook->NewWidgetClass = UBBPMusicPage::StaticClass();
	MusicPageHook->NewWidgetName = TEXT("BBPMusicPage");
	MusicPageHook->ParentWidgetName = TEXT("mWidgetSwitcher");
	MusicPageHook->ParentWidgetType = EWidgetBlueprintHookParentType::Direct;
	MusicPageHook->SlotConfiguration = CreateDefaultSubobject<UWidgetBlueprintHookSlot_Generic>(TEXT("MusicPageSlot"));
	WidgetBlueprintHooks.Add(MusicPageHook);
}
