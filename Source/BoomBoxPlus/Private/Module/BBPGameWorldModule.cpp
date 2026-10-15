#include "Module/BBPGameWorldModule.h"
#include "Playback/BBPAdaChatCommand.h"
#include "Playlist/BBPPlaylistSubsystem.h"

UBBPGameWorldModule::UBBPGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(ABBPPlaylistSubsystem::StaticClass());
	mChatCommands.Add(ABBPAdaChatCommand::StaticClass());
}
