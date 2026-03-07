#include "VergilAgentSubsystem.h"

#include "VergilLog.h"

void UVergilAgentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem initialized."));
}

void UVergilAgentSubsystem::Deinitialize()
{
	UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem deinitialized."));
	AuditTrail.Reset();
	Super::Deinitialize();
}

void UVergilAgentSubsystem::RecordAuditEntry(const FVergilAgentAuditEntry& Entry)
{
	AuditTrail.Add(Entry);

	if (AuditTrail.Num() > MaxAuditEntries)
	{
		const int32 NumToRemove = AuditTrail.Num() - MaxAuditEntries;
		AuditTrail.RemoveAt(0, NumToRemove);
	}
}

TArray<FVergilAgentAuditEntry> UVergilAgentSubsystem::GetRecentAuditEntries() const
{
	return AuditTrail;
}

FVergilSupportedContractManifest UVergilAgentSubsystem::InspectSupportedContracts() const
{
	return Vergil::GetSupportedContractManifest();
}

TArray<FVergilSupportedDescriptorContract> UVergilAgentSubsystem::InspectSupportedDescriptorContracts() const
{
	return Vergil::GetSupportedContractManifest().SupportedDescriptors;
}

FString UVergilAgentSubsystem::InspectSupportedContractsAsJson(const bool bPrettyPrint) const
{
	return Vergil::SerializeSupportedContractManifest(bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeSupportedContracts() const
{
	return Vergil::DescribeSupportedContractManifest();
}

FString UVergilAgentSubsystem::DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands) const
{
	return Vergil::DescribeCommandPlan(Commands);
}

FString UVergilAgentSubsystem::InspectCommandPlanAsJson(const TArray<FVergilCompilerCommand>& Commands, const bool bPrettyPrint) const
{
	return Vergil::SerializeCommandPlan(Commands, bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeDocument(const FVergilGraphDocument& Document) const
{
	return Vergil::DescribeGraphDocument(Document);
}

FString UVergilAgentSubsystem::InspectDocumentAsJson(const FVergilGraphDocument& Document, const bool bPrettyPrint) const
{
	return Vergil::SerializeGraphDocument(Document, bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics) const
{
	return Vergil::DescribeDiagnostics(Diagnostics);
}

FString UVergilAgentSubsystem::InspectDiagnosticsAsJson(const TArray<FVergilDiagnostic>& Diagnostics, const bool bPrettyPrint) const
{
	return Vergil::SerializeDiagnostics(Diagnostics, bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeCompileResult(const FVergilCompileResult& Result) const
{
	return Vergil::DescribeCompileResult(Result);
}

FString UVergilAgentSubsystem::InspectCompileResultAsJson(const FVergilCompileResult& Result, const bool bPrettyPrint) const
{
	return Vergil::SerializeCompileResult(Result, bPrettyPrint);
}

void UVergilAgentSubsystem::ClearAuditTrail()
{
	AuditTrail.Reset();
}
