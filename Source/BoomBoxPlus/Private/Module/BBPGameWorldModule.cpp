#include "Module/BBPGameWorldModule.h"
#include "Playback/BBPAdaChatCommand.h"
#include "Playlist/BBPPlaylistSubsystem.h"
#include "Playlist/BBPBoomBoxChatCommand.h"

UBBPGameWorldModule::UBBPGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(ABBPPlaylistSubsystem::StaticClass());
	mChatCommands.Add(ABBPAdaChatCommand::StaticClass());
	mChatCommands.Add(ABBPBoomBoxChatCommand::StaticClass());
}
