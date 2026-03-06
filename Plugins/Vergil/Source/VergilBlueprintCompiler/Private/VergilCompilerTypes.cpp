#include "VergilCompilerTypes.h"

#include "Algo/StableSort.h"

namespace
{
	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	int32 GetDeterministicCommandPhase(const FVergilCompilerCommand& Command)
	{
		switch (Command.Type)
		{
		case EVergilCommandType::EnsureDispatcher:
		case EVergilCommandType::AddDispatcherParameter:
		case EVergilCommandType::EnsureVariable:
		case EVergilCommandType::SetVariableMetadata:
		case EVergilCommandType::SetVariableDefault:
		case EVergilCommandType::EnsureFunctionGraph:
		case EVergilCommandType::EnsureMacroGraph:
		case EVergilCommandType::EnsureComponent:
		case EVergilCommandType::AttachComponent:
		case EVergilCommandType::SetComponentProperty:
		case EVergilCommandType::EnsureInterface:
		case EVergilCommandType::RenameMember:
			return 0;

		case EVergilCommandType::EnsureGraph:
		case EVergilCommandType::AddNode:
		case EVergilCommandType::SetNodeMetadata:
		case EVergilCommandType::RemoveNode:
		case EVergilCommandType::MoveNode:
			return 1;

		case EVergilCommandType::ConnectPins:
			return 2;

		case EVergilCommandType::FinalizeNode:
			return 3;

		case EVergilCommandType::CompileBlueprint:
			return 4;

		case EVergilCommandType::SetClassDefault:
			return 5;

		default:
			return 6;
		}
	}
}

void Vergil::NormalizeCommandPlan(TArray<FVergilCompilerCommand>& Commands)
{
	Algo::StableSort(Commands, [](const FVergilCompilerCommand& A, const FVergilCompilerCommand& B)
	{
		return GetDeterministicCommandPhase(A) < GetDeterministicCommandPhase(B);
	});
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
