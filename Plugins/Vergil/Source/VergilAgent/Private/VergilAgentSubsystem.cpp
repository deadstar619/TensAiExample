#include "VergilAgentSubsystem.h"

#include "Engine/Blueprint.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "VergilEditorSubsystem.h"
#include "VergilLog.h"

namespace
{
	inline constexpr TCHAR PersistedAuditTrailFormatName[] = TEXT("Vergil.AgentAuditLog");
	inline constexpr int32 PersistedAuditTrailFormatVersion = 1;
	inline constexpr TCHAR PersistedAuditTrailRelativePath[] = TEXT("Vergil/AgentAuditTrail.json");
	inline constexpr TCHAR DefaultTargetGraphName[] = TEXT("EventGraph");

	FString TrimOptionalPath(const FString& Path)
	{
		return Path.TrimStartAndEnd();
	}

	FString NormalizeBlueprintReference(const FString& BlueprintPath)
	{
		const FString TrimmedPath = TrimOptionalPath(BlueprintPath);
		if (TrimmedPath.IsEmpty())
		{
			return FString();
		}

		if (TrimmedPath.Contains(TEXT(".")))
		{
			const FString PackagePath = FPackageName::ObjectPathToPackageName(TrimmedPath);
			return PackagePath.IsEmpty() ? TrimmedPath : PackagePath;
		}

		return TrimmedPath;
	}

	FString BuildBlueprintObjectPath(const FString& BlueprintPath)
	{
		const FString PackagePath = NormalizeBlueprintReference(BlueprintPath);
		if (PackagePath.IsEmpty())
		{
			return FString();
		}

		if (PackagePath.Contains(TEXT(".")))
		{
			return PackagePath;
		}

		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (AssetName.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
	}

	UBlueprint* ResolveBlueprintFromReference(const FString& BlueprintPath)
	{
		const FString ObjectPath = BuildBlueprintObjectPath(BlueprintPath);
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}

		if (UBlueprint* const ExistingBlueprint = FindObject<UBlueprint>(nullptr, *ObjectPath))
		{
			return ExistingBlueprint;
		}

		return LoadObject<UBlueprint>(nullptr, *ObjectPath);
	}

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

	void AddAgentErrorDiagnostic(FVergilCompileResult& Result, const FName Code, const FString& Message)
	{
		Result.Diagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Error, Code, Message));
		Result.bSucceeded = false;
	}

	FVergilAgentResponse MakeBaseResponse(const FVergilAgentRequest& Request)
	{
		FVergilAgentResponse Response;
		Response.RequestId = Request.Context.RequestId;
		Response.Operation = Request.Operation;
		return Response;
	}

	FString FormatTargetBlueprintLabel(const FString& BlueprintPath)
	{
		return BlueprintPath.IsEmpty() ? FString(TEXT("<none>")) : BlueprintPath;
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

FVergilAgentRequest UVergilAgentSubsystem::MakeApplyRequestFromPlan(
	const FVergilAgentRequestContext& Context,
	const FVergilAgentRequest& PlannedRequest,
	const FVergilCompileResult& PlannedResult) const
{
	FVergilAgentRequest ApplyRequest;
	ApplyRequest.Context = Context;
	ApplyRequest.Operation = EVergilAgentOperation::ApplyCommandPlan;
	ApplyRequest.Apply.TargetBlueprintPath = NormalizeBlueprintReference(
		!PlannedRequest.Plan.TargetBlueprintPath.IsEmpty()
			? PlannedRequest.Plan.TargetBlueprintPath
			: PlannedRequest.Plan.Document.BlueprintPath);
	ApplyRequest.Apply.Commands = PlannedResult.Commands;
	Vergil::NormalizeCommandPlan(ApplyRequest.Apply.Commands);

	FVergilCompileStatistics PlannedStatistics = PlannedResult.Statistics;
	PlannedStatistics.RebuildCommandStatistics(ApplyRequest.Apply.Commands);
	ApplyRequest.Apply.ExpectedCommandPlanFingerprint = PlannedStatistics.CommandPlanFingerprint;
	return ApplyRequest;
}

FVergilAgentResponse UVergilAgentSubsystem::ExecuteRequest(const FVergilAgentRequest& Request)
{
	const FVergilAgentRequest NormalizedRequest = NormalizeRequest(Request);

	FVergilAgentResponse Response;
	switch (NormalizedRequest.Operation)
	{
	case EVergilAgentOperation::PlanDocument:
		Response = ExecutePlanRequest(NormalizedRequest);
		break;

	case EVergilAgentOperation::ApplyCommandPlan:
		Response = ExecuteApplyRequest(NormalizedRequest);
		break;

	default:
		Response = MakeBaseResponse(NormalizedRequest);
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("Agent request is missing a supported operation.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingAgentOperation"),
			TEXT("Agent request must specify either PlanDocument or ApplyCommandPlan."));
		break;
	}

	FVergilAgentAuditEntry AuditEntry;
	AuditEntry.Request = NormalizedRequest;
	AuditEntry.Response = Response;
	RecordAuditEntry(AuditEntry);
	return Response;
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

FVergilReflectionSymbolInfo UVergilAgentSubsystem::InspectReflectionSymbol(const FString& Query) const
{
	return Vergil::InspectReflectionSymbol(Query);
}

FString UVergilAgentSubsystem::DescribeReflectionSymbol(const FString& Query) const
{
	return Vergil::DescribeReflectionSymbol(Vergil::InspectReflectionSymbol(Query));
}

FString UVergilAgentSubsystem::InspectReflectionSymbolAsJson(const FString& Query, const bool bPrettyPrint) const
{
	return Vergil::SerializeReflectionSymbol(Vergil::InspectReflectionSymbol(Query), bPrettyPrint);
}

FVergilReflectionDiscoveryResults UVergilAgentSubsystem::DiscoverReflectionSymbols(const FString& Query, const int32 MaxResults) const
{
	return Vergil::DiscoverReflectionSymbols(Query, MaxResults);
}

FString UVergilAgentSubsystem::DescribeReflectionDiscovery(const FString& Query, const int32 MaxResults) const
{
	return Vergil::DescribeReflectionDiscovery(Vergil::DiscoverReflectionSymbols(Query, MaxResults));
}

FString UVergilAgentSubsystem::InspectReflectionDiscoveryAsJson(const FString& Query, const int32 MaxResults, const bool bPrettyPrint) const
{
	return Vergil::SerializeReflectionDiscovery(Vergil::DiscoverReflectionSymbols(Query, MaxResults), bPrettyPrint);
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

FVergilAgentRequest UVergilAgentSubsystem::NormalizeRequest(const FVergilAgentRequest& Request) const
{
	FVergilAgentRequest NormalizedRequest = Request;
	if (!NormalizedRequest.Context.RequestId.IsValid())
	{
		NormalizedRequest.Context.RequestId = FGuid::NewGuid();
	}

	switch (NormalizedRequest.Operation)
	{
	case EVergilAgentOperation::PlanDocument:
	{
		const FString NormalizedTargetPath = NormalizeBlueprintReference(
			!NormalizedRequest.Plan.TargetBlueprintPath.IsEmpty()
				? NormalizedRequest.Plan.TargetBlueprintPath
				: NormalizedRequest.Plan.Document.BlueprintPath);
		NormalizedRequest.Plan.TargetBlueprintPath = NormalizedTargetPath;

		if (NormalizedRequest.Plan.Document.BlueprintPath.IsEmpty())
		{
			NormalizedRequest.Plan.Document.BlueprintPath = NormalizedTargetPath;
		}
		else
		{
			NormalizedRequest.Plan.Document.BlueprintPath = NormalizeBlueprintReference(NormalizedRequest.Plan.Document.BlueprintPath);
		}

		if (NormalizedRequest.Plan.TargetGraphName.IsNone())
		{
			NormalizedRequest.Plan.TargetGraphName = FName(DefaultTargetGraphName);
		}
		break;
	}

	case EVergilAgentOperation::ApplyCommandPlan:
		NormalizedRequest.Apply.TargetBlueprintPath = NormalizeBlueprintReference(NormalizedRequest.Apply.TargetBlueprintPath);
		NormalizedRequest.Apply.ExpectedCommandPlanFingerprint = NormalizedRequest.Apply.ExpectedCommandPlanFingerprint.TrimStartAndEnd();
		Vergil::NormalizeCommandPlan(NormalizedRequest.Apply.Commands);
		break;

	default:
		break;
	}

	return NormalizedRequest;
}

FVergilAgentResponse UVergilAgentSubsystem::ExecutePlanRequest(const FVergilAgentRequest& Request) const
{
	FVergilAgentResponse Response = MakeBaseResponse(Request);

	if (GEditor == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("Vergil planning requires the editor subsystem.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingEditorContext"),
			TEXT("Vergil agent planning requires an editor context."));
		return Response;
	}

	if (Request.Plan.TargetBlueprintPath.IsEmpty())
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("PlanDocument requests require a target Blueprint path.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingPlanTargetBlueprintPath"),
			TEXT("PlanDocument requests require a target Blueprint path."));
		return Response;
	}

	if (!Request.Plan.Document.BlueprintPath.IsEmpty()
		&& Request.Plan.Document.BlueprintPath != Request.Plan.TargetBlueprintPath)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("PlanDocument request target path does not match the document BlueprintPath.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("PlanTargetBlueprintPathMismatch"),
			FString::Printf(
				TEXT("PlanDocument target '%s' does not match document BlueprintPath '%s'."),
				*Request.Plan.TargetBlueprintPath,
				*Request.Plan.Document.BlueprintPath));
		return Response;
	}

	UBlueprint* const Blueprint = ResolveBlueprintFromReference(Request.Plan.TargetBlueprintPath);
	if (Blueprint == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("PlanDocument request could not resolve the target Blueprint.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("UnableToResolveTargetBlueprint"),
			FString::Printf(
				TEXT("Could not resolve the target Blueprint '%s'."),
				*FormatTargetBlueprintLabel(Request.Plan.TargetBlueprintPath)));
		return Response;
	}

	UVergilEditorSubsystem* const EditorSubsystem = GEditor->GetEditorSubsystem<UVergilEditorSubsystem>();
	if (EditorSubsystem == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("Vergil planning requires the editor subsystem.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingVergilEditorSubsystem"),
			TEXT("UVergilEditorSubsystem was unavailable for plan execution."));
		return Response;
	}

	const FVergilCompileRequest CompileRequest = EditorSubsystem->MakeCompileRequest(
		Blueprint,
		Request.Plan.Document,
		Request.Plan.TargetGraphName,
		Request.Plan.bAutoLayout,
		Request.Plan.bGenerateComments);
	Response.Result = EditorSubsystem->CompileRequest(CompileRequest, false);
	Response.State = Response.Result.bSucceeded ? EVergilAgentExecutionState::Completed : EVergilAgentExecutionState::Failed;
	Response.Message = Response.Result.bSucceeded
		? FString::Printf(
			TEXT("Planned %d commands for '%s'."),
			Response.Result.Commands.Num(),
			*FormatTargetBlueprintLabel(Request.Plan.TargetBlueprintPath))
		: FString::Printf(
			TEXT("Planning failed for '%s'."),
			*FormatTargetBlueprintLabel(Request.Plan.TargetBlueprintPath));
	return Response;
}

FVergilAgentResponse UVergilAgentSubsystem::ExecuteApplyRequest(const FVergilAgentRequest& Request) const
{
	FVergilAgentResponse Response = MakeBaseResponse(Request);
	Response.Result.Commands = Request.Apply.Commands;
	Response.Result.Statistics.bApplyRequested = true;
	Response.Result.Statistics.bCommandPlanNormalized = true;
	Response.Result.Statistics.RebuildCommandStatistics(Response.Result.Commands);

	if (Request.Apply.TargetBlueprintPath.IsEmpty())
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("ApplyCommandPlan requests require a target Blueprint path.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingApplyTargetBlueprintPath"),
			TEXT("ApplyCommandPlan requests require a target Blueprint path."));
		return Response;
	}

	if (Request.Apply.ExpectedCommandPlanFingerprint.IsEmpty())
	{
		Response.State = EVergilAgentExecutionState::Rejected;
		Response.Message = TEXT("ApplyCommandPlan requests require the reviewed command-plan fingerprint.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingExpectedCommandPlanFingerprint"),
			TEXT("ApplyCommandPlan requests require ExpectedCommandPlanFingerprint so apply stays separated from planning."));
		return Response;
	}

	if (Request.Apply.ExpectedCommandPlanFingerprint != Response.Result.Statistics.CommandPlanFingerprint)
	{
		Response.State = EVergilAgentExecutionState::Rejected;
		Response.Message = TEXT("ApplyCommandPlan request fingerprint did not match the normalized command plan.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("CommandPlanFingerprintMismatch"),
			FString::Printf(
				TEXT("ApplyCommandPlan fingerprint '%s' did not match the normalized command plan fingerprint '%s'."),
				*Request.Apply.ExpectedCommandPlanFingerprint,
				*Response.Result.Statistics.CommandPlanFingerprint));
		return Response;
	}

	if (GEditor == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("Vergil apply requires the editor subsystem.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingEditorContext"),
			TEXT("Vergil agent apply requires an editor context."));
		return Response;
	}

	UBlueprint* const Blueprint = ResolveBlueprintFromReference(Request.Apply.TargetBlueprintPath);
	if (Blueprint == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("ApplyCommandPlan request could not resolve the target Blueprint.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("UnableToResolveTargetBlueprint"),
			FString::Printf(
				TEXT("Could not resolve the target Blueprint '%s'."),
				*FormatTargetBlueprintLabel(Request.Apply.TargetBlueprintPath)));
		return Response;
	}

	UVergilEditorSubsystem* const EditorSubsystem = GEditor->GetEditorSubsystem<UVergilEditorSubsystem>();
	if (EditorSubsystem == nullptr)
	{
		Response.State = EVergilAgentExecutionState::Failed;
		Response.Message = TEXT("Vergil apply requires the editor subsystem.");
		AddAgentErrorDiagnostic(
			Response.Result,
			TEXT("MissingVergilEditorSubsystem"),
			TEXT("UVergilEditorSubsystem was unavailable for apply execution."));
		return Response;
	}

	Response.Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, Request.Apply.Commands);
	Response.State = Response.Result.bApplied ? EVergilAgentExecutionState::Completed : EVergilAgentExecutionState::Failed;
	Response.Message = Response.Result.bApplied
		? FString::Printf(
			TEXT("Applied %d commands to '%s'."),
			Response.Result.ExecutedCommandCount,
			*FormatTargetBlueprintLabel(Request.Apply.TargetBlueprintPath))
		: FString::Printf(
			TEXT("Apply failed for '%s'."),
			*FormatTargetBlueprintLabel(Request.Apply.TargetBlueprintPath));
	return Response;
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
