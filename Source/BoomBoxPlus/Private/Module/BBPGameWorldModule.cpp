#include "Module/BBPGameWorldModule.h"
#include "Playlist/BBPPlaylistSubsystem.h"

UBBPGameWorldModule::UBBPGameWorldModule()
{
	bRootModule = true;
	ModSubsystems.Add(ABBPPlaylistSubsystem::StaticClass());
}
