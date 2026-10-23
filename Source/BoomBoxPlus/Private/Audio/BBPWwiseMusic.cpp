#include "Audio/BBPWwiseMusic.h"
#include "Audio/BBPStreamingSoundWave.h"
#include "Audio/BBPWwise.h"
#include "Audio/SFModWwiseAudioInput.h"
#include "BoomBoxPlus.h"
#include "Features/IModularFeatures.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include <AK/SoundEngine/Common/AkCommonDefs.h>
#include <AK/SoundEngine/Common/AkCallbackTypes.h>
#include <atomic>

namespace
{
	// Audio for one playing id, read on Wwise's audio thread.
	struct FFeed
	{
		UBBPStreamingSoundWave* Wave = nullptr;
		int32 SampleRate = 48000;
		int32 Channels = 2;
		std::atomic<float> Gain{ 0.f };
		std::atomic<bool> bPaused{ false };
		std::atomic<bool> bEnded{ false };
		// Wwise audio thread only.
		TArray<int16> Scratch;
	};

	FCriticalSection FeedsLock;
	TMap<uint32, TSharedPtr<FFeed>> Feeds;

	bool bTried = false;
	bool bReady = false;
	uint32 EventId = 0;

	// AK_EndOfEvent.
	constexpr uint32 EndOfEventFlag = 0x0001;

	// Body offsets of a Sound object: its source (plugin id, stream type, source id, cache id, media size, source
	// bits) takes bytes 4 to 21; the node settings follow.
	constexpr int32 SoundSourceEnd = 22;

	// Byte offsets in a bank file's BKHD: bank id (12), bank type (28).
	constexpr int32 BankIdOffset = 12;
	constexpr int32 BankTypeOffset = 28;

	void WriteSilence(AkAudioBuffer* Buffer)
	{
		// ZeroPadToMaxFrames only zeroes past uValidFrames.
		Buffer->uValidFrames = 0;
		Buffer->ZeroPadToMaxFrames();
		Buffer->eState = AK_DataReady;
	}

	bool FillMusic(uint32 PlayingId, AkAudioBuffer* Buffer)
	{
		FScopeLock Lock(&FeedsLock);
		const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId);
		if (!Found)
		{
			return false;
		}
		const uint32 Frames = Buffer->MaxFrames();
		FFeed* Feed = Found->Get();
		const int32 Channels = FMath::Min<int32>(Buffer->NumChannels(), Feed->Channels);
		if (!Feed->Wave || Feed->bPaused.load() || Channels <= 0)
		{
			WriteSilence(Buffer);
			return true;
		}
		Feed->Scratch.SetNumUninitialized(Frames * Feed->Channels, EAllowShrinking::No);
		Feed->Wave->GeneratePCM(Feed->Scratch.GetData(), Frames * Feed->Channels);
		const float Scale = Feed->Gain.load() / 32768.f;
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			AkSampleType* Out = Buffer->GetChannel(Channel);
			for (uint32 Frame = 0; Frame < Frames; ++Frame)
			{
				Out[Frame] = Feed->Scratch[Frame * Feed->Channels + Channel] * Scale;
			}
		}
		for (uint32 Channel = Channels; Channel < Buffer->NumChannels(); ++Channel)
		{
			FMemory::Memzero(Buffer->GetChannel(Channel), Frames * sizeof(AkSampleType));
		}
		Buffer->uValidFrames = static_cast<AkUInt16>(Frames);
		Buffer->eState = AK_DataReady;
		return true;
	}

	void SetFormat(AkAudioFormat& Format, int32 SampleRate, int32 Channels)
	{
		Format.uSampleRate = SampleRate;
		Format.channelConfig.SetStandard(Channels == 1 ? AK_SPEAKER_SETUP_MONO : AK_SPEAKER_SETUP_STEREO);
		Format.uBlockAlign = sizeof(AkSampleType) * Format.channelConfig.uNumChannels;
	}

	bool GetMusicFormat(uint32 PlayingId, AkAudioFormat& Format)
	{
		FScopeLock Lock(&FeedsLock);
		const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId);
		if (!Found)
		{
			return false;
		}
		SetFormat(Format, (*Found)->SampleRate, (*Found)->Channels);
		return true;
	}

	// BoomBoxPlus's music as one of the Audio Input sources shared with other mods (SFModWwiseAudioInput.h).
	class FMusicSource : public ISFModWwiseAudioInputSource
	{
	public:
		virtual bool FillAudioInput(uint32 PlayingId, AkAudioBuffer* Buffer) override { return FillMusic(PlayingId, Buffer); }
		virtual bool GetAudioInputFormat(uint32 PlayingId, AkAudioFormat& Format) override { return GetMusicFormat(PlayingId, Format); }
	};
	FMusicSource MusicSource;
	bool bSourceRegistered = false;

	// The game's single set of Audio Input callbacks: each mod's registered source is asked in turn.
	void HandleAudioInput(uint32 PlayingId, AkAudioBuffer* Buffer)
	{
		IModularFeatures::FScopedLockModularFeatureList Lock;
		for (ISFModWwiseAudioInputSource* Source : IModularFeatures::Get().GetModularFeatureImplementations<ISFModWwiseAudioInputSource>(ISFModWwiseAudioInputSource::GetName()))
		{
			if (Source->FillAudioInput(PlayingId, Buffer))
			{
				return;
			}
		}
		WriteSilence(Buffer);
	}

	void HandleAudioFormat(uint32 PlayingId, AkAudioFormat& Format)
	{
		IModularFeatures::FScopedLockModularFeatureList Lock;
		for (ISFModWwiseAudioInputSource* Source : IModularFeatures::Get().GetModularFeatureImplementations<ISFModWwiseAudioInputSource>(ISFModWwiseAudioInputSource::GetName()))
		{
			if (Source->GetAudioInputFormat(PlayingId, Format))
			{
				return;
			}
		}
		SetFormat(Format, 48000, 2);
	}

	void HandleEventCallback(int32 Type, void* EventInfo, void* CallbackInfo, void* Cookie)
	{
		if ((static_cast<uint32>(Type) & EndOfEventFlag) == 0 || !EventInfo)
		{
			return;
		}
		const uint32 PlayingId = static_cast<AkEventCallbackInfo*>(EventInfo)->playingID;
		FScopeLock Lock(&FeedsLock);
		if (const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId))
		{
			(*Found)->bEnded = true;
		}
	}

	// Builds the music bank from a game Boom Box song bank: the song's sound gets an Audio Input source (with its plugin
	// settings object before it), every object a new id, the bank a new id, and the actions point at it as a user bank.
	bool BuildBank(const TArray<uint8>& Song, TArray<uint8>& OutBank, uint32& OutEventId)
	{
		int32 HircStart = INDEX_NONE;
		const TArray<BBPWwise::FObject> Objects = BBPWwise::ReadObjects(Song, HircStart);
		if (HircStart == INDEX_NONE || Song.Num() < BankTypeOffset + 4 || FMemory::Memcmp(Song.GetData(), "BKHD", 4) != 0)
		{
			return false;
		}
		const BBPWwise::FObject* Event = Objects.FindByPredicate([](const BBPWwise::FObject& Object) { return Object.Type == 4; });
		if (!Event || Objects.FilterByPredicate([](const BBPWwise::FObject& Object) { return Object.Type == 4; }).Num() != 1)
		{
			return false;
		}
		// The event's first action: a Play (type 0x0403) whose target is the song's sound.
		uint32 ActionId = 0;
		FMemory::Memcpy(&ActionId, Song.GetData() + Event->Start + Event->Size - 4, sizeof(ActionId));
		const BBPWwise::FObject* Action = Objects.FindByPredicate([&Song, ActionId](const BBPWwise::FObject& Object) { return Object.Type == 3 && Object.GetId(Song) == ActionId; });
		if (!Action || Action->Size < 5 + 10 || Song[Action->Start + 5 + 4] != 0x03 || Song[Action->Start + 5 + 5] != 0x04)
		{
			return false;
		}
		uint32 SoundId = 0;
		FMemory::Memcpy(&SoundId, Song.GetData() + Action->Start + 5 + 6, sizeof(SoundId));
		if (!Objects.ContainsByPredicate([&Song, SoundId](const BBPWwise::FObject& Object) { return Object.Type == 2 && Object.GetId(Song) == SoundId && Object.Size > 5 + SoundSourceEnd; }))
		{
			return false;
		}

		TMap<uint32, uint32> NewIds;
		for (const BBPWwise::FObject& Object : Objects)
		{
			const uint32 Id = Object.GetId(Song);
			NewIds.Add(Id, BBPWwise::IdFromName(FString::Printf(TEXT("BBP_Music_%08x"), Id)));
		}
		const uint32 BankId = BBPWwise::IdFromName(TEXT("BBP_Music_Bank"));
		const uint32 SourceId = BBPWwise::IdFromName(TEXT("BBP_Music_Input"));

		auto Remap = [&NewIds](TArray<uint8>& Body)
		{
			for (int32 i = 0; i + 4 <= Body.Num(); ++i)
			{
				uint32 Value = 0;
				FMemory::Memcpy(&Value, Body.GetData() + i, sizeof(Value));
				if (const uint32* NewId = NewIds.Find(Value))
				{
					FMemory::Memcpy(Body.GetData() + i, NewId, sizeof(*NewId));
					i += 3;
				}
			}
		};
		auto AppendU32 = [](TArray<uint8>& Out, uint32 Value) { Out.Append(reinterpret_cast<const uint8*>(&Value), sizeof(Value)); };
		auto AppendObject = [&AppendU32](TArray<uint8>& Out, uint8 Type, const TArray<uint8>& Body)
		{
			Out.Add(Type);
			AppendU32(Out, Body.Num());
			Out.Append(Body);
		};

		TArray<uint8> Hirc;
		uint32 Count = 0;
		for (const BBPWwise::FObject& Object : Objects)
		{
			const uint32 Id = Object.GetId(Song);
			TArray<uint8> Body(Song.GetData() + Object.Start + 5, Object.Size - 5);
			Remap(Body);
			if (Object.Type == 2 && Id == SoundId)
			{
				// Plugin settings for the Audio Input source: plugin id, no parameters (the plugin's defaults), no media,
				// RTPCs or states.
				TArray<uint8> Settings;
				AppendU32(Settings, SourceId);
				AppendU32(Settings, BBPWwise::AudioInputPluginId);
				AppendU32(Settings, 0);
				Settings.AddZeroed(7);
				AppendObject(Hirc, 17, Settings);
				++Count;

				TArray<uint8> Source;
				AppendU32(Source, NewIds[SoundId]);
				AppendU32(Source, BBPWwise::AudioInputPluginId);
				Source.Add(0);
				AppendU32(Source, SourceId);
				AppendU32(Source, 0);
				AppendU32(Source, 0);
				Source.Add(0);
				// A plugin source's parameter size (its parameters are in the settings object).
				AppendU32(Source, 0);
				Source.Append(Body.GetData() + SoundSourceEnd, Body.Num() - SoundSourceEnd);
				Body = MoveTemp(Source);
			}
			else if (Object.Type == 3 && Body.Num() >= 8)
			{
				// An action ends with the id and type of the bank holding its target.
				FMemory::Memcpy(Body.GetData() + Body.Num() - 8, &BankId, sizeof(BankId));
				FMemory::Memzero(Body.GetData() + Body.Num() - 4, 4);
			}
			AppendObject(Hirc, Object.Type, Body);
			++Count;
		}

		uint32 HeaderSize = 0;
		FMemory::Memcpy(&HeaderSize, Song.GetData() + 4, sizeof(HeaderSize));
		if (HeaderSize < BankTypeOffset + 4 - 8 || 8 + (int64)HeaderSize > Song.Num())
		{
			return false;
		}
		TArray<uint8> Header(Song.GetData() + 8, HeaderSize);
		FMemory::Memcpy(Header.GetData() + BankIdOffset - 8, &BankId, sizeof(BankId));
		FMemory::Memzero(Header.GetData() + BankTypeOffset - 8, 4);

		OutBank.Reset();
		OutBank.Append(reinterpret_cast<const uint8*>("BKHD"), 4);
		AppendU32(OutBank, Header.Num());
		OutBank.Append(Header);
		OutBank.Append(reinterpret_cast<const uint8*>("HIRC"), 4);
		AppendU32(OutBank, Hirc.Num() + 4);
		AppendU32(OutBank, Count);
		OutBank.Append(Hirc);
		OutEventId = NewIds[Event->GetId(Song)];
		return true;
	}

	// UAkComponent::UpdateGameObjectPosition, from the game's AkAudio DLL.
	void (*GetUpdatePosition())(UObject*)
	{
		static void (*Function)(UObject*) = []() -> void (*)(UObject*)
		{
#if PLATFORM_WINDOWS
			for (const TCHAR* Dll : { TEXT("FactoryGameSteam-AkAudio-Win64-Shipping.dll"), TEXT("FactoryGameEGS-AkAudio-Win64-Shipping.dll") })
			{
				if (void* Module = GetModuleHandleW(Dll))
				{
					return reinterpret_cast<void (*)(UObject*)>(FPlatformProcess::GetDllExport(Module, TEXT("?UpdateGameObjectPosition@UAkComponent@@UEAAXXZ")));
				}
			}
#endif
			return nullptr;
		}();
		return Function;
	}
}

bool BBPWwiseMusic::EnsureReady()
{
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	if (!BBPWwise::GetCalls().IsComplete() || !GetUpdatePosition())
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Wwise music: the game's Wwise functions aren't all there; Custom Music plays through Unreal"));
		return false;
	}
	// Another mod may have registered it already; the bank load shows whether it's there.
	BBPWwise::RegisterBuiltInPlugin(BBPWwise::AudioInputPluginId, TEXT("Wwise music"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectContentDir() / TEXT("WwiseAudio") / TEXT("Event")), TEXT("Play_Boombox_*.bnk"), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File);
		if (Name.Contains(TEXT("_UI_")) || Name.Contains(TEXT("Animation")))
		{
			continue;
		}
		TArray<uint8> Song;
		TArray<uint8> Bank;
		if (!FFileHelper::LoadFileToArray(Song, *File) || !BuildBank(Song, Bank, EventId))
		{
			continue;
		}
		BBPWwise::PrepareBank(Bank, TEXT("Wwise music"));
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Wwise music: built from the game's '%s'"), *Name);
		if (!BBPWwise::LoadBank(Bank, TEXT("Wwise music")))
		{
			break;
		}
		if (!bSourceRegistered)
		{
			IModularFeatures::Get().RegisterModularFeature(ISFModWwiseAudioInputSource::GetName(), &MusicSource);
			bSourceRegistered = true;
		}
		bReady = BBPWwise::SetAudioInputCallbacks(&HandleAudioInput, &HandleAudioFormat);
		break;
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Wwise music: %s (%d Boom Box song bank(s) found)"),
		bReady ? TEXT("ready; Custom Music plays through the game's audio engine") : TEXT("unavailable; Custom Music plays through Unreal"), Files.Num());
	return bReady;
}

uint32 BBPWwiseMusic::Play(UBBPStreamingSoundWave* Wave, UObject* Emitter, int32 SampleRate, int32 Channels)
{
	if (!EnsureReady() || !Wave || !Emitter)
	{
		return 0;
	}
	TSharedPtr<FFeed> Feed = MakeShared<FFeed>();
	Feed->Wave = Wave;
	Feed->SampleRate = SampleRate;
	Feed->Channels = Channels;
	UpdatePosition(Emitter);
	// The callbacks wait on the lock, so the feed is registered before Wwise first asks for its format.
	FScopeLock Lock(&FeedsLock);
	const uint32 PlayingId = BBPWwise::GetCalls().PostEvent(EventId, reinterpret_cast<uint64>(Emitter), EndOfEventFlag, &HandleEventCallback, nullptr, 0, nullptr, 0);
	if (PlayingId != 0)
	{
		Feeds.Add(PlayingId, Feed);
	}
	return PlayingId;
}

void BBPWwiseMusic::Stop(uint32 PlayingId)
{
	if (PlayingId == 0)
	{
		return;
	}
	{
		FScopeLock Lock(&FeedsLock);
		Feeds.Remove(PlayingId);
	}
	BBPWwise::GetCalls().StopPlayingID(PlayingId, 0, 4);
}

void BBPWwiseMusic::SetGain(uint32 PlayingId, float Gain)
{
	FScopeLock Lock(&FeedsLock);
	if (const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId))
	{
		(*Found)->Gain = Gain;
	}
}

void BBPWwiseMusic::SetPaused(uint32 PlayingId, bool bPaused)
{
	FScopeLock Lock(&FeedsLock);
	if (const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId))
	{
		(*Found)->bPaused = bPaused;
	}
}

bool BBPWwiseMusic::HasEnded(uint32 PlayingId)
{
	FScopeLock Lock(&FeedsLock);
	const TSharedPtr<FFeed>* Found = Feeds.Find(PlayingId);
	return !Found || (*Found)->bEnded.load();
}

void BBPWwiseMusic::UpdatePosition(UObject* Emitter)
{
	if (void (*Update)(UObject*) = GetUpdatePosition(); Update && IsValid(Emitter))
	{
		Update(Emitter);
	}
}
