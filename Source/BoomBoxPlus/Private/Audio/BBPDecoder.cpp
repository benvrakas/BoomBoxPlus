#include "Audio/BBPDecoder.h"
#include "Audio/BBPDecoderLibs.h"

namespace
{
	constexpr int32 MaxSupportedChannels = 8;
	constexpr int32 MaxSupportedSampleRate = 192000;
	constexpr uint64 Mp3SeekPointIntervalSeconds = 2;
	constexpr uint64 MaxMp3SeekPoints = 4096;

	// Builds and binds an MP3 seek table so seeks don't decode from the start of the file.
	void BindMp3SeekTable(drmp3* Mp3, uint64 TotalFrames, TArray<uint8>& Storage)
	{
		const uint64 FramesPerPoint = FMath::Max<uint64>(1, (uint64)Mp3->sampleRate * Mp3SeekPointIntervalSeconds);
		drmp3_uint32 NumPoints = (drmp3_uint32)FMath::Clamp<uint64>(TotalFrames / FramesPerPoint, 1, MaxMp3SeekPoints);
		Storage.SetNumZeroed(NumPoints * sizeof(drmp3_seek_point));
		drmp3_seek_point* Points = reinterpret_cast<drmp3_seek_point*>(Storage.GetData());
		if (drmp3_calculate_seek_points(Mp3, &NumPoints, Points))
		{
			drmp3_bind_seek_table(Mp3, NumPoints, Points);
		}
		else
		{
			Storage.Empty();
		}
	}
}

EBBPAudioFormat BBPSniffAudioFormat(const TArray<uint8>& FileBytes)
{
	const int32 Num = FileBytes.Num();
	const uint8* B = FileBytes.GetData();

	// RIFF....WAVE
	if (Num >= 12 && B[0] == 'R' && B[1] == 'I' && B[2] == 'F' && B[3] == 'F'
		&& B[8] == 'W' && B[9] == 'A' && B[10] == 'V' && B[11] == 'E')
	{
		return EBBPAudioFormat::Wav;
	}

	// OggS
	if (Num >= 4 && B[0] == 'O' && B[1] == 'g' && B[2] == 'g' && B[3] == 'S')
	{
		return EBBPAudioFormat::Vorbis;
	}

	// ID3v2 tag
	if (Num >= 3 && B[0] == 'I' && B[1] == 'D' && B[2] == '3')
	{
		return EBBPAudioFormat::Mp3;
	}

	// MPEG audio frame sync (11 set bits)
	if (Num >= 2 && B[0] == 0xFF && (B[1] & 0xE0) == 0xE0)
	{
		return EBBPAudioFormat::Mp3;
	}

	return EBBPAudioFormat::Unknown;
}

FBBPDecoder::~FBBPDecoder()
{
	Close();
}

void FBBPDecoder::Close()
{
	if (DecoderHandle)
	{
		switch (Format)
		{
		case EBBPAudioFormat::Mp3:
			drmp3_uninit(static_cast<drmp3*>(DecoderHandle));
			delete static_cast<drmp3*>(DecoderHandle);
			Mp3SeekTable.Empty();
			break;
		case EBBPAudioFormat::Wav:
			drwav_uninit(static_cast<drwav*>(DecoderHandle));
			delete static_cast<drwav*>(DecoderHandle);
			break;
		case EBBPAudioFormat::Vorbis:
			stb_vorbis_close(static_cast<stb_vorbis*>(DecoderHandle));
			break;
		default:
			break;
		}
		DecoderHandle = nullptr;
	}
	Format = EBBPAudioFormat::Unknown;
	SampleRate = 0;
	Channels = 0;
	TotalFrames = 0;
	SourceBytes.Empty();
}

bool FBBPDecoder::Open(TArray<uint8>&& FileBytes)
{
	Close();

	const EBBPAudioFormat Sniffed = BBPSniffAudioFormat(FileBytes);
	if (Sniffed == EBBPAudioFormat::Unknown)
	{
		return false;
	}

	SourceBytes = MoveTemp(FileBytes);

	switch (Sniffed)
	{
	case EBBPAudioFormat::Mp3:
	{
		drmp3* Mp3 = new drmp3();
		if (!drmp3_init_memory(Mp3, SourceBytes.GetData(), SourceBytes.Num(), nullptr))
		{
			delete Mp3;
			SourceBytes.Empty();
			return false;
		}
		DecoderHandle = Mp3;
		Format = EBBPAudioFormat::Mp3;
		SampleRate = (int32)Mp3->sampleRate;
		Channels = (int32)Mp3->channels;
		TotalFrames = drmp3_get_pcm_frame_count(Mp3);
		BindMp3SeekTable(Mp3, TotalFrames, Mp3SeekTable);
		break;
	}
	case EBBPAudioFormat::Wav:
	{
		drwav* Wav = new drwav();
		if (!drwav_init_memory(Wav, SourceBytes.GetData(), SourceBytes.Num(), nullptr))
		{
			delete Wav;
			SourceBytes.Empty();
			return false;
		}
		DecoderHandle = Wav;
		Format = EBBPAudioFormat::Wav;
		SampleRate = (int32)Wav->sampleRate;
		Channels = (int32)Wav->channels;
		TotalFrames = Wav->totalPCMFrameCount;
		break;
	}
	case EBBPAudioFormat::Vorbis:
	{
		int VorbisError = 0;
		stb_vorbis* Vorbis = stb_vorbis_open_memory(SourceBytes.GetData(), SourceBytes.Num(), &VorbisError, nullptr);
		if (!Vorbis)
		{
			SourceBytes.Empty();
			return false;
		}
		DecoderHandle = Vorbis;
		Format = EBBPAudioFormat::Vorbis;
		const stb_vorbis_info Info = stb_vorbis_get_info(Vorbis);
		SampleRate = (int32)Info.sample_rate;
		Channels = Info.channels;
		TotalFrames = stb_vorbis_stream_length_in_samples(Vorbis);
		break;
	}
	default:
		return false;
	}

	// Rejects streams whose reported format is out of range.
	if (Channels <= 0 || Channels > MaxSupportedChannels || SampleRate <= 0 || SampleRate > MaxSupportedSampleRate)
	{
		Close();
		return false;
	}

	return true;
}

int32 FBBPDecoder::ReadFrames(int16* Dst, int32 NumFrames)
{
	if (!IsOpen() || NumFrames <= 0)
	{
		return 0;
	}

	switch (Format)
	{
	case EBBPAudioFormat::Mp3:
		return (int32)drmp3_read_pcm_frames_s16(static_cast<drmp3*>(DecoderHandle), (drmp3_uint64)NumFrames, (drmp3_int16*)Dst);
	case EBBPAudioFormat::Wav:
		return (int32)drwav_read_pcm_frames_s16(static_cast<drwav*>(DecoderHandle), (drwav_uint64)NumFrames, (drwav_int16*)Dst);
	case EBBPAudioFormat::Vorbis:
		// Buffer size is given in samples (frames * channels); the return value is in frames.
		return stb_vorbis_get_samples_short_interleaved(static_cast<stb_vorbis*>(DecoderHandle), Channels, Dst, NumFrames * Channels);
	default:
		return 0;
	}
}

bool FBBPDecoder::SeekToFrame(uint64 FrameIndex)
{
	if (!IsOpen())
	{
		return false;
	}

	switch (Format)
	{
	case EBBPAudioFormat::Mp3:
		return drmp3_seek_to_pcm_frame(static_cast<drmp3*>(DecoderHandle), (drmp3_uint64)FrameIndex) != 0;
	case EBBPAudioFormat::Wav:
		return drwav_seek_to_pcm_frame(static_cast<drwav*>(DecoderHandle), (drwav_uint64)FrameIndex) != 0;
	case EBBPAudioFormat::Vorbis:
		return stb_vorbis_seek(static_cast<stb_vorbis*>(DecoderHandle), (unsigned int)FrameIndex) != 0;
	default:
		return false;
	}
}

namespace
{
	// Returns a big-endian 32-bit integer.
	uint32 ReadBE32(const uint8* P)
	{
		return ((uint32)P[0] << 24) | ((uint32)P[1] << 16) | ((uint32)P[2] << 8) | (uint32)P[3];
	}

	// Returns a little-endian 32-bit integer.
	uint32 ReadLE32(const uint8* P)
	{
		return (uint32)P[0] | ((uint32)P[1] << 8) | ((uint32)P[2] << 16) | ((uint32)P[3] << 24);
	}

	// Returns an ID3v2 synchsafe integer (7 bits per byte).
	uint32 ReadSynchsafe32(const uint8* P)
	{
		return ((uint32)(P[0] & 0x7F) << 21) | ((uint32)(P[1] & 0x7F) << 14) | ((uint32)(P[2] & 0x7F) << 7) | (uint32)(P[3] & 0x7F);
	}

	// Converts Latin-1 bytes to a string, stopping at the first null.
	FString Latin1ToString(const uint8* P, int32 Len)
	{
		FString Out;
		for (int32 i = 0; i < Len && P[i] != 0; ++i)
		{
			Out.AppendChar((TCHAR)P[i]);
		}
		return Out.TrimStartAndEnd();
	}

	// Converts UTF-8 bytes to a string, stopping at the first null.
	FString Utf8ToString(const uint8* P, int32 Len)
	{
		int32 End = 0;
		while (End < Len && P[End] != 0)
		{
			++End;
		}
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(P), End);
		return FString(Converted.Length(), Converted.Get()).TrimStartAndEnd();
	}

	// Converts UTF-16 bytes to a string, stopping at the first null code unit.
	FString Utf16ToString(const uint8* P, int32 Len, bool bBigEndian)
	{
		FString Out;
		for (int32 i = 0; i + 1 < Len; i += 2)
		{
			const TCHAR Unit = bBigEndian ? (TCHAR)((P[i] << 8) | P[i + 1]) : (TCHAR)(P[i] | (P[i + 1] << 8));
			if (Unit == 0)
			{
				break;
			}
			Out.AppendChar(Unit);
		}
		return Out.TrimStartAndEnd();
	}

	// Decodes the body of an ID3v2 text frame (encoding byte followed by text).
	FString DecodeId3Text(const uint8* P, int32 Len)
	{
		if (Len < 1)
		{
			return FString();
		}
		const uint8 Encoding = P[0];
		++P;
		--Len;
		switch (Encoding)
		{
		case 0:
			return Latin1ToString(P, Len);
		case 1:
			if (Len >= 2 && P[0] == 0xFE && P[1] == 0xFF)
			{
				return Utf16ToString(P + 2, Len - 2, true);
			}
			if (Len >= 2 && P[0] == 0xFF && P[1] == 0xFE)
			{
				return Utf16ToString(P + 2, Len - 2, false);
			}
			return Utf16ToString(P, Len, false);
		case 2:
			return Utf16ToString(P, Len, true);
		case 3:
			return Utf8ToString(P, Len);
		default:
			return FString();
		}
	}

	// Reads TIT2/TPE1 from an ID3v2.3 or ID3v2.4 tag at the start of the file.
	void ReadId3v2(const TArray<uint8>& Bytes, FString& OutTitle, FString& OutArtist)
	{
		const uint8* B = Bytes.GetData();
		if (Bytes.Num() < 10 || B[0] != 'I' || B[1] != 'D' || B[2] != '3')
		{
			return;
		}
		const uint8 Version = B[3];
		if (Version != 3 && Version != 4)
		{
			return;
		}
		const uint8 Flags = B[5];
		const int32 End = FMath::Min<int32>(Bytes.Num(), 10 + (int32)ReadSynchsafe32(B + 6));
		int32 Pos = 10;
		if ((Flags & 0x40) && Pos + 4 <= End)
		{
			Pos += Version == 4 ? (int32)ReadSynchsafe32(B + Pos) : (int32)ReadBE32(B + Pos) + 4;
		}
		while (Pos + 10 <= End && B[Pos] != 0)
		{
			const uint32 FrameSize = Version == 4 ? ReadSynchsafe32(B + Pos + 4) : ReadBE32(B + Pos + 4);
			const int32 DataStart = Pos + 10;
			if (FrameSize == 0 || (int64)DataStart + FrameSize > End)
			{
				break;
			}
			if (FMemory::Memcmp(B + Pos, "TIT2", 4) == 0 && OutTitle.IsEmpty())
			{
				OutTitle = DecodeId3Text(B + DataStart, (int32)FrameSize);
			}
			else if (FMemory::Memcmp(B + Pos, "TPE1", 4) == 0 && OutArtist.IsEmpty())
			{
				OutArtist = DecodeId3Text(B + DataStart, (int32)FrameSize);
			}
			Pos = DataStart + (int32)FrameSize;
		}
	}

	// Reads title/artist from an ID3v1 tag in the last 128 bytes of the file.
	void ReadId3v1(const TArray<uint8>& Bytes, FString& OutTitle, FString& OutArtist)
	{
		if (Bytes.Num() < 128)
		{
			return;
		}
		const uint8* Tag = Bytes.GetData() + Bytes.Num() - 128;
		if (Tag[0] != 'T' || Tag[1] != 'A' || Tag[2] != 'G')
		{
			return;
		}
		if (OutTitle.IsEmpty())
		{
			OutTitle = Latin1ToString(Tag + 3, 30);
		}
		if (OutArtist.IsEmpty())
		{
			OutArtist = Latin1ToString(Tag + 33, 30);
		}
	}

	// Reads INAM/IART from a RIFF LIST/INFO chunk.
	void ReadRiffInfo(const TArray<uint8>& Bytes, FString& OutTitle, FString& OutArtist)
	{
		const uint8* B = Bytes.GetData();
		const int32 Num = Bytes.Num();
		int32 Pos = 12;
		while (Pos + 8 <= Num)
		{
			const uint32 ChunkSize = ReadLE32(B + Pos + 4);
			const int32 DataStart = Pos + 8;
			if ((int64)DataStart + ChunkSize > Num)
			{
				break;
			}
			if (FMemory::Memcmp(B + Pos, "LIST", 4) == 0 && ChunkSize >= 4 && FMemory::Memcmp(B + DataStart, "INFO", 4) == 0)
			{
				int32 Sub = DataStart + 4;
				const int32 ListEnd = DataStart + (int32)ChunkSize;
				while (Sub + 8 <= ListEnd)
				{
					const uint32 SubSize = ReadLE32(B + Sub + 4);
					if ((int64)Sub + 8 + SubSize > ListEnd)
					{
						break;
					}
					if (FMemory::Memcmp(B + Sub, "INAM", 4) == 0)
					{
						OutTitle = Latin1ToString(B + Sub + 8, (int32)SubSize);
					}
					else if (FMemory::Memcmp(B + Sub, "IART", 4) == 0)
					{
						OutArtist = Latin1ToString(B + Sub + 8, (int32)SubSize);
					}
					Sub += 8 + (int32)SubSize + (SubSize & 1);
				}
				return;
			}
			Pos = DataStart + (int32)ChunkSize + (ChunkSize & 1);
		}
	}
}

void FBBPDecoder::ReadTags(FString& OutTitle, FString& OutArtist) const
{
	OutTitle.Reset();
	OutArtist.Reset();

	switch (Format)
	{
	case EBBPAudioFormat::Mp3:
		ReadId3v2(SourceBytes, OutTitle, OutArtist);
		ReadId3v1(SourceBytes, OutTitle, OutArtist);
		break;
	case EBBPAudioFormat::Wav:
		ReadRiffInfo(SourceBytes, OutTitle, OutArtist);
		break;
	case EBBPAudioFormat::Vorbis:
	{
		const stb_vorbis_comment Comments = stb_vorbis_get_comment(static_cast<stb_vorbis*>(DecoderHandle));
		for (int32 i = 0; i < Comments.comment_list_length; ++i)
		{
			const FString Entry = UTF8_TO_TCHAR(Comments.comment_list[i]);
			FString Key, Value;
			if (Entry.Split(TEXT("="), &Key, &Value))
			{
				if (OutTitle.IsEmpty() && Key.Equals(TEXT("TITLE"), ESearchCase::IgnoreCase))
				{
					OutTitle = Value.TrimStartAndEnd();
				}
				else if (OutArtist.IsEmpty() && Key.Equals(TEXT("ARTIST"), ESearchCase::IgnoreCase))
				{
					OutArtist = Value.TrimStartAndEnd();
				}
			}
		}
		break;
	}
	default:
		break;
	}
}
