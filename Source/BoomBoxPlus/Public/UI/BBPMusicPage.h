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
struct FBBPMatchProgress;

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

	// Loads the Custom Music tape into this page's Boom Box if another tape (or none) is in.
	void EnsureCustomMusicLoaded();

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

	// This player's own Custom Music volume (the mod's MusicVolume setting), editable right here instead of only in Mods menu.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USlider> MyVolumeSlider;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MyVolumeText;

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

	// Where the player pastes an internet radio stream address.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> RadioUrlBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RadioPlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> RadioAddButton;

	// Tuning progress or the reason a stream couldn't be played.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RadioStatusText;

	// One button per recent station (newest first); pressing one plays it now.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBBPGameButton>> RadioStationButtons;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	// Mark the results or queue list for rebuilding; the rebuild happens on a later tick, at most every RebuildInterval.
	void RefreshResults();
	void RefreshQueue();

	// Rebuild the results or queue list now.
	void RebuildResults();
	void RebuildQueue();

	void RefreshTransport();
	void RefreshLink();

	// Shows the first Count rows of Pool in List, creating rows only when the pool is too small, and hides the rest.
	void SyncRows(UScrollBox* List, TArray<TObjectPtr<UBBPTrackRow>>& Pool, int32 Count, TFunctionRef<void(UBBPTrackRow& Row, int32 Index)> Setup);

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

	UFUNCTION()
	void HandleConnectSpotify();

	// Shows or hides the Connect Spotify button for the current sign-in state.
	void RefreshSpotifyButton();

	// Last text sent to the online search, rerun after connecting Spotify.
	FString LastOnlineQuery;

	// Starts an online search, or resolves a pasted link, for the search box text.
	void RunOnlineSearch(const FString& Text);

	// Shows a Spotify playlist's matches as they arrive.
	void HandleMatchProgress(int32 Generation, const FBBPMatchProgress& Progress);

	// Stops this page's running Spotify match, unless it is already queueing into the Boom Box.
	void CancelMatchJob();

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

	// Writes the dragged value to the mod's MusicVolume setting immediately, and updates the % label.
	UFUNCTION()
	void HandleMyVolumeChanged(float Value);

	UFUNCTION()
	void HandleMyVolumeCaptureBegin();

	UFUNCTION()
	void HandleMyVolumeCaptureEnd();

	// Refreshes MyVolumeText and, unless the slider is being dragged, MyVolumeSlider's value from the live setting.
	void RefreshMyVolume();

	// True while the player is dragging the personal volume slider.
	bool bChangingMyVolume = false;

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

	// Status line above the results (library empty, no matches, online search progress).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ResultsMessageText;

	// Signs in to Spotify so whole playlists can be read; shown while the app key is set but not signed in.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> SpotifyButton;

	// "Add all" for playlist results.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> AddAllButton;

	// Note under the queue (empty queue, or how much of a long queue is shown).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> QueueMessageText;

	// Row widgets reused between rebuilds.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBBPTrackRow>> ResultRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBBPTrackRow>> QueueRows;

	bool bResultsDirty = false;
	bool bQueueDirty = false;
	float RebuildCooldown = 0.f;

	// Extra line from the online search (e.g. a Spotify playlist was only partly read).
	FString OnlineNote;

	// Running Spotify match job (0 when none), its playlist size, and whether Add all has handed it the queueing.
	int32 MatchJobId = 0;
	int32 MatchTotal = 0;
	bool bMatchQueued = false;

	// Increments on every new search so late responses to older searches are ignored.
	int32 SearchGeneration = 0;
	float SearchDebounceTimer = -1.f;
	bool bBoundLibrary = false;

	// Channel whose change events this page is bound to.
	TWeakObjectPtr<ABBPMusicChannel> BoundChannel;

	// True while the player is dragging the seek slider, so playback updates don't move it.
	bool bSeeking = false;

	// Holds the page at a fixed share of the screen so it doesn't resize as lists fill.
	UPROPERTY(Transient)
	TObjectPtr<class USizeBox> PageSizeBox;

	// Resizes PageSizeBox to the current screen size; cheap when nothing changed.
	void UpdatePageSize();

	// Checks the radio field's stream and then plays it now (bPlayNow) or adds it to the end of the queue.
	void TuneRadio(bool bPlayNow);

	// Plays recent station Index now.
	void PlayRecentStation(int32 Index);

	// Relabels the recent station buttons from the saved list.
	void RefreshRadioStations();

	// Sets the line under the radio field.
	void SetRadioStatus(const FString& Status, bool bError);

	UFUNCTION()
	void HandleRadioPlayNow();

	UFUNCTION()
	void HandleRadioAdd();

	UFUNCTION()
	void HandleRadioUrlCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	UFUNCTION()
	void HandleRecentStation0();

	UFUNCTION()
	void HandleRecentStation1();

	UFUNCTION()
	void HandleRecentStation2();

	UFUNCTION()
	void HandleRecentStation3();

	// Increments on every tune-in so a slow reply to an older one is ignored.
	int32 RadioGeneration = 0;

	// Shows a link message under the link field for a few seconds.
	void SetLinkMessage(const FText& Message);

	// Last link message and when it was set (platform seconds).
	FText LinkMessage;
	double LinkMessageTime = -1.0;

	// Every constructed page, so tape changes can bring the right ones forward.
	static TArray<TWeakObjectPtr<UBBPMusicPage>> LivePages;
};
