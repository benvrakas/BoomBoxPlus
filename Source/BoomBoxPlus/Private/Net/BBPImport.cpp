#include "Net/BBPImport.h"
#include "BoomBoxPlus.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// Longest search text kept for one song.
	constexpr int32 MaxQueryLength = 200;

	// Returns the index where an http:// or https:// address starts in Line, or INDEX_NONE.
	int32 FindUrlStart(const FString& Line)
	{
		const int32 Http = Line.Find(TEXT("http://"), ESearchCase::IgnoreCase);
		const int32 Https = Line.Find(TEXT("https://"), ESearchCase::IgnoreCase);
		if (Http == INDEX_NONE)
		{
			return Https;
		}
		return Https == INDEX_NONE ? Http : FMath::Min(Http, Https);
	}

	bool IsUrlEnd(TCHAR Char)
	{
		return FChar::IsWhitespace(Char) || Char == TEXT('"') || Char == TEXT('\'') || Char == TEXT('<') || Char == TEXT('>');
	}

	// Trims separators and quotes from both ends and turns runs of whitespace into single spaces.
	FString CleanText(const FString& Text)
	{
		static const FString Separators = TEXT(" \t,;|=:\"'-");
		FString Out;
		Out.Reserve(Text.Len());
		for (const TCHAR Char : Text)
		{
			const TCHAR Normal = FChar::IsWhitespace(Char) ? TEXT(' ') : Char;
			if (Normal == TEXT(' ') && (Out.IsEmpty() || Out.EndsWith(TEXT(" "))))
			{
				continue;
			}
			Out.AppendChar(Normal);
		}
		int32 Start = 0;
		int32 End = Out.Len();
		int32 Unused = 0;
		while (Start < End && Separators.FindChar(Out[Start], Unused))
		{
			++Start;
		}
		while (End > Start && Separators.FindChar(Out[End - 1], Unused))
		{
			--End;
		}
		return Out.Mid(Start, End - Start);
	}

	// Returns the title part of an "#EXTINF:<length> <attributes>,<title>" line.
	FString ExtInfTitle(const FString& Line)
	{
		const int32 QuotedComma = Line.Find(TEXT("\","), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (QuotedComma != INDEX_NONE)
		{
			return CleanText(Line.Mid(QuotedComma + 2));
		}
		int32 Comma = INDEX_NONE;
		return Line.FindChar(TEXT(','), Comma) ? CleanText(Line.Mid(Comma + 1)) : FString();
	}

	// Returns the length in an "#EXTINF:<seconds>,..." line, or 0.
	float ExtInfSeconds(const FString& Line)
	{
		const FString Rest = Line.Mid(8);
		int32 End = 0;
		while (End < Rest.Len() && (FChar::IsDigit(Rest[End]) || Rest[End] == TEXT('.') || Rest[End] == TEXT('-')))
		{
			++End;
		}
		return FMath::Max(0.f, FCString::Atof(*Rest.Left(End)));
	}

	// Parses "m:ss" or "h:mm:ss" (optionally in brackets) into seconds; returns a negative number if Text isn't one.
	float ParseClock(FString Text)
	{
		Text.TrimStartAndEndInline();
		if (Text.StartsWith(TEXT("(")) && Text.EndsWith(TEXT(")")))
		{
			Text = Text.Mid(1, Text.Len() - 2);
		}
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(":"), false);
		if (Parts.Num() < 2 || Parts.Num() > 3)
		{
			return -1.f;
		}
		float Seconds = 0.f;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			if (Parts[i].IsEmpty() || Parts[i].Len() > 2 || !Parts[i].IsNumeric() || (i > 0 && Parts[i].Len() != 2))
			{
				return -1.f;
			}
			Seconds = Seconds * 60.f + FCString::Atof(*Parts[i]);
		}
		return Seconds;
	}

	// Splits one CSV/TSV line into trimmed cells, honouring double quotes.
	TArray<FString> SplitCells(const FString& Line, TCHAR Delimiter)
	{
		TArray<FString> Cells;
		FString Cell;
		bool bQuoted = false;
		for (int32 i = 0; i < Line.Len(); ++i)
		{
			const TCHAR Char = Line[i];
			if (bQuoted)
			{
				if (Char != TEXT('"'))
				{
					Cell.AppendChar(Char);
				}
				else if (i + 1 < Line.Len() && Line[i + 1] == TEXT('"'))
				{
					Cell.AppendChar(Char);
					++i;
				}
				else
				{
					bQuoted = false;
				}
			}
			else if (Char == TEXT('"'))
			{
				bQuoted = true;
			}
			else if (Char == Delimiter)
			{
				Cells.Add(Cell.TrimStartAndEnd());
				Cell.Reset();
			}
			else
			{
				Cell.AppendChar(Char);
			}
		}
		Cells.Add(Cell.TrimStartAndEnd());
		return Cells;
	}

	// Columns of a song table's header row.
	struct FSongColumns
	{
		int32 Title = INDEX_NONE;
		int32 Artist = INDEX_NONE;
		int32 Duration = INDEX_NONE;
		bool bDurationInMs = false;
		TCHAR Delimiter = TEXT(',');
	};

	// Finds the title, artist and duration columns in a header row. Returns false if Line isn't a song table header.
	bool ReadSongHeader(const FString& Line, FSongColumns& Out)
	{
		Out.Delimiter = Line.Contains(TEXT("\t")) ? TEXT('\t') : TEXT(',');
		const TArray<FString> Cells = SplitCells(Line, Out.Delimiter);
		if (Cells.Num() < 2)
		{
			return false;
		}
		static const TArray<FString> TitleNames = { TEXT("track name"), TEXT("title"), TEXT("track"), TEXT("track title"), TEXT("song"), TEXT("song name"), TEXT("name") };
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			const FString Cell = Cells[i].ToLower();
			if (Out.Title == INDEX_NONE && TitleNames.Contains(Cell))
			{
				Out.Title = i;
			}
			else if (Out.Artist == INDEX_NONE && Cell.Contains(TEXT("artist")))
			{
				Out.Artist = i;
			}
			else if (Out.Duration == INDEX_NONE && (Cell.Contains(TEXT("duration")) || Cell == TEXT("length") || Cell == TEXT("time")))
			{
				Out.Duration = i;
				Out.bDurationInMs = Cell.Contains(TEXT("ms"));
			}
		}
		return Out.Title != INDEX_NONE;
	}

	// Returns the first artist of a list separated by commas or semicolons.
	FString FirstArtist(const FString& Artists)
	{
		int32 Cut = Artists.Len();
		int32 At = INDEX_NONE;
		if (Artists.FindChar(TEXT(','), At)) Cut = FMath::Min(Cut, At);
		if (Artists.FindChar(TEXT(';'), At)) Cut = FMath::Min(Cut, At);
		return Artists.Left(Cut).TrimStartAndEnd();
	}

	// Drops list numbering ("1. ", "01 - ", "3) ") and bullets ("- ", "* ") from the start of a song line.
	FString StripNumbering(const FString& Line)
	{
		for (const TCHAR* Bullet : { TEXT("- "), TEXT("* "), TEXT("• ") })
		{
			if (Line.StartsWith(Bullet))
			{
				return Line.Mid(FCString::Strlen(Bullet)).TrimStart();
			}
		}
		int32 i = 0;
		while (i < Line.Len() && FChar::IsDigit(Line[i]))
		{
			++i;
		}
		if (i == 0 || i > 4)
		{
			return Line;
		}
		int32 j = i;
		while (j < Line.Len() && Line[j] == TEXT(' '))
		{
			++j;
		}
		static const FString Marks = TEXT(".)-:");
		int32 Unused = 0;
		if (j < Line.Len() && Marks.FindChar(Line[j], Unused) && j + 1 < Line.Len() && FChar::IsWhitespace(Line[j + 1]))
		{
			return Line.Mid(j + 1).TrimStart();
		}
		return Line;
	}

	bool IsAudioFilePath(const FString& Line)
	{
		static const TArray<FString> Extensions = { TEXT("mp3"), TEXT("ogg"), TEXT("wav"), TEXT("flac"), TEXT("m4a"), TEXT("aac"), TEXT("opus"), TEXT("wma") };
		return Extensions.Contains(FPaths::GetExtension(Line).ToLower()) || Line.Contains(TEXT("\\")) || Line.StartsWith(TEXT("file:"), ESearchCase::IgnoreCase);
	}

	void AddSong(TArray<FBBPWantedTrack>& Songs, int32& Skipped, const FString& Query, float Seconds)
	{
		if (Query.IsEmpty())
		{
			++Skipped;
			return;
		}
		if (Songs.Num() >= BBPImport::MaxSongs)
		{
			++Skipped;
			return;
		}
		FBBPWantedTrack& Song = Songs.AddDefaulted_GetRef();
		Song.Query = Query.Left(MaxQueryLength);
		Song.Duration = FMath::Max(0.f, Seconds);
	}
}

bool BBPImport::LoadListFile(const FString& Path, FString& OutText, FString& OutError)
{
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Size < 0)
	{
		OutError = TEXT("Couldn't open that file.");
		return false;
	}
	if (Size > MaxFileBytes)
	{
		OutError = TEXT("That file is too big for a list (over 4 MB).");
		return false;
	}
	if (!FFileHelper::LoadFileToString(OutText, *Path))
	{
		OutError = TEXT("Couldn't read that file.");
		return false;
	}
	return true;
}

TArray<BBPImport::FStation> BBPImport::ParseStations(const FString& Text)
{
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	TArray<FStation> Stations;
	TSet<FString> Seen;
	auto Add = [&Stations, &Seen](const FString& Name, const FString& Url)
	{
		const FString Address = Url.TrimStartAndEnd();
		if (!Address.IsEmpty() && !Seen.Contains(Address.ToLower()))
		{
			Seen.Add(Address.ToLower());
			Stations.Add({ CleanText(Name), Address });
		}
	};

	// .pls: FileN=<address>, TitleN=<name>.
	TMap<int32, FString> PlsFiles;
	TMap<int32, FString> PlsTitles;
	for (const FString& Line : Lines)
	{
		FString Key, Value;
		if (!Line.TrimStartAndEnd().Split(TEXT("="), &Key, &Value))
		{
			continue;
		}
		if (Key.StartsWith(TEXT("File"), ESearchCase::IgnoreCase) && Key.Mid(4).IsNumeric())
		{
			PlsFiles.Add(FCString::Atoi(*Key.Mid(4)), Value);
		}
		else if (Key.StartsWith(TEXT("Title"), ESearchCase::IgnoreCase) && Key.Mid(5).IsNumeric())
		{
			PlsTitles.Add(FCString::Atoi(*Key.Mid(5)), Value);
		}
	}
	if (PlsFiles.Num() > 0)
	{
		PlsFiles.KeySort(TLess<int32>());
		for (const TPair<int32, FString>& File : PlsFiles)
		{
			Add(PlsTitles.FindRef(File.Key), File.Value);
		}
		return Stations;
	}

	// .m3u and plain lists: one address per line, named by the line's other text or the #EXTINF line above it.
	FString PendingName;
	for (const FString& RawLine : Lines)
	{
		const FString Line = RawLine.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("#EXTINF"), ESearchCase::IgnoreCase))
		{
			PendingName = ExtInfTitle(Line);
			continue;
		}
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("[")))
		{
			continue;
		}
		const int32 Start = FindUrlStart(Line);
		if (Start == INDEX_NONE)
		{
			PendingName.Reset();
			continue;
		}
		int32 End = Start;
		while (End < Line.Len() && !IsUrlEnd(Line[End]))
		{
			++End;
		}
		FString Url = Line.Mid(Start, End - Start);
		while (Url.EndsWith(TEXT(",")) || Url.EndsWith(TEXT(";")) || Url.EndsWith(TEXT("|")) || Url.EndsWith(TEXT(")")) || Url.EndsWith(TEXT("]")))
		{
			Url.LeftChopInline(1);
		}
		FString Name = CleanText(Line.Left(Start) + TEXT(" ") + Line.Mid(End));
		if (Name.IsEmpty())
		{
			Name = PendingName;
		}
		PendingName.Reset();
		Add(Name, Url);
	}
	return Stations;
}

TArray<FBBPWantedTrack> BBPImport::ParseSongs(const FString& Text, int32& OutSkipped)
{
	OutSkipped = 0;
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	TArray<FBBPWantedTrack> Songs;
	if (Lines.Num() == 0)
	{
		return Songs;
	}

	FSongColumns Columns;
	if (ReadSongHeader(Lines[0], Columns))
	{
		for (int32 Row = 1; Row < Lines.Num(); ++Row)
		{
			const TArray<FString> Cells = SplitCells(Lines[Row], Columns.Delimiter);
			const FString Title = Cells.IsValidIndex(Columns.Title) ? CleanText(Cells[Columns.Title]) : FString();
			const FString Artist = Cells.IsValidIndex(Columns.Artist) ? FirstArtist(Cells[Columns.Artist]) : FString();
			float Seconds = 0.f;
			if (Cells.IsValidIndex(Columns.Duration))
			{
				const FString& Value = Cells[Columns.Duration];
				Seconds = Columns.bDurationInMs ? FCString::Atof(*Value) / 1000.f
					: Value.Contains(TEXT(":")) ? ParseClock(Value) : FCString::Atof(*Value);
			}
			AddSong(Songs, OutSkipped, Title.IsEmpty() || Artist.IsEmpty() ? Title : Title + TEXT(" ") + Artist, Seconds);
		}
		return Songs;
	}

	FString PendingName;
	float PendingSeconds = 0.f;
	for (const FString& RawLine : Lines)
	{
		FString Line = RawLine.TrimStartAndEnd();
		if (Line.StartsWith(TEXT("#EXTINF"), ESearchCase::IgnoreCase))
		{
			PendingName = ExtInfTitle(Line);
			PendingSeconds = ExtInfSeconds(Line);
			continue;
		}
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("//")))
		{
			continue;
		}
		const FString Name = PendingName;
		const float NameSeconds = PendingSeconds;
		PendingName.Reset();
		PendingSeconds = 0.f;

		if (Line.StartsWith(TEXT("http://"), ESearchCase::IgnoreCase) || Line.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase))
		{
			const EBBPLinkKind Kind = UBBPNetSubsystem::ClassifyLink(Line);
			if (Kind == EBBPLinkKind::YouTubeVideo || Kind == EBBPLinkKind::SoundCloudTrack)
			{
				AddSong(Songs, OutSkipped, Line, 0.f);
			}
			else if (!Name.IsEmpty())
			{
				AddSong(Songs, OutSkipped, Name, NameSeconds);
			}
			else
			{
				++OutSkipped;
			}
			continue;
		}
		if (IsAudioFilePath(Line))
		{
			AddSong(Songs, OutSkipped, !Name.IsEmpty() ? Name : CleanText(FPaths::GetBaseFilename(Line).Replace(TEXT("_"), TEXT(" "))), NameSeconds);
			continue;
		}

		Line = StripNumbering(Line);
		float Seconds = NameSeconds;
		int32 LastSpace = INDEX_NONE;
		const int32 LastTab = Line.Find(TEXT("\t"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		Line.FindLastChar(TEXT(' '), LastSpace);
		const int32 Cut = FMath::Max(LastSpace, LastTab);
		if (Cut > 0)
		{
			const float Clock = ParseClock(Line.Mid(Cut + 1));
			if (Clock > 0.f)
			{
				Seconds = Clock;
				Line.LeftInline(Cut);
			}
		}
		AddSong(Songs, OutSkipped, CleanText(Line), Seconds);
	}
	return Songs;
}
