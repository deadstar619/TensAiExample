#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "VergilCompilerTypes.h"
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

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeDocument(const FVergilGraphDocument& Document) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeDocument(const FVergilGraphDocument& Document, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString DescribeCompileResult(const FVergilCompileResult& Result) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	FString SerializeCompileResult(const FVergilCompileResult& Result, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult ExecuteSerializedCommandPlan(UBlueprint* Blueprint, const FString& SerializedCommandPlan) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	const UVergilDeveloperSettings* GetVergilSettings() const;
};
