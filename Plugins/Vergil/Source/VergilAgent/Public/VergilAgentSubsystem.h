#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "VergilContractInfo.h"
#include "VergilAgentTypes.h"
#include "VergilAgentSubsystem.generated.h"

UCLASS()
class VERGILAGENT_API UVergilAgentSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Vergil|Agent")
	void RecordAuditEntry(const FVergilAgentAuditEntry& Entry);

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	TArray<FVergilAgentAuditEntry> GetRecentAuditEntries() const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FVergilSupportedContractManifest InspectSupportedContracts() const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	TArray<FVergilSupportedDescriptorContract> InspectSupportedDescriptorContracts() const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectSupportedContractsAsJson(bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeSupportedContracts() const;

	UFUNCTION(BlueprintCallable, Category = "Vergil|Agent")
	void ClearAuditTrail();

private:
	UPROPERTY(Transient)
	TArray<FVergilAgentAuditEntry> AuditTrail;

	UPROPERTY(EditAnywhere, Category = "Vergil|Agent")
	int32 MaxAuditEntries = 100;
};
