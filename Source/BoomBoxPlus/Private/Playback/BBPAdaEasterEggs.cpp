#include "Playback/BBPAdaEasterEggs.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "AkAudioEvent.h"
#include "Audio/BBPDecoder.h"
#include "Audio/BBPStreamingSoundWave.h"
#include "BBPConfig.h"
#include "BoomBoxPlus.h"
#include "Components/AudioComponent.h"
#include "Dom/JsonObject.h"
#include "FGBoomBoxPlayer.h"
#include "FGHUD.h"
#include "FGPlayerState.h"
#include "FGStorySubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Narrative/FGMessage.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Playlist/BBPPlaylistTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/FGGameUI.h"
#include "UI/Message/FGAudioMessage.h"
#include "UI/Message/FGMessageSender.h"

float UBBPAdaEasterEggs::VoiceGain = 5.f;
TArray<TWeakObjectPtr<UBBPAdaEasterEggs>> UBBPAdaEasterEggs::Instances;
bool UBBPAdaEasterEggs::bBankTried = false;
bool UBBPAdaEasterEggs::bBankLoaded = false;

namespace
{
	// Shortest time between two lines, and before the first one after loading.
	constexpr double MinGapSeconds = 600.0;
	constexpr double FirstLineDelaySeconds = 60.0;

	// A line queued in the game's message box that hasn't started by then plays without the box.
	constexpr double BoxTimeoutSeconds = 8.0;

	// Extra time the message box keeps the text up after the clip ends (it closed 0.9 s early without it).
	constexpr float BoxExtraSeconds = 1.f;

	// The box closing with more than this left of the clip means the player skipped the message: the voice stops too.
	constexpr float SkipDetectSeconds = 2.f;

	// Fallback only: how long ADA's jingle plays before the voice starts.
	constexpr float JingleSeconds = 1.2f;

	const TCHAR* AdaSenderPath = TEXT("/Game/FactoryGame/Interface/UI/Message/Sender/Sender_ADA.Sender_ADA_C");

	// The mod's Wwise bank (made by D:/SFMod/Tools/ADA/make_bank.py) and its event names, BBP_ADA_<line>.
	const TCHAR* BankFileName = TEXT("BBP_ADA.bnk");
	const TCHAR* EventPrefix = TEXT("BBP_ADA_");

	// Byte offset of the bank type in a bank's BKHD header ('BKHD', chunk size, then version, bank id, language id,
	// alignment and flags, project id, bank type), and the type of a bank loaded by hand.
	constexpr int32 BankTypeOffset = 28;
	constexpr uint32 UserBankType = 0;

	// The Wwise game object ADA's voice plays on (registered with the bank; uses the default listeners).
	constexpr uint64 VoiceGameObject = 0x4242504144414441ull;

	// The game's own AK::SoundEngine functions, found by their exported names in its Wwise DLL. The modding kit's
	// Wwise (2023.1) doesn't export them and the game's (2025.1) virtual API may be laid out differently, while these
	// plain functions are matched by exact signature.
	struct FBBPWwiseCalls
	{
		int32 (*LoadBankMemoryCopy)(const void*, uint32, uint32&) = nullptr;
		// The overload that also reports the bank's type; the plain one only accepts User banks, and ours is an Event bank.
		int32 (*LoadBankMemoryCopyTyped)(const void*, uint32, uint32&, uint32&) = nullptr;
		uint32 (*PostEvent)(uint32, uint64, uint32, void*, void*, uint32, void*, uint32) = nullptr;
		int32 (*RegisterGameObj)(uint64, const char*) = nullptr;
		void (*StopPlayingID)(uint32, int32, int32) = nullptr;

		bool IsComplete() const { return LoadBankMemoryCopy && PostEvent && RegisterGameObj && StopPlayingID; }
	};

	const FBBPWwiseCalls& GetWwiseCalls()
	{
		static const FBBPWwiseCalls Calls = []
		{
			FBBPWwiseCalls Found;
#if PLATFORM_WINDOWS
			for (const TCHAR* Dll : { TEXT("FactoryGameSteam-WwiseSoundEngine-Win64-Shipping.dll"), TEXT("FactoryGameEGS-WwiseSoundEngine-Win64-Shipping.dll") })
			{
				void* Module = GetModuleHandleW(Dll);
				if (!Module)
				{
					continue;
				}
				Found.LoadBankMemoryCopy = reinterpret_cast<decltype(Found.LoadBankMemoryCopy)>(FPlatformProcess::GetDllExport(Module, TEXT("?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI@Z")));
				Found.LoadBankMemoryCopyTyped = reinterpret_cast<decltype(Found.LoadBankMemoryCopyTyped)>(FPlatformProcess::GetDllExport(Module, TEXT("?LoadBankMemoryCopy@SoundEngine@AK@@YA?AW4AKRESULT@@PEBXIAEAI1@Z")));
				Found.PostEvent = reinterpret_cast<decltype(Found.PostEvent)>(FPlatformProcess::GetDllExport(Module, TEXT("?PostEvent@SoundEngine@AK@@YAII_KIP6AXW4AkCallbackType@@PEAUAkEventCallbackInfo@@PEAX3@Z3IPEAUAkExternalSourceInfo@@I@Z")));
				Found.RegisterGameObj = reinterpret_cast<decltype(Found.RegisterGameObj)>(FPlatformProcess::GetDllExport(Module, TEXT("?RegisterGameObj@SoundEngine@AK@@YA?AW4AKRESULT@@_KPEBD@Z")));
				Found.StopPlayingID = reinterpret_cast<decltype(Found.StopPlayingID)>(FPlatformProcess::GetDllExport(Module, TEXT("?StopPlayingID@SoundEngine@AK@@YAXIHW4AkCurveInterpolation@@@Z")));
				UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: game Wwise functions in %s: LoadBankMemoryCopy %s (with bank type %s), PostEvent %s, RegisterGameObj %s, StopPlayingID %s"), Dll,
					Found.LoadBankMemoryCopy ? TEXT("found") : TEXT("MISSING"), Found.LoadBankMemoryCopyTyped ? TEXT("found") : TEXT("MISSING"), Found.PostEvent ? TEXT("found") : TEXT("MISSING"),
					Found.RegisterGameObj ? TEXT("found") : TEXT("MISSING"), Found.StopPlayingID ? TEXT("found") : TEXT("MISSING"));
				break;
			}
#endif
			return Found;
		}();
		return Calls;
	}

	// Wwise's id for a name: FNV-1 of the lowercase name (as AK::SoundEngine::GetIDFromString).
	uint32 WwiseId(const FString& Name)
	{
		uint32 Hash = 2166136261u;
		for (const TCHAR Char : Name.ToLower())
		{
			Hash = (Hash * 16777619u) ^ static_cast<uint8>(Char);
		}
		return Hash;
	}

	constexpr float IdleSeconds1 = 300.f;
	constexpr float IdleSeconds2 = 900.f;
	// Moving or turning less than this still counts as idle.
	constexpr float IdleMoveTolerance = 100.f;
	constexpr float IdleTurnToleranceDegrees = 5.f;

	constexpr int32 LoopPlays1 = 4;
	constexpr int32 LoopPlays2 = 10;
	// A position jump back by more than this on the same entry counts as the song starting again (repeat one).
	constexpr float RestartJumpSeconds = 5.f;

	constexpr float CopyrightChance = 0.1f;
	constexpr int32 AiOneIn = 500;

	// Another player this close to a full-volume Boom Box (cm).
	constexpr float VolumeNeighbourDistance = 3000.f;
	constexpr float VolumeCheckInterval = 1.f;

	// Seconds between removals of the mod's lines from the game's saved lists of played messages.
	constexpr float PlayedCleanupInterval = 5.f;

	// Removes messages made by UBBPAdaEasterEggs from Owner's array property PropertyName; returns how many.
	int32 RemoveEasterEggMessages(UObject* Owner, const TCHAR* PropertyName)
	{
		FArrayProperty* Property = Owner ? FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName) : nullptr;
		FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
		if (!Inner)
		{
			static TSet<FString> Reported;
			if (Owner && !Reported.Contains(PropertyName))
			{
				Reported.Add(PropertyName);
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: %s has no message list '%s'; its lines may stay in the save"), *Owner->GetClass()->GetName(), PropertyName);
			}
			return 0;
		}
		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(Owner));
		int32 Removed = 0;
		for (int32 i = Array.Num() - 1; i >= 0; --i)
		{
			const UObject* Message = Inner->GetObjectPropertyValue(Array.GetRawPtr(i));
			if (Message && Message->GetTypedOuter<UBBPAdaEasterEggs>())
			{
				Array.RemoveValues(i, 1);
				++Removed;
			}
		}
		return Removed;
	}

	constexpr float DuckLevel = 0.2f;
	constexpr float DuckFadeSeconds = 0.3f;

	bool ContainsAny(const FString& Text, std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* Word : Words)
		{
			if (Text.Contains(Word, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
}

void UBBPAdaEasterEggs::Initialize(UBBPPlaybackController* InController)
{
	Controller = InController;
	LastLineTime = FPlatformTime::Seconds() - MinGapSeconds + FirstLineDelaySeconds;
	Instances.RemoveAll([](const TWeakObjectPtr<UBBPAdaEasterEggs>& Instance) { return !Instance.IsValid(); });
	Instances.Add(this);
	LoadLines();
	LoadBank();
}

void UBBPAdaEasterEggs::LoadBank()
{
	if (bBankTried || AudioDir.IsEmpty())
	{
		return;
	}
	bBankTried = true;
	const FBBPWwiseCalls& Wwise = GetWwiseCalls();
	const FString Path = AudioDir / BankFileName;
	TArray<uint8> Bytes;
	if (!Wwise.IsComplete() || !FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: %s; lines play through Unreal"),
			Wwise.IsComplete() ? *FString::Printf(TEXT("no Wwise bank at '%s'"), *Path) : TEXT("the game's Wwise functions weren't found"));
		return;
	}
	// The bank's header comes from one of the game's per-event banks (type 30), which Wwise only loads through the
	// game's own asset system; marked as a hand-loaded bank it loads like any other.
	if (Bytes.Num() > BankTypeOffset + 4 && FMemory::Memcmp(Bytes.GetData(), "BKHD", 4) == 0)
	{
		uint32 HeaderType = 0;
		FMemory::Memcpy(&HeaderType, Bytes.GetData() + BankTypeOffset, sizeof(HeaderType));
		if (HeaderType != UserBankType)
		{
			FMemory::Memcpy(Bytes.GetData() + BankTypeOffset, &UserBankType, sizeof(UserBankType));
			UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: bank header type %u changed to %u (user bank) before loading"), HeaderType, UserBankType);
		}
	}
	uint32 BankId = 0;
	uint32 BankType = 0;
	const int32 Result = Wwise.LoadBankMemoryCopyTyped ? Wwise.LoadBankMemoryCopyTyped(Bytes.GetData(), Bytes.Num(), BankId, BankType)
		: Wwise.LoadBankMemoryCopy(Bytes.GetData(), Bytes.Num(), BankId);
	// AK_Success is 1, AK_BankAlreadyLoaded 21.
	bBankLoaded = Result == 1 || Result == 21;
	if (bBankLoaded)
	{
		Wwise.RegisterGameObj(VoiceGameObject, "BBP_ADA");
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: Wwise bank '%s' (%d bytes) %s (result %d, bank %u, type %u)"), BankFileName, Bytes.Num(),
		bBankLoaded ? TEXT("loaded; lines play on ADA's voice bus") : TEXT("FAILED to load; lines play through Unreal"), Result, BankId, BankType);
}

bool UBBPAdaEasterEggs::PostWwiseVoice()
{
	if (!bBankLoaded)
	{
		return false;
	}
	const FString EventName = EventPrefix + LineName;
	const uint32 PlayingId = GetWwiseCalls().PostEvent(WwiseId(EventName), VoiceGameObject, 0, nullptr, nullptr, 0, nullptr, 0);
	if (PlayingId == 0)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: Wwise wouldn't play '%s'; playing it through Unreal"), *EventName);
		return false;
	}
	WwisePlayingId = PlayingId;
	return true;
}

void UBBPAdaEasterEggs::Shutdown()
{
	StopLine();
	Instances.RemoveAll([this](const TWeakObjectPtr<UBBPAdaEasterEggs>& Instance) { return !Instance.IsValid() || Instance.Get() == this; });
}

void UBBPAdaEasterEggs::LoadLines()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BoomBoxPlus"));
	if (!Plugin)
	{
		return;
	}
	AudioDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / TEXT("Audio") / TEXT("ADA"));
	FString Json;
	TArray<TSharedPtr<FJsonValue>> Lines;
	if (!FFileHelper::LoadFileToString(Json, *(AudioDir / TEXT("lines.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Lines))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: no lines in '%s'; easter eggs won't play"), *AudioDir);
		return;
	}
	for (const TSharedPtr<FJsonValue>& Value : Lines)
	{
		const TSharedPtr<FJsonObject>* Line = nullptr;
		FString Name;
		FString Text;
		if (Value->TryGetObject(Line) && (*Line)->TryGetStringField(TEXT("name"), Name) && (*Line)->TryGetStringField(TEXT("subtitle"), Text))
		{
			Subtitles.Add(Name, Text);
		}
	}
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: %d easter-egg lines"), Subtitles.Num());
}

TArray<FString> UBBPAdaEasterEggs::GetLineNames() const
{
	TArray<FString> Names;
	Subtitles.GetKeys(Names);
	Names.Sort();
	return Names;
}

UFGGameUI* UBBPAdaEasterEggs::GetGameUI() const
{
	const APlayerController* PlayerController = Controller ? UGameplayStatics::GetPlayerController(Controller, 0) : nullptr;
	const AFGHUD* Hud = PlayerController ? Cast<AFGHUD>(PlayerController->GetHUD()) : nullptr;
	return Hud ? Hud->GetGameUI() : nullptr;
}

TSubclassOf<UFGMessageSender> UBBPAdaEasterEggs::GetAdaSender()
{
	if (!AdaSender)
	{
		AdaSender = TSoftClassPtr<UFGMessageSender>(FSoftObjectPath(AdaSenderPath)).LoadSynchronous();
		if (!AdaSender)
		{
			UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: can't load ADA's message sender '%s'"), AdaSenderPath);
		}
	}
	return AdaSender;
}

void UBBPAdaEasterEggs::Tick(float DeltaSeconds)
{
	const double Now = FPlatformTime::Seconds();
	if (PendingMessage && Now - PendingSince > BoxTimeoutSeconds)
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: the message box didn't start '%s' within %.0f s; playing it with the mod's own subtitle"), *LineName, BoxTimeoutSeconds);
		StartVoice(true, true);
	}
	if (ShownBox.IsValid() || ShownBoxSince > 0.0)
	{
		UFGGameUI* GameUI = GetGameUI();
		if (!ShownBox.IsValid() || !GameUI || GameUI->GetActiveAudioMessage() != ShownBox.Get())
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: message box closed after %.1f s (voice clip %.1f s)"), Now - ShownBoxSince, LineDuration);
			ShownBox = nullptr;
			ShownBoxSince = 0.0;
			if (IsSpeaking() && LineTimeLeft > SkipDetectSeconds)
			{
				UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: message skipped; stopping '%s'"), *LineName);
				StopLine();
			}
		}
	}
	if (Wave)
	{
		if (VoiceDelay > 0.f)
		{
			VoiceDelay -= DeltaSeconds;
			if (VoiceDelay <= 0.f && Component)
			{
				Component->Play();
			}
		}
		else
		{
			LineTimeLeft -= DeltaSeconds;
		}
		if (Component)
		{
			Component->SetVolumeMultiplier(Controller && !Controller->IsMutedInBackground() ? Controller->GetVoiceVolumeScale() * VoiceGain : 0.f);
		}
		if (VoiceDelay <= 0.f && (LineTimeLeft <= 0.f || Wave->IsFinished() || Wave->HasFailed()))
		{
			StopLine();
		}
	}
	if (WwisePlayingId != 0)
	{
		LineTimeLeft -= DeltaSeconds;
		if (LineTimeLeft <= 0.f)
		{
			WwisePlayingId = 0;
			Subtitle.Reset();
		}
	}
	const float DuckTarget = IsSpeaking() ? DuckLevel : 1.f;
	MusicDuck = FMath::FInterpConstantTo(MusicDuck, DuckTarget, DeltaSeconds, (1.f - DuckLevel) / DuckFadeSeconds);

	PlayedCleanupTimer -= DeltaSeconds;
	if (PlayedCleanupTimer <= 0.f)
	{
		PlayedCleanupTimer = PlayedCleanupInterval;
		RemoveFromPlayedMessages();
	}

	if (!Controller || Subtitles.Num() == 0)
	{
		return;
	}
	// Every line is about the Boom Box the local player is holding; linked Boom Boxes share a channel, so each player
	// holding one of them hears the line on their own game.
	const AFGBoomBoxPlayer* Held = GetHeldBoomBox();
	const ABBPMusicChannel* Channel = Held ? Controller->GetPlaylist()->FindChannel(Held) : nullptr;
	if (Channel && !Channel->IsPlaying())
	{
		Channel = nullptr;
	}
	CheckIdle(DeltaSeconds, Channel);
	CheckTrack(Channel);
	VolumeCheckTimer -= DeltaSeconds;
	if (VolumeCheckTimer <= 0.f)
	{
		VolumeCheckTimer = VolumeCheckInterval;
		CheckVolume(Held);
	}
}

void UBBPAdaEasterEggs::RemoveFromPlayedMessages()
{
	if (!Controller)
	{
		return;
	}
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Controller, 0);
	int32 Removed = RemoveEasterEggMessages(PlayerController ? PlayerController->GetPlayerState<AFGPlayerState>() : nullptr, TEXT("mPlayedMessages"));
	if (Controller->GetWorld() && Controller->GetWorld()->GetNetMode() != NM_Client)
	{
		Removed += RemoveEasterEggMessages(AFGStorySubsystem::Get(Controller->GetWorld()), TEXT("mAllPlayedMessages"));
	}
	if (Removed > 0)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: took %d easter-egg line(s) out of the game's saved played messages"), Removed);
	}
}

void UBBPAdaEasterEggs::NotifyMusicPageOpened()
{
	if (FMath::RandHelper(AiOneIn) == 0)
	{
		TryPlay(TEXT("ai"));
	}
}

bool UBBPAdaEasterEggs::PlayNow(const FString& Name)
{
	return TryPlay(Name, FString(), true, true);
}

void UBBPAdaEasterEggs::NotifyAudioMessageStarted(UFGAudioMessage* AudioMessage)
{
	const UFGMessage* Message = AudioMessage ? AudioMessage->GetMessage() : nullptr;
	if (!Message)
	{
		return;
	}
	for (const TWeakObjectPtr<UBBPAdaEasterEggs>& Instance : Instances)
	{
		UBBPAdaEasterEggs* Eggs = Instance.Get();
		if (Eggs && Eggs->PendingMessage == Message)
		{
			UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: message box started '%s' %.1f s after queuing; starting the voice"), *Eggs->LineName, FPlatformTime::Seconds() - Eggs->PendingSince);
			Eggs->ShownBox = AudioMessage;
			Eggs->ShownBoxSince = FPlatformTime::Seconds();
			Eggs->StartVoice(false, false);
			return;
		}
	}
}

bool UBBPAdaEasterEggs::TryPlay(const FString& Name, const FString& SubtitleText, bool bIgnoreGap, bool bForce)
{
	const double Now = FPlatformTime::Seconds();
	const FString* Text = Subtitles.Find(Name);
	UFGGameUI* GameUI = GetGameUI();
	const TCHAR* Refusal = !Controller ? TEXT("no playback controller")
		: !Text ? TEXT("no such line")
		: !UBBPConfig::GetBool(Controller, UBBPConfig::AdaEasterEggsKey, true) ? TEXT("ADA easter eggs are off")
		: (!bForce && Played.Contains(Name)) ? TEXT("already played this session")
		: (IsSpeaking() || PendingMessage) ? TEXT("another easter-egg line is playing")
		: Controller->IsMutedInBackground() ? TEXT("the game is in the background")
		: (!bForce && !bIgnoreGap && Now - LastLineTime < MinGapSeconds) ? TEXT("too soon after the last line")
		// ADA is already talking (one of the game's own messages): this line waits for another chance.
		: (GameUI && GameUI->GetActiveAudioMessage()) ? TEXT("one of the game's own messages is playing")
		: nullptr;
	if (Refusal)
	{
		// Triggers such as idle ask every tick while they hold; each refusal is logged once until it changes.
		const FString Reason = Name + TEXT(": ") + Refusal;
		if (Reason != LastRefusal)
		{
			LastRefusal = Reason;
			UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: '%s' triggered but not played (%s)"), *Name, Refusal);
		}
		return false;
	}
	LastRefusal.Reset();
	const FString Path = AudioDir / (Name + TEXT(".ogg"));
	TArray<uint8> Bytes;
	FBBPDecoder Probe;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path) || !Probe.Open(MoveTemp(Bytes)))
	{
		UE_LOG(LogBoomBoxPlus, Warning, TEXT("ADA: can't open '%s'"), *Path);
		Played.Add(Name);
		return false;
	}
	LineName = Name;
	LineText = SubtitleText.IsEmpty() ? *Text : SubtitleText;
	LinePath = Path;
	LineSampleRate = Probe.GetSampleRate();
	LineChannels = Probe.GetChannels();
	LineDuration = Probe.GetDurationSeconds();
	Probe.Close();
	Played.Add(Name);
	LastLineTime = Now;

	const TSubclassOf<UFGMessageSender> Sender = GetAdaSender();
	if (!GameUI || !Sender)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: no game message box; playing '%s' with the mod's own subtitle"), *Name);
		StartVoice(true, Sender != nullptr);
		return true;
	}

	// A legacy-style message: its dialogue has no Wwise audio, and the subtitle stays up for the clip's length.
	const FText Line = FText::FromString(LineText);
	FMessageSubtitle MessageSubtitle(Line, Sender, 0.f, nullptr);
	MessageSubtitle.TimeOnScreen = LineDuration + BoxExtraSeconds;
	FMessageDialogue Dialogue;
	Dialogue.MessageSubtitles = { MessageSubtitle };
	UFGMessage* Message = NewObject<UFGMessage>(this);
	Message->mTitle = FText::FromString(TEXT("ADA"));
	Message->mMessage = Line;
	Message->mSubtitles = { MessageSubtitle };
	Message->mMessageSharing = EMessageSharing::EMS_NeverShare;
	Message->mCanBeQueued = true;
	Message->mCanBeRepeated = true;
	Message->mCanInterrupt = false;
	Message->mPlayInterruptMessage = false;
	Message->mBlockedByCooldown = false;
	Message->mIsLegacyMessage = true;
	Message->mSenderClass = Sender.Get();
	Message->mDialogueData = { Dialogue };
	Message->mSubtitleTimeMultiplier = (LineDuration + BoxExtraSeconds) / FMath::Max(1, LineText.Len());
	PendingMessage = Message;
	PendingSince = Now;
	GameUI->AddPendingMessages({ Message });
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: queued '%s' (%.1f s) in the game's message box"), *Name, LineDuration);
	return true;
}

void UBBPAdaEasterEggs::StartVoice(bool bOwnSubtitle, bool bJingle)
{
	PendingMessage = nullptr;
	if (!bJingle && PostWwiseVoice())
	{
		LineTimeLeft = LineDuration + 0.5f;
		Subtitle = bOwnSubtitle ? LineText : FString();
		UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: playing '%s' on ADA's Wwise voice bus (%.1f s)"), *LineName, LineDuration);
		return;
	}
	Wave = NewObject<UBBPStreamingSoundWave>(this);
	if (!Wave->StartStream(LinePath, LineSampleRate, LineChannels, 0.f))
	{
		Wave = nullptr;
		return;
	}
	Component = UGameplayStatics::CreateSound2D(Controller, Wave, Controller->GetVoiceVolumeScale() * VoiceGain, 1.f, 0.f, nullptr, false, false);
	if (!Component)
	{
		StopLine();
		return;
	}
	VoiceDelay = 0.f;
	if (bJingle)
	{
		const UFGMessageSender* SenderDefaults = AdaSender ? AdaSender->GetDefaultObject<UFGMessageSender>() : nullptr;
		if (SenderDefaults && SenderDefaults->mMessageJingle)
		{
			const APawn* Pawn = UGameplayStatics::GetPlayerPawn(Controller, 0);
			SenderDefaults->mMessageJingle->PostOnActor(Pawn, FOnAkPostEventCallback(), 0, false);
			VoiceDelay = JingleSeconds;
		}
	}
	if (VoiceDelay <= 0.f)
	{
		Component->Play();
	}
	LineTimeLeft = LineDuration + 0.5f;
	Subtitle = bOwnSubtitle ? LineText : FString();
	UE_LOG(LogBoomBoxPlus, Log, TEXT("ADA: playing '%s' (%.1f s%s)"), *LineName, LineDuration, bOwnSubtitle ? TEXT(", own subtitle") : TEXT(""));
}

void UBBPAdaEasterEggs::StopLine()
{
	PendingMessage = nullptr;
	if (WwisePlayingId != 0)
	{
		// AkCurveInterpolation_Linear is 4.
		GetWwiseCalls().StopPlayingID(WwisePlayingId, 200, 4);
		WwisePlayingId = 0;
	}
	if (Component)
	{
		Component->Stop();
		Component->DestroyComponent();
		Component = nullptr;
	}
	if (Wave)
	{
		Wave->StopStream();
		Wave = nullptr;
	}
	Subtitle.Reset();
	LineTimeLeft = 0.f;
	VoiceDelay = 0.f;
}

void UBBPAdaEasterEggs::CheckIdle(float DeltaSeconds, const ABBPMusicChannel* Channel)
{
	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Controller, 0);
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	// Menus show the mouse cursor; time spent in one isn't idling.
	if (!Pawn || !Channel || PlayerController->ShouldShowMouseCursor())
	{
		IdleSeconds = 0.f;
		bIdleSaidThisStretch = false;
		if (Pawn)
		{
			IdleLocation = Pawn->GetActorLocation();
			IdleRotation = PlayerController->GetControlRotation();
		}
		return;
	}
	const FRotator Rotation = PlayerController->GetControlRotation();
	const bool bMoved = FVector::Dist(Pawn->GetActorLocation(), IdleLocation) > IdleMoveTolerance
		|| !Rotation.Equals(IdleRotation, IdleTurnToleranceDegrees);
	if (bMoved)
	{
		IdleSeconds = 0.f;
		bIdleSaidThisStretch = false;
		IdleLocation = Pawn->GetActorLocation();
		IdleRotation = Rotation;
		return;
	}
	IdleSeconds += DeltaSeconds;
	if (IdleSeconds >= IdleSeconds1 && !bIdleSaidThisStretch && TryPlay(TEXT("idle")))
	{
		bIdleSaidThisStretch = true;
	}
	if (IdleSeconds >= IdleSeconds2 && bIdleSaidThisStretch)
	{
		TryPlay(TEXT("idle2"), FString(), true);
	}
}

void UBBPAdaEasterEggs::CheckTrack(const ABBPMusicChannel* Channel)
{
	FBBPQueueEntry Entry;
	// A moment with nothing heard (the next song still loading, a station reconnecting) keeps the count going.
	if (!Channel || !Channel->GetCurrentEntry(Entry))
	{
		return;
	}
	const float Position = Channel->GetPlaybackPosition();
	const bool bNewChannel = TrackChannel.Get() != Channel;
	const bool bStarted = bNewChannel || Entry.EntryId != TrackEntryId || Position < LastTrackPosition - RestartJumpSeconds;
	LastTrackPosition = Position;
	if (!bStarted)
	{
		return;
	}
	TrackChannel = Channel;
	TrackEntryId = Entry.EntryId;
	const FBBPTrack& Track = Entry.Track;
	ConsecutivePlays = (!bNewChannel && Track.Id == LastTrackId) ? ConsecutivePlays + 1 : 1;
	LastTrackId = Track.Id;

	if (ConsecutivePlays == LoopPlays1 && TryPlay(TEXT("loop")))
	{
		return;
	}
	if (ConsecutivePlays == LoopPlays2 && TryPlay(TEXT("loop2")))
	{
		return;
	}
	const FString About = Track.Title + TEXT(" | ") + Track.Artist + TEXT(" | ") + Track.Uploader;
	if ((Track.Id == TEXT("yt:dQw4w9WgXcQ") || Track.Title.Contains(TEXT("never gonna give you up"), ESearchCase::IgnoreCase)) && TryPlay(TEXT("rickroll")))
	{
		return;
	}
	if ((ContainsAny(Track.Artist + Track.Uploader, { TEXT("coffee stain") }) || Track.Title.Contains(TEXT("satisfactory"), ESearchCase::IgnoreCase)) && TryPlay(TEXT("ost")))
	{
		return;
	}
	if (ContainsAny(About, { TEXT("metal"), TEXT("doom"), TEXT("thrash"), TEXT("deathcore") }) && TryPlay(TEXT("metal")))
	{
		return;
	}
	if (ContainsAny(About, { TEXT("lofi"), TEXT("lo-fi"), TEXT("lo fi"), TEXT("chillhop"), TEXT("beats to relax") }) && TryPlay(TEXT("lofi")))
	{
		return;
	}
	const bool bOfficialMusicChannel = Track.Uploader.EndsWith(TEXT("VEVO"), ESearchCase::IgnoreCase) || Track.Uploader.EndsWith(TEXT(" - Topic"), ESearchCase::IgnoreCase);
	if (bOfficialMusicChannel && FMath::FRand() < CopyrightChance)
	{
		TryPlay(TEXT("copyright"));
	}
}

AFGBoomBoxPlayer* UBBPAdaEasterEggs::GetHeldBoomBox() const
{
	const ABBPPlaylistSubsystem* Playlist = Controller->GetPlaylist();
	const APawn* Pawn = UGameplayStatics::GetPlayerPawn(Controller, 0);
	if (!Playlist || !Pawn)
	{
		return nullptr;
	}
	for (const FBBPActiveBoomBox& Active : Playlist->GetActiveBoomBoxes())
	{
		if (IsValid(Active.BoomBox) && Active.BoomBox->IsInEquipmentMode() && Active.BoomBox->GetmOwningCharacter() == Pawn)
		{
			return Active.BoomBox;
		}
	}
	return nullptr;
}

void UBBPAdaEasterEggs::CheckVolume(const AFGBoomBoxPlayer* Held)
{
	const ABBPPlaylistSubsystem* Playlist = Controller->GetPlaylist();
	const UWorld* World = Controller->GetWorld();
	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(Controller, 0);
	const APawn* LocalPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!Held || !Playlist || !LocalPawn || !GameState || Played.Contains(TEXT("volume")))
	{
		return;
	}
	const FBBPActiveBoomBox* Active = Playlist->GetActiveBoomBoxes().FindByPredicate([Held](const FBBPActiveBoomBox& A) { return A.BoomBox == Held; });
	if (!Active || Active->Volume < 0.99f)
	{
		return;
	}
	const FVector Location = Held->GetActorLocation();
	const bool bNeighbour = GameState->PlayerArray.ContainsByPredicate([LocalPawn, &Location](const APlayerState* PlayerState)
	{
		const APawn* Other = PlayerState ? PlayerState->GetPawn() : nullptr;
		return Other && Other != LocalPawn && FVector::Dist(Other->GetActorLocation(), Location) <= VolumeNeighbourDistance;
	});
	if (!bNeighbour)
	{
		return;
	}
	const APlayerState* LocalState = PlayerController->PlayerState;
	FString Text = Subtitles.FindRef(TEXT("volume"));
	Text.ReplaceInline(TEXT("Pioneer {name}"), LocalState ? *(TEXT("Pioneer ") + LocalState->GetPlayerName()) : TEXT("A Pioneer"));
	TryPlay(TEXT("volume"), Text);
}
