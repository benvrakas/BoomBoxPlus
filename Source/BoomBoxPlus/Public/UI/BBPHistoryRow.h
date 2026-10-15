#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Playback/BBPSongHistory.h"
#include "BBPHistoryRow.generated.h"

class UBBPGameButton;
class UTextBlock;

// One song in the History view: title, artist and source, when it played, Play (or Search, for a song a radio station
// announced) and Copy (the link, or the song's name where there's no link).
UCLASS()
class BOOMBOXPLUS_API UBBPHistoryRow : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetEntry(const FBBPHistoryEntry& InEntry);

	// "Artist - Title" for searching or copying: the announced title of a radio song, else the track's artist and title.
	static FString GetSongName(const FBBPHistoryEntry& Entry);

protected:
	virtual void NativeOnInitialized() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	// "Artist, Source" or "Radio: <station>".
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SourceText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TimeText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> PlayButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBBPGameButton> CopyButton;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	UFUNCTION()
	void HandlePlay();

	UFUNCTION()
	void HandleCopy();

	// The link Copy copies: a YouTube or SoundCloud page, or a station's stream address. Empty for local files and
	// radio songs, which copy their name.
	FString GetLink() const;

	FBBPHistoryEntry Entry;
};
