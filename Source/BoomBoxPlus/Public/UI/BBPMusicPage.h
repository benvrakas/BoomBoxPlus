#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Library/BBPTrack.h"
#include "Containers/Ticker.h"
#include "BBPMusicPage.generated.h"

class ABBPMusicChannel;
class AFGBoomBoxPlayer;
class USlider;
class UBBPGameButton;
class UScrollBox;
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

	// Shows the server's reply to a link or unlink request on open pages for BoomBox.
	static void NotifyLinkResult(AFGBoomBoxPlayer* BoomBox, const FString& Message);

	// Makes this page the visible page of its Boom Box window.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void ShowPage();

	// Shows this page and keeps it shown briefly, in case the window selects its own page afterwards.
	void RequestShow();

	// Switches the Boom Box window back to its tape list.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void ShowTapeList();

	// Switches the Boom Box window back to its first (player) page.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|UI")
	void ShowPlayerPage();

	// Returns the Boom Box this window belongs to, or null if it can't be found.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|UI")
	AFGBoomBoxPlayer* GetBoomBox() const;

	// Returns the channel this page's Boom Box plays, or null if it has none yet.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|UI")
	ABBPMusicChannel* GetChannel() const;

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
	TObjectPtr<UBBPGameButton> PlayPauseButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> NextButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> PreviousButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> ShuffleButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RepeatButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> ClearQueueButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> TapesButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> BackButton;

	// Loads the Custom Music tape into this Boom Box; shown only while it isn't loaded.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> UseBoomBoxButton;

	// Explains that this Boom Box won't play the queue until Custom Music is loaded.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NotLoadedText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> OpenFolderButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RescanButton;

	// Track position; dragging it seeks.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> SeekSlider;

	// This Boom Box's link code and how many Boom Boxes share its queue.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LinkCodeText;

	// Where the player types another Boom Box's link code.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> LinkCodeBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> LinkButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> UnlinkButton;

	// Result of the last link or unlink.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LinkMessageText;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	void RefreshResults();
	void RefreshQueue();
	void RefreshTransport();
	void RefreshLink();

	// Binds to library change events once the library exists.
	void TryBindSources();

	// Rebinds to this Boom Box's channel when it gets one or changes (link/unlink).
	void UpdateChannelBinding();

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

	UFUNCTION()
	void HandleUseBoomBox();

	UFUNCTION()
	void HandleLink();

	UFUNCTION()
	void HandleUnlink();

	UFUNCTION()
	void HandleLinkCodeCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleMembersChanged();

	UFUNCTION()
	void HandleSeekBegin();

	UFUNCTION()
	void HandleSeekEnd();

	// Switches to the first switcher page whose class name contains NameFragment.
	bool ShowSiblingPage(const TCHAR* NameFragment);

	UBBPTrackRow* MakeRow();

	// Adds a row or message to a list with the spacing between rows.
	void AddToList(UScrollBox* List, UWidget* Widget);

	// Re-shows this page while a show request is active. Runs on the core ticker, which ticks even while this page is hidden.
	bool TickShowRequest(float DeltaTime);

	// Stops any active show request.
	void CancelShowRequest();

	FTSTicker::FDelegateHandle ShowTickerHandle;
	float ShowRequestTimeLeft = 0.f;

	FString SearchQuery;

	// Results from YouTube/SoundCloud for the last committed search.
	TArray<FBBPTrack> OnlineResults;
	FString OnlineStatus;
	bool bOnlineIsCollection = false;

	// Increments on every new search so late responses to older searches are ignored.
	int32 SearchGeneration = 0;
	float SearchDebounceTimer = -1.f;
	bool bBoundLibrary = false;

	// Channel whose change events this page is bound to.
	TWeakObjectPtr<ABBPMusicChannel> BoundChannel;

	// True while the player is dragging the seek slider, so playback updates don't move it.
	bool bSeeking = false;

	// Every constructed page, so tape changes can bring the right ones forward.
	static TArray<TWeakObjectPtr<UBBPMusicPage>> LivePages;
};
