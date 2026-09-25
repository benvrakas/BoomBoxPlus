#pragma once

#include "CoreMinimal.h"

// Helpers shared by everything that handles internet radio streams (tuning in, validating, playing).
namespace BBPRadio
{
	// Output format ffmpeg converts every station to.
	constexpr int32 SampleRate = 48000;
	constexpr int32 Channels = 2;

	// Returns Text as a stream URL, adding "http://" when no scheme was typed, or empty if it can't be one.
	FString NormalizeUrl(const FString& Text);

	// Returns true for an http(s) URL with no spaces, quotes or control characters, short enough to replicate.
	// Every machine checks this before handing a URL (possibly queued by another player) to ffmpeg.
	bool IsStreamUrl(const FString& Url);

	// Returns true if Url points at a .pls or .m3u station playlist rather than the stream itself.
	bool IsPlaylistUrl(const FString& Url);

	// Returns the first stream URL listed in a .pls or .m3u playlist, or empty.
	FString ParsePlaylist(const FString& Body);

	// Returns true if Url is a YouTube page whose live stream address must be looked up (per machine) with yt-dlp.
	bool IsYouTubeUrl(const FString& Url);

	// Returns true if Url, a stream address produced by yt-dlp, is safe to hand to ffmpeg (https/http, no quotes or spaces).
	bool IsResolvedStreamUrl(const FString& Url);

	// Returns the queue track id for a station URL; the same on every machine.
	FString MakeTrackId(const FString& Url);

	// ffmpeg arguments that open Url as input, limited to web protocols. With bReconnect, ffmpeg also reconnects after network drops.
	TArray<FString> MakeInputArgs(const FString& Url, bool bReconnect);

	// Reads one ffmpeg log line: sets OutStationName from "icy-name", OutSongTitle from "StreamTitle" (either may stay empty).
	void ParseLogLine(const FString& Line, FString& OutStationName, FString& OutSongTitle);
}
