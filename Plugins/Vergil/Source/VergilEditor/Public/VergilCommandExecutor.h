#pragma once

#include "CoreMinimal.h"

#include "VergilCompilerTypes.h"

class UBlueprint;

class VERGILEDITOR_API FVergilCommandExecutor final
{
public:
	bool Execute(
		UBlueprint* Blueprint,
		const TArray<FVergilCompilerCommand>& Commands,
		TArray<FVergilDiagnostic>& Diagnostics,
		int32* OutExecutedCommandCount = nullptr,
		FVergilTransactionAudit* OutTransactionAudit = nullptr) const;
};
