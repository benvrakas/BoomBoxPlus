#include "Net/BBPNetSubsystem.h"
#include "Async/Async.h"
#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "JsonObjectConverter.h"
#include "Library/BBPLibrarySubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/BBPProcess.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "GenericPlatform/GenericPlatformHttp.h"

namespace
{
	constexpr int32 NetCacheVersion = 1;
	constexpr int32 YouTubeSearchResults = 8;
	constexpr int32 SoundCloudSearchResults = 4;
	constexpr int32 MaxCollectionTracks = 100;
	constexpr int32 SpotifyMatchCandidates = 3;
	constexpr float HttpTimeoutSeconds = 15.f;
	const TCHAR* BrowserUserAgent = TEXT("Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

	// Arguments every yt-dlp call starts with.
	TArray<FString> BaseYtDlpArgs()
	{
		return { TEXT("--ignore-config"), TEXT("--no-warnings"), TEXT("--encoding"), TEXT("utf-8"), TEXT("--socket-timeout"), TEXT("15") };
	}

	// Returns the host of an http(s) URL in lower case, or empty if Text isn't one.
	FString GetHost(const FString& Text)
	{
		FString Rest;
		if (Text.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
		{
			Rest = Text.Mid(8);
		}
		else if (Text.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase))
		{
			Rest = Text.Mid(7);
		}
		else
		{
			return FString();
		}
		int32 End = INDEX_NONE;
		if (Rest.FindChar(TEXT('/'), End))
		{
			Rest.LeftInline(End);
		}
		Rest.ToLowerInline();
		return Rest.StartsWith(TEXT("www.")) ? Rest.Mid(4) : Rest;
	}

	// Removes decorations like "(Official Video)" or "[Lyrics]" from a YouTube title.
	FString CleanVideoTitle(const FString& Title)
	{
		static const FRegexPattern Decoration(TEXT("\\s*[\\(\\[][^\\)\\]]*(official|video|audio|lyric|visuali[sz]er|hd|4k|remaster)[^\\)\\]]*[\\)\\]]"), ERegexPatternFlags::CaseInsensitive);
		FString Result = Title;
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			FRegexMatcher Matcher(Decoration, Result);
			if (!Matcher.FindNext())
			{
				break;
			}
			Result = Result.Left(Matcher.GetMatchBeginning()) + Result.Mid(Matcher.GetMatchEnding());
		}
		return Result.TrimStartAndEnd();
	}

	// Converts one line of yt-dlp --dump-json output into a track.
	bool ParseTrackJson(const FString& Line, FBBPTrack& OutTrack)
	{
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Line), Json) || !Json.IsValid())
		{
			return false;
		}
		FString Id, Title, Artist, Url, Extractor;
		Json->TryGetStringField(TEXT("id"), Id);
		Json->TryGetStringField(TEXT("title"), Title);
		Json->TryGetStringField(TEXT("url"), Url);
		if (!Json->TryGetStringField(TEXT("ie_key"), Extractor))
		{
			Json->TryGetStringField(TEXT("extractor_key"), Extractor);
		}
		if (!Json->TryGetStringField(TEXT("channel"), Artist) || Artist.IsEmpty())
		{
			Json->TryGetStringField(TEXT("uploader"), Artist);
		}
		double Duration = 0.0;
		Json->TryGetNumberField(TEXT("duration"), Duration);
		if (Url.IsEmpty())
		{
			Json->TryGetStringField(TEXT("webpage_url"), Url);
		}
		if (Id.IsEmpty() || Url.IsEmpty())
		{
			return false;
		}

		const bool bSoundCloud = Extractor.Contains(TEXT("Soundcloud"), ESearchCase::IgnoreCase);
		OutTrack.Source = bSoundCloud ? EBBPTrackSource::SoundCloud : EBBPTrackSource::YouTube;
		OutTrack.Id = (bSoundCloud ? TEXT("sc:") : TEXT("yt:")) + Id;
		OutTrack.SourceRef = Url;
		OutTrack.Duration = (float)Duration;
		OutTrack.Title = Title.IsEmpty() ? Id : Title;
		OutTrack.Artist = Artist;

		// "Artist - Title" video titles give a better artist than the channel name.
		FString TitleArtist, TitleName;
		if (!bSoundCloud && OutTrack.Title.Split(TEXT(" - "), &TitleArtist, &TitleName))
		{
			OutTrack.Artist = TitleArtist.TrimStartAndEnd();
			OutTrack.Title = TitleName;
		}
		OutTrack.Title = CleanVideoTitle(OutTrack.Title);
		return true;
	}

	// Parses every JSON line of yt-dlp output into tracks.
	TArray<FBBPTrack> ParseTrackLines(const FString& StdOut)
	{
		TArray<FString> Lines;
		StdOut.ParseIntoArrayLines(Lines);
		TArray<FBBPTrack> Tracks;
		for (const FString& Line : Lines)
		{
			FBBPTrack Track;
			if (Line.StartsWith(TEXT("{")) && ParseTrackJson(Line, Track))
			{
				Tracks.Add(MoveTemp(Track));
			}
		}
		return Tracks;
	}

	// Returns yt-dlp's error line from its stderr, for showing to the player.
	FString SummarizeError(const FBBPProcessResult& Result)
	{
		if (!Result.bLaunched)
		{
			return TEXT("yt-dlp could not be started");
		}
		TArray<FString> Lines;
		Result.StdErr.ParseIntoArrayLines(Lines);
		for (int32 i = Lines.Num() - 1; i >= 0; --i)
		{
			if (Lines[i].StartsWith(TEXT("ERROR:")))
			{
				return Lines[i].Mid(6).TrimStartAndEnd();
			}
		}
		return FString::Printf(TEXT("yt-dlp exited with code %d"), Result.ReturnCode);
	}

	// Runs a flat yt-dlp listing (search spec or playlist URL) and returns its tracks.
	TArray<FBBPTrack> RunListing(const FString& YtDlpPath, const FString& Target, FString& OutError)
	{
		TArray<FString> Args = BaseYtDlpArgs();
		Args.Append({ TEXT("--flat-playlist"), TEXT("--dump-json"), TEXT("--"), Target });
		const FBBPProcessResult Result = BBPRunProcess(YtDlpPath, Args);
		TArray<FBBPTrack> Tracks = ParseTrackLines(Result.StdOut);
		if (Tracks.Num() == 0 && Result.ReturnCode != 0)
		{
			OutError = SummarizeError(Result);
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: listing '%s' failed: %s"), *Target, *OutError);
		}
		return Tracks;
	}

	// Picks the candidate whose duration is closest to TargetSeconds (or the first if the duration is unknown).
	int32 PickClosestDuration(const TArray<FBBPTrack>& Candidates, float TargetSeconds)
	{
		int32 Best = 0;
		float BestDiff = TNumericLimits<float>::Max();
		for (int32 i = 0; i < Candidates.Num() && TargetSeconds > 0.f; ++i)
		{
			const float Diff = FMath::Abs(Candidates[i].Duration - TargetSeconds);
			if (Diff < BestDiff)
			{
				BestDiff = Diff;
				Best = i;
			}
		}
		return Best;
	}

	// Returns the first artist of a comma-separated artist list.
	FString PrimaryArtist(const FString& Artists)
	{
		FString First, Rest;
		return Artists.Split(TEXT(","), &First, &Rest) ? First.TrimStartAndEnd() : Artists.TrimStartAndEnd();
	}

	// Extracts the JSON array that follows "Key": in Html by bracket matching, or empty if absent.
	FString ExtractJsonArray(const FString& Html, const FString& Key)
	{
		const int32 KeyPos = Html.Find(FString::Printf(TEXT("\"%s\":["), *Key));
		if (KeyPos == INDEX_NONE)
		{
			return FString();
		}
		const int32 Start = Html.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, KeyPos);
		int32 Depth = 0;
		bool bInString = false;
		for (int32 i = Start; i < Html.Len(); ++i)
		{
			const TCHAR C = Html[i];
			if (bInString)
			{
				if (C == TEXT('\\'))
				{
					++i;
				}
				else if (C == TEXT('"'))
				{
					bInString = false;
				}
				continue;
			}
			if (C == TEXT('"'))
			{
				bInString = true;
			}
			else if (C == TEXT('['))
			{
				++Depth;
			}
			else if (C == TEXT(']') && --Depth == 0)
			{
				return Html.Mid(Start, i - Start + 1);
			}
		}
		return FString();
	}

	// Sends a GET request and calls back on the game thread with the body, or an error message.
	void HttpGet(const FString& Url, TFunction<void(const FString& Body, const FString& Error)> OnDone)
	{
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
		Request->SetURL(Url);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("User-Agent"), BrowserUserAgent);
		Request->SetTimeout(HttpTimeoutSeconds);
		Request->OnProcessRequestComplete().BindLambda([Url, OnDone](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
		{
			if (!bSucceeded || !Response.IsValid())
			{
				OnDone(FString(), TEXT("network error"));
				return;
			}
			if (Response->GetResponseCode() != 200)
			{
				OnDone(FString(), FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode()));
				return;
			}
			OnDone(Response->GetContentAsString(), FString());
		});
		if (!Request->ProcessRequest())
		{
			OnDone(FString(), TEXT("could not start request"));
		}
	}
}

void UBBPNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UBBPLibrarySubsystem>();

	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BoomBoxPlus")))
	{
		const FString ToolsDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Tools"));
		YtDlpPath = ToolsDir / TEXT("yt-dlp.exe");
		FfmpegDir = ToolsDir / TEXT("ffmpeg");
	}
	bToolsAvailable = FPaths::FileExists(YtDlpPath) && FPaths::FileExists(FfmpegDir / TEXT("ffmpeg.exe"));
	if (bToolsAvailable)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: tools found at '%s'"), *FPaths::GetPath(YtDlpPath));
	}
	else
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: yt-dlp/ffmpeg not found (expected '%s' and '%s'); YouTube and SoundCloud disabled. Run Tools/FetchTools.ps1 before packaging."),
			*YtDlpPath, *(FfmpegDir / TEXT("ffmpeg.exe")));
	}

	FString CacheJson;
	if (FFileHelper::LoadFileToString(CacheJson, *(GetCacheDir() / TEXT("NetCache.json")))
		&& FJsonObjectConverter::JsonObjectStringToUStruct(CacheJson, &Cache) && Cache.Version == NetCacheVersion)
	{
		Cache.Entries.RemoveAll([](const FBBPNetCacheEntry& Entry) { return !FPaths::FileExists(Entry.FilePath); });
		for (const FBBPNetCacheEntry& Entry : Cache.Entries)
		{
			GetLibrary()->RegisterExternalFile(Entry.Track, Entry.FilePath);
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: %d cached downloads"), Cache.Entries.Num());
	}
	else
	{
		Cache = FBBPNetCache();
		Cache.Version = NetCacheVersion;
	}
}

EBBPLinkKind UBBPNetSubsystem::ClassifyLink(const FString& Text)
{
	const FString Url = Text.TrimStartAndEnd();
	const FString Host = GetHost(Url);
	if (Host == TEXT("youtube.com") || Host == TEXT("m.youtube.com") || Host == TEXT("music.youtube.com") || Host == TEXT("youtu.be"))
	{
		const bool bHasVideo = Url.Contains(TEXT("v=")) || Host == TEXT("youtu.be") || Url.Contains(TEXT("/shorts/"));
		return (Url.Contains(TEXT("list=")) && !bHasVideo) || Url.Contains(TEXT("/playlist")) ? EBBPLinkKind::YouTubePlaylist : EBBPLinkKind::YouTubeVideo;
	}
	if (Host == TEXT("soundcloud.com") || Host == TEXT("m.soundcloud.com") || Host == TEXT("on.soundcloud.com"))
	{
		return Url.Contains(TEXT("/sets/")) ? EBBPLinkKind::SoundCloudSet : EBBPLinkKind::SoundCloudTrack;
	}
	if (Host == TEXT("open.spotify.com"))
	{
		if (Url.Contains(TEXT("/track/")))
		{
			return EBBPLinkKind::SpotifyTrack;
		}
		if (Url.Contains(TEXT("/playlist/")) || Url.Contains(TEXT("/album/")))
		{
			return EBBPLinkKind::SpotifyCollection;
		}
	}
	return EBBPLinkKind::None;
}

void UBBPNetSubsystem::Search(const FString& Query, FBBPOnNetTracks OnDone)
{
	if (!bToolsAvailable)
	{
		OnDone.ExecuteIfBound({}, TEXT("YouTube/SoundCloud unavailable (tools missing)"));
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: searching for '%s'"), *Query);
	Async(EAsyncExecution::ThreadPool, [YtDlp = YtDlpPath, Query, OnDone]()
	{
		FString YouTubeError, SoundCloudError;
		TArray<FBBPTrack> Tracks = RunListing(YtDlp, FString::Printf(TEXT("ytsearch%d:%s"), YouTubeSearchResults, *Query), YouTubeError);
		Tracks.Append(RunListing(YtDlp, FString::Printf(TEXT("scsearch%d:%s"), SoundCloudSearchResults, *Query), SoundCloudError));
		const FString Error = Tracks.Num() > 0 ? FString() : (!YouTubeError.IsEmpty() ? YouTubeError : SoundCloudError);
		AsyncTask(ENamedThreads::GameThread, [Tracks = MoveTemp(Tracks), Error, OnDone]()
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: search returned %d results%s%s"), Tracks.Num(), Error.IsEmpty() ? TEXT("") : TEXT(": "), *Error);
			OnDone.ExecuteIfBound(Tracks, Error);
		});
	});
}

void UBBPNetSubsystem::ResolveLink(const FString& Url, FBBPOnNetTracks OnDone)
{
	const EBBPLinkKind Kind = ClassifyLink(Url);
	if (!bToolsAvailable || Kind == EBBPLinkKind::None || Kind == EBBPLinkKind::SpotifyTrack || Kind == EBBPLinkKind::SpotifyCollection)
	{
		OnDone.ExecuteIfBound({}, bToolsAvailable ? TEXT("Not a YouTube or SoundCloud link") : TEXT("YouTube/SoundCloud unavailable (tools missing)"));
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: resolving link '%s'"), *Url);
	Async(EAsyncExecution::ThreadPool, [YtDlp = YtDlpPath, Url = Url.TrimStartAndEnd(), OnDone]()
	{
		FString Error;
		TArray<FBBPTrack> Tracks = RunListing(YtDlp, Url, Error);
		if (Tracks.Num() > MaxCollectionTracks)
		{
			Tracks.SetNum(MaxCollectionTracks);
		}
		AsyncTask(ENamedThreads::GameThread, [Tracks = MoveTemp(Tracks), Error, OnDone]()
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: link resolved to %d tracks"), Tracks.Num());
			OnDone.ExecuteIfBound(Tracks, Error);
		});
	});
}

void UBBPNetSubsystem::ResolveSpotifyTrack(const FString& Url, FBBPOnNetText OnDone)
{
	const FString TrackUrl = Url.TrimStartAndEnd();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: resolving Spotify track '%s'"), *TrackUrl);
	HttpGet(FString::Printf(TEXT("https://open.spotify.com/oembed?url=%s"), *FGenericPlatformHttp::UrlEncode(TrackUrl)),
		[TrackUrl, OnDone](const FString& OEmbedBody, const FString& OEmbedError)
	{
		FString Title;
		TSharedPtr<FJsonObject> Json;
		if (OEmbedError.IsEmpty() && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OEmbedBody), Json) && Json.IsValid())
		{
			Json->TryGetStringField(TEXT("title"), Title);
		}
		if (Title.IsEmpty())
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: Spotify oEmbed failed for '%s': %s"), *TrackUrl, OEmbedError.IsEmpty() ? TEXT("no title") : *OEmbedError);
			OnDone.ExecuteIfBound(FString(), TEXT("Couldn't read that Spotify link; type the song name instead"));
			return;
		}
		// The track page's og:description is "Artist · Album · Song · Year"; the artist is optional polish.
		HttpGet(TrackUrl, [Title, OnDone](const FString& PageBody, const FString& PageError)
		{
			FString Artist;
			static const FRegexPattern OgDescription(TEXT("<meta property=\"og:description\" content=\"([^\"]*)\""));
			FRegexMatcher Matcher(OgDescription, PageBody);
			if (PageError.IsEmpty() && Matcher.FindNext())
			{
				FString First, Rest;
				const FString Description = Matcher.GetCaptureGroup(1);
				Artist = PrimaryArtist(Description.Split(TEXT(" · "), &First, &Rest) ? First : Description);
			}
			else
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: Spotify artist unavailable (%s); searching by title only"), PageError.IsEmpty() ? TEXT("page format changed") : *PageError);
			}
			const FString SearchText = Artist.IsEmpty() ? Title : FString::Printf(TEXT("%s %s"), *Title, *Artist);
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: Spotify track resolved to '%s'"), *SearchText);
			OnDone.ExecuteIfBound(SearchText, FString());
		});
	});
}

void UBBPNetSubsystem::ResolveSpotifyCollection(const FString& Url, FBBPOnNetTracks OnDone)
{
	if (!bToolsAvailable)
	{
		OnDone.ExecuteIfBound({}, TEXT("YouTube/SoundCloud unavailable (tools missing)"));
		return;
	}
	FString EmbedUrl = Url.TrimStartAndEnd().Replace(TEXT("open.spotify.com/"), TEXT("open.spotify.com/embed/"));
	int32 QueryStart = INDEX_NONE;
	if (EmbedUrl.FindChar(TEXT('?'), QueryStart))
	{
		EmbedUrl.LeftInline(QueryStart);
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: resolving Spotify collection '%s'"), *EmbedUrl);

	HttpGet(EmbedUrl, [YtDlp = YtDlpPath, OnDone](const FString& Body, const FString& Error)
	{
		const FString TrackListJson = Error.IsEmpty() ? ExtractJsonArray(Body, TEXT("trackList")) : FString();
		TArray<TSharedPtr<FJsonValue>> Items;
		if (TrackListJson.IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TrackListJson), Items))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: Spotify trackList not found (%s)"), Error.IsEmpty() ? TEXT("page format changed") : *Error);
			OnDone.ExecuteIfBound({}, TEXT("Couldn't read that Spotify playlist"));
			return;
		}

		struct FWanted { FString Query; float Duration; };
		TArray<FWanted> Wanted;
		for (const TSharedPtr<FJsonValue>& Item : Items)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Item.IsValid() || !Item->TryGetObject(Object))
			{
				continue;
			}
			FString Title, Subtitle;
			double DurationMs = 0.0;
			(*Object)->TryGetStringField(TEXT("title"), Title);
			(*Object)->TryGetStringField(TEXT("subtitle"), Subtitle);
			(*Object)->TryGetNumberField(TEXT("duration"), DurationMs);
			if (!Title.IsEmpty())
			{
				Wanted.Add({ FString::Printf(TEXT("%s %s"), *Title, *PrimaryArtist(Subtitle)), (float)(DurationMs / 1000.0) });
			}
			if (Wanted.Num() >= MaxCollectionTracks)
			{
				break;
			}
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: Spotify collection has %d tracks; matching on YouTube"), Wanted.Num());

		Async(EAsyncExecution::ThreadPool, [YtDlp, Wanted = MoveTemp(Wanted), OnDone]()
		{
			TArray<FBBPTrack> Matches;
			for (const FWanted& Item : Wanted)
			{
				FString MatchError;
				const TArray<FBBPTrack> Candidates = RunListing(YtDlp, FString::Printf(TEXT("ytsearch%d:%s"), SpotifyMatchCandidates, *Item.Query), MatchError);
				if (Candidates.Num() > 0)
				{
					Matches.Add(Candidates[PickClosestDuration(Candidates, Item.Duration)]);
				}
				else
				{
					UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: no YouTube match for '%s'"), *Item.Query);
				}
			}
			AsyncTask(ENamedThreads::GameThread, [Matches = MoveTemp(Matches), Total = Wanted.Num(), OnDone]()
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: matched %d of %d Spotify tracks"), Matches.Num(), Total);
				OnDone.ExecuteIfBound(Matches, Matches.Num() == 0 ? FString(TEXT("No matches found")) : FString());
			});
		});
	});
}

bool UBBPNetSubsystem::IsDownloadPending(const FString& TrackId) const
{
	return DownloadingId == TrackId || DownloadQueue.ContainsByPredicate([&TrackId](const FBBPTrack& T) { return T.Id == TrackId; });
}

void UBBPNetSubsystem::EnsureDownloaded(const FBBPTrack& Track)
{
	if (Track.Source == EBBPTrackSource::Local || !Track.IsValid())
	{
		return;
	}
	if (FBBPNetCacheEntry* Cached = Cache.Entries.FindByPredicate([&Track](const FBBPNetCacheEntry& E) { return E.Track.Id == Track.Id; }))
	{
		Cached->LastUsedTicks = FDateTime::UtcNow().GetTicks();
		if (!GetLibrary()->HasTrack(Track.Id))
		{
			GetLibrary()->RegisterExternalFile(Cached->Track, Cached->FilePath);
		}
		return;
	}
	if (!bToolsAvailable || IsDownloadPending(Track.Id))
	{
		return;
	}
	const FString Host = GetHost(Track.SourceRef);
	const bool bAllowedHost = Host.EndsWith(TEXT("youtube.com")) || Host == TEXT("youtu.be") || Host.EndsWith(TEXT("soundcloud.com"));
	if (!bAllowedHost)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: refusing to download '%s' from unsupported host '%s'"), *Track.Title, *Host);
		return;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: queued download of '%s' (%s)"), *Track.Title, *Track.Id);
	DownloadQueue.Add(Track);
	StartNextDownload();
}

void UBBPNetSubsystem::StartNextDownload()
{
	if (!DownloadingId.IsEmpty() || DownloadQueue.Num() == 0)
	{
		return;
	}
	const FBBPTrack Track = DownloadQueue[0];
	DownloadQueue.RemoveAt(0);
	DownloadingId = Track.Id;

	const FString CacheDir = GetCacheDir();
	IFileManager::Get().MakeDirectory(*CacheDir, true);
	const FString Prefix = Track.Source == EBBPTrackSource::SoundCloud ? TEXT("sc") : TEXT("yt");

	TArray<FString> Args = BaseYtDlpArgs();
	Args.Append({
		TEXT("-f"), TEXT("bestaudio"), TEXT("-x"), TEXT("--audio-format"), TEXT("vorbis"), TEXT("--audio-quality"), TEXT("5"),
		TEXT("--ffmpeg-location"), FfmpegDir, TEXT("--no-playlist"), TEXT("--no-progress"),
		TEXT("-o"), CacheDir / (Prefix + TEXT("_%(id)s.%(ext)s")), TEXT("--print"), TEXT("after_move:filepath"),
		TEXT("--"), Track.SourceRef
	});

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: downloading '%s' (%d more queued)"), *Track.Title, DownloadQueue.Num());
	TWeakObjectPtr<UBBPNetSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, YtDlp = YtDlpPath, Args = MoveTemp(Args), Track]()
	{
		const double StartTime = FPlatformTime::Seconds();
		const FBBPProcessResult Result = BBPRunProcess(YtDlp, Args);
		TArray<FString> Lines;
		Result.StdOut.ParseIntoArrayLines(Lines);
		const FString FilePath = Lines.Num() > 0 ? Lines.Last().TrimStartAndEnd() : FString();
		const bool bOk = Result.ReturnCode == 0 && !FilePath.IsEmpty() && FPaths::FileExists(FilePath);
		const FString Error = bOk ? FString() : SummarizeError(Result);
		const double Elapsed = FPlatformTime::Seconds() - StartTime;
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Track, FilePath, Error, Elapsed]()
		{
			if (UBBPNetSubsystem* This = WeakThis.Get())
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: download of '%s' %s after %.1f s"), *Track.Title, Error.IsEmpty() ? TEXT("finished") : TEXT("failed"), Elapsed);
				This->FinishDownload(Track, FilePath, Error);
			}
		});
	});
}

void UBBPNetSubsystem::FinishDownload(const FBBPTrack& Track, const FString& FilePath, const FString& Error)
{
	DownloadingId.Reset();
	if (Error.IsEmpty())
	{
		FBBPNetCacheEntry& Entry = Cache.Entries.AddDefaulted_GetRef();
		Entry.Track = Track;
		Entry.FilePath = FilePath;
		Entry.LastUsedTicks = FDateTime::UtcNow().GetTicks();
		GetLibrary()->RegisterExternalFile(Track, FilePath);
		EvictOldDownloads();
		SaveCache();
	}
	else
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: could not download '%s' (%s): %s"), *Track.Title, *Track.SourceRef, *Error);
	}
	StartNextDownload();
}

void UBBPNetSubsystem::EvictOldDownloads()
{
	const int32 MaxCached = FMath::Max(5, UBBPConfig::GetInt(GetGameInstance(), UBBPConfig::MaxCachedSongsKey, 50));
	if (Cache.Entries.Num() <= MaxCached)
	{
		return;
	}

	TSet<FString> Protected;
	const UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (const ABBPPlaylistSubsystem* Playlist = World ? ABBPPlaylistSubsystem::Get(World) : nullptr)
	{
		for (const FBBPQueueEntry& Entry : Playlist->GetQueue())
		{
			Protected.Add(Entry.Track.Id);
		}
	}

	Cache.Entries.Sort([](const FBBPNetCacheEntry& A, const FBBPNetCacheEntry& B) { return A.LastUsedTicks < B.LastUsedTicks; });
	for (int32 i = 0; i < Cache.Entries.Num() && Cache.Entries.Num() > MaxCached;)
	{
		const FBBPNetCacheEntry& Entry = Cache.Entries[i];
		if (Protected.Contains(Entry.Track.Id))
		{
			++i;
			continue;
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Net: evicting cached '%s'"), *Entry.Track.Title);
		GetLibrary()->UnregisterExternalFile(Entry.Track.Id);
		if (!IFileManager::Get().Delete(*Entry.FilePath, false, true, true))
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: could not delete '%s'"), *Entry.FilePath);
		}
		Cache.Entries.RemoveAt(i);
	}
}

void UBBPNetSubsystem::SaveCache() const
{
	FString Json;
	const FString Path = GetCacheDir() / TEXT("NetCache.json");
	if (!FJsonObjectConverter::UStructToJsonObjectString(Cache, Json) || !FFileHelper::SaveStringToFile(Json, *Path))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Net: could not write '%s'"), *Path);
	}
}

FString UBBPNetSubsystem::GetCacheDir() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("Cache"));
}

UBBPLibrarySubsystem* UBBPNetSubsystem::GetLibrary() const
{
	return GetGameInstance()->GetSubsystem<UBBPLibrarySubsystem>();
}
