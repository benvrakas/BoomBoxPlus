#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPRadioPage.generated.h"

class UBBPGameButton;
class UBBPStationRow;
class UEditableTextBox;
class UScrollBox;
class UTextBlock;

// The music page's Radio view: add a station from its stream address, and play, rename or remove saved stations.
UCLASS()
class BOOMBOXPLUS_API UBBPRadioPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// Rebuilds the station rows from the saved list and marks the one playing on this Boom Box.
	void Refresh();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Where the player pastes a stream address.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> AddressBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> AddButton;

	// Adds every station in a list file the player picks.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> ImportButton;

	// Progress or result of the last Add.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AddStatusText;

	// "Your stations (N of Max)".
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StationsTitle;

	// Shown instead of rows while no station is saved.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> StationList;

	// Rows created so far; extras are collapsed rather than destroyed.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBBPStationRow>> Rows;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	UFUNCTION()
	void HandleAdd();

	UFUNCTION()
	void HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleImport();

	// Reads a station list file and adds its stations.
	void ImportFile(const FString& Path);

	void SetAddStatus(const FText& Status, bool bError);

	// Id of the station playing on this page's Boom Box, or empty.
	FString GetPlayingStationId() const;

	// Increments on every Add so a late answer to an earlier one is ignored.
	int32 AddGeneration = 0;

	// Playing station the rows last showed; the rows are refreshed when it changes.
	FString ShownPlayingId;
	float PlayingCheckTimer = 0.f;
};
