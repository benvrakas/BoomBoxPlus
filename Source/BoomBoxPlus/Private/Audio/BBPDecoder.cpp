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
