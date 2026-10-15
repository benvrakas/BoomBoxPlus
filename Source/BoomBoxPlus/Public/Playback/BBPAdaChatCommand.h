#pragma once

#include "CoreMinimal.h"
#include "Command/ChatCommandInstance.h"
#include "BBPAdaChatCommand.generated.h"

// "/ada <line>" plays an ADA easter-egg line now, "/ada list" lists them, "/ada volume <0-4>" sets her voice volume for
// this session. Runs where the command is handled (the host), so it only affects the host's own game.
UCLASS()
class BOOMBOXPLUS_API ABBPAdaChatCommand : public AChatCommandInstance
{
	GENERATED_BODY()

public:
	ABBPAdaChatCommand();

	virtual EExecutionStatus ExecuteCommand_Implementation(UCommandSender* Sender, const TArray<FString>& Arguments, const FString& Label) override;
};
