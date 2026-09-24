#pragma once

#include "CoreMinimal.h"
#include "BBPBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "BBPTrackRow.generated.h"

class UButton;
class UTextBlock;

// One track in the search results or the queue, with the buttons that act on it.
UCLASS()
class BOOMBOXPLUS_API UBBPTrackRow : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows a library/search result with +Front and +End.
	void SetupAsResult(const FBBPTrack& InTrack);

	// Shows a queue entry with play, move and remove controls.
	void SetupAsQueueEntry(const FBBPQueueEntry& Entry, int32 InIndex, int32 QueueLength, EBBPEntryAvailability Availability);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> AddFrontButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> AddEndButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> MoveUpButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> MoveDownButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RemoveButton;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	UFUNCTION()
	void HandleAddFront();

	UFUNCTION()
	void HandleAddEnd();

	UFUNCTION()
	void HandlePlay();

	UFUNCTION()
	void HandleMoveUp();

	UFUNCTION()
	void HandleMoveDown();

	UFUNCTION()
	void HandleRemove();

	FBBPTrack Track;
	int32 EntryId = INDEX_NONE;
	int32 Index = INDEX_NONE;
};
