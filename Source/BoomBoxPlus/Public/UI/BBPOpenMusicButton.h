#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPOpenMusicButton.generated.h"

class UButton;

// "Custom Music" button added beside the Boom Box's Change Tape button; opens the music page of the same window.
UCLASS()
class BOOMBOXPLUS_API UBBPOpenMusicButton : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

private:
	UFUNCTION()
	void HandleOpen();

	// Sets the label of the vanilla-styled button.
	void SetTileableText(const FText& Text);

	// Copies the Change Tape button's look and slot layout.
	void MatchChangeTapeButton();

	// The game's own button widget, used so this button looks like its neighbours.
	UPROPERTY()
	TObjectPtr<UUserWidget> TileableButton;

	bool bMatchedChangeTape = false;
};
