#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "BBPGameButton.generated.h"

class UTextBlock;

// A button drawn with Satisfactory's own button widget (BPW_TileableButton), falling back to a plain button if it can't be loaded.
UCLASS()
class BOOMBOXPLUS_API UBBPGameButton : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|UI")
	FOnButtonClickedEvent OnClicked;

	// Sets the button's text.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void SetLabel(const FText& InLabel);

	// Returns the clickable widget inside, for keyboard and gamepad focus.
	UWidget* GetFocusTarget() const;

	// Sets a label on an instance of the game's button widget.
	static void SetGameButtonText(UUserWidget* GameButton, const FText& Text);

	// Path of the game's button widget class.
	static const TCHAR* GameButtonClassPath;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	// True to draw with the game's button widget; false for a light flat button (used for list rows).
	virtual bool UsesGameWidget() const { return true; }

private:
	UFUNCTION()
	void HandleClicked();

	// Hides the arrow icon the game's button draws beside its label.
	void HideGameButtonIcon();

	// The game's button widget, or null when using the fallback.
	UPROPERTY()
	TObjectPtr<UUserWidget> GameButton;

	// The clickable button: the game widget's inner button, or the fallback.
	UPROPERTY()
	TObjectPtr<UButton> InnerButton;

	// Label of the fallback button.
	UPROPERTY()
	TObjectPtr<UTextBlock> FallbackLabel;
};

// A flat, lightweight button in the game's colours, for places with many buttons such as list rows.
UCLASS()
class BOOMBOXPLUS_API UBBPCompactButton : public UBBPGameButton
{
	GENERATED_BODY()

protected:
	virtual bool UsesGameWidget() const override { return false; }
};
