#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "BBPStopAllChatCommand.generated.h"

// "/bbp stopall" pauses every Custom Music Boom Box in the world (their queues stay). Only the host can run it, or the
// server console on a dedicated server.
UCLASS()
class BOOMBOXPLUS_API ABBPStopAllChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ABBPStopAllChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};
