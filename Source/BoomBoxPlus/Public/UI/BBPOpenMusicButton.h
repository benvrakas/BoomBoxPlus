#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPOpenMusicButton.generated.h"

class UButton;

// "Custom Music" button added to the Boom Box's first page; opens the music page of the same window.
UCLASS()
class BOOMBOXPLUS_API UBBPOpenMusicButton : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

private:
	UFUNCTION()
	void HandleOpen();
};
