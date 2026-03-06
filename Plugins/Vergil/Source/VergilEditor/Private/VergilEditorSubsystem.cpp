#include "VergilEditorSubsystem.h"

#include "VergilBlueprintCompilerService.h"
#include "VergilCommandExecutor.h"
#include "VergilDeveloperSettings.h"
#include "VergilLog.h"

namespace
{
	void RefreshCompileResultState(FVergilCompileResult& Result)
	{
		Result.bSucceeded = true;
		for (const FVergilDiagnostic& Diagnostic : Result.Diagnostics)
		{
			if (Diagnostic.Severity == EVergilDiagnosticSeverity::Error)
			{
				Result.bSucceeded = false;
				break;
			}
		}
	}
}

void UVergilEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogVergil, Log, TEXT("Vergil editor subsystem initialized."));
}

void UVergilEditorSubsystem::Deinitialize()
{
	UE_LOG(LogVergil, Log, TEXT("Vergil editor subsystem deinitialized."));
	Super::Deinitialize();
}

FVergilCompileResult UVergilEditorSubsystem::CompileDocument(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const bool bAutoLayout,
	const bool bGenerateComments,
	const bool bApplyCommands) const
{
	FVergilCompileRequest Request;
	Request.TargetBlueprint = Blueprint;
	Request.Document = Document;
	Request.bAutoLayout = bAutoLayout;
	Request.bGenerateComments = bGenerateComments;

	const FVergilBlueprintCompilerService CompilerService;
	FVergilCompileResult Result = CompilerService.Compile(Request);
	UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeCompileResult(Result).ToDisplayString());

	if (bApplyCommands && Result.bSucceeded)
	{
		const FVergilCommandExecutor Executor;
		Result.bApplied = Executor.Execute(Blueprint, Result.Commands, Result.Diagnostics, &Result.ExecutedCommandCount);
		RefreshCompileResultState(Result);
		UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
	}

	return Result;
}

FVergilCompileResult UVergilEditorSubsystem::ExecuteCommandPlan(UBlueprint* Blueprint, const TArray<FVergilCompilerCommand>& Commands) const
{
	FVergilCompileResult Result;
	Result.Commands = Commands;

	const FVergilCommandExecutor Executor;
	Result.bApplied = Executor.Execute(Blueprint, Commands, Result.Diagnostics, &Result.ExecutedCommandCount);
	RefreshCompileResultState(Result);
	UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
	return Result;
}

const UVergilDeveloperSettings* UVergilEditorSubsystem::GetVergilSettings() const
{
	return GetDefault<UVergilDeveloperSettings>();
}
