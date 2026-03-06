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
		TArray<FVergilDiagnostic>* InDiagnostics,
		TArray<FVergilCompilerCommand>* InCommands,
		const FName InGraphName)
		: Blueprint(InBlueprint)
		, Graph(InGraph)
		, Diagnostics(InDiagnostics)
		, Commands(InCommands)
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

	FName GetGraphName() const
	{
		return GraphName;
	}

	void AddCommand(const FVergilCompilerCommand& Command) const
	{
		if (Commands == nullptr)
		{
			return;
		}

		Commands->Add(Command);
	}

private:
	UBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	TArray<FVergilDiagnostic>* Diagnostics = nullptr;
	TArray<FVergilCompilerCommand>* Commands = nullptr;
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
