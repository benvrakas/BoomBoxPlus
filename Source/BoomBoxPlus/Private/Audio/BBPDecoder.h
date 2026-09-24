#pragma once

#include "CoreMinimal.h"

// Audio container formats the decoder can open.
enum class EBBPAudioFormat : uint8
{
	Unknown,
	Mp3,
	Wav,
	Vorbis
};

// Identifies the audio format of a file from its leading bytes.
EBBPAudioFormat BBPSniffAudioFormat(const TArray<uint8>& FileBytes);

// Decodes MP3, WAV or OGG Vorbis file bytes into interleaved 16-bit PCM.
class FBBPDecoder
{
public:
	FBBPDecoder() = default;
	~FBBPDecoder();

	FBBPDecoder(const FBBPDecoder&) = delete;
	FBBPDecoder& operator=(const FBBPDecoder&) = delete;

	// Takes ownership of the file bytes and opens a decoder for them. Returns false if the data cannot be decoded.
	bool Open(TArray<uint8>&& FileBytes);

	// Writes up to NumFrames interleaved PCM frames into Dst and returns how many were written; 0 at end of stream.
	int32 ReadFrames(int16* Dst, int32 NumFrames);

	// Moves the read position to an absolute frame index. Returns false if the decoder rejects the position.
	bool SeekToFrame(uint64 FrameIndex);

	// Releases the decoder and the file bytes.
	void Close();

	// Reads the title and artist tags embedded in the file. Leaves an output empty if the file has no such tag.
	void ReadTags(FString& OutTitle, FString& OutArtist) const;

	bool IsOpen() const { return Format != EBBPAudioFormat::Unknown; }
	EBBPAudioFormat GetFormat() const { return Format; }
	int32 GetSampleRate() const { return SampleRate; }
	int32 GetChannels() const { return Channels; }
	uint64 GetTotalFrames() const { return TotalFrames; }

	// Returns why the last Open() failed; empty after a successful Open().
	const FString& GetLastError() const { return LastError; }

	// Returns the track length in seconds.
	float GetDurationSeconds() const { return SampleRate > 0 ? (float)TotalFrames / (float)SampleRate : 0.f; }

private:
	EBBPAudioFormat Format = EBBPAudioFormat::Unknown;
	int32 SampleRate = 0;
	int32 Channels = 0;
	uint64 TotalFrames = 0;

	// Compressed file bytes the decoder reads from; must outlive DecoderHandle.
	TArray<uint8> SourceBytes;

	// MP3 seek points bound to the decoder (raw drmp3_seek_point storage); must outlive DecoderHandle.
	TArray<uint8> Mp3SeekTable;

	// The format-specific decoder instance (drmp3*, drwav* or stb_vorbis*).
	void* DecoderHandle = nullptr;

	FString LastError;
};
