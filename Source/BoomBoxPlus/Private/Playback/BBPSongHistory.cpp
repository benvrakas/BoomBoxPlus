#include "Playback/BBPSongHistory.h"
#include "BoomBoxPlus.h"
#include "FGBoomBoxPlayer.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Playlist/BBPPlaylistTypes.h"

namespace
{
	constexpr float CheckInterval = 0.5f;

	// Longest wait before a change is written to disk.
	constexpr float SaveDelay = 2.f;
}

void UBBPSongHistory::Initialize(UBBPPlaybackController* InController)
{
	Controller = InController;
	Load();
}

void UBBPSongHistory::Shutdown()
{
	if (bDirty)
	{
		Save();
	}
}

const TArray<FBBPHistoryEntry>& UBBPSongHistory::GetHistory(const AFGBoomBoxPlayer* BoomBox) const
{
	static const TArray<FBBPHistoryEntry> None;
	const FBBPHistoryList* List = BoomBox ? History.BoomBoxes.Find(GetKey(BoomBox)) : nullptr;
	return List ? List->Entries : None;
}

FString UBBPSongHistory::GetKey(const AFGBoomBoxPlayer* BoomBox) const
{
	// The server's name for the Boom Box, which every machine shares and which survives reloads.
	const ABBPPlaylistSubsystem* Playlist = Controller ? Controller->GetPlaylist() : nullptr;
	if (Playlist)
	{
		const FBBPActiveBoomBox* Active = Playlist->GetActiveBoomBoxes().FindByPredicate([BoomBox](const FBBPActiveBoomBox& A) { return A.BoomBox == BoomBox; });
		if (Active && !Active->Key.IsEmpty())
		{
			return Active->Key;
		}
	}
	return ABBPPlaylistSubsystem::GetBoomBoxKey(BoomBox);
}

void UBBPSongHistory::Tick(float DeltaSeconds)
{
	SaveTimer -= DeltaSeconds;
	if (bDirty && SaveTimer <= 0.f)
	{
		Save();
	}
	CheckTimer -= DeltaSeconds;
	const ABBPPlaylistSubsystem* Playlist = Controller ? Controller->GetPlaylist() : nullptr;
	if (CheckTimer > 0.f || !Playlist)
	{
		return;
	}
	CheckTimer = CheckInterval;

	for (const FBBPActiveBoomBox& Active : Playlist->GetActiveBoomBoxes())
	{
		const ABBPMusicChannel* Channel = IsValid(Active.BoomBox) ? Playlist->FindChannel(Active.BoomBox) : nullptr;
		FBBPQueueEntry Entry;
		if (!Channel || !Channel->IsPlaying() || !Channel->GetCurrentEntry(Entry))
		{
			continue;
		}
		const FString Key = Active.Key.IsEmpty() ? ABBPPlaylistSubsystem::GetBoomBoxKey(Active.BoomBox) : Active.Key;
		FSeen& Last = Seen.FindOrAdd(Key);
		if (Entry.EntryId != Last.EntryId)
		{
			Last.EntryId = Entry.EntryId;
			Last.RadioSong.Reset();
			FBBPHistoryEntry Added;
			Added.Track = Entry.Track;
			Added.PlayedAt = FDateTime::UtcNow().ToUnixTimestamp();
			Add(Key, Added);
		}
		if (!Entry.Track.IsLive())
		{
			continue;
		}
		FString Song;
		bool bBuffering = false;
		// Some stations announce only their own name; that isn't a song.
		if (Controller->GetLiveStatus(Channel, Song, bBuffering) && !Song.IsEmpty() && Song != Last.RadioSong
			&& !Song.Equals(Entry.Track.Title, ESearchCase::IgnoreCase))
		{
			Last.RadioSong = Song;
			FBBPHistoryEntry Added;
			Added.Track = Entry.Track;
			Added.RadioSong = Song;
			Added.PlayedAt = FDateTime::UtcNow().ToUnixTimestamp();
			Add(Key, Added);
		}
	}
}

void UBBPSongHistory::Add(const FString& Key, const FBBPHistoryEntry& Entry)
{
	TArray<FBBPHistoryEntry>& Entries = History.BoomBoxes.FindOrAdd(Key).Entries;
	Entries.Insert(Entry, 0);
	if (Entries.Num() > MaxEntries)
	{
		Entries.SetNum(MaxEntries);
	}
	++Revision;
	if (!bDirty)
	{
		bDirty = true;
		SaveTimer = SaveDelay;
	}
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("History: %s played '%s'"), *Key, Entry.RadioSong.IsEmpty() ? *Entry.Track.Title : *Entry.RadioSong);
}

FString UBBPSongHistory::GetPath()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("History.json"));
}

void UBBPSongHistory::Load()
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *GetPath()))
	{
		return;
	}
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &History))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("History: could not read '%s'; starting empty"), *GetPath());
		History = FBBPHistoryFile();
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("History: %d Boom Box history(s) loaded"), History.BoomBoxes.Num());
}

void UBBPSongHistory::Save()
{
	bDirty = false;
	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(History, Json) || !FFileHelper::SaveStringToFile(Json, *GetPath()))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("History: could not write '%s'"), *GetPath());
	}
}
