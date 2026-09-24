#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "BBPMusicPage.generated.h"

class AFGBoomBoxPlayer;
class UButton;
class UEditableTextBox;
class UFGTapeData;
class UScrollBox;
class UTextBlock;
class UBBPTrackRow;

// The Custom Music page added to the Boom Box window: search, results, queue and transport controls.
UCLASS()
class BOOMBOXPLUS_API UBBPMusicPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shows this page on every open Boom Box window for BoomBox when Custom Music has just been loaded into it.
	static void NotifyTapeChanged(AFGBoomBoxPlayer* BoomBox, TSubclassOf<UFGTapeData> NewTape);

	// Makes this page the visible page of its Boom Box window.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void ShowPage();

	// Switches the Boom Box window back to its tape list.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void ShowTapeList();

	// Returns the Boom Box this window belongs to, or null if it can't be found.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|UI")
	AFGBoomBoxPlayer* GetBoomBox() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Row widget class for results and queue entries.
	UPROPERTY(EditDefaultsOnly, Category = "BoomBoxPlus|UI")
	TSubclassOf<UBBPTrackRow> TrackRowClass;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> SearchBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ResultsList;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> QueueList;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NowPlayingText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PositionText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LyricText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QueueHeaderText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LibraryStatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PlayPauseButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> NextButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PreviousButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ShuffleButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RepeatButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClearQueueButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> TapesButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenFolderButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RescanButton;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	void RefreshResults();
	void RefreshQueue();
	void RefreshTransport();

	// Binds to library and playlist change events once they exist.
	void TryBindSources();

	UFUNCTION()
	void HandleSearchChanged(const FText& Text);

	UFUNCTION()
	void HandleSearchCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleAddAllOnline();

	// Starts an online search, or resolves a pasted link, for the search box text.
	void RunOnlineSearch(const FString& Text);

	// Stores online results for the current search generation and refreshes the list.
	void ShowOnlineResults(int32 Generation, const TArray<FBBPTrack>& Tracks, const FString& Error, bool bIsCollection);

	UFUNCTION()
	void HandleLibraryChanged();

	UFUNCTION()
	void HandleQueueChanged();

	UFUNCTION()
	void HandlePlaybackChanged();

	UFUNCTION()
	void HandlePlayPause();

	UFUNCTION()
	void HandleNext();

	UFUNCTION()
	void HandlePrevious();

	UFUNCTION()
	void HandleShuffle();

	UFUNCTION()
	void HandleRepeat();

	UFUNCTION()
	void HandleClearQueue();

	UFUNCTION()
	void HandleOpenFolder();

	UFUNCTION()
	void HandleRescan();

	UBBPTrackRow* MakeRow();

	FString SearchQuery;

	// Results from YouTube/SoundCloud for the last committed search.
	TArray<FBBPTrack> OnlineResults;
	FString OnlineStatus;
	bool bOnlineIsCollection = false;

	// Increments on every new search so late responses to older searches are ignored.
	int32 SearchGeneration = 0;
	float SearchDebounceTimer = -1.f;
	bool bShowRequested = false;
	bool bBoundLibrary = false;
	bool bBoundPlaylist = false;

	// Every constructed page, so tape changes can bring the right ones forward.
	static TArray<TWeakObjectPtr<UBBPMusicPage>> LivePages;
};
