#include "Module/BBPGameInstanceModule.h"
#include "BBPConfig.h"
#include "Network/BBPRemoteCallObject.h"
#include "Patching/WidgetBlueprintHookManager.h"
#include "UI/BBPMusicPage.h"
#include "UI/BBPOpenMusicButton.h"

void UBBPGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase Phase)
{
	// SML registers the configuration during initialization; its editor classes must be in place first.
	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		UBBPConfig::UseSMLEditorClasses();
	}
	Super::DispatchLifecycleEvent(Phase);
}

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

	// Adds a "Custom Music" button beside the Boom Box first page's Change Tape button.
	UWidgetBlueprintHookData* OpenButtonHook = CreateDefaultSubobject<UWidgetBlueprintHookData>(TEXT("OpenMusicButtonHook"));
	OpenButtonHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BoomBox/BPW_BoomBox_Player.BPW_BoomBox_Player_C")));
	OpenButtonHook->NewWidgetClass = UBBPOpenMusicButton::StaticClass();
	OpenButtonHook->NewWidgetName = TEXT("BBPOpenMusicButton");
	OpenButtonHook->ParentWidgetName = TEXT("mChangeTape");
	OpenButtonHook->ParentWidgetType = EWidgetBlueprintHookParentType::Indirect_Child;
	OpenButtonHook->SlotConfiguration = CreateDefaultSubobject<UWidgetBlueprintHookSlot_Generic>(TEXT("OpenMusicButtonSlot"));
	WidgetBlueprintHooks.Add(OpenButtonHook);
}
