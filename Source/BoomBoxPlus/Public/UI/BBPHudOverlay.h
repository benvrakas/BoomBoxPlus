#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPHudOverlay.generated.h"

class UTextBlock;

// On-screen lyric line and "now playing" notification, shown while the local player can hear a Custom Music Boom Box.
UCLASS()
class BOOMBOXPLUS_API UBBPHudOverlay : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoomBoxPlus|UI")
	bool bShowLyrics = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoomBoxPlus|UI")
	bool bShowNowPlaying = true;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LyricText;

	// Container shown and faded for the now-playing notification.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> NowPlayingBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NowPlayingTitleText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NowPlayingArtistText;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	// Returns true if the local player is within hearing range of a Boom Box playing Custom Music.
	bool IsInEarshot() const;

	int32 LastEntryId = INDEX_NONE;
	float NowPlayingTimeLeft = 0.f;
};
