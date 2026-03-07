#include "VergilEditorSubsystem.h"

#include "VergilBlueprintCompilerService.h"
#include "VergilCommandExecutor.h"
#include "VergilDeveloperSettings.h"
#include "VergilEditorExecutionUtils.h"
#include "VergilLog.h"

namespace
{
	inline const FName DefaultTargetGraphName(TEXT("EventGraph"));

	FName ResolveConfiguredTargetGraphName(const UVergilDeveloperSettings* Settings, const FName RequestedTargetGraphName)
	{
		if (!RequestedTargetGraphName.IsNone())
		{
			return RequestedTargetGraphName;
		}

		if (Settings != nullptr && !Settings->DefaultTargetGraphName.IsNone())
		{
			return Settings->DefaultTargetGraphName;
		}

		return DefaultTargetGraphName;
	}

	FVergilCompileRequest BuildCompileRequest(
		UBlueprint* Blueprint,
		const FVergilGraphDocument& Document,
		const FName TargetGraphName,
		const TOptional<bool> bAutoLayoutOverride,
		const TOptional<bool> bGenerateCommentsOverride)
	{
		const UVergilDeveloperSettings* const Settings = GetDefault<UVergilDeveloperSettings>();

		FVergilCompileRequest Request;
		Request.TargetBlueprint = Blueprint;
		Request.Document = Document;
		Request.TargetGraphName = ResolveConfiguredTargetGraphName(Settings, TargetGraphName);
		if (Settings != nullptr)
		{
			Request.bAutoLayout = Settings->bDefaultAutoLayoutEnabled;
			Request.AutoLayout = Settings->DefaultAutoLayout;
			Request.bGenerateComments = Settings->bDefaultCommentGenerationEnabled;
			Request.CommentGeneration = Settings->DefaultCommentGeneration;
			Request.bTreatStructuralWarningsAsErrors = Settings->bTreatStructuralWarningsAsErrors;
		}

		if (bAutoLayoutOverride.IsSet())
		{
			Request.bAutoLayout = bAutoLayoutOverride.GetValue();
		}

		if (bGenerateCommentsOverride.IsSet())
		{
			Request.bGenerateComments = bGenerateCommentsOverride.GetValue();
		}

		return Request;
	}

	FVergilCompileResult PlanCompileRequest(
		const FVergilCompileRequest& Request,
		const bool bApplyCommands,
		FVergilGraphDocument* OutEffectiveDocument = nullptr)
	{
		const FVergilBlueprintCompilerService CompilerService;
		FVergilCompileResult Result = CompilerService.Compile(Request, OutEffectiveDocument);
		Result.Statistics.bApplyRequested = bApplyCommands;
		return Result;
	}

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
		const TCHAR* MetadataLabel)
	{
		const FVergilCommandExecutor Executor;
		Result.Statistics.bExecutionAttempted = true;
		Result.Statistics.bExecutionUsedReturnedCommandPlan = true;
		++Result.Statistics.ApplyInvocationCount;
		Result.bApplied = Executor.Execute(
			Blueprint,
			Result.Commands,
			Result.Diagnostics,
			&Result.ExecutedCommandCount,
			&Result.Statistics.TransactionAudit);
		RefreshCompileResultState(Result);
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

FVergilCompileRequest UVergilEditorSubsystem::MakeDefaultCompileRequest(UBlueprint* Blueprint, const FVergilGraphDocument& Document) const
{
	return BuildCompileRequest(Blueprint, Document, NAME_None, TOptional<bool>(), TOptional<bool>());
}

FVergilCompileRequest UVergilEditorSubsystem::MakeCompileRequest(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const FName TargetGraphName,
	const bool bAutoLayout,
	const bool bGenerateComments) const
{
	return BuildCompileRequest(
		Blueprint,
		Document,
		TargetGraphName,
		TOptional<bool>(bAutoLayout),
		TOptional<bool>(bGenerateComments));
}

FVergilCompileResult UVergilEditorSubsystem::CompileRequest(const FVergilCompileRequest& Request, const bool bApplyCommands) const
{
	FVergilCompileResult Result = PlanCompileRequest(Request, bApplyCommands);
	LogCommandPlanIfPresent(TEXT("Vergil planned command plan"), Result.Commands);
	UE_LOG(LogVergil, Log, TEXT("%s"), *Vergil::SummarizeCompileResult(Result).ToDisplayString());
	LogCompileResultMetadata(TEXT("Vergil compile result"), Result);

	if (bApplyCommands && Result.bSucceeded)
	{
		ApplyCommandPlanResult(Request.TargetBlueprint, Result, TEXT("Vergil apply result"));
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
		NAME_None,
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
	FVergilCompileResult Result = VergilEditor::PrepareCommandPlanForExecution(Commands);
	LogCommandPlanIfPresent(TEXT("Vergil direct command plan"), Result.Commands);

	ApplyCommandPlanResult(Blueprint, Result, TEXT("Vergil direct apply result"));
	return Result;
}

FVergilCommandPlanPreview UVergilEditorSubsystem::PreviewCompileRequest(const FVergilCompileRequest& Request) const
{
	FVergilGraphDocument EffectiveDocument = Request.Document;
	const FVergilCompileResult Result = PlanCompileRequest(Request, false, &EffectiveDocument);
	return Vergil::MakeCommandPlanPreview(Request, EffectiveDocument, Result);
}

FVergilCommandPlanPreview UVergilEditorSubsystem::PreviewDocument(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const bool bAutoLayout,
	const bool bGenerateComments) const
{
	return PreviewDocumentToGraph(Blueprint, Document, NAME_None, bAutoLayout, bGenerateComments);
}

FVergilCommandPlanPreview UVergilEditorSubsystem::PreviewDocumentToGraph(
	UBlueprint* Blueprint,
	const FVergilGraphDocument& Document,
	const FName TargetGraphName,
	const bool bAutoLayout,
	const bool bGenerateComments) const
{
	return PreviewCompileRequest(MakeCompileRequest(Blueprint, Document, TargetGraphName, bAutoLayout, bGenerateComments));
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

FVergilDocumentDiff UVergilEditorSubsystem::DiffDocuments(const FVergilGraphDocument& Before, const FVergilGraphDocument& After) const
{
	return Vergil::DiffGraphDocuments(Before, After);
}

FString UVergilEditorSubsystem::DescribeDocumentDiff(const FVergilDocumentDiff& Diff) const
{
	return Vergil::DescribeDocumentDiff(Diff);
}

FString UVergilEditorSubsystem::InspectDocumentDiffAsJson(const FVergilDocumentDiff& Diff, const bool bPrettyPrint) const
{
	return Vergil::SerializeDocumentDiff(Diff, bPrettyPrint);
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

FString UVergilEditorSubsystem::DescribeCommandPlanPreview(const FVergilCommandPlanPreview& Preview) const
{
	return Vergil::DescribeCommandPlanPreview(Preview);
}

FString UVergilEditorSubsystem::InspectCommandPlanPreviewAsJson(const FVergilCommandPlanPreview& Preview, const bool bPrettyPrint) const
{
	return Vergil::SerializeCommandPlanPreview(Preview, bPrettyPrint);
}

FVergilReflectionSymbolInfo UVergilEditorSubsystem::InspectReflectionSymbol(const FString& Query) const
{
	return Vergil::InspectReflectionSymbol(Query);
}

FString UVergilEditorSubsystem::DescribeReflectionSymbol(const FString& Query) const
{
	return Vergil::DescribeReflectionSymbol(Vergil::InspectReflectionSymbol(Query));
}

FString UVergilEditorSubsystem::InspectReflectionSymbolAsJson(const FString& Query, const bool bPrettyPrint) const
{
	return Vergil::SerializeReflectionSymbol(Vergil::InspectReflectionSymbol(Query), bPrettyPrint);
}

FVergilReflectionDiscoveryResults UVergilEditorSubsystem::DiscoverReflectionSymbols(const FString& Query, const int32 MaxResults) const
{
	return Vergil::DiscoverReflectionSymbols(Query, MaxResults);
}

FString UVergilEditorSubsystem::DescribeReflectionDiscovery(const FString& Query, const int32 MaxResults) const
{
	return Vergil::DescribeReflectionDiscovery(Vergil::DiscoverReflectionSymbols(Query, MaxResults));
}

FString UVergilEditorSubsystem::InspectReflectionDiscoveryAsJson(const FString& Query, const int32 MaxResults, const bool bPrettyPrint) const
{
	return Vergil::SerializeReflectionDiscovery(Vergil::DiscoverReflectionSymbols(Query, MaxResults), bPrettyPrint);
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
