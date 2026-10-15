#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BBPHistoryPage.generated.h"

class UBBPHistoryRow;
class UScrollBox;
class UTextBlock;

// The music page's History view: the songs this Boom Box (and its linked group) played, newest first.
UCLASS()
class BOOMBOXPLUS_API UBBPHistoryPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// Rebuilds the rows from this Boom Box's history.
	void Refresh();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// "Played on this Boom Box (N)".
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	// Shown instead of rows while the history is empty.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EmptyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> HistoryList;

	// Rows created so far; extras are collapsed rather than destroyed.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBBPHistoryRow>> Rows;

private:
	// Builds a plain layout when no Blueprint subclass supplies one.
	void BuildDefaultLayout();

	// History revision the rows last showed; they're rebuilt when it changes.
	int32 ShownRevision = -1;
	float CheckTimer = 0.f;
};
