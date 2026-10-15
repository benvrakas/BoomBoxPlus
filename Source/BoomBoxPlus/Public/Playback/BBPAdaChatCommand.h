#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "BBPAdaChatCommand.generated.h"

// "/ada <line>" plays an ADA easter-egg line now, "/ada list" lists them, "/ada volume <0-10>" sets her voice volume for
// this session. Chat commands run on the server, so a remote player's command is sent on to that player's game
// (UBBPRemoteCallObject::Client_AdaCommand) and affects only their game, like the host's affects only the host's.
UCLASS()
class BOOMBOXPLUS_API ABBPAdaChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ABBPAdaChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;

	// Runs the command on this machine's game; sets the reply for the player and its colour.
	static EExecutionStatus RunLocally(const UObject* WorldContext, const TArray<FString>& Arguments, FString& OutReply, FLinearColor& OutColor);
};
