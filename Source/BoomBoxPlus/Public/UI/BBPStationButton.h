#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "BBPStationButton.generated.h"

class UBBPGameButton;

// One saved radio station on the music page: a button that plays it now, and an "x" that removes it from the list.
UCLASS()
class BOOMBOXPLUS_API UBBPStationButton : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetStation(const FBBPTrack& InStation);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RemoveButton;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	UFUNCTION()
	void HandlePlay();

	UFUNCTION()
	void HandleRemove();

	FBBPTrack Station;
};
