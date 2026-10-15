#pragma once

#include "CoreMinimal.h"
#include "Net/BBPNetSubsystem.h"

// Reads station lists and song lists from files the player imports.
namespace BBPImport
{
	// A station read from a list: its name (may be empty) and address as written.
	struct FStation
	{
		FString Name;
		FString Url;
	};

	// Largest list file read, in bytes.
	constexpr int64 MaxFileBytes = 4 * 1024 * 1024;

	// Most songs taken from one list.
	constexpr int32 MaxSongs = 1000;

	// Reads a list file (UTF-8, UTF-16 or ANSI). Returns false with a full-sentence error for the player.
	bool LoadListFile(const FString& Path, FString& OutText, FString& OutError);

	// Reads stations from a .pls or .m3u playlist, or from a text list with one station per line: an http(s) address
	// with an optional name before or after it, separated by a tab, comma, "|" or " - " (e.g. "Name<tab>http://...").
	// Duplicate addresses are dropped.
	TArray<FStation> ParseStations(const FString& Text);

	// Reads songs from a CSV/TSV with a title column (and optionally artist and duration columns, as Exportify writes),
	// an .m3u playlist, or a text list with one song per line ("Artist - Title", a search, or a YouTube/SoundCloud
	// link; numbering, bullets and a trailing m:ss length are handled). OutSkipped counts lines that weren't songs.
	TArray<FBBPWantedTrack> ParseSongs(const FString& Text, int32& OutSkipped);
}
