#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPNearbyBoomBoxRow.generated.h"

class AFGBoomBoxPlayer;
class USlider;
class UTextBlock;

// One Boom Box the player can hear, on the music page: which one it is, and this player's own volume for it.
UCLASS()
class BOOMBOXPLUS_API UBBPNearbyBoomBoxRow : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows BoomBox with Label; the slider follows this player's volume for it unless it's being dragged.
	void Setup(AFGBoomBoxPlayer* InBoomBox, const FText& Label, float MyVolume);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LabelText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> VolumeSlider;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> VolumeText;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	UFUNCTION()
	void HandleVolumeChanged(float Value);

	UFUNCTION()
	void HandleCaptureBegin();

	UFUNCTION()
	void HandleCaptureEnd();

	void ShowVolume(float Value);

	TWeakObjectPtr<AFGBoomBoxPlayer> BoomBox;

	// True while the slider is dragged, so refreshes don't move it under the cursor.
	bool bDragging = false;
};
