#include "Hooks/BBPHooks.h"
#include "BBPBlueprintLibrary.h"
#include "BoomBoxPlus.h"
#include "FGBoomBoxPlayer.h"
#include "FGBoomboxListenerInterface.h"
#include "FGUnlockSubsystem.h"
#include "Patching/NativeHookManager.h"
#include "Playback/BBPPlaybackController.h"
#include "Playlist/BBPMusicChannel.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Tape/BBPCustomMusicTape.h"
#include "UI/BBPMusicPage.h"

namespace
{
	enum class EBBPTransportAction : uint8
	{
		Play,
		Stop,
		Toggle,
		Next,
		Previous
	};

	const TCHAR* ToString(EBBPTransportAction Action)
	{
		switch (Action)
		{
		case EBBPTransportAction::Play: return TEXT("Play");
		case EBBPTransportAction::Stop: return TEXT("Stop");
		case EBBPTransportAction::Toggle: return TEXT("Toggle");
		case EBBPTransportAction::Next: return TEXT("Next");
		case EBBPTransportAction::Previous: return TEXT("Previous");
		default: return TEXT("?");
		}
	}

	// Sends a Boom Box button press to its channel: directly on the server, through the RCO on a client.
	void RouteTransport(AFGBoomBoxPlayer* BoomBox, EBBPTransportAction Action)
	{
		if (BoomBox->HasAuthority())
		{
			ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(BoomBox);
			ABBPMusicChannel* Channel = Playlist ? Playlist->GetOrCreateChannel(BoomBox) : nullptr;
			if (!Channel)
			{
				UE_LOG(LogBoomBoxPlus, Warning, TEXT("Hook: %s on server but no channel for %s (subsystem %d)"), ToString(Action), *GetNameSafe(BoomBox), Playlist != nullptr);
				return;
			}
			switch (Action)
			{
			case EBBPTransportAction::Play: Channel->SetPlaying(true); break;
			case EBBPTransportAction::Stop: Channel->SetPlaying(false); break;
			case EBBPTransportAction::Toggle: Channel->SetPlaying(!Channel->IsPlaying()); break;
			case EBBPTransportAction::Next: Channel->Skip(); break;
			case EBBPTransportAction::Previous: Channel->Previous(); break;
			}
			return;
		}
		switch (Action)
		{
		case EBBPTransportAction::Play: UBBPBlueprintLibrary::RequestSetPlaying(BoomBox, true); break;
		case EBBPTransportAction::Stop: UBBPBlueprintLibrary::RequestSetPlaying(BoomBox, false); break;
		case EBBPTransportAction::Toggle: UBBPBlueprintLibrary::RequestTogglePlaying(BoomBox); break;
		case EBBPTransportAction::Next: UBBPBlueprintLibrary::RequestSkip(BoomBox); break;
		case EBBPTransportAction::Previous: UBBPBlueprintLibrary::RequestPrevious(BoomBox); break;
		}
	}

	// Returns true if BoomBox is playing Custom Music, logging the intercepted call.
	bool IsCustomMusic(AFGBoomBoxPlayer* BoomBox, const TCHAR* FunctionName)
	{
		if (!BoomBox || !UBBPCustomMusicTape::IsCustomMusicTape(BoomBox->GetCurrentTape()))
		{
			return false;
		}
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook: %s on %s (%s) intercepted"), FunctionName, *GetNameSafe(BoomBox),
			BoomBox->HasAuthority() ? TEXT("server") : TEXT("client"));
		return true;
	}
}

void RegisterBBPHooks()
{
#if WITH_EDITOR
	UE_LOG(LogBoomBoxPlus, Log, TEXT("Editor build: native hooks not installed"));
#else
	InstallBBPHooks();
#endif
}

void InstallBBPHooks()
{
	// Adds the Custom Music tape to every unlocked-tapes query without saving it as an unlock.
	SUBSCRIBE_METHOD_AFTER(AFGUnlockSubsystem::GetUnlockedTapes, [](const AFGUnlockSubsystem* Self, TArray<TSubclassOf<UFGTapeData>>& OutTapes)
	{
		static bool bLoggedFirstCall = false;
		if (!bLoggedFirstCall)
		{
			bLoggedFirstCall = true;
			UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook: GetUnlockedTapes fired for the first time (%d vanilla tapes); adding Custom Music"), OutTapes.Num());
		}
		OutTapes.AddUnique(UBBPCustomMusicTape::StaticClass());
	});

	// Button presses: replace vanilla Wwise playback with the Boom Box's channel while Custom Music is loaded.
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::BeginPlaySequence, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Instigator)
	{
		if (IsCustomMusic(Self, TEXT("BeginPlaySequence")))
		{
			Scope.Cancel();
			RouteTransport(Self, EBBPTransportAction::Play);
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::BeginStopSequence, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Instigator)
	{
		if (IsCustomMusic(Self, TEXT("BeginStopSequence")))
		{
			Scope.Cancel();
			RouteTransport(Self, EBBPTransportAction::Stop);
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::BeginNextSongSequence, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Instigator)
	{
		if (IsCustomMusic(Self, TEXT("BeginNextSongSequence")))
		{
			Scope.Cancel();
			RouteTransport(Self, EBBPTransportAction::Next);
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::BeginPreviousSongSequence, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Instigator)
	{
		if (IsCustomMusic(Self, TEXT("BeginPreviousSongSequence")))
		{
			Scope.Cancel();
			RouteTransport(Self, EBBPTransportAction::Previous);
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::TogglePlaybackNow, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		if (IsCustomMusic(Self, TEXT("TogglePlaybackNow")))
		{
			Scope.Cancel();
			RouteTransport(Self, EBBPTransportAction::Toggle);
		}
	});

	// Direct playback calls: block Wwise playback of the empty Custom Music playlist, without acting a second time.
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::PlayNow, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		if (IsCustomMusic(Self, TEXT("PlayNow")))
		{
			Scope.Cancel();
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::StopNow, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		if (IsCustomMusic(Self, TEXT("StopNow")))
		{
			Scope.Cancel();
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::NextNow, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		if (IsCustomMusic(Self, TEXT("NextNow")))
		{
			Scope.Cancel();
		}
	});
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::PrevNow, [](auto& Scope, AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		if (IsCustomMusic(Self, TEXT("PrevNow")))
		{
			Scope.Cancel();
		}
	});

	// Brings the Custom Music page forward in open Boom Box windows when Custom Music is chosen.
	SUBSCRIBE_METHOD_AFTER(AFGBoomBoxPlayer::BeginChangeTapeSequence, [](AFGBoomBoxPlayer* Self, TSubclassOf<UFGTapeData> NewTape, AFGCharacterPlayer* Instigator)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook: BeginChangeTapeSequence on %s (%s) to %s"), *GetNameSafe(Self),
			Self->HasAuthority() ? TEXT("server") : TEXT("client"), *GetNameSafe(NewTape.Get()));
		UBBPMusicPage::NotifyTapeChanged(Self, NewTape);
	});

	// Gives the game a valid song for the Custom Music tape (whose playlist is empty): the current track, or a placeholder.
	SUBSCRIBE_METHOD(AFGBoomBoxPlayer::GetCurrentSong, [](auto& Scope, AFGBoomBoxPlayer* Self)
	{
		if (!Self || !UBBPCustomMusicTape::IsCustomMusicTape(Self->GetCurrentTape()))
		{
			return;
		}
		const ABBPPlaylistSubsystem* Playlist = ABBPPlaylistSubsystem::Get(Self);
		FBBPVanillaSong Info;
		FSongData Song;
		if (UBBPPlaybackController::DescribeForVanillaPage(ABBPPlaylistSubsystem::FindChannelFor(Self), Playlist ? Playlist->GetPlaybackController() : nullptr, Info))
		{
			Song.SongName = FName(*Info.Title.Left(NAME_SIZE - 1));
			Song.ArtistName = FName(*Info.Artist.Left(NAME_SIZE - 1));
			Song.CachedMaximumSongDuration = Info.Duration;
		}
		else
		{
			Song.SongName = FName(TEXT("Custom Music"));
			Song.ArtistName = FName(TEXT("BoomBoxPlus"));
		}
		Scope.Override(Song);
	});

	// Observes tape changes so the in-game flow can be followed in the log.
	SUBSCRIBE_METHOD_AFTER(AFGBoomBoxPlayer::LoadTapeNow, [](AFGBoomBoxPlayer* Self, AFGCharacterPlayer* Character)
	{
		UE_LOG(LogBoomBoxPlus, Log, TEXT("Hook: LoadTapeNow on %s (%s), tape is now %s"), *GetNameSafe(Self),
			Self->HasAuthority() ? TEXT("server") : TEXT("client"), *GetNameSafe(Self->GetCurrentTape().Get()));
	});

	UE_LOG(LogBoomBoxPlus, Log, TEXT("Hooks installed: GetUnlockedTapes, Boom Box transport (Begin*/Toggle/*Now), BeginChangeTapeSequence, GetCurrentSong, LoadTapeNow"));
}
