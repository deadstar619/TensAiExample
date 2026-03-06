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

void UVergilAgentSubsystem::ClearAuditTrail()
{
	AuditTrail.Reset();
}
