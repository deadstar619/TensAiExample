#pragma once

#include "CoreMinimal.h"
#include "VergilPermissionTypes.generated.h"

UENUM(BlueprintType)
enum class EVergilAgentWritePermissionPolicy : uint8
{
	AllowAll,
	RequireExplicitApproval,
	DenyAll
};
