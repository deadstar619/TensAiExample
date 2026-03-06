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

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult CompileDocument(UBlueprint* Blueprint, const FVergilGraphDocument& Document, bool bAutoLayout = true, bool bGenerateComments = true, bool bApplyCommands = false) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil")
	FVergilCompileResult ExecuteCommandPlan(UBlueprint* Blueprint, const TArray<FVergilCompilerCommand>& Commands) const;

	UFUNCTION(BlueprintPure, Category = "Vergil")
	const UVergilDeveloperSettings* GetVergilSettings() const;
};
