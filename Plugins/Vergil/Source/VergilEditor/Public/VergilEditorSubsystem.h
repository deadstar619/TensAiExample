#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "VergilCompilerTypes.h"
#include "VergilReflectionInfo.h"
#include "VergilEditorSubsystem.generated.h"

class UBlueprint;
class UVergilDeveloperSettings;

UCLASS()
class VERGILEDITOR_API UVergilEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FVergilCompileRequest MakeCompileRequest(
		UBlueprint* Blueprint,
		const FVergilGraphDocument& Document,
		FName TargetGraphName = TEXT("EventGraph"),
		bool bAutoLayout = true,
		bool bGenerateComments = true) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult CompileRequest(const FVergilCompileRequest& Request, bool bApplyCommands = false) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult CompileDocument(UBlueprint* Blueprint, const FVergilGraphDocument& Document, bool bAutoLayout = true, bool bGenerateComments = true, bool bApplyCommands = false) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult CompileDocumentToGraph(
		UBlueprint* Blueprint,
		const FVergilGraphDocument& Document,
		FName TargetGraphName,
		bool bAutoLayout = true,
		bool bGenerateComments = true,
		bool bApplyCommands = false) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult ExecuteCommandPlan(UBlueprint* Blueprint, const TArray<FVergilCompilerCommand>& Commands) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCommandPlanPreview PreviewCompileRequest(const FVergilCompileRequest& Request) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCommandPlanPreview PreviewDocument(UBlueprint* Blueprint, const FVergilGraphDocument& Document, bool bAutoLayout = true, bool bGenerateComments = true) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCommandPlanPreview PreviewDocumentToGraph(
		UBlueprint* Blueprint,
		const FVergilGraphDocument& Document,
		FName TargetGraphName,
		bool bAutoLayout = true,
		bool bGenerateComments = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeDocument(const FVergilGraphDocument& Document) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeDocument(const FVergilGraphDocument& Document, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FVergilDocumentDiff DiffDocuments(const FVergilGraphDocument& Before, const FVergilGraphDocument& After) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeDocumentDiff(const FVergilDocumentDiff& Diff) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString InspectDocumentDiffAsJson(const FVergilDocumentDiff& Diff, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeCompileResult(const FVergilCompileResult& Result) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeCompileResult(const FVergilCompileResult& Result, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeCommandPlanPreview(const FVergilCommandPlanPreview& Preview) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString InspectCommandPlanPreviewAsJson(const FVergilCommandPlanPreview& Preview, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FVergilReflectionSymbolInfo InspectReflectionSymbol(const FString& Query) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeReflectionSymbol(const FString& Query) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString InspectReflectionSymbolAsJson(const FString& Query, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FVergilReflectionDiscoveryResults DiscoverReflectionSymbols(const FString& Query, int32 MaxResults = 25) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeReflectionDiscovery(const FString& Query, int32 MaxResults = 25) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString InspectReflectionDiscoveryAsJson(const FString& Query, int32 MaxResults = 25, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult ExecuteSerializedCommandPlan(UBlueprint* Blueprint, const FString& SerializedCommandPlan) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	const UVergilDeveloperSettings* GetVergilSettings() const;
};
