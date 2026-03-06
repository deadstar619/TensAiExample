#pragma once

#include "CoreMinimal.h"

struct FVergilCompileRequest;
struct FVergilCompileResult;
class FVergilCompilerContext;

class VERGILBLUEPRINTCOMPILER_API IVergilCompilerPass
{
public:
	virtual ~IVergilCompilerPass() = default;

	virtual FName GetPassName() const = 0;
	virtual bool Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const = 0;
};
