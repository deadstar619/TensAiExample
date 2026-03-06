#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VergilAutomationTestInterface.generated.h"

UINTERFACE(BlueprintType)
class VERGILAUTOMATION_API UVergilAutomationTestInterface : public UInterface
{
	GENERATED_BODY()
};

class VERGILAUTOMATION_API IVergilAutomationTestInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Vergil")
	bool VergilAutomationInterfacePing() const;
};
