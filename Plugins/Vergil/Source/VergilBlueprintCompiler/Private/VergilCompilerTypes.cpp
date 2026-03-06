#include "VergilCompilerTypes.h"

namespace
{
	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}
}

int32 FVergilDiagnosticSummary::GetTotalCount() const
{
	return InfoCount + WarningCount + ErrorCount;
}

bool FVergilDiagnosticSummary::HasErrors() const
{
	return ErrorCount > 0;
}

FString FVergilDiagnosticSummary::ToDisplayString() const
{
	return FString::Printf(
		TEXT("info=%d warnings=%d errors=%d total=%d"),
		InfoCount,
		WarningCount,
		ErrorCount,
		GetTotalCount());
}

FString FVergilExecutionSummary::ToDisplayString() const
{
	const FString EffectiveLabel = Label.IsEmpty() ? TEXT("Vergil") : Label;
	return FString::Printf(
		TEXT("%s succeeded=%s applied=%s planned=%d executed=%d diagnostics={%s}"),
		*EffectiveLabel,
		LexBoolString(bSucceeded),
		LexBoolString(bApplied),
		PlannedCommandCount,
		ExecutedCommandCount,
		*Diagnostics.ToDisplayString());
}

FVergilDiagnosticSummary Vergil::SummarizeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics)
{
	FVergilDiagnosticSummary Summary;
	for (const FVergilDiagnostic& Diagnostic : Diagnostics)
	{
		switch (Diagnostic.Severity)
		{
		case EVergilDiagnosticSeverity::Info:
			++Summary.InfoCount;
			break;

		case EVergilDiagnosticSeverity::Warning:
			++Summary.WarningCount;
			break;

		case EVergilDiagnosticSeverity::Error:
			++Summary.ErrorCount;
			break;

		default:
			break;
		}
	}

	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeCompileResult(const FVergilCompileResult& Result)
{
	FVergilExecutionSummary Summary;
	Summary.Label = TEXT("Compile");
	Summary.Diagnostics = SummarizeDiagnostics(Result.Diagnostics);
	Summary.bSucceeded = Result.bSucceeded && !Summary.Diagnostics.HasErrors();
	Summary.PlannedCommandCount = Result.Commands.Num();
	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeApplyResult(const FVergilCompileResult& Result)
{
	FVergilExecutionSummary Summary;
	Summary.Label = TEXT("Apply");
	Summary.Diagnostics = SummarizeDiagnostics(Result.Diagnostics);
	Summary.bApplied = Result.bApplied;
	Summary.bSucceeded = Result.bApplied && !Summary.Diagnostics.HasErrors();
	Summary.PlannedCommandCount = Result.Commands.Num();
	Summary.ExecutedCommandCount = Result.ExecutedCommandCount;
	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeTestResult(
	const FString& Label,
	const bool bSucceeded,
	const TArray<FVergilDiagnostic>& Diagnostics,
	const int32 PlannedCommandCount,
	const int32 ExecutedCommandCount)
{
	FVergilExecutionSummary Summary;
	Summary.Label = Label;
	Summary.Diagnostics = SummarizeDiagnostics(Diagnostics);
	Summary.bSucceeded = bSucceeded && !Summary.Diagnostics.HasErrors();
	Summary.bApplied = ExecutedCommandCount > 0;
	Summary.PlannedCommandCount = PlannedCommandCount;
	Summary.ExecutedCommandCount = ExecutedCommandCount;
	return Summary;
}
