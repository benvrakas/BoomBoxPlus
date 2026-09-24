#include "Module/BBPGameInstanceModule.h"
#include "Network/BBPRemoteCallObject.h"

UBBPGameInstanceModule::UBBPGameInstanceModule()
{
	bRootModule = true;
	RemoteCallObjects.Add(UBBPRemoteCallObject::StaticClass());
}
