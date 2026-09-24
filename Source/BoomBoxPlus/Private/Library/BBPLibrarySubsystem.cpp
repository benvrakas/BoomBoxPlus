#include "Library/BBPLibrarySubsystem.h"
#include "Algo/AllOf.h"
#include "Audio/BBPDecoder.h"
#include "BoomBoxPlus.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
	constexpr int32 LibraryCacheVersion = 1;

	const TCHAR* MusicFolderReadme =
		TEXT("Put music files (.mp3, .ogg, .wav) in this folder, then open the Boom Box and choose Custom Music.\r\n")
		TEXT("Sub-folders are included. Everyone in a session hears a track only if they have the same file.\r\n");

	// Returns true for file extensions the library scans.
	bool IsAudioFile(const FString& Path)
	{
		const FString Ext = FPaths::GetExtension(Path);
		return Ext.Equals(TEXT("mp3"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("ogg"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("wav"), ESearchCase::IgnoreCase);
	}

	// Fills missing title/artist from an "Artist - Title" file name, or uses the file name as the title.
	void ApplyFileNameFallback(const FString& FilePath, FString& Title, FString& Artist)
	{
		const FString Stem = FPaths::GetBaseFilename(FilePath);
		FString NameArtist, NameTitle;
		const bool bSplit = Stem.Split(TEXT(" - "), &NameArtist, &NameTitle);
		if (Title.IsEmpty())
		{
			Title = bSplit ? NameTitle.TrimStartAndEnd() : Stem;
		}
		if (Artist.IsEmpty() && bSplit)
		{
			Artist = NameArtist.TrimStartAndEnd();
		}
	}

	// Hashes, decodes and reads tags for one file. Returns false if it isn't playable audio.
	bool BuildLocalTrack(const FString& FilePath, int64 FileSize, int64 ModifiedTicks, FBBPLocalTrack& Out)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: could not read '%s' (locked or unreadable?)"), *FilePath);
			return false;
		}

		FSHAHash Hash;
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);

		FBBPDecoder Decoder;
		if (!Decoder.Open(MoveTemp(Bytes)))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: skipping '%s': %s"), *FilePath, *Decoder.GetLastError());
			return false;
		}

		Out.FilePath = FilePath;
		Out.FileSize = FileSize;
		Out.ModifiedTicks = ModifiedTicks;
		Out.SampleRate = Decoder.GetSampleRate();
		Out.Channels = Decoder.GetChannels();
		Out.Track.Id = Hash.ToString();
		Out.Track.Source = EBBPTrackSource::Local;
		Out.Track.Duration = Decoder.GetDurationSeconds();
		Decoder.ReadTags(Out.Track.Title, Out.Track.Artist);
		const bool bHadTags = !Out.Track.Title.IsEmpty();
		ApplyFileNameFallback(FilePath, Out.Track.Title, Out.Track.Artist);
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Library: added '%s' - '%s' (%.1f s, %d Hz, %d ch, id %s, title from %s)"),
			*Out.Track.Artist, *Out.Track.Title, Out.Track.Duration, Out.SampleRate, Out.Channels, *Out.Track.Id,
			bHadTags ? TEXT("tags") : TEXT("file name"));
		return true;
	}

	// Scans every audio file under Folders, reusing cached entries whose size and modification time are unchanged.
	TArray<FBBPLocalTrack> ScanFolders(const TArray<FString>& Folders, const TMap<FString, FBBPLocalTrack>& CacheByPath)
	{
		TArray<FBBPLocalTrack> Results;
		TSet<FString> SeenIds;
		int32 NumReused = 0;
		int32 NumDecoded = 0;
		int32 NumSkipped = 0;
		int32 NumDuplicates = 0;
		const double StartTime = FPlatformTime::Seconds();
		for (const FString& Folder : Folders)
		{
			if (!FPaths::DirectoryExists(Folder))
			{
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: folder '%s' does not exist"), *Folder);
				continue;
			}
			IFileManager::Get().IterateDirectoryStatRecursively(*Folder, [&](const TCHAR* Path, const FFileStatData& Stat)
			{
				if (Stat.bIsDirectory || !IsAudioFile(Path))
				{
					return true;
				}
				const int64 Ticks = Stat.ModificationTime.GetTicks();
				FBBPLocalTrack Entry;
				const FBBPLocalTrack* Cached = CacheByPath.Find(Path);
				if (Cached && Cached->FileSize == Stat.FileSize && Cached->ModifiedTicks == Ticks)
				{
					Entry = *Cached;
					++NumReused;
				}
				else if (BuildLocalTrack(Path, Stat.FileSize, Ticks, Entry))
				{
					++NumDecoded;
				}
				else
				{
					++NumSkipped;
					return true;
				}
				// Keeps the first copy of duplicate files.
				if (!SeenIds.Contains(Entry.Track.Id))
				{
					SeenIds.Add(Entry.Track.Id);
					Results.Add(MoveTemp(Entry));
				}
				else
				{
					++NumDuplicates;
					UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Library: '%s' is a duplicate of an earlier file"), Path);
				}
				return true;
			});
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Library scan: %d tracks (%d cached, %d decoded, %d skipped, %d duplicates) in %.2f s"),
			Results.Num(), NumReused, NumDecoded, NumSkipped, NumDuplicates, FPlatformTime::Seconds() - StartTime);
		return Results;
	}
}

void UBBPLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString MusicFolder = GetMusicFolder();
	IFileManager::Get().MakeDirectory(*MusicFolder, true);
	const FString Readme = MusicFolder / TEXT("README.txt");
	if (!FPaths::FileExists(Readme))
	{
		FFileHelper::SaveStringToFile(MusicFolderReadme, *Readme);
	}

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Library: music folder is '%s'"), *MusicFolder);

	FBBPLibraryCache Cache;
	FString CacheJson;
	if (!FFileHelper::LoadFileToString(CacheJson, *GetCachePath()))
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Library: no scan cache yet"));
	}
	else if (!FJsonObjectConverter::JsonObjectStringToUStruct(CacheJson, &Cache))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: scan cache '%s' is unreadable; rescanning everything"), *GetCachePath());
	}
	else if (Cache.Version != LibraryCacheVersion)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Library: scan cache is version %d, expected %d; rescanning everything"), Cache.Version, LibraryCacheVersion);
	}
	else
	{
		ApplyScanResults(MoveTemp(Cache.Tracks), false);
	}

	Rescan();
}

void UBBPLibrarySubsystem::Rescan()
{
	if (bScanning)
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Library: rescan requested during a scan; queued"));
		bRescanQueued = true;
		return;
	}
	bScanning = true;

	TMap<FString, FBBPLocalTrack> CacheByPath;
	for (const FBBPLocalTrack& Local : Tracks)
	{
		CacheByPath.Add(Local.FilePath, Local);
	}

	TWeakObjectPtr<UBBPLibrarySubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Folders = GetScanFolders(), CacheByPath = MoveTemp(CacheByPath)]()
	{
		TArray<FBBPLocalTrack> Results = ScanFolders(Folders, CacheByPath);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Results = MoveTemp(Results)]() mutable
		{
			if (UBBPLibrarySubsystem* This = WeakThis.Get())
			{
				This->bScanning = false;
				This->ApplyScanResults(MoveTemp(Results));
				if (This->bRescanQueued)
				{
					This->bRescanQueued = false;
					This->Rescan();
				}
			}
		});
	});
}

void UBBPLibrarySubsystem::ApplyScanResults(TArray<FBBPLocalTrack>&& Results, bool bSaveCache)
{
	Tracks = MoveTemp(Results);
	Tracks.Sort([](const FBBPLocalTrack& A, const FBBPLocalTrack& B)
	{
		const int32 ByArtist = A.Track.Artist.Compare(B.Track.Artist, ESearchCase::IgnoreCase);
		return ByArtist != 0 ? ByArtist < 0 : A.Track.Title.Compare(B.Track.Title, ESearchCase::IgnoreCase) < 0;
	});

	TrackIndexById.Reset();
	for (int32 i = 0; i < Tracks.Num(); ++i)
	{
		TrackIndexById.Add(Tracks[i].Track.Id, i);
	}

	if (bSaveCache)
	{
		FBBPLibraryCache Cache;
		Cache.Version = LibraryCacheVersion;
		Cache.Tracks = Tracks;
		FString CacheJson;
		if (!FJsonObjectConverter::UStructToJsonObjectString(Cache, CacheJson) || !FFileHelper::SaveStringToFile(CacheJson, *GetCachePath()))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: could not write scan cache '%s'"), *GetCachePath());
		}
	}

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Music library: %d tracks"), Tracks.Num());
	OnLibraryChanged.Broadcast();
}

TArray<FBBPTrack> UBBPLibrarySubsystem::Search(const FString& Query, int32 MaxResults) const
{
	TArray<FString> Words;
	Query.ParseIntoArrayWS(Words);

	TArray<FBBPTrack> Results;
	for (const FBBPLocalTrack& Local : Tracks)
	{
		if (Results.Num() >= MaxResults)
		{
			break;
		}
		const FString Haystack = Local.Track.Title + TEXT(" ") + Local.Track.Artist + TEXT(" ") + FPaths::GetBaseFilename(Local.FilePath);
		const bool bMatches = Algo::AllOf(Words, [&Haystack](const FString& Word)
		{
			return Haystack.Contains(Word, ESearchCase::IgnoreCase);
		});
		if (bMatches)
		{
			Results.Add(Local.Track);
		}
	}
	return Results;
}

bool UBBPLibrarySubsystem::HasTrack(const FString& TrackId) const
{
	return TrackIndexById.Contains(TrackId);
}

const FBBPLocalTrack* UBBPLibrarySubsystem::FindLocalTrack(const FString& TrackId) const
{
	const int32* Index = TrackIndexById.Find(TrackId);
	return Index ? &Tracks[*Index] : nullptr;
}

FString UBBPLibrarySubsystem::GetMusicFolder() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("Music"));
}

void UBBPLibrarySubsystem::OpenMusicFolder() const
{
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Library: opening music folder '%s'"), *GetMusicFolder());
	FPlatformProcess::ExploreFolder(*GetMusicFolder());
}

FString UBBPLibrarySubsystem::GetCachePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("LibraryCache.json");
}

TArray<FString> UBBPLibrarySubsystem::GetScanFolders() const
{
	TArray<FString> Folders = { GetMusicFolder() };
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BoomBoxPlus")))
	{
		const FString BundledMusic = Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Music");
		if (FPaths::DirectoryExists(BundledMusic))
		{
			Folders.Add(BundledMusic);
		}
	}
	else
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Library: BoomBoxPlus plugin not found by the plugin manager; bundled music skipped"));
	}
	return Folders;
}
