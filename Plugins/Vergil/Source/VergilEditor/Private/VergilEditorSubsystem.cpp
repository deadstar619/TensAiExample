#include "VergilEditorSubsystem.h"

#include "VergilBlueprintCompilerService.h"
#include "VergilCommandExecutor.h"
#include "VergilDeveloperSettings.h"
#include "VergilLog.h"

namespace
{
	FVergilCompileRequest BuildCompileRequest(
		UBlueprint* Blueprint,
		const FVergilGraphDocument& Document,
		const FName TargetGraphName,
		const bool bAutoLayout,
		const bool bGenerateComments)
	{
		const UVergilDeveloperSettings* const Settings = GetDefault<UVergilDeveloperSettings>();

		FVergilCompileRequest Request;
		Request.TargetBlueprint = Blueprint;
		Request.Document = Document;
		Request.TargetGraphName = TargetGraphName;
		Request.bAutoLayout = bAutoLayout;
		if (Settings != nullptr)
		{
			Request.AutoLayout.HorizontalSpacing = Settings->DefaultNodeSpacing;
			Request.AutoLayout.VerticalSpacing = Settings->DefaultNodeSpacing;
			Request.AutoLayout.CommentPadding = Settings->DefaultCommentPadding;
		}
		Request.bGenerateComments = bGenerateComments;
		return Request;
	}

	FVergilCompileResult PlanCompileRequest(const FVergilCompileRequest& Request, const bool bApplyCommands)
	{
		const FVergilBlueprintCompilerService CompilerService;
		FVergilCompileResult Result = CompilerService.Compile(Request);
		Result.Statistics.bApplyRequested = bApplyCommands;
		return Result;
	}

	FName InferCommandPlanTargetGraphName(const TArray<FVergilCompilerCommand>& Commands)
	{
		FName InferredGraphName = NAME_None;
		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.GraphName.IsNone())
			{
				continue;
			}

			if (InferredGraphName.IsNone())
			{
				InferredGraphName = Command.GraphName;
				continue;
			}

			if (InferredGraphName != Command.GraphName)
			{
				return NAME_None;
			}
		}

		return InferredGraphName;
	}

	void RefreshCompileResultState(FVergilCompileResult& Result, const bool bInferTargetGraphName = false)
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

		if (bInferTargetGraphName && Result.Statistics.TargetGraphName.IsNone())
		{
			Result.Statistics.TargetGraphName = InferCommandPlanTargetGraphName(Result.Commands);
		}

		Result.Statistics.RebuildCommandStatistics(Result.Commands);
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

	void LogCompileResultMetadata(const TCHAR* Label, const FVergilCompileResult& Result)
	{
		UE_LOG(LogVergil, Verbose, TEXT("%s stats: %s"), Label, *Result.Statistics.ToDisplayString());

		if (Result.PassRecords.Num() == 0)
		{
			return;
		}

		TArray<FString> Lines;
		Lines.Reserve(Result.PassRecords.Num());
		for (const FVergilCompilePassRecord& PassRecord : Result.PassRecords)
		{
			Lines.Add(PassRecord.ToDisplayString());
		}

		UE_LOG(LogVergil, VeryVerbose, TEXT("%s passes:\n%s"), Label, *FString::Join(Lines, TEXT("\n")));
	}

	void ApplyCommandPlanResult(
		UBlueprint* Blueprint,
		FVergilCompileResult& Result,
		const bool bInferTargetGraphName,
		const TCHAR* MetadataLabel)
	{
		const FVergilCommandExecutor Executor;
		Result.Statistics.bExecutionAttempted = true;
		Result.Statistics.bExecutionUsedReturnedCommandPlan = true;
		++Result.Statistics.ApplyInvocationCount;
		Result.bApplied = Executor.Execute(Blueprint, Result.Commands, Result.Diagnostics, &Result.ExecutedCommandCount);
		RefreshCompileResultState(Result, bInferTargetGraphName);
		UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
		LogCompileResultMetadata(MetadataLabel, Result);
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

FVergilCompileRequest UVergilEditorSubsystem::MakeCompileRequest(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const FName TargetGraphName,
	const bool bAutoLayout,
	const bool bGenerateComments) const
{
	return BuildCompileRequest(Blueprint, Document, TargetGraphName, bAutoLayout, bGenerateComments);
}

FVergilCompileResult UVergilEditorSubsystem::CompileRequest(const FVergilCompileRequest& Request, const bool bApplyCommands) const
{
	FVergilCompileResult Result = PlanCompileRequest(Request, bApplyCommands);
	LogCommandPlanIfPresent(TEXT("Vergil planned command plan"), Result.Commands);
	UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeCompileResult(Result).ToDisplayString());
	LogCompileResultMetadata(TEXT("Vergil compile result"), Result);

	if (bApplyCommands && Result.bSucceeded)
	{
		ApplyCommandPlanResult(Request.TargetBlueprint, Result, false, TEXT("Vergil apply result"));
	}

	return Result;
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
	return CompileRequest(MakeCompileRequest(Blueprint, Document, TargetGraphName, bAutoLayout, bGenerateComments), bApplyCommands);
}

FVergilCompileResult UVergilEditorSubsystem::ExecuteCommandPlan(UBlueprint* Blueprint, const TArray<FVergilCompilerCommand>& Commands) const
{
	FVergilCompileResult Result;
	Result.Commands = Commands;
	Result.Statistics.bApplyRequested = true;
	Vergil::NormalizeCommandPlan(Result.Commands);
	Result.Statistics.bCommandPlanNormalized = true;
	RefreshCompileResultState(Result, true);
	LogCommandPlanIfPresent(TEXT("Vergil direct command plan"), Result.Commands);

	ApplyCommandPlanResult(Blueprint, Result, true, TEXT("Vergil direct apply result"));
	return Result;
}

FString UVergilEditorSubsystem::SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, const bool bPrettyPrint) const
{
	return Vergil::SerializeCommandPlan(Commands, bPrettyPrint);
}

FString UVergilEditorSubsystem::DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands) const
{
	return Vergil::DescribeCommandPlan(Commands);
}

FString UVergilEditorSubsystem::DescribeDocument(const FVergilGraphDocument& Document) const
{
	return Vergil::DescribeGraphDocument(Document);
}

FString UVergilEditorSubsystem::SerializeDocument(const FVergilGraphDocument& Document, const bool bPrettyPrint) const
{
	return Vergil::SerializeGraphDocument(Document, bPrettyPrint);
}

FString UVergilEditorSubsystem::DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics) const
{
	return Vergil::DescribeDiagnostics(Diagnostics);
}

FString UVergilEditorSubsystem::SerializeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics, const bool bPrettyPrint) const
{
	return Vergil::SerializeDiagnostics(Diagnostics, bPrettyPrint);
}

FString UVergilEditorSubsystem::DescribeCompileResult(const FVergilCompileResult& Result) const
{
	return Vergil::DescribeCompileResult(Result);
}

FString UVergilEditorSubsystem::SerializeCompileResult(const FVergilCompileResult& Result, const bool bPrettyPrint) const
{
	return Vergil::SerializeCompileResult(Result, bPrettyPrint);
}

FVergilCompileResult UVergilEditorSubsystem::ExecuteSerializedCommandPlan(UBlueprint* Blueprint, const FString& SerializedCommandPlan) const
{
	FVergilCompileResult Result;
	Result.Statistics.bApplyRequested = true;
	if (!Vergil::DeserializeCommandPlan(SerializedCommandPlan, Result.Commands, &Result.Diagnostics))
	{
		RefreshCompileResultState(Result);
		UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString());
		LogCompileResultMetadata(TEXT("Vergil serialized apply result"), Result);
		return Result;
	}

	return ExecuteCommandPlan(Blueprint, Result.Commands);
}

const UVergilDeveloperSettings* UVergilEditorSubsystem::GetVergilSettings() const
{
	return GetDefault<UVergilDeveloperSettings>();
}
