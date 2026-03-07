#include "VergilAgentTypes.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace
{
	inline constexpr TCHAR AgentRequestFormatName[] = TEXT("Vergil.AgentRequest");
	inline constexpr int32 AgentRequestFormatVersion = 1;
	inline constexpr TCHAR AgentResponseFormatName[] = TEXT("Vergil.AgentResponse");
	inline constexpr int32 AgentResponseFormatVersion = 1;
	inline constexpr TCHAR AgentAuditEntryFormatName[] = TEXT("Vergil.AgentAuditEntry");
	inline constexpr int32 AgentAuditEntryFormatVersion = 1;

	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString LexGuidString(const FGuid& Value)
	{
		return Value.IsValid() ? Value.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	}

	FString LexOptionalNameString(const FName Value)
	{
		return Value.IsNone() ? FString(TEXT("<none>")) : Value.ToString();
	}

	const TCHAR* LexAgentExecutionStateString(const EVergilAgentExecutionState State)
	{
		switch (State)
		{
		case EVergilAgentExecutionState::Pending:
			return TEXT("Pending");

		case EVergilAgentExecutionState::Approved:
			return TEXT("Approved");

		case EVergilAgentExecutionState::Completed:
			return TEXT("Completed");

		case EVergilAgentExecutionState::Rejected:
			return TEXT("Rejected");

		case EVergilAgentExecutionState::Failed:
			return TEXT("Failed");

		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LexAgentOperationString(const EVergilAgentOperation Operation)
	{
		switch (Operation)
		{
		case EVergilAgentOperation::None:
			return TEXT("None");

		case EVergilAgentOperation::PlanDocument:
			return TEXT("PlanDocument");

		case EVergilAgentOperation::ApplyCommandPlan:
			return TEXT("ApplyCommandPlan");

		default:
			return TEXT("Unknown");
		}
	}

	FString EscapeDisplayValue(const FString& Value)
	{
		FString EscapedValue = Value;
		EscapedValue.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		EscapedValue.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return EscapedValue;
	}

	FString JoinNameArray(const TArray<FName>& Values)
	{
		if (Values.Num() == 0)
		{
			return TEXT("<none>");
		}

		TArray<FString> Tokens;
		Tokens.Reserve(Values.Num());
		for (const FName Value : Values)
		{
			Tokens.Add(Value.IsNone() ? FString(TEXT("<none>")) : Value.ToString());
		}

		return FString::Join(Tokens, TEXT(", "));
	}

	bool HasDocumentPayload(const FVergilGraphDocument& Document)
	{
		return Document.SchemaVersion > 0
			|| !Document.BlueprintPath.IsEmpty()
			|| Document.Metadata.Num() > 0
			|| Document.Variables.Num() > 0
			|| Document.Functions.Num() > 0
			|| Document.Dispatchers.Num() > 0
			|| Document.Macros.Num() > 0
			|| Document.Components.Num() > 0
			|| Document.Interfaces.Num() > 0
			|| Document.ClassDefaults.Num() > 0
			|| Document.ConstructionScriptNodes.Num() > 0
			|| Document.ConstructionScriptEdges.Num() > 0
			|| Document.Nodes.Num() > 0
			|| Document.Edges.Num() > 0
			|| Document.Tags.Num() > 0;
	}

	bool HasCompileResultPayload(const FVergilCompileResult& Result)
	{
		return Result.bSucceeded
			|| Result.bApplied
			|| Result.ExecutedCommandCount > 0
			|| Result.Diagnostics.Num() > 0
			|| Result.Commands.Num() > 0
			|| Result.PassRecords.Num() > 0
			|| Result.Statistics.PlannedCommandCount > 0
			|| Result.Statistics.RequestedSchemaVersion > 0
			|| Result.Statistics.EffectiveSchemaVersion > 0;
	}

	template <typename PrintPolicy>
	void WriteNameArray(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const TArray<FName>& Values)
	{
		Writer.WriteArrayStart(FieldName);
		for (const FName Value : Values)
		{
			Writer.WriteValue(Value.ToString());
		}
		Writer.WriteArrayEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentRequestContextJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentRequestContext& Context)
	{
		Writer.WriteObjectStart(TEXT("context"));
		Writer.WriteValue(TEXT("requestId"), LexGuidString(Context.RequestId));
		Writer.WriteValue(TEXT("summary"), Context.Summary);
		Writer.WriteValue(TEXT("inputText"), Context.InputText);
		WriteNameArray(Writer, TEXT("tags"), Context.Tags);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentPlanPayloadJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentPlanPayload& Payload)
	{
		Writer.WriteObjectStart(TEXT("plan"));
		Writer.WriteValue(TEXT("targetBlueprintPath"), Payload.TargetBlueprintPath);
		Writer.WriteValue(TEXT("targetGraphName"), Payload.TargetGraphName.ToString());
		Writer.WriteValue(TEXT("autoLayoutRequested"), Payload.bAutoLayout);
		Writer.WriteValue(TEXT("generateCommentsRequested"), Payload.bGenerateComments);
		Writer.WriteRawJSONValue(TEXT("document"), Vergil::SerializeGraphDocument(Payload.Document, false));
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentApplyPayloadJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentApplyPayload& Payload)
	{
		Writer.WriteObjectStart(TEXT("apply"));
		Writer.WriteValue(TEXT("targetBlueprintPath"), Payload.TargetBlueprintPath);
		Writer.WriteValue(TEXT("expectedCommandPlanFingerprint"), Payload.ExpectedCommandPlanFingerprint);
		Writer.WriteRawJSONValue(TEXT("commandPlan"), Vergil::SerializeCommandPlan(Payload.Commands, false));
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentRequestJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentRequest& Request)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), AgentRequestFormatName);
		Writer.WriteValue(TEXT("version"), AgentRequestFormatVersion);
		Writer.WriteValue(TEXT("operation"), LexAgentOperationString(Request.Operation));
		Writer.WriteValue(TEXT("writeRequest"), Request.IsWriteRequest());
		WriteAgentRequestContextJson(Writer, Request.Context);

		switch (Request.Operation)
		{
		case EVergilAgentOperation::PlanDocument:
			WriteAgentPlanPayloadJson(Writer, Request.Plan);
			break;

		case EVergilAgentOperation::ApplyCommandPlan:
			WriteAgentApplyPayloadJson(Writer, Request.Apply);
			break;

		default:
			break;
		}

		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentResponseJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentResponse& Response)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), AgentResponseFormatName);
		Writer.WriteValue(TEXT("version"), AgentResponseFormatVersion);
		Writer.WriteValue(TEXT("requestId"), LexGuidString(Response.RequestId));
		Writer.WriteValue(TEXT("operation"), LexAgentOperationString(Response.Operation));
		Writer.WriteValue(TEXT("state"), LexAgentExecutionStateString(Response.State));
		Writer.WriteValue(TEXT("message"), Response.Message);

		if (HasCompileResultPayload(Response.Result))
		{
			Writer.WriteRawJSONValue(TEXT("result"), Vergil::SerializeCompileResult(Response.Result, false));
		}

		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteAgentAuditEntryJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilAgentAuditEntry& Entry)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), AgentAuditEntryFormatName);
		Writer.WriteValue(TEXT("version"), AgentAuditEntryFormatVersion);
		Writer.WriteValue(TEXT("timestampUtc"), Entry.TimestampUtc);
		Writer.WriteRawJSONValue(TEXT("request"), Vergil::SerializeAgentRequest(Entry.Request, false));
		Writer.WriteRawJSONValue(TEXT("response"), Vergil::SerializeAgentResponse(Entry.Response, false));
		Writer.WriteObjectEnd();
	}
}

FString FVergilAgentRequestContext::ToDisplayString() const
{
	return FString::Printf(
		TEXT("requestId=%s tags=%d"),
		RequestId.IsValid() ? *LexGuidString(RequestId) : TEXT("<none>"),
		Tags.Num());
}

FString FVergilAgentPlanPayload::ToDisplayString() const
{
	return FString::Printf(
		TEXT("targetBlueprint=%s graph=%s autoLayout=%s comments=%s hasDocument=%s"),
		TargetBlueprintPath.IsEmpty() ? TEXT("<none>") : *TargetBlueprintPath,
		*LexOptionalNameString(TargetGraphName),
		LexBoolString(bAutoLayout),
		LexBoolString(bGenerateComments),
		LexBoolString(HasDocumentPayload(Document)));
}

FString FVergilAgentApplyPayload::ToDisplayString() const
{
	return FString::Printf(
		TEXT("targetBlueprint=%s commands=%d expectedFingerprint=%s"),
		TargetBlueprintPath.IsEmpty() ? TEXT("<none>") : *TargetBlueprintPath,
		Commands.Num(),
		ExpectedCommandPlanFingerprint.IsEmpty() ? TEXT("<none>") : *ExpectedCommandPlanFingerprint);
}

bool FVergilAgentRequest::IsWriteRequest() const
{
	return Operation == EVergilAgentOperation::ApplyCommandPlan;
}

FString FVergilAgentRequest::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s version=%d operation=%s write=%s context={%s}"),
		AgentRequestFormatName,
		AgentRequestFormatVersion,
		LexAgentOperationString(Operation),
		LexBoolString(IsWriteRequest()),
		*Context.ToDisplayString());
}

FString FVergilAgentResponse::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s version=%d requestId=%s operation=%s state=%s hasResult=%s"),
		AgentResponseFormatName,
		AgentResponseFormatVersion,
		RequestId.IsValid() ? *LexGuidString(RequestId) : TEXT("<none>"),
		LexAgentOperationString(Operation),
		LexAgentExecutionStateString(State),
		LexBoolString(HasCompileResultPayload(Result)));
}

FString FVergilAgentAuditEntry::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s version=%d timestampUtc=%s"),
		AgentAuditEntryFormatName,
		AgentAuditEntryFormatVersion,
		TimestampUtc.IsEmpty() ? TEXT("<none>") : *TimestampUtc);
}

FString Vergil::GetAgentRequestFormatName()
{
	return AgentRequestFormatName;
}

int32 Vergil::GetAgentRequestFormatVersion()
{
	return AgentRequestFormatVersion;
}

FString Vergil::DescribeAgentRequest(const FVergilAgentRequest& Request)
{
	TArray<FString> Lines;
	Lines.Add(Request.ToDisplayString());
	Lines.Add(FString::Printf(TEXT("summary: %s"), Request.Context.Summary.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(Request.Context.Summary)));
	Lines.Add(FString::Printf(TEXT("inputText: %s"), Request.Context.InputText.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(Request.Context.InputText)));
	Lines.Add(FString::Printf(TEXT("tags: %s"), *JoinNameArray(Request.Context.Tags)));

	switch (Request.Operation)
	{
	case EVergilAgentOperation::PlanDocument:
		Lines.Add(TEXT("plan:"));
		Lines.Add(Request.Plan.ToDisplayString());
		Lines.Add(TEXT("document:"));
		Lines.Add(Vergil::DescribeGraphDocument(Request.Plan.Document));
		break;

	case EVergilAgentOperation::ApplyCommandPlan:
		Lines.Add(TEXT("apply:"));
		Lines.Add(Request.Apply.ToDisplayString());
		Lines.Add(TEXT("commandPlan:"));
		Lines.Add(Vergil::DescribeCommandPlan(Request.Apply.Commands));
		break;

	default:
		Lines.Add(TEXT("payload: <none>"));
		break;
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeAgentRequest(const FVergilAgentRequest& Request, const bool bPrettyPrint)
{
	FString SerializedRequest;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedRequest);
		WriteAgentRequestJson(*Writer, Request);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedRequest);
		WriteAgentRequestJson(*Writer, Request);
		Writer->Close();
	}

	return SerializedRequest;
}

FString Vergil::GetAgentResponseFormatName()
{
	return AgentResponseFormatName;
}

int32 Vergil::GetAgentResponseFormatVersion()
{
	return AgentResponseFormatVersion;
}

FString Vergil::DescribeAgentResponse(const FVergilAgentResponse& Response)
{
	TArray<FString> Lines;
	Lines.Add(Response.ToDisplayString());
	Lines.Add(FString::Printf(TEXT("message: %s"), Response.Message.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(Response.Message)));

	if (HasCompileResultPayload(Response.Result))
	{
		Lines.Add(TEXT("result:"));
		Lines.Add(Vergil::DescribeCompileResult(Response.Result));
	}
	else
	{
		Lines.Add(TEXT("result: <none>"));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeAgentResponse(const FVergilAgentResponse& Response, const bool bPrettyPrint)
{
	FString SerializedResponse;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedResponse);
		WriteAgentResponseJson(*Writer, Response);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedResponse);
		WriteAgentResponseJson(*Writer, Response);
		Writer->Close();
	}

	return SerializedResponse;
}

FString Vergil::GetAgentAuditEntryFormatName()
{
	return AgentAuditEntryFormatName;
}

int32 Vergil::GetAgentAuditEntryFormatVersion()
{
	return AgentAuditEntryFormatVersion;
}

FString Vergil::DescribeAgentAuditEntry(const FVergilAgentAuditEntry& Entry)
{
	TArray<FString> Lines;
	Lines.Add(Entry.ToDisplayString());
	Lines.Add(TEXT("request:"));
	Lines.Add(Vergil::DescribeAgentRequest(Entry.Request));
	Lines.Add(TEXT("response:"));
	Lines.Add(Vergil::DescribeAgentResponse(Entry.Response));
	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeAgentAuditEntry(const FVergilAgentAuditEntry& Entry, const bool bPrettyPrint)
{
	FString SerializedEntry;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedEntry);
		WriteAgentAuditEntryJson(*Writer, Entry);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedEntry);
		WriteAgentAuditEntryJson(*Writer, Entry);
		Writer->Close();
	}

	return SerializedEntry;
}
