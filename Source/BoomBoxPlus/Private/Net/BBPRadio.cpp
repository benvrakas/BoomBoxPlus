#include "Net/BBPRadio.h"
#include "Misc/SecureHash.h"

namespace
{
	constexpr int32 MaxUrlLength = 500;

	bool HasWebScheme(const FString& Url)
	{
		return Url.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || Url.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase);
	}

	// Returns false if Url contains whitespace, control characters, quotes, backslashes or backticks.
	bool HasOnlySafeChars(const FString& Url)
	{
		for (const TCHAR Char : Url)
		{
			if (Char <= 32 || Char == 127 || Char == TEXT('"') || Char == TEXT('\'') || Char == TEXT('\\') || Char == TEXT('`'))
			{
				return false;
			}
		}
		return true;
	}

	// Returns Url's path without query or fragment, lower-case.
	FString GetLowerPath(const FString& Url)
	{
		FString Path = Url;
		int32 Cut = INDEX_NONE;
		if (Path.FindChar(TEXT('?'), Cut) || Path.FindChar(TEXT('#'), Cut))
		{
			Path.LeftInline(Cut);
		}
		return Path.ToLower();
	}
}

FString BBPRadio::NormalizeUrl(const FString& Text)
{
	const FString Url = Text.TrimStartAndEnd();
	if (HasWebScheme(Url))
	{
		return Url;
	}
	// Another scheme (file://, rtmp://, ...) is never accepted; a bare "host/path" gets http://.
	if (Url.Contains(TEXT("://")) || !Url.Contains(TEXT(".")) || Url.Contains(TEXT(" ")))
	{
		return FString();
	}
	return TEXT("http://") + Url;
}

bool BBPRadio::IsStreamUrl(const FString& Url)
{
	if (Url.Len() > MaxUrlLength || !HasWebScheme(Url))
	{
		return false;
	}
	const int32 HostStart = Url.Find(TEXT("://")) + 3;
	return HostStart < Url.Len() && Url[HostStart] != TEXT('/') && HasOnlySafeChars(Url);
}

bool BBPRadio::IsPlaylistUrl(const FString& Url)
{
	const FString Path = GetLowerPath(Url);
	return Path.EndsWith(TEXT(".pls")) || Path.EndsWith(TEXT(".m3u"));
}

FString BBPRadio::ParsePlaylist(const FString& Body)
{
	TArray<FString> Lines;
	Body.ParseIntoArrayLines(Lines);
	for (FString Line : Lines)
	{
		Line.TrimStartAndEndInline();
		// .pls entries look like "File1=http://..."; .m3u entries are bare URLs between "#" comment lines.
		if (Line.StartsWith(TEXT("File"), ESearchCase::IgnoreCase))
		{
			int32 Equals = INDEX_NONE;
			if (Line.FindChar(TEXT('='), Equals))
			{
				Line.RightChopInline(Equals + 1);
				Line.TrimStartAndEndInline();
			}
		}
		if (!Line.StartsWith(TEXT("#")) && IsStreamUrl(Line))
		{
			return Line;
		}
	}
	return FString();
}

bool BBPRadio::IsYouTubeUrl(const FString& Url)
{
	FString Rest = Url.Contains(TEXT("://")) ? Url.RightChop(Url.Find(TEXT("://")) + 3) : Url;
	int32 Slash = INDEX_NONE;
	if (Rest.FindChar(TEXT('/'), Slash))
	{
		Rest.LeftInline(Slash);
	}
	Rest.ToLowerInline();
	return Rest == TEXT("youtube.com") || Rest == TEXT("www.youtube.com") || Rest == TEXT("m.youtube.com")
		|| Rest == TEXT("music.youtube.com") || Rest == TEXT("youtu.be");
}

bool BBPRadio::IsResolvedStreamUrl(const FString& Url)
{
	return HasWebScheme(Url) && Url.Len() <= 8192 && HasOnlySafeChars(Url);
}

FString BBPRadio::MakeTrackId(const FString& Url)
{
	return TEXT("radio-") + FMD5::HashAnsiString(*Url);
}

TArray<FString> BBPRadio::MakeInputArgs(const FString& Url, bool bReconnect)
{
	TArray<FString> Args = { TEXT("-protocol_whitelist"), TEXT("http,https,tcp,tls,crypto,httpproxy") };
	if (bReconnect)
	{
		Args.Append({
			TEXT("-reconnect"), TEXT("1"),
			TEXT("-reconnect_streamed"), TEXT("1"),
			TEXT("-reconnect_on_network_error"), TEXT("1"),
			TEXT("-reconnect_delay_max"), TEXT("10"),
		});
	}
	Args.Append({
		// Microseconds without data before a read gives up (and, with bReconnect, reconnects).
		TEXT("-rw_timeout"), TEXT("15000000"),
		TEXT("-user_agent"), TEXT("BoomBoxPlus"),
		TEXT("-i"), Url,
	});
	return Args;
}

void BBPRadio::ParseLogLine(const FString& Line, FString& OutStationName, FString& OutSongTitle)
{
	static const FString UpdateMarker = TEXT("Metadata update for StreamTitle:");
	const int32 UpdateAt = Line.Find(UpdateMarker);
	if (UpdateAt != INDEX_NONE)
	{
		OutSongTitle = Line.RightChop(UpdateAt + UpdateMarker.Len()).TrimStartAndEnd();
		return;
	}
	// Metadata block lines: "    icy-name        : Station".
	FString Key, Value;
	if (!Line.Split(TEXT(":"), &Key, &Value))
	{
		return;
	}
	Key.TrimStartAndEndInline();
	Value.TrimStartAndEndInline();
	if (Key.Equals(TEXT("icy-name"), ESearchCase::IgnoreCase))
	{
		OutStationName = Value;
	}
	else if (Key.Equals(TEXT("StreamTitle"), ESearchCase::IgnoreCase))
	{
		OutSongTitle = Value;
	}
}
