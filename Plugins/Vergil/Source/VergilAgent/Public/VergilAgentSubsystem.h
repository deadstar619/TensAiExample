#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "VergilContractInfo.h"
#include "VergilCompilerTypes.h"
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

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeAgentRequest(const FVergilAgentRequest& Request) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectAgentRequestAsJson(const FVergilAgentRequest& Request, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeAgentResponse(const FVergilAgentResponse& Response) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectAgentResponseAsJson(const FVergilAgentResponse& Response, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeAgentAuditEntry(const FVergilAgentAuditEntry& Entry) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectAgentAuditEntryAsJson(const FVergilAgentAuditEntry& Entry, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString GetAuditTrailPersistencePath() const;

	UFUNCTION(BlueprintCallable, Category = "Vergil|Agent")
	bool FlushAuditTrailToDisk();

	UFUNCTION(BlueprintCallable, Category = "Vergil|Agent")
	bool ReloadAuditTrailFromDisk();

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectCommandPlanAsJson(const TArray<FVergilCompilerCommand>& Commands, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeDocument(const FVergilGraphDocument& Document) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectDocumentAsJson(const FVergilGraphDocument& Document, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectDiagnosticsAsJson(const TArray<FVergilDiagnostic>& Diagnostics, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString DescribeCompileResult(const FVergilCompileResult& Result) const;

	UFUNCTION(BlueprintPure, Category = "Vergil|Agent")
	FString InspectCompileResultAsJson(const FVergilCompileResult& Result, bool bPrettyPrint = true) const;

	UFUNCTION(BlueprintCallable, Category = "Vergil|Agent")
	void ClearAuditTrail();

private:
	UPROPERTY(Transient)
	TArray<FVergilAgentAuditEntry> AuditTrail;

	UPROPERTY(EditAnywhere, Category = "Vergil|Agent")
	int32 MaxAuditEntries = 100;

	void TrimAuditTrailToMaxEntries();
	bool TryLoadAuditTrailFromDisk(TArray<FVergilAgentAuditEntry>& OutEntries, FString* OutErrorMessage = nullptr) const;
	bool TryWriteAuditTrailToDisk(const TArray<FVergilAgentAuditEntry>& Entries, FString* OutErrorMessage = nullptr) const;
};
