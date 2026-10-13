#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "BBPStationRow.generated.h"

class UBBPGameButton;
class UBorder;
class UEditableTextBox;
class UTextBlock;

// One saved radio station on the Radio page: its name (click to rename), its address, Play and Remove.
UCLASS()
class BOOMBOXPLUS_API UBBPStationRow : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows Station; bPlaying highlights the row and disables Play.
	void SetStation(const FBBPTrack& InStation, bool bPlaying);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> RowBackground;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> NameBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AddressText;

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

	// Renames the station when the name is committed with Enter or by clicking away; Esc puts the old name back.
	UFUNCTION()
	void HandleNameCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	FBBPTrack Station;
};
