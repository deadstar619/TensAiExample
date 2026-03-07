#pragma once

#include "CoreMinimal.h"
#include "VergilCommandTypes.h"
#include "VergilDiagnostic.h"
#include "VergilGraphDocument.h"
#include "VergilCompilerTypes.generated.h"

class UBlueprint;
class UEdGraph;

class VERGILBLUEPRINTCOMPILER_API FVergilCompilerContext
{
public:
	FVergilCompilerContext(
		UBlueprint* InBlueprint,
		UEdGraph* InGraph,
		const FVergilGraphDocument* InDocument,
		TArray<FVergilDiagnostic>* InDiagnostics,
		const FName InGraphName)
		: Blueprint(InBlueprint)
		, Graph(InGraph)
		, Document(InDocument)
		, Diagnostics(InDiagnostics)
		, GraphName(InGraphName)
	{
	}

	void AddDiagnostic(const EVergilDiagnosticSeverity Severity, const FName Code, const FString& Message, const FGuid& SourceId = FGuid()) const
	{
		if (Diagnostics == nullptr)
		{
			return;
		}

		Diagnostics->Add(FVergilDiagnostic::Make(Severity, Code, Message, SourceId));
	}

	UBlueprint* GetBlueprint() const
	{
		return Blueprint;
	}

	UEdGraph* GetGraph() const
	{
		return Graph;
	}

	const FVergilGraphDocument& GetDocument() const
	{
		check(Document != nullptr);
		return *Document;
	}

	void SetWorkingDocument(const FVergilGraphDocument& InDocument)
	{
		WorkingDocument = InDocument;
		Document = &WorkingDocument;
	}

	void SetWorkingDocument(FVergilGraphDocument&& InDocument)
	{
		WorkingDocument = MoveTemp(InDocument);
		Document = &WorkingDocument;
	}

	FName GetGraphName() const
	{
		return GraphName;
	}

	void ResetLoweredNodeCommands()
	{
		LoweredNodeCommands.Reset();
	}

	void ResetPostCompileFinalizeCommands()
	{
		PostCompileFinalizeCommands.Reset();
	}

	void ResetPostCommentCommands()
	{
		PostCommentCommands.Reset();
	}

	void ResetPostLayoutCommands()
	{
		PostLayoutCommands.Reset();
	}

	void AddCommand(const FVergilCompilerCommand& Command)
	{
		LoweredNodeCommands.Add(Command);
	}

	void AddPostCompileFinalizeCommand(const FVergilCompilerCommand& Command)
	{
		PostCompileFinalizeCommands.Add(Command);
	}

	void AddPostCommentCommand(const FVergilCompilerCommand& Command)
	{
		PostCommentCommands.Add(Command);
	}

	void AddPostLayoutCommand(const FVergilCompilerCommand& Command)
	{
		PostLayoutCommands.Add(Command);
	}

	const TArray<FVergilCompilerCommand>& GetLoweredNodeCommands() const
	{
		return LoweredNodeCommands;
	}

	const TArray<FVergilCompilerCommand>& GetPostCompileFinalizeCommands() const
	{
		return PostCompileFinalizeCommands;
	}

	const TArray<FVergilCompilerCommand>& GetPostCommentCommands() const
	{
		return PostCommentCommands;
	}

	const TArray<FVergilCompilerCommand>& GetPostLayoutCommands() const
	{
		return PostLayoutCommands;
	}

private:
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	const FVergilGraphDocument* Document = nullptr;
	FVergilGraphDocument WorkingDocument;
	TArray<FVergilDiagnostic>* Diagnostics = nullptr;
	TArray<FVergilCompilerCommand> LoweredNodeCommands;
	TArray<FVergilCompilerCommand> PostCompileFinalizeCommands;
	TArray<FVergilCompilerCommand> PostCommentCommands;
	TArray<FVergilCompilerCommand> PostLayoutCommands;
	FName GraphName = TEXT("EventGraph");
};

class VERGILBLUEPRINTCOMPILER_API IVergilNodeHandler
{
public:
	virtual ~IVergilNodeHandler() = default;

	virtual FName GetDescriptor() const = 0;
	virtual bool CanHandle(const FVergilGraphNode& Node) const
	{
		return Node.Descriptor == GetDescriptor();
	}

	virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const = 0;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilAutoLayoutSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVector2D Origin = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "64.0"))
	float HorizontalSpacing = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "64.0"))
	float VerticalSpacing = 320.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "16.0"))
	float CommentPadding = 96.0f;
};

UENUM(BlueprintType)
enum class EVergilCommentMoveMode : uint8
{
	GroupMovement,
	NoGroupMovement
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCommentGenerationSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "16.0"))
	float DefaultWidth = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "16.0"))
	float DefaultHeight = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ClampMin = "1"))
	int32 DefaultFontSize = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FLinearColor DefaultColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bShowBubbleWhenZoomed = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bColorBubble = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	EVergilCommentMoveMode MoveMode = EVergilCommentMoveMode::GroupMovement;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCompileRequest
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UBlueprint> TargetBlueprint = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilGraphDocument Document;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName TargetGraphName = TEXT("EventGraph");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bAutoLayout = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ScriptName = "AutoLayoutSettings"))
	FVergilAutoLayoutSettings AutoLayout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bGenerateComments = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil", meta = (ScriptName = "CommentGenerationSettings"))
	FVergilCommentGenerationSettings CommentGeneration;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCompilePassRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName PassName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bSucceeded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 DiagnosticCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ErrorCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PlannedCommandCount = 0;

	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCompileStatistics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName TargetGraphName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 RequestedSchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 EffectiveSchemaVersion = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bAutoLayoutRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bGenerateCommentsRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bApplyRequested = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bExecutionAttempted = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bCommandPlanNormalized = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString CommandPlanFingerprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bExecutionUsedReturnedCommandPlan = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PlanningInvocationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ApplyInvocationCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 SourceNodeCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 SourceEdgeCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PlannedCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 BlueprintDefinitionCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 GraphStructureCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ConnectionCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 FinalizeCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ExplicitCompileCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PostBlueprintCompileCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName LastCompletedPassName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FName FailedPassName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FName> CompletedPassNames;

	int32 GetCompletedPassCount() const;
	int32 GetTotalAccountedCommandCount() const;
	void RebuildCommandStatistics(const TArray<FVergilCompilerCommand>& Commands);
	void SetTargetDocumentStatistics(const FVergilGraphDocument& Document);
	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilCompileResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bSucceeded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bApplied = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ExecutedCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilDiagnostic> Diagnostics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilCompilerCommand> Commands;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilCompileStatistics Statistics;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	TArray<FVergilCompilePassRecord> PassRecords;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilDiagnosticSummary
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 InfoCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 WarningCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ErrorCount = 0;

	int32 GetTotalCount() const;
	bool HasErrors() const;
	FString ToDisplayString() const;
};

USTRUCT(BlueprintType)
struct VERGILBLUEPRINTCOMPILER_API FVergilExecutionSummary
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FString Label;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bSucceeded = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bApplied = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 PlannedCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	int32 ExecutedCommandCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	FVergilDiagnosticSummary Diagnostics;

	FString ToDisplayString() const;
};

namespace Vergil
{
	VERGILBLUEPRINTCOMPILER_API FString GetDiagnosticsInspectionFormatName();
	VERGILBLUEPRINTCOMPILER_API int32 GetDiagnosticsInspectionFormatVersion();
	VERGILBLUEPRINTCOMPILER_API FString DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics);
	VERGILBLUEPRINTCOMPILER_API FString SerializeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics, bool bPrettyPrint = true);
	VERGILBLUEPRINTCOMPILER_API FString GetCompileResultInspectionFormatName();
	VERGILBLUEPRINTCOMPILER_API int32 GetCompileResultInspectionFormatVersion();
	VERGILBLUEPRINTCOMPILER_API FString DescribeCompileResult(const FVergilCompileResult& Result);
	VERGILBLUEPRINTCOMPILER_API FString SerializeCompileResult(const FVergilCompileResult& Result, bool bPrettyPrint = true);
	VERGILBLUEPRINTCOMPILER_API FString GetCommandPlanFormatName();
	VERGILBLUEPRINTCOMPILER_API int32 GetCommandPlanFormatVersion();
	VERGILBLUEPRINTCOMPILER_API void NormalizeCommandPlan(TArray<FVergilCompilerCommand>& Commands);
	VERGILBLUEPRINTCOMPILER_API FString DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands);
	VERGILBLUEPRINTCOMPILER_API FString SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, bool bPrettyPrint = true);
	VERGILBLUEPRINTCOMPILER_API bool DeserializeCommandPlan(
		const FString& SerializedCommandPlan,
		TArray<FVergilCompilerCommand>& OutCommands,
		TArray<FVergilDiagnostic>* OutDiagnostics = nullptr);
	VERGILBLUEPRINTCOMPILER_API FVergilDiagnosticSummary SummarizeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics);
	VERGILBLUEPRINTCOMPILER_API FVergilExecutionSummary SummarizeCompileResult(const FVergilCompileResult& Result);
	VERGILBLUEPRINTCOMPILER_API FVergilExecutionSummary SummarizeApplyResult(const FVergilCompileResult& Result);
	VERGILBLUEPRINTCOMPILER_API FVergilExecutionSummary SummarizeTestResult(
		const FString& Label,
		bool bSucceeded,
		const TArray<FVergilDiagnostic>& Diagnostics,
		int32 PlannedCommandCount = 0,
		int32 ExecutedCommandCount = 0);
}
