#include "VergilAgentSubsystem.h"

#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "VergilLog.h"

namespace
{
	inline constexpr TCHAR PersistedAuditTrailFormatName[] = TEXT("Vergil.AgentAuditLog");
	inline constexpr int32 PersistedAuditTrailFormatVersion = 1;
	inline constexpr TCHAR PersistedAuditTrailRelativePath[] = TEXT("Vergil/AgentAuditTrail.json");

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

	void TrimAuditEntries(TArray<FVergilAgentAuditEntry>& Entries, const int32 MaxAuditEntries)
	{
		if (MaxAuditEntries >= 0 && Entries.Num() > MaxAuditEntries)
		{
			const int32 NumToRemove = Entries.Num() - MaxAuditEntries;
			Entries.RemoveAt(0, NumToRemove);
		}
	}

	FString GetPersistedAuditTrailPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), PersistedAuditTrailRelativePath);
	}

	bool SerializePersistedAuditTrail(
		const TArray<FVergilAgentAuditEntry>& Entries,
		FString& OutSerializedAuditTrail,
		FString* OutErrorMessage = nullptr)
	{
		TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
		RootObject->SetStringField(TEXT("format"), PersistedAuditTrailFormatName);
		RootObject->SetNumberField(TEXT("version"), PersistedAuditTrailFormatVersion);

		TArray<TSharedPtr<FJsonValue>> SerializedEntries;
		SerializedEntries.Reserve(Entries.Num());
		for (const FVergilAgentAuditEntry& Entry : Entries)
		{
			TSharedRef<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			if (!FJsonObjectConverter::UStructToJsonObject(FVergilAgentAuditEntry::StaticStruct(), &Entry, EntryObject, 0, 0))
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = TEXT("Failed to serialize an agent audit entry.");
				}
				return false;
			}

			SerializedEntries.Add(MakeShared<FJsonValueObject>(EntryObject));
		}

		RootObject->SetArrayField(TEXT("entries"), SerializedEntries);

		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutSerializedAuditTrail);
		if (!FJsonSerializer::Serialize(RootObject, Writer) || !Writer->Close())
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = TEXT("Failed to write the persisted audit log as JSON.");
			}
			return false;
		}

		return true;
	}

	bool DeserializePersistedAuditTrail(
		const FString& SerializedAuditTrail,
		TArray<FVergilAgentAuditEntry>& OutEntries,
		FString* OutErrorMessage = nullptr)
	{
		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SerializedAuditTrail);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = TEXT("Persisted audit log JSON could not be parsed.");
			}
			return false;
		}

		FString FormatName;
		if (!RootObject->TryGetStringField(TEXT("format"), FormatName) || FormatName != PersistedAuditTrailFormatName)
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = FString::Printf(
					TEXT("Persisted audit log format '%s' is unsupported."),
					FormatName.IsEmpty() ? TEXT("<missing>") : *FormatName);
			}
			return false;
		}

		int32 Version = 0;
		if (!RootObject->TryGetNumberField(TEXT("version"), Version) || Version != PersistedAuditTrailFormatVersion)
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = FString::Printf(
					TEXT("Persisted audit log version '%d' is unsupported."),
					Version);
			}
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* EntryValues = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("entries"), EntryValues) || EntryValues == nullptr)
		{
			if (OutErrorMessage != nullptr)
			{
				*OutErrorMessage = TEXT("Persisted audit log is missing the entries array.");
			}
			return false;
		}

		TArray<FVergilAgentAuditEntry> ParsedEntries;
		ParsedEntries.Reserve(EntryValues->Num());
		for (const TSharedPtr<FJsonValue>& EntryValue : *EntryValues)
		{
			if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object)
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = TEXT("Persisted audit log contains a non-object entry.");
				}
				return false;
			}

			FVergilAgentAuditEntry ParsedEntry;
			if (!FJsonObjectConverter::JsonObjectToUStruct(EntryValue->AsObject().ToSharedRef(), &ParsedEntry, 0, 0))
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = TEXT("Persisted audit log contains an entry that could not be deserialized.");
				}
				return false;
			}

			ParsedEntries.Add(NormalizeAuditEntry(ParsedEntry));
		}

		OutEntries = MoveTemp(ParsedEntries);
		return true;
	}
}

void UVergilAgentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadAuditTrailFromDisk();
	UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem initialized with %d persisted audit entries."), AuditTrail.Num());
}

void UVergilAgentSubsystem::Deinitialize()
{
	FlushAuditTrailToDisk();
	UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem deinitialized."));
	AuditTrail.Reset();
	Super::Deinitialize();
}

void UVergilAgentSubsystem::RecordAuditEntry(const FVergilAgentAuditEntry& Entry)
{
	AuditTrail.Add(NormalizeAuditEntry(Entry));
	TrimAuditTrailToMaxEntries();
	if (!FlushAuditTrailToDisk())
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem failed to persist a recorded audit entry."));
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

FString UVergilAgentSubsystem::GetAuditTrailPersistencePath() const
{
	return GetPersistedAuditTrailPath();
}

bool UVergilAgentSubsystem::FlushAuditTrailToDisk()
{
	const FString PersistencePath = GetAuditTrailPersistencePath();
	IFileManager& FileManager = IFileManager::Get();

	if (AuditTrail.Num() == 0)
	{
		return !FileManager.FileExists(*PersistencePath) || FileManager.Delete(*PersistencePath, false, true, true);
	}

	FString ErrorMessage;
	if (!TryWriteAuditTrailToDisk(AuditTrail, &ErrorMessage))
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem failed to flush audit trail: %s"), *ErrorMessage);
		return false;
	}

	return true;
}

bool UVergilAgentSubsystem::ReloadAuditTrailFromDisk()
{
	TArray<FVergilAgentAuditEntry> LoadedEntries;
	FString ErrorMessage;
	if (!TryLoadAuditTrailFromDisk(LoadedEntries, &ErrorMessage))
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem failed to reload persisted audit trail: %s"), *ErrorMessage);
		return false;
	}

	AuditTrail = MoveTemp(LoadedEntries);
	const int32 LoadedEntryCount = AuditTrail.Num();
	TrimAuditTrailToMaxEntries();
	if (AuditTrail.Num() != LoadedEntryCount && !FlushAuditTrailToDisk())
	{
		return false;
	}

	return true;
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
	if (!FlushAuditTrailToDisk())
	{
		UE_LOG(LogVergil, Log, TEXT("Vergil agent subsystem failed to delete the persisted audit trail after clearing it."));
	}
}

void UVergilAgentSubsystem::TrimAuditTrailToMaxEntries()
{
	TrimAuditEntries(AuditTrail, MaxAuditEntries);
}

bool UVergilAgentSubsystem::TryLoadAuditTrailFromDisk(TArray<FVergilAgentAuditEntry>& OutEntries, FString* OutErrorMessage) const
{
	const FString PersistencePath = GetAuditTrailPersistencePath();
	if (!IFileManager::Get().FileExists(*PersistencePath))
	{
		OutEntries.Reset();
		return true;
	}

	FString SerializedAuditTrail;
	if (!FFileHelper::LoadFileToString(SerializedAuditTrail, *PersistencePath))
	{
		if (OutErrorMessage != nullptr)
		{
			*OutErrorMessage = FString::Printf(TEXT("Could not read '%s'."), *PersistencePath);
		}
		return false;
	}

	return DeserializePersistedAuditTrail(SerializedAuditTrail, OutEntries, OutErrorMessage);
}

bool UVergilAgentSubsystem::TryWriteAuditTrailToDisk(const TArray<FVergilAgentAuditEntry>& Entries, FString* OutErrorMessage) const
{
	const FString PersistencePath = GetAuditTrailPersistencePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PersistencePath), true);

	FString SerializedAuditTrail;
	if (!SerializePersistedAuditTrail(Entries, SerializedAuditTrail, OutErrorMessage))
	{
		return false;
	}

	if (!FFileHelper::SaveStringToFile(
		SerializedAuditTrail,
		*PersistencePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		if (OutErrorMessage != nullptr)
		{
			*OutErrorMessage = FString::Printf(TEXT("Could not write '%s'."), *PersistencePath);
		}
		return false;
	}

	return true;
}
