#include "VergilBlueprintCompilerService.h"

#include "Algo/AnyOf.h"

#include "VergilCompilerPasses.h"
#include "VergilCompilerTypes.h"
#include "VergilLog.h"

FVergilCompileResult FVergilBlueprintCompilerService::Compile(const FVergilCompileRequest& Request, FVergilGraphDocument* OutEffectiveDocument) const
{
	FVergilCompileResult Result;
	Result.Statistics.TargetGraphName = Request.TargetGraphName;
	Result.Statistics.RequestedSchemaVersion = Request.Document.SchemaVersion;
	Result.Statistics.EffectiveSchemaVersion = Request.Document.SchemaVersion;
	Result.Statistics.bAutoLayoutRequested = Request.bAutoLayout;
	Result.Statistics.bGenerateCommentsRequested = Request.bGenerateComments;
	Result.Statistics.SetTargetDocumentStatistics(Request.Document);

	if (Request.TargetBlueprint == nullptr)
	{
		if (OutEffectiveDocument != nullptr)
		{
			*OutEffectiveDocument = Request.Document;
		}

		Result.Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("MissingTargetBlueprint"),
			TEXT("Compile request requires a target UBlueprint.")));
		return Result;
	}

	Result.Statistics.PlanningInvocationCount = 1;

	FVergilCompilerContext Context(
		Request.TargetBlueprint,
		nullptr,
		&Request.Document,
		&Result.Diagnostics,
		Request.TargetGraphName);

	const TArray<TSharedRef<IVergilCompilerPass, ESPMode::ThreadSafe>> Passes =
	{
		MakeShared<FVergilSchemaMigrationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilStructuralValidationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilSemanticValidationPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilSymbolResolutionPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilTypeResolutionPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilNodeLoweringPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilConnectionLegalityPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilPostCompileFinalizePass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilCommentPostPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilLayoutPostPass, ESPMode::ThreadSafe>(),
		MakeShared<FVergilCommandPlanningPass, ESPMode::ThreadSafe>()
	};

	for (const TSharedRef<IVergilCompilerPass, ESPMode::ThreadSafe>& Pass : Passes)
	{
		const FName PassName = Pass->GetPassName();
		const bool bPassSucceeded = Pass->Run(Request, Context, Result);

		FVergilCompilePassRecord PassRecord;
		PassRecord.PassName = PassName;
		PassRecord.bSucceeded = bPassSucceeded;
		PassRecord.DiagnosticCount = Result.Diagnostics.Num();
		PassRecord.ErrorCount = Vergil::SummarizeDiagnostics(Result.Diagnostics).ErrorCount;
		PassRecord.PlannedCommandCount = Result.Commands.Num();
		Result.PassRecords.Add(PassRecord);

		if (bPassSucceeded)
		{
			Result.Statistics.CompletedPassNames.Add(PassName);
			Result.Statistics.LastCompletedPassName = PassName;
		}
		else
		{
			Result.Statistics.FailedPassName = PassName;
		}

		if (!bPassSucceeded)
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
					*PassName.ToString(),
					Result.Diagnostics.Num());
			}
			else
			{
				UE_LOG(LogVergil, Warning, TEXT("Vergil compiler pass '%s' failed."), *PassName.ToString());
			}

			break;
		}
	}

	Result.Statistics.SetTargetDocumentStatistics(Context.GetDocument());
	Vergil::NormalizeCommandPlan(Result.Commands);
	Result.Statistics.bCommandPlanNormalized = true;
	Result.Statistics.RebuildCommandStatistics(Result.Commands);

	Result.bSucceeded = !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});

	if (!Result.bSucceeded)
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil compile request failed with %d diagnostics."), Result.Diagnostics.Num());
	}

	if (OutEffectiveDocument != nullptr)
	{
		*OutEffectiveDocument = Context.GetDocument();
	}

	return Result;
}
