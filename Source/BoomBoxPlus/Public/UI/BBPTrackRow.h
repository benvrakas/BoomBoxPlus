#pragma once

#include "CoreMinimal.h"
#include "BBPBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "BBPTrackRow.generated.h"

class AFGBoomBoxPlayer;
class UBBPGameButton;
class UBorder;
class UTextBlock;

// One track in the search results or the queue, with the buttons that act on it.
UCLASS()
class BOOMBOXPLUS_API UBBPTrackRow : public UUserWidget
{
	GENERATED_BODY()

public:
	// Sets the Boom Box whose queue this row's buttons act on.
	void SetBoomBox(AFGBoomBoxPlayer* InBoomBox);

	// Shows a library/search result with +Front and +End.
	void SetupAsResult(const FBBPTrack& InTrack);

	// Shows a queue entry with play, move and remove controls.
	void SetupAsQueueEntry(const FBBPQueueEntry& Entry, int32 InIndex, int32 QueueLength, EBBPEntryAvailability Availability);

protected:
	virtual void NativeOnInitialized() override;

	// Row background; tinted while the row's track is playing.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RowBackground;

	// Orange marker at the row's left edge, shown while the row's track is playing.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PlayingMarker;

	// "YOUTUBE" / "SOUNDCLOUD" tag for online tracks.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SourceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DetailText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> AddFrontButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> AddEndButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> MoveUpButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> MoveDownButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RemoveButton;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	TWeakObjectPtr<AFGBoomBoxPlayer> BoomBox;

	// Shows the online source of Track, or hides the tag for local files.
	void ShowSourceTag();

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
