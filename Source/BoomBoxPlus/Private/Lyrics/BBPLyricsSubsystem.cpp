#include "Lyrics/BBPLyricsSubsystem.h"
#include "Algo/BinarySearch.h"
#include "BoomBoxPlus.h"
#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const TCHAR* LrclibGetUrl = TEXT("https://lrclib.net/api/get");
	const TCHAR* UserAgent = TEXT("BoomBoxPlus/1.0 (Satisfactory mod)");
	constexpr float MaxDurationMismatchSeconds = 5.f;
	constexpr float HttpTimeoutSeconds = 15.f;
	const TCHAR* NoLyricsMarker = TEXT("[bbp:none]");
}

FBBPLyrics UBBPLyricsSubsystem::ParseLrc(const FString& LrcText)
{
	FBBPLyrics Result;
	TArray<FString> RawLines;
	LrcText.ParseIntoArrayLines(RawLines);

	for (const FString& Raw : RawLines)
	{
		// A line may carry several timestamps: "[00:12.00][01:30.00]text".
		TArray<float> Times;
		int32 Pos = 0;
		while (Pos < Raw.Len() && Raw[Pos] == TEXT('['))
		{
			const int32 Close = Raw.Find(TEXT("]"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Pos);
			if (Close == INDEX_NONE)
			{
				break;
			}
			const FString Tag = Raw.Mid(Pos + 1, Close - Pos - 1);
			FString Minutes, Seconds;
			if (Tag.Split(TEXT(":"), &Minutes, &Seconds) && Minutes.IsNumeric())
			{
				Times.Add(FCString::Atof(*Minutes) * 60.f + FCString::Atof(*Seconds));
			}
			Pos = Close + 1;
		}
		if (Times.Num() == 0)
		{
			continue;
		}
		const FString Text = Raw.Mid(Pos).TrimStartAndEnd();
		for (float Time : Times)
		{
			Result.Lines.Add({ Time, Text });
		}
	}

	Result.Lines.Sort([](const FBBPLyricLine& A, const FBBPLyricLine& B) { return A.Time < B.Time; });
	return Result;
}

void UBBPLyricsSubsystem::RequestLyrics(const FBBPTrack& Track, const FString& LocalFilePath)
{
	if (!Track.IsValid() || LyricsByTrack.Contains(Track.Id) || PendingTracks.Contains(Track.Id))
	{
		return;
	}

	if (!LocalFilePath.IsEmpty())
	{
		const FString Sidecar = FPaths::ChangeExtension(LocalFilePath, TEXT("lrc"));
		FString LrcText;
		if (FFileHelper::LoadFileToString(LrcText, *Sidecar))
		{
			FBBPLyrics Lyrics = ParseLrc(LrcText);
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Lyrics: %d lines from sidecar '%s'"), Lyrics.Lines.Num(), *Sidecar);
			StoreLyrics(Track.Id, MoveTemp(Lyrics), FString());
			return;
		}
	}

	FString Cached;
	if (FFileHelper::LoadFileToString(Cached, *GetCachePath(Track.Id)))
	{
		FBBPLyrics Lyrics = Cached.StartsWith(NoLyricsMarker) ? FBBPLyrics() : ParseLrc(Cached);
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Lyrics: %d lines from cache for '%s'"), Lyrics.Lines.Num(), *Track.Title);
		StoreLyrics(Track.Id, MoveTemp(Lyrics), FString());
		return;
	}

	FetchFromLrclib(Track);
}

void UBBPLyricsSubsystem::FetchFromLrclib(const FBBPTrack& Track)
{
	if (Track.Title.IsEmpty() || Track.Artist.IsEmpty())
	{
		UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Lyrics: skipping lookup for '%s', artist or title unknown"), *Track.Title);
		StoreLyrics(Track.Id, FBBPLyrics(), FString());
		return;
	}

	FString Url = FString::Printf(TEXT("%s?track_name=%s&artist_name=%s"), LrclibGetUrl,
		*FGenericPlatformHttp::UrlEncode(Track.Title), *FGenericPlatformHttp::UrlEncode(Track.Artist));
	if (Track.Duration > 0.f)
	{
		Url += FString::Printf(TEXT("&duration=%d"), FMath::RoundToInt(Track.Duration));
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), UserAgent);
	Request->SetTimeout(HttpTimeoutSeconds);

	PendingTracks.Add(Track.Id);
	TWeakObjectPtr<UBBPLyricsSubsystem> WeakThis(this);
	const FBBPTrack TrackCopy = Track;
	Request->OnProcessRequestComplete().BindLambda([WeakThis, TrackCopy](FHttpRequestPtr, FHttpResponsePtr Response, bool bSucceeded)
	{
		UBBPLyricsSubsystem* This = WeakThis.Get();
		if (!This)
		{
			return;
		}
		This->PendingTracks.Remove(TrackCopy.Id);

		if (!bSucceeded || !Response.IsValid())
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Lyrics: LRCLIB request for '%s' failed (network error); will retry next time"), *TrackCopy.Title);
			return;
		}
		const int32 Code = Response->GetResponseCode();
		if (Code == 404)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Lyrics: none on LRCLIB for '%s' - '%s'"), *TrackCopy.Artist, *TrackCopy.Title);
			This->StoreLyrics(TrackCopy.Id, FBBPLyrics(), NoLyricsMarker);
			return;
		}
		if (Code != 200)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Lyrics: LRCLIB returned HTTP %d for '%s'; will retry next time"), Code, *TrackCopy.Title);
			return;
		}

		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Lyrics: could not parse LRCLIB response for '%s'"), *TrackCopy.Title);
			return;
		}

		bool bInstrumental = false;
		Json->TryGetBoolField(TEXT("instrumental"), bInstrumental);
		double RemoteDuration = 0.0;
		Json->TryGetNumberField(TEXT("duration"), RemoteDuration);
		FString Synced;
		Json->TryGetStringField(TEXT("syncedLyrics"), Synced);

		if (bInstrumental || Synced.IsEmpty())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Lyrics: '%s' is %s"), *TrackCopy.Title, bInstrumental ? TEXT("instrumental") : TEXT("without synced lyrics"));
			This->StoreLyrics(TrackCopy.Id, FBBPLyrics(), NoLyricsMarker);
			return;
		}
		if (TrackCopy.Duration > 0.f && RemoteDuration > 0.0 && FMath::Abs(RemoteDuration - TrackCopy.Duration) > MaxDurationMismatchSeconds)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Lyrics: LRCLIB match for '%s' is %.0f s but the track is %.0f s; not using it"),
				*TrackCopy.Title, RemoteDuration, TrackCopy.Duration);
			This->StoreLyrics(TrackCopy.Id, FBBPLyrics(), NoLyricsMarker);
			return;
		}

		FBBPLyrics Lyrics = ParseLrc(Synced);
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Lyrics: %d lines from LRCLIB for '%s'"), Lyrics.Lines.Num(), *TrackCopy.Title);
		This->StoreLyrics(TrackCopy.Id, MoveTemp(Lyrics), Synced);
	});

	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("Lyrics: querying LRCLIB for '%s' - '%s'"), *Track.Artist, *Track.Title);
	if (!Request->ProcessRequest())
	{
		PendingTracks.Remove(Track.Id);
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Lyrics: could not start LRCLIB request for '%s'"), *Track.Title);
	}
}

void UBBPLyricsSubsystem::StoreLyrics(const FString& TrackId, FBBPLyrics&& Lyrics, const FString& LrcTextToCache)
{
	LyricsByTrack.Add(TrackId, MoveTemp(Lyrics));
	if (!LrcTextToCache.IsEmpty() && !FFileHelper::SaveStringToFile(LrcTextToCache, *GetCachePath(TrackId)))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Lyrics: could not write cache '%s'"), *GetCachePath(TrackId));
	}
}

FString UBBPLyricsSubsystem::GetLineAt(const FString& TrackId, float PositionSeconds) const
{
	const FBBPLyrics* Lyrics = LyricsByTrack.Find(TrackId);
	if (!Lyrics || Lyrics->Lines.Num() == 0)
	{
		return FString();
	}
	const int32 Next = Algo::UpperBoundBy(Lyrics->Lines, PositionSeconds, &FBBPLyricLine::Time);
	return Next > 0 ? Lyrics->Lines[Next - 1].Text : FString();
}

bool UBBPLyricsSubsystem::HasLyrics(const FString& TrackId) const
{
	const FBBPLyrics* Lyrics = LyricsByTrack.Find(TrackId);
	return Lyrics && Lyrics->Lines.Num() > 0;
}

FString UBBPLyricsSubsystem::GetCachePath(const FString& TrackId) const
{
	return FPaths::ProjectSavedDir() / TEXT("BoomBoxPlus") / TEXT("Lyrics") / (TrackId + TEXT(".lrc"));
}
