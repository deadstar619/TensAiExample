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

	void LogCommandPlanIfPresent(const TCHAR* Label, const TArray<FVergilCompilerCommand>& Commands)
	{
		if (Commands.Num() == 0)
		{
			UE_LOG(LogVergil, Verbose, TEXT("%s: <empty command plan>"), Label);
			return;
		}

		UE_LOG(LogVergil, Verbose, TEXT("%s (%d commands)\n%s"), Label, Commands.Num(), *Vergil::DescribeCommandPlan(Commands));
		UE_LOG(LogVergil, VeryVerbose, TEXT("%s json=%s"), Label, *Vergil::SerializeCommandPlan(Commands, false));
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
	return CompileDocumentToGraph(
		Blueprint,
		Document,
		TEXT("EventGraph"),
		bAutoLayout,
		bGenerateComments,
		bApplyCommands);
}

FVergilCompileResult UVergilEditorSubsystem::CompileDocumentToGraph(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const FName TargetGraphName,
	const bool bAutoLayout,
	const bool bGenerateComments,
	const bool bApplyCommands) const
{
	FVergilCompileRequest Request;
	Request.TargetBlueprint = Blueprint;
	Request.Document = Document;
	Request.bAutoLayout = bAutoLayout;
	Request.bGenerateComments = bGenerateComments;
	Request.TargetGraphName = TargetGraphName;

	const FVergilBlueprintCompilerService CompilerService;
	FVergilCompileResult Result = CompilerService.Compile(Request);
	LogCommandPlanIfPresent(TEXT("Vergil planned command plan"), Result.Commands);
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
	Vergil::NormalizeCommandPlan(Result.Commands);
	LogCommandPlanIfPresent(TEXT("Vergil direct command plan"), Result.Commands);

	const FVergilCommandExecutor Executor;
	Result.bApplied = Executor.Execute(Blueprint, Result.Commands, Result.Diagnostics, &Result.ExecutedCommandCount);
	RefreshCompileResultState(Result);
	UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
	return Result;
}

FString UVergilEditorSubsystem::SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, const bool bPrettyPrint) const
{
	return Vergil::SerializeCommandPlan(Commands, bPrettyPrint);
}

FVergilCompileResult UVergilEditorSubsystem::ExecuteSerializedCommandPlan(UBlueprint* Blueprint, const FString& SerializedCommandPlan) const
{
	FVergilCompileResult Result;
	if (!Vergil::DeserializeCommandPlan(SerializedCommandPlan, Result.Commands, &Result.Diagnostics))
	{
		RefreshCompileResultState(Result);
		UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
		return Result;
	}

	return ExecuteCommandPlan(Blueprint, Result.Commands);
}

const UVergilDeveloperSettings* UVergilEditorSubsystem::GetVergilSettings() const
{
	return GetDefault<UVergilDeveloperSettings>();
}
