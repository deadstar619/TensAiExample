#include "VergilAgentSubsystem.h"

#include "VergilLog.h"

namespace
{
	FVergilAgentAuditEntry NormalizeAuditEntry(const FVergilAgentAuditEntry& Entry)
	{
		FVergilAgentAuditEntry NormalizedEntry = Entry;

		if (!NormalizedEntry.Request.Context.RequestId.IsValid())
		{
			NormalizedEntry.Request.Context.RequestId = FGuid::NewGuid();
		}

		if (!NormalizedEntry.Response.RequestId.IsValid())
		{
			NormalizedEntry.Response.RequestId = NormalizedEntry.Request.Context.RequestId;
		}

		if (NormalizedEntry.Response.Operation == EVergilAgentOperation::None)
		{
			NormalizedEntry.Response.Operation = NormalizedEntry.Request.Operation;
		}

		if (NormalizedEntry.TimestampUtc.IsEmpty())
		{
			NormalizedEntry.TimestampUtc = FDateTime::UtcNow().ToIso8601();
		}

		return NormalizedEntry;
	}
}

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
	AuditTrail.Add(NormalizeAuditEntry(Entry));

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

FString UVergilAgentSubsystem::DescribeAgentRequest(const FVergilAgentRequest& Request) const
{
	return Vergil::DescribeAgentRequest(Request);
}

FString UVergilAgentSubsystem::InspectAgentRequestAsJson(const FVergilAgentRequest& Request, const bool bPrettyPrint) const
{
	return Vergil::SerializeAgentRequest(Request, bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeAgentResponse(const FVergilAgentResponse& Response) const
{
	return Vergil::DescribeAgentResponse(Response);
}

FString UVergilAgentSubsystem::InspectAgentResponseAsJson(const FVergilAgentResponse& Response, const bool bPrettyPrint) const
{
	return Vergil::SerializeAgentResponse(Response, bPrettyPrint);
}

FString UVergilAgentSubsystem::DescribeAgentAuditEntry(const FVergilAgentAuditEntry& Entry) const
{
	return Vergil::DescribeAgentAuditEntry(Entry);
}

FString UVergilAgentSubsystem::InspectAgentAuditEntryAsJson(const FVergilAgentAuditEntry& Entry, const bool bPrettyPrint) const
{
	return Vergil::SerializeAgentAuditEntry(Entry, bPrettyPrint);
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
