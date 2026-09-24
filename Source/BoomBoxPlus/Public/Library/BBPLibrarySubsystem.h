#pragma once

#include "CoreMinimal.h"
#include "Library/BBPTrack.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BBPLibrarySubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBBPOnLibraryChanged);

// Scan cache written between sessions.
USTRUCT()
struct FBBPLibraryCache
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Version = 0;

	UPROPERTY()
	TArray<FBBPLocalTrack> Tracks;
};

// The music files available on this machine.
UCLASS()
class BOOMBOXPLUS_API UBBPLibrarySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// Rescans the music folders in the background.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|Library")
	void Rescan();

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Library")
	bool IsScanning() const { return bScanning; }

	// Returns tracks whose title, artist or file name contain every word of Query; all tracks if Query is empty.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|Library")
	TArray<FBBPTrack> Search(const FString& Query, int32 MaxResults = 200) const;

	// Returns true if a track with this id is available on this machine.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Library")
	bool HasTrack(const FString& TrackId) const;

	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Library")
	int32 GetTrackCount() const { return Tracks.Num(); }

	// Returns the folder players put their music in.
	UFUNCTION(BlueprintPure, Category = "BoomBoxPlus|Library")
	FString GetMusicFolder() const;

	// Opens the music folder in the system file browser.
	UFUNCTION(BlueprintCallable, Category = "BoomBoxPlus|Library")
	void OpenMusicFolder() const;

	// Returns the local file for a track id, or null if it isn't on this machine.
	const FBBPLocalTrack* FindLocalTrack(const FString& TrackId) const;

	// Broadcast on the game thread after a scan completes.
	UPROPERTY(BlueprintAssignable, Category = "BoomBoxPlus|Library")
	FBBPOnLibraryChanged OnLibraryChanged;

private:
	// Stores scan results and saves the cache.
	void ApplyScanResults(TArray<FBBPLocalTrack>&& Results);

	FString GetCachePath() const;
	TArray<FString> GetScanFolders() const;

	TArray<FBBPLocalTrack> Tracks;
	TMap<FString, int32> TrackIndexById;
	bool bScanning = false;
	bool bRescanQueued = false;
};
