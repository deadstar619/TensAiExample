#pragma once

#include "CoreMinimal.h"
#include "VergilCompilerTypes.h"

class VERGILBLUEPRINTCOMPILER_API FVergilBlueprintCompilerService final
{
public:
	FVergilCompileResult Compile(const FVergilCompileRequest& Request) const;
};
