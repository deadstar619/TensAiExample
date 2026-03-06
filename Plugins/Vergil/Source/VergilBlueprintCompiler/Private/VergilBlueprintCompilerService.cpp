#include "VergilBlueprintCompilerService.h"

#include "Algo/AnyOf.h"

#include "VergilCompilerPasses.h"
#include "VergilCompilerTypes.h"
#include "VergilLog.h"

FVergilCompileResult FVergilBlueprintCompilerService::Compile(const FVergilCompileRequest& Request) const
{
	FVergilCompileResult Result;

	if (Request.TargetBlueprint == nullptr)
	{
		Result.Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("MissingTargetBlueprint"),
			TEXT("Compile request requires a target UBlueprint.")));
		return Result;
	}

	FVergilCompilerContext Context(
		Request.TargetBlueprint,
		nullptr,
		&Request.Document,
		&Result.Diagnostics,
		&Result.Commands,
		Request.TargetGraphName);

	const TArray<TSharedRef<IVergilCompilerPass, ESPMode::ThreadSafe>> Passes =
	{
		MakeShared<FVergilSchemaMigrationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilStructuralValidationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilSemanticValidationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilCommandPlanningPass, ESPMode::ThreadSafe>()
	};

	for (const TSharedRef<IVergilCompilerPass, ESPMode::ThreadSafe>& Pass : Passes)
	{
		if (!Pass->Run(Request, Context, Result))
		{
			const bool bHasErrorDiagnostics = Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
			{
				return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
			});

			if (bHasErrorDiagnostics)
			{
				UE_LOG(
					LogVergil,
					Log,
					TEXT("Vergil compiler pass '%s' stopped after reporting %d error diagnostics."),
					*Pass->GetPassName().ToString(),
					Result.Diagnostics.Num());
			}
			else
			{
				UE_LOG(LogVergil, Warning, TEXT("Vergil compiler pass '%s' failed."), *Pass->GetPassName().ToString());
			}

			break;
		}
	}

	Vergil::NormalizeCommandPlan(Result.Commands);

	Result.bSucceeded = !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});

	if (!Result.bSucceeded)
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil compile request failed with %d diagnostics."), Result.Diagnostics.Num());
	}

	return Result;
}
