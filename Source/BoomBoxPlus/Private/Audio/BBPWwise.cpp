#include "Audio/BBPWwise.h"
#include "BoomBoxPlus.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "Wwise/API/WwiseSoundEngineAPI.h"
#include "AK/SoundEngine/Common/IAkPlugin.h"
#endif

namespace
{
	// Byte offsets in a bank file: 'BKHD', chunk size, version, bank id (12), language id, alignment and flags,
	// project id, bank type (28).
	constexpr int32 BankIdOffset = 12;
	constexpr int32 BankTypeOffset = 28;
	constexpr uint32 UserBankType = 0;

	// AK_Success.
	constexpr int32 WwiseSuccess = 1;

#if PLATFORM_WINDOWS
	void* GetSoundEngineModule()
	{
		for (const TCHAR* Dll : { TEXT("FactoryGameSteam-WwiseSoundEngine-Win64-Shipping.dll"), TEXT("FactoryGameEGS-WwiseSoundEngine-Win64-Shipping.dll") })
		{
			if (void* Module = GetModuleHandleW(Dll))
			{
				return Module;
			}
		}
		return nullptr;
	}
#endif

	// Points every play action that names bank BankId at it as a user bank (an action ends with the id and type of the
	// bank holding its target). Returns how many changed.
	int32 MarkActionsUserBank(TArray<uint8>& Bytes, uint32 BankId)
	{
		int32 HircStart = INDEX_NONE;
		int32 Changed = 0;
		for (const BBPWwise::FObject& Object : BBPWwise::ReadObjects(Bytes, HircStart))
		{
			if (Object.Type != 3 || Object.Size < 5 + 8)
			{
				continue;
			}
			uint8* Tail = Bytes.GetData() + Object.Start + Object.Size - 8;
			uint32 TargetBank = 0;
			uint32 TargetType = 0;
			FMemory::Memcpy(&TargetBank, Tail, sizeof(TargetBank));
			FMemory::Memcpy(&TargetType, Tail + 4, sizeof(TargetType));
			if (TargetBank == BankId && TargetType != UserBankType)
			{
				FMemory::Memcpy(Tail + 4, &UserBankType, sizeof(UserBankType));
				++Changed;
			}
		}
		return Changed;
	}

	// Moves the actor-mixers after the sounds: Wwise looks up a container's children while loading it.
	bool PutChildrenFirst(TArray<uint8>& Bytes)
	{
		int32 HircStart = INDEX_NONE;
		const TArray<BBPWwise::FObject> Objects = BBPWwise::ReadObjects(Bytes, HircStart);
		const int32 FirstSound = Objects.IndexOfByPredicate([](const BBPWwise::FObject& Object) { return Object.Type == 2; });
		const int32 FirstMixer = Objects.IndexOfByPredicate([](const BBPWwise::FObject& Object) { return Object.Type == 7; });
		const int32 LastSound = Objects.FindLastByPredicate([](const BBPWwise::FObject& Object) { return Object.Type == 2; });
		if (FirstSound == INDEX_NONE || FirstMixer == INDEX_NONE || FirstMixer > LastSound)
		{
			return false;
		}
		TArray<uint8> Body;
		for (const uint8 Type : { 2, 7 })
		{
			for (const BBPWwise::FObject& Object : Objects)
			{
				if (Object.Type == Type)
				{
					Body.Append(Bytes.GetData() + Object.Start, Object.Size);
				}
			}
		}
		for (const BBPWwise::FObject& Object : Objects)
		{
			if (Object.Type != 2 && Object.Type != 7)
			{
				Body.Append(Bytes.GetData() + Object.Start, Object.Size);
			}
		}
		if (Body.Num() != Objects.Last().Start + Objects.Last().Size - Objects[0].Start)
		{
			return false;
		}
		FMemory::Memcpy(Bytes.GetData() + Objects[0].Start, Body.GetData(), Body.Num());
		return true;
	}
}

const BBPWwise::FCalls& BBPWwise::GetCalls()
{
	static const FCalls Calls = []
	{
		FCalls Found;
#if PLATFORM_WINDOWS
		void* Module = GetSoundEngineModule();
		if (!Module)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("Wwise: the game's sound engine DLL isn't loaded"));
			return Found;
		}
		Found.LoadBankMemoryCopy = reinterpret_cast<decltype(Found.LoadBankMemoryCopy)>(FPlatformProcess::GetDllExport(Module, TEXT("?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI1@Z")));
		Found.PostEvent = reinterpret_cast<decltype(Found.PostEvent)>(FPlatformProcess::GetDllExport(Module, TEXT("?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkEventCallbackInfo@@PEAX3@Z3IPEAUAkExternalSourceInfo@@I@Z")));
		Found.RegisterGameObj = reinterpret_cast<decltype(Found.RegisterGameObj)>(FPlatformProcess::GetDllExport(Module, TEXT("?RegisterGameObj@SoundEngine@AK@@YA?AW4AKRESULT@@_KPEBD@Z")));
		Found.StopPlayingID = reinterpret_cast<decltype(Found.StopPlayingID)>(FPlatformProcess::GetDllExport(Module, TEXT("?StopPlayingID@SoundEngine@AK@@YAXIHW4AkCurveInterpolation@@@Z")));
		Found.RegisterPlugin = reinterpret_cast<decltype(Found.RegisterPlugin)>(FPlatformProcess::GetDllExport(Module, TEXT("?RegisterPlugin@SoundEngine@AK@@YA?AW4AKRESULT@@W4AkPluginType@@IIP6APEAVIAkPlugin@2@PEAVIAkPluginMemAlloc@2@@ZP6APEAVIAkPluginParam@2@1@ZP6A?AW43@AEAIPEAUAkDeviceDescription@@@Z@Z")));
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Wwise: game sound engine functions %s"), Found.IsComplete() ? TEXT("found") : TEXT("INCOMPLETE; Wwise playback off"));
#endif
		return Found;
	}();
	return Calls;
}

uint32 BBPWwise::IdFromName(const FString& Name)
{
	uint32 Hash = 2166136261u;
	for (const TCHAR Char : Name.ToLower())
	{
		Hash = (Hash * 16777619u) ^ static_cast<uint8>(Char);
	}
	return Hash;
}

uint32 BBPWwise::FObject::GetId(const TArray<uint8>& Bytes) const
{
	uint32 Id = 0;
	if (Size >= 9)
	{
		FMemory::Memcpy(&Id, Bytes.GetData() + Start + 5, sizeof(Id));
	}
	return Id;
}

TArray<BBPWwise::FObject> BBPWwise::ReadObjects(const TArray<uint8>& Bytes, int32& OutHircStart)
{
	TArray<FObject> Objects;
	OutHircStart = INDEX_NONE;
	for (int32 Pos = 0; Pos + 8 <= Bytes.Num();)
	{
		uint32 Size = 0;
		FMemory::Memcpy(&Size, Bytes.GetData() + Pos + 4, sizeof(Size));
		if (Pos + 8 + (int64)Size > Bytes.Num())
		{
			break;
		}
		if (FMemory::Memcmp(Bytes.GetData() + Pos, "HIRC", 4) == 0)
		{
			OutHircStart = Pos;
			const int32 End = Pos + 8 + (int32)Size;
			uint32 Count = 0;
			FMemory::Memcpy(&Count, Bytes.GetData() + Pos + 8, sizeof(Count));
			int32 ObjectPos = Pos + 12;
			for (uint32 i = 0; i < Count && ObjectPos + 5 <= End; ++i)
			{
				uint32 ObjectSize = 0;
				FMemory::Memcpy(&ObjectSize, Bytes.GetData() + ObjectPos + 1, sizeof(ObjectSize));
				if (ObjectPos + 5 + (int64)ObjectSize > End)
				{
					break;
				}
				Objects.Add({ Bytes[ObjectPos], ObjectPos, (int32)ObjectSize + 5 });
				ObjectPos += 5 + ObjectSize;
			}
			break;
		}
		Pos += 8 + Size;
	}
	return Objects;
}

void BBPWwise::PrepareBank(TArray<uint8>& Bytes, const TCHAR* Label)
{
	if (Bytes.Num() < BankTypeOffset + 4 || FMemory::Memcmp(Bytes.GetData(), "BKHD", 4) != 0)
	{
		return;
	}
	uint32 BankId = 0;
	uint32 HeaderType = 0;
	FMemory::Memcpy(&BankId, Bytes.GetData() + BankIdOffset, sizeof(BankId));
	FMemory::Memcpy(&HeaderType, Bytes.GetData() + BankTypeOffset, sizeof(HeaderType));
	// The game's per-event banks are type 30, which Wwise only takes from the game's own asset system.
	if (HeaderType != UserBankType)
	{
		FMemory::Memcpy(Bytes.GetData() + BankTypeOffset, &UserBankType, sizeof(UserBankType));
	}
	const int32 Actions = MarkActionsUserBank(Bytes, BankId);
	const bool bReordered = PutChildrenFirst(Bytes);
	UE_LOG(LogBoomBoxPlus, Verbose, TEXT("%s: bank prepared (header type %u -> %u, %d action(s) marked, objects %s)"),
		Label, HeaderType, UserBankType, Actions, bReordered ? TEXT("reordered") : TEXT("in order"));
}

bool BBPWwise::LoadBank(const TArray<uint8>& Bytes, const TCHAR* Label)
{
	const FCalls& Calls = GetCalls();
	if (!Calls.IsComplete())
	{
		return false;
	}
	uint32 BankId = 0;
	uint32 BankType = 0;
	const int32 Result = Calls.LoadBankMemoryCopy(Bytes.GetData(), Bytes.Num(), BankId, BankType);
	UE_LOG(LogBoomBoxPlus, Log, TEXT("%s: Wwise bank (%d bytes) %s (result %d)"), Label, Bytes.Num(), Result == WwiseSuccess ? TEXT("loaded") : TEXT("FAILED to load"), Result);
	return Result == WwiseSuccess;
}

bool BBPWwise::RegisterBuiltInPlugin(uint32 PluginId, const TCHAR* Label)
{
#if PLATFORM_WINDOWS
	const FCalls& Calls = GetCalls();
	void* Module = GetSoundEngineModule();
	// g_pAKPluginList: every AK::PluginRegistration compiled into the DLL, linked through pNext.
	AK::PluginRegistration** List = Module ? reinterpret_cast<AK::PluginRegistration**>(FPlatformProcess::GetDllExport(Module, TEXT("g_pAKPluginList"))) : nullptr;
	if (!Calls.RegisterPlugin || !List)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("%s: the game's Wwise plugin list or RegisterPlugin isn't exported"), Label);
		return false;
	}
	const uint32 Type = PluginId & 0xF;
	const uint32 Company = (PluginId >> 4) & 0xFFF;
	const uint32 Id = PluginId >> 16;
	int32 Listed = 0;
	for (const AK::PluginRegistration* Entry = *List; Entry && Listed < 1000; Entry = Entry->pNext, ++Listed)
	{
		if ((uint32)Entry->m_eType == Type && Entry->m_ulCompanyID == Company && Entry->m_ulPluginID == Id)
		{
			const int32 Result = Calls.RegisterPlugin(Entry->m_eType, Company, Id, reinterpret_cast<void*>(Entry->m_pCreateFunc),
				reinterpret_cast<void*>(Entry->m_pCreateParamFunc), reinterpret_cast<void*>(Entry->m_pGetDeviceListFunc));
			UE_LOG(LogBoomBoxPlus, Log, TEXT("%s: built-in Wwise plugin %08x %s (result %d)"), Label, PluginId, Result == WwiseSuccess ? TEXT("registered") : TEXT("FAILED to register"), Result);
			return Result == WwiseSuccess;
		}
	}
	UE_LOG(LogBoomBoxPlus, Warning, TEXT("%s: Wwise plugin %08x isn't among the %d built into the game"), Label, PluginId, Listed);
#endif
	return false;
}

bool BBPWwise::SetAudioInputCallbacks(FAudioInputExecute Execute, FAudioInputFormat Format)
{
#if PLATFORM_WINDOWS
	void* Module = GetSoundEngineModule();
	// IWwiseSoundEngineModule::SoundEngine, the game's sound engine API object.
	IWwiseSoundEngineAPI** Engine = Module ? reinterpret_cast<IWwiseSoundEngineAPI**>(FPlatformProcess::GetDllExport(Module, TEXT("?SoundEngine@IWwiseSoundEngineModule@@2PEAVIWwiseSoundEngineAPI@@EA"))) : nullptr;
	if (!Engine || !*Engine || !(*Engine)->AudioInputPlugin)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("Wwise: the game's Audio Input plugin couldn't be reached"));
		return false;
	}
	(*Engine)->AudioInputPlugin->SetAudioInputCallbacks(Execute, Format, nullptr);
	return true;
#else
	return false;
#endif
}
