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

	void AddCommand(const FVergilCompilerCommand& Command)
	{
		LoweredNodeCommands.Add(Command);
	}

	void AddPostCompileFinalizeCommand(const FVergilCompilerCommand& Command)
	{
		PostCompileFinalizeCommands.Add(Command);
	}

	const TArray<FVergilCompilerCommand>& GetLoweredNodeCommands() const
	{
		return LoweredNodeCommands;
	}

	const TArray<FVergilCompilerCommand>& GetPostCompileFinalizeCommands() const
	{
		return PostCompileFinalizeCommands;
	}

private:
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	const FVergilGraphDocument* Document = nullptr;
	FVergilGraphDocument WorkingDocument;
	TArray<FVergilDiagnostic>* Diagnostics = nullptr;
	TArray<FVergilCompilerCommand> LoweredNodeCommands;
	TArray<FVergilCompilerCommand> PostCompileFinalizeCommands;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vergil")
	bool bGenerateComments = true;
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
