#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "BBPBoomBoxChatCommand.generated.h"

// "/bbp list" lists the Custom Music Boom Boxes the player's own game knows: what each streams there, or that it's out
// of range. "/bbp stopall" pauses every Custom Music Boom Box in the world (their queues stay); only the host can run
// it, or the server console on a dedicated server. Chat commands run on the server, so a remote player's list comes
// from their own game (UBBPRemoteCallObject::Client_ListStreams).
UCLASS()
class BOOMBOXPLUS_API ABBPBoomBoxChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ABBPBoomBoxChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

private:
	EExecutionStatus StopAll(UCommandSender* Sender);
	EExecutionStatus List(UCommandSender* Sender);
};
