#pragma once

#include "CoreMinimal.h"
#include "VergilCompilerTypes.h"

class UBlueprint;

namespace VergilEditor
{
	VERGILEDITOR_API FString NormalizeBlueprintReference(const FString& BlueprintPath);
	VERGILEDITOR_API UBlueprint* ResolveBlueprintFromReference(const FString& BlueprintPath);
	VERGILEDITOR_API FVergilCompileResult PrepareCommandPlanForExecution(
		const TArray<FVergilCompilerCommand>& Commands,
		bool bInferTargetGraphName = true);
}
