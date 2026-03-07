#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VergilCompilerTypes.h"
#include "VergilPermissionTypes.h"
#include "VergilDeveloperSettings.generated.h"

UCLASS(Config = EditorPerProjectUserSettings, DefaultConfig, meta = (DisplayName = "Vergil"))
class VERGILEDITOR_API UVergilDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVergilDeveloperSettings();

	virtual FName GetContainerName() const override;
	virtual FName GetCategoryName() const override;
	virtual FName GetSectionName() const override;

	UPROPERTY(Config, EditAnywhere, Category = "Agent")
	bool bEnableAgentTools = true;

	UPROPERTY(Config, EditAnywhere, Category = "Agent")
	EVergilAgentWritePermissionPolicy AgentWritePermissionPolicy = EVergilAgentWritePermissionPolicy::RequireExplicitApproval;

	UPROPERTY(Config, EditAnywhere, Category = "Compiler")
	bool bEnableExperimentalCompilerPasses = false;

	UPROPERTY(Config, EditAnywhere, Category = "Compiler")
	FName DefaultTargetGraphName = TEXT("EventGraph");

	UPROPERTY(Config, EditAnywhere, Category = "Compiler")
	bool bDefaultAutoLayoutEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Layout", meta = (ShowOnlyInnerProperties))
	FVergilAutoLayoutSettings DefaultAutoLayout;

	UPROPERTY(Config, EditAnywhere, Category = "Compiler")
	bool bDefaultCommentGenerationEnabled = true;

	UPROPERTY(Config, EditAnywhere, Category = "Comments", meta = (ShowOnlyInnerProperties))
	FVergilCommentGenerationSettings DefaultCommentGeneration;

	UPROPERTY(Config, EditAnywhere, Category = "Validation")
	bool bTreatStructuralWarningsAsErrors = false;
};
