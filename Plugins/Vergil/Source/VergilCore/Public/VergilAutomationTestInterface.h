#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VergilAutomationTestInterface.generated.h"

UINTERFACE(BlueprintType)
class VERGILCORE_API UVergilAutomationTestInterface : public UInterface
{
	GENERATED_BODY()
};

class VERGILCORE_API IVergilAutomationTestInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintPure = false, BlueprintNativeEvent, Category = "Vergil")
	bool VergilAutomationInterfacePing() const;
};
