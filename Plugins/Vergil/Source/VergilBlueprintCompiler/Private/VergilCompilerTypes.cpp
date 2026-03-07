#include "VergilCompilerTypes.h"

#include "Algo/StableSort.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Misc/Crc.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	inline constexpr TCHAR CommandPlanFormatName[] = TEXT("Vergil.CommandPlan");
	inline constexpr int32 CommandPlanFormatVersion = 1;
	inline constexpr TCHAR DiagnosticsInspectionFormatName[] = TEXT("Vergil.Diagnostics");
	inline constexpr int32 DiagnosticsInspectionFormatVersion = 1;
	inline constexpr TCHAR CompileResultInspectionFormatName[] = TEXT("Vergil.CompileResult");
	inline constexpr int32 CompileResultInspectionFormatVersion = 1;
	inline constexpr TCHAR CommandPlanPreviewInspectionFormatName[] = TEXT("Vergil.CommandPlanPreview");
	inline constexpr int32 CommandPlanPreviewInspectionFormatVersion = 1;

	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	const TCHAR* LexCommandTypeString(const EVergilCommandType Type)
	{
		switch (Type)
		{
		case EVergilCommandType::EnsureDispatcher:
			return TEXT("EnsureDispatcher");
		case EVergilCommandType::AddDispatcherParameter:
			return TEXT("AddDispatcherParameter");
		case EVergilCommandType::SetBlueprintMetadata:
			return TEXT("SetBlueprintMetadata");
		case EVergilCommandType::EnsureVariable:
			return TEXT("EnsureVariable");
		case EVergilCommandType::SetVariableMetadata:
			return TEXT("SetVariableMetadata");
		case EVergilCommandType::SetVariableDefault:
			return TEXT("SetVariableDefault");
		case EVergilCommandType::EnsureFunctionGraph:
			return TEXT("EnsureFunctionGraph");
		case EVergilCommandType::EnsureMacroGraph:
			return TEXT("EnsureMacroGraph");
		case EVergilCommandType::EnsureComponent:
			return TEXT("EnsureComponent");
		case EVergilCommandType::AttachComponent:
			return TEXT("AttachComponent");
		case EVergilCommandType::SetComponentProperty:
			return TEXT("SetComponentProperty");
		case EVergilCommandType::EnsureInterface:
			return TEXT("EnsureInterface");
		case EVergilCommandType::SetClassDefault:
			return TEXT("SetClassDefault");
		case EVergilCommandType::EnsureGraph:
			return TEXT("EnsureGraph");
		case EVergilCommandType::AddNode:
			return TEXT("AddNode");
		case EVergilCommandType::SetNodeMetadata:
			return TEXT("SetNodeMetadata");
		case EVergilCommandType::ConnectPins:
			return TEXT("ConnectPins");
		case EVergilCommandType::RemoveNode:
			return TEXT("RemoveNode");
		case EVergilCommandType::RenameMember:
			return TEXT("RenameMember");
		case EVergilCommandType::MoveNode:
			return TEXT("MoveNode");
		case EVergilCommandType::FinalizeNode:
			return TEXT("FinalizeNode");
		case EVergilCommandType::CompileBlueprint:
			return TEXT("CompileBlueprint");
		default:
			return TEXT("Unknown");
		}
	}

	bool TryParseCommandTypeString(const FString& InValue, EVergilCommandType& OutType)
	{
		if (InValue == TEXT("EnsureDispatcher"))
		{
			OutType = EVergilCommandType::EnsureDispatcher;
			return true;
		}
		if (InValue == TEXT("AddDispatcherParameter"))
		{
			OutType = EVergilCommandType::AddDispatcherParameter;
			return true;
		}
		if (InValue == TEXT("SetBlueprintMetadata"))
		{
			OutType = EVergilCommandType::SetBlueprintMetadata;
			return true;
		}
		if (InValue == TEXT("EnsureVariable"))
		{
			OutType = EVergilCommandType::EnsureVariable;
			return true;
		}
		if (InValue == TEXT("SetVariableMetadata"))
		{
			OutType = EVergilCommandType::SetVariableMetadata;
			return true;
		}
		if (InValue == TEXT("SetVariableDefault"))
		{
			OutType = EVergilCommandType::SetVariableDefault;
			return true;
		}
		if (InValue == TEXT("EnsureFunctionGraph"))
		{
			OutType = EVergilCommandType::EnsureFunctionGraph;
			return true;
		}
		if (InValue == TEXT("EnsureMacroGraph"))
		{
			OutType = EVergilCommandType::EnsureMacroGraph;
			return true;
		}
		if (InValue == TEXT("EnsureComponent"))
		{
			OutType = EVergilCommandType::EnsureComponent;
			return true;
		}
		if (InValue == TEXT("AttachComponent"))
		{
			OutType = EVergilCommandType::AttachComponent;
			return true;
		}
		if (InValue == TEXT("SetComponentProperty"))
		{
			OutType = EVergilCommandType::SetComponentProperty;
			return true;
		}
		if (InValue == TEXT("EnsureInterface"))
		{
			OutType = EVergilCommandType::EnsureInterface;
			return true;
		}
		if (InValue == TEXT("SetClassDefault"))
		{
			OutType = EVergilCommandType::SetClassDefault;
			return true;
		}
		if (InValue == TEXT("EnsureGraph"))
		{
			OutType = EVergilCommandType::EnsureGraph;
			return true;
		}
		if (InValue == TEXT("AddNode"))
		{
			OutType = EVergilCommandType::AddNode;
			return true;
		}
		if (InValue == TEXT("SetNodeMetadata"))
		{
			OutType = EVergilCommandType::SetNodeMetadata;
			return true;
		}
		if (InValue == TEXT("ConnectPins"))
		{
			OutType = EVergilCommandType::ConnectPins;
			return true;
		}
		if (InValue == TEXT("RemoveNode"))
		{
			OutType = EVergilCommandType::RemoveNode;
			return true;
		}
		if (InValue == TEXT("RenameMember"))
		{
			OutType = EVergilCommandType::RenameMember;
			return true;
		}
		if (InValue == TEXT("MoveNode"))
		{
			OutType = EVergilCommandType::MoveNode;
			return true;
		}
		if (InValue == TEXT("FinalizeNode"))
		{
			OutType = EVergilCommandType::FinalizeNode;
			return true;
		}
		if (InValue == TEXT("CompileBlueprint"))
		{
			OutType = EVergilCommandType::CompileBlueprint;
			return true;
		}

		return false;
	}

	FString LexNameString(const FName Value)
	{
		return Value.IsNone() ? FString() : Value.ToString();
	}

	FString LexGuidString(const FGuid& Value)
	{
		return Value.IsValid() ? Value.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
	}

	FString LexOptionalNameString(const FName Value)
	{
		return Value.IsNone() ? FString(TEXT("<none>")) : Value.ToString();
	}

	const TCHAR* LexDiagnosticSeverityString(const EVergilDiagnosticSeverity Severity)
	{
		switch (Severity)
		{
		case EVergilDiagnosticSeverity::Info:
			return TEXT("Info");

		case EVergilDiagnosticSeverity::Warning:
			return TEXT("Warning");

		case EVergilDiagnosticSeverity::Error:
			return TEXT("Error");

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

	FString BuildCommandPlanFingerprint(const TArray<FVergilCompilerCommand>& Commands)
	{
		const FString SerializedPlan = Vergil::SerializeCommandPlan(Commands, false);
		return FString::Printf(TEXT("%08x"), FCrc::StrCrc32(*SerializedPlan));
	}

	void AddSerializationDiagnostic(
		TArray<FVergilDiagnostic>* Diagnostics,
		const FName Code,
		const FString& Message)
	{
		if (Diagnostics == nullptr)
		{
			return;
		}

		Diagnostics->Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Error, Code, Message));
	}

	template <typename PrintPolicy>
	void WritePlannedPinJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilPlannedPin& PlannedPin)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("pinId"), LexGuidString(PlannedPin.PinId));
		Writer.WriteValue(TEXT("name"), LexNameString(PlannedPin.Name));
		Writer.WriteValue(TEXT("isInput"), PlannedPin.bIsInput);
		Writer.WriteValue(TEXT("isExec"), PlannedPin.bIsExec);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteCommandJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilCompilerCommand& Command)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("type"), LexCommandTypeString(Command.Type));
		Writer.WriteValue(TEXT("graphName"), LexNameString(Command.GraphName));
		Writer.WriteValue(TEXT("nodeId"), LexGuidString(Command.NodeId));
		Writer.WriteValue(TEXT("sourceNodeId"), LexGuidString(Command.SourceNodeId));
		Writer.WriteValue(TEXT("sourcePinId"), LexGuidString(Command.SourcePinId));
		Writer.WriteValue(TEXT("targetNodeId"), LexGuidString(Command.TargetNodeId));
		Writer.WriteValue(TEXT("targetPinId"), LexGuidString(Command.TargetPinId));
		Writer.WriteValue(TEXT("name"), LexNameString(Command.Name));
		Writer.WriteValue(TEXT("secondaryName"), LexNameString(Command.SecondaryName));
		Writer.WriteValue(TEXT("stringValue"), Command.StringValue);

		Writer.WriteObjectStart(TEXT("attributes"));
		TArray<FName> AttributeKeys;
		Command.Attributes.GetKeys(AttributeKeys);
		AttributeKeys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName AttributeKey : AttributeKeys)
		{
			Writer.WriteValue(AttributeKey.ToString(), Command.Attributes.FindRef(AttributeKey));
		}
		Writer.WriteObjectEnd();

		Writer.WriteObjectStart(TEXT("position"));
		Writer.WriteValue(TEXT("x"), Command.Position.X);
		Writer.WriteValue(TEXT("y"), Command.Position.Y);
		Writer.WriteObjectEnd();

		Writer.WriteArrayStart(TEXT("plannedPins"));
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			WritePlannedPinJson(Writer, PlannedPin);
		}
		Writer.WriteArrayEnd();

		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteCommandPlanJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TArray<FVergilCompilerCommand>& Commands)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), CommandPlanFormatName);
		Writer.WriteValue(TEXT("version"), CommandPlanFormatVersion);
		Writer.WriteArrayStart(TEXT("commands"));
		for (const FVergilCompilerCommand& Command : Commands)
		{
			WriteCommandJson(Writer, Command);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDiagnosticJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilDiagnostic& Diagnostic)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("severity"), LexDiagnosticSeverityString(Diagnostic.Severity));
		Writer.WriteValue(TEXT("code"), LexNameString(Diagnostic.Code));
		Writer.WriteValue(TEXT("message"), Diagnostic.Message);
		Writer.WriteValue(TEXT("sourceId"), LexGuidString(Diagnostic.SourceId));
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDiagnosticsJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TArray<FVergilDiagnostic>& Diagnostics)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), DiagnosticsInspectionFormatName);
		Writer.WriteValue(TEXT("version"), DiagnosticsInspectionFormatVersion);
		Writer.WriteArrayStart(TEXT("diagnostics"));
		for (const FVergilDiagnostic& Diagnostic : Diagnostics)
		{
			WriteDiagnosticJson(Writer, Diagnostic);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDiagnosticsJsonField(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const TArray<FVergilDiagnostic>& Diagnostics)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("format"), DiagnosticsInspectionFormatName);
		Writer.WriteValue(TEXT("version"), DiagnosticsInspectionFormatVersion);
		Writer.WriteArrayStart(TEXT("diagnostics"));
		for (const FVergilDiagnostic& Diagnostic : Diagnostics)
		{
			WriteDiagnosticJson(Writer, Diagnostic);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDiagnosticSummaryJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilDiagnosticSummary& Summary)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("infoCount"), Summary.InfoCount);
		Writer.WriteValue(TEXT("warningCount"), Summary.WarningCount);
		Writer.WriteValue(TEXT("errorCount"), Summary.ErrorCount);
		Writer.WriteValue(TEXT("totalCount"), Summary.GetTotalCount());
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteExecutionSummaryJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilExecutionSummary& Summary)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("label"), Summary.Label);
		Writer.WriteValue(TEXT("succeeded"), Summary.bSucceeded);
		Writer.WriteValue(TEXT("applied"), Summary.bApplied);
		Writer.WriteValue(TEXT("plannedCommandCount"), Summary.PlannedCommandCount);
		Writer.WriteValue(TEXT("executedCommandCount"), Summary.ExecutedCommandCount);
		WriteDiagnosticSummaryJson(Writer, TEXT("diagnostics"), Summary.Diagnostics);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteCompilePassRecordJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilCompilePassRecord& PassRecord)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("passName"), LexNameString(PassRecord.PassName));
		Writer.WriteValue(TEXT("succeeded"), PassRecord.bSucceeded);
		Writer.WriteValue(TEXT("diagnosticCount"), PassRecord.DiagnosticCount);
		Writer.WriteValue(TEXT("errorCount"), PassRecord.ErrorCount);
		Writer.WriteValue(TEXT("plannedCommandCount"), PassRecord.PlannedCommandCount);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteUndoRedoSnapshotJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilUndoRedoSnapshot& Snapshot)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("queueLength"), Snapshot.QueueLength);
		Writer.WriteValue(TEXT("undoCount"), Snapshot.UndoCount);
		Writer.WriteValue(TEXT("canUndo"), Snapshot.bCanUndo);
		Writer.WriteValue(TEXT("canRedo"), Snapshot.bCanRedo);
		Writer.WriteValue(TEXT("blueprintReferencedByUndoBuffer"), Snapshot.bBlueprintReferencedByUndoBuffer);
		Writer.WriteValue(TEXT("nextUndoTransactionId"), LexGuidString(Snapshot.NextUndoTransactionId));
		Writer.WriteValue(TEXT("nextUndoTitle"), Snapshot.NextUndoTitle);
		Writer.WriteValue(TEXT("nextUndoContext"), Snapshot.NextUndoContext);
		Writer.WriteValue(TEXT("nextUndoPrimaryObjectPath"), Snapshot.NextUndoPrimaryObjectPath);
		Writer.WriteValue(TEXT("nextRedoTransactionId"), LexGuidString(Snapshot.NextRedoTransactionId));
		Writer.WriteValue(TEXT("nextRedoTitle"), Snapshot.NextRedoTitle);
		Writer.WriteValue(TEXT("nextRedoContext"), Snapshot.NextRedoContext);
		Writer.WriteValue(TEXT("nextRedoPrimaryObjectPath"), Snapshot.NextRedoPrimaryObjectPath);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteTransactionAuditJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilTransactionAudit& TransactionAudit)
	{
		Writer.WriteObjectStart(TEXT("transactionAudit"));
		Writer.WriteValue(TEXT("recorded"), TransactionAudit.bRecorded);
		Writer.WriteValue(TEXT("nestedInActiveTransaction"), TransactionAudit.bNestedInActiveTransaction);
		Writer.WriteValue(TEXT("openedScopedTransaction"), TransactionAudit.bOpenedScopedTransaction);
		Writer.WriteValue(TEXT("transactionContext"), TransactionAudit.TransactionContext);
		Writer.WriteValue(TEXT("transactionTitle"), TransactionAudit.TransactionTitle);
		Writer.WriteValue(TEXT("primaryObjectPath"), TransactionAudit.PrimaryObjectPath);
		WriteUndoRedoSnapshotJson(Writer, TEXT("before"), TransactionAudit.BeforeState);
		Writer.WriteObjectStart(TEXT("recovery"));
		Writer.WriteValue(TEXT("required"), TransactionAudit.bRecoveryRequired);
		Writer.WriteValue(TEXT("attempted"), TransactionAudit.bRecoveryAttempted);
		Writer.WriteValue(TEXT("succeeded"), TransactionAudit.bRecoverySucceeded);
		Writer.WriteValue(TEXT("method"), TransactionAudit.RecoveryMethod);
		Writer.WriteValue(TEXT("message"), TransactionAudit.RecoveryMessage);
		WriteUndoRedoSnapshotJson(Writer, TEXT("failure"), TransactionAudit.FailureState);
		Writer.WriteObjectEnd();
		WriteUndoRedoSnapshotJson(Writer, TEXT("after"), TransactionAudit.AfterState);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteCompileStatisticsJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilCompileStatistics& Statistics)
	{
		Writer.WriteObjectStart(TEXT("statistics"));
		Writer.WriteValue(TEXT("targetGraphName"), LexNameString(Statistics.TargetGraphName));
		Writer.WriteValue(TEXT("requestedSchemaVersion"), Statistics.RequestedSchemaVersion);
		Writer.WriteValue(TEXT("effectiveSchemaVersion"), Statistics.EffectiveSchemaVersion);
		Writer.WriteValue(TEXT("autoLayoutRequested"), Statistics.bAutoLayoutRequested);
		Writer.WriteValue(TEXT("generateCommentsRequested"), Statistics.bGenerateCommentsRequested);
		Writer.WriteValue(TEXT("applyRequested"), Statistics.bApplyRequested);
		Writer.WriteValue(TEXT("executionAttempted"), Statistics.bExecutionAttempted);
		Writer.WriteValue(TEXT("commandPlanNormalized"), Statistics.bCommandPlanNormalized);
		Writer.WriteValue(TEXT("commandPlanFingerprint"), Statistics.CommandPlanFingerprint);
		Writer.WriteValue(TEXT("executionUsedReturnedCommandPlan"), Statistics.bExecutionUsedReturnedCommandPlan);
		Writer.WriteValue(TEXT("planningInvocationCount"), Statistics.PlanningInvocationCount);
		Writer.WriteValue(TEXT("applyInvocationCount"), Statistics.ApplyInvocationCount);
		WriteTransactionAuditJson(Writer, Statistics.TransactionAudit);
		Writer.WriteValue(TEXT("sourceNodeCount"), Statistics.SourceNodeCount);
		Writer.WriteValue(TEXT("sourceEdgeCount"), Statistics.SourceEdgeCount);
		Writer.WriteValue(TEXT("plannedCommandCount"), Statistics.PlannedCommandCount);
		Writer.WriteValue(TEXT("blueprintDefinitionCommandCount"), Statistics.BlueprintDefinitionCommandCount);
		Writer.WriteValue(TEXT("graphStructureCommandCount"), Statistics.GraphStructureCommandCount);
		Writer.WriteValue(TEXT("connectionCommandCount"), Statistics.ConnectionCommandCount);
		Writer.WriteValue(TEXT("finalizeCommandCount"), Statistics.FinalizeCommandCount);
		Writer.WriteValue(TEXT("explicitCompileCommandCount"), Statistics.ExplicitCompileCommandCount);
		Writer.WriteValue(TEXT("postBlueprintCompileCommandCount"), Statistics.PostBlueprintCompileCommandCount);
		Writer.WriteValue(TEXT("lastCompletedPassName"), LexNameString(Statistics.LastCompletedPassName));
		Writer.WriteValue(TEXT("failedPassName"), LexNameString(Statistics.FailedPassName));
		Writer.WriteArrayStart(TEXT("completedPassNames"));
		for (const FName CompletedPassName : Statistics.CompletedPassNames)
		{
			Writer.WriteValue(CompletedPassName.ToString());
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteCompileResultJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilCompileResult& Result)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), CompileResultInspectionFormatName);
		Writer.WriteValue(TEXT("version"), CompileResultInspectionFormatVersion);
		Writer.WriteValue(TEXT("succeeded"), Result.bSucceeded);
		Writer.WriteValue(TEXT("applied"), Result.bApplied);
		Writer.WriteValue(TEXT("executedCommandCount"), Result.ExecutedCommandCount);
		WriteCompileStatisticsJson(Writer, Result.Statistics);
		WriteDiagnosticSummaryJson(Writer, TEXT("diagnosticSummary"), Vergil::SummarizeDiagnostics(Result.Diagnostics));
		WriteExecutionSummaryJson(Writer, TEXT("compileSummary"), Vergil::SummarizeCompileResult(Result));
		WriteExecutionSummaryJson(Writer, TEXT("applySummary"), Vergil::SummarizeApplyResult(Result));

		Writer.WriteArrayStart(TEXT("passRecords"));
		for (const FVergilCompilePassRecord& PassRecord : Result.PassRecords)
		{
			WriteCompilePassRecordJson(Writer, PassRecord);
		}
		Writer.WriteArrayEnd();

		WriteDiagnosticsJsonField(Writer, TEXT("diagnostics"), Result.Diagnostics);

		Writer.WriteObjectStart(TEXT("commandPlan"));
		Writer.WriteValue(TEXT("format"), CommandPlanFormatName);
		Writer.WriteValue(TEXT("version"), CommandPlanFormatVersion);
		Writer.WriteArrayStart(TEXT("commands"));
		for (const FVergilCompilerCommand& Command : Result.Commands)
		{
			WriteCommandJson(Writer, Command);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
		Writer.WriteObjectEnd();
	}

	FString ResolvePreviewBlueprintPath(const FVergilCompileRequest& Request)
	{
		if (Request.TargetBlueprint != nullptr)
		{
			return Request.TargetBlueprint->GetPathName();
		}

		return Request.Document.BlueprintPath.TrimStartAndEnd();
	}

	template <typename PrintPolicy>
	void WriteCommandPlanPreviewJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilCommandPlanPreview& Preview)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), CommandPlanPreviewInspectionFormatName);
		Writer.WriteValue(TEXT("version"), CommandPlanPreviewInspectionFormatVersion);
		Writer.WriteValue(TEXT("targetBlueprintPath"), Preview.TargetBlueprintPath);
		Writer.WriteValue(TEXT("targetGraphName"), LexNameString(Preview.TargetGraphName));
		Writer.WriteValue(TEXT("autoLayout"), Preview.bAutoLayout);
		Writer.WriteValue(TEXT("generateComments"), Preview.bGenerateComments);
		Writer.WriteRawJSONValue(TEXT("sourceDocument"), Vergil::SerializeGraphDocument(Preview.SourceDocument, false));
		Writer.WriteRawJSONValue(TEXT("effectiveDocument"), Vergil::SerializeGraphDocument(Preview.EffectiveDocument, false));
		Writer.WriteRawJSONValue(TEXT("documentDiff"), Vergil::SerializeDocumentDiff(Preview.DocumentDiff, false));
		Writer.WriteRawJSONValue(TEXT("compileResult"), Vergil::SerializeCompileResult(Preview.Result, false));
		Writer.WriteObjectEnd();
	}

	bool TryDeserializeNameField(
		const TSharedPtr<FJsonObject>& JsonObject,
		const TCHAR* FieldName,
		FName& OutValue)
	{
		FString FieldValue;
		if (!JsonObject->TryGetStringField(FieldName, FieldValue))
		{
			return true;
		}

		OutValue = FieldValue.IsEmpty() ? NAME_None : FName(*FieldValue);
		return true;
	}

	bool TryDeserializeGuidField(
		const TSharedPtr<FJsonObject>& JsonObject,
		const TCHAR* FieldName,
		FGuid& OutValue,
		TArray<FVergilDiagnostic>* Diagnostics)
	{
		FString FieldValue;
		if (!JsonObject->TryGetStringField(FieldName, FieldValue))
		{
			return true;
		}

		if (FieldValue.IsEmpty())
		{
			OutValue.Invalidate();
			return true;
		}

		if (!FGuid::Parse(FieldValue, OutValue))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandGuidInvalid"),
				FString::Printf(TEXT("Serialized command field '%s' must contain a valid GUID string."), FieldName));
			return false;
		}

		return true;
	}

	bool TryDeserializePositionField(
		const TSharedPtr<FJsonObject>& JsonObject,
		FVector2D& OutPosition,
		TArray<FVergilDiagnostic>* Diagnostics)
	{
		if (!JsonObject->HasField(TEXT("position")))
		{
			return true;
		}

		const TSharedPtr<FJsonObject>* PositionObject = nullptr;
		if (!JsonObject->TryGetObjectField(TEXT("position"), PositionObject) || PositionObject == nullptr)
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandPositionInvalid"),
				TEXT("Serialized command field 'position' must be an object containing numeric x and y fields."));
			return false;
		}

		double PositionX = 0.0;
		double PositionY = 0.0;
		if (!(*PositionObject)->TryGetNumberField(TEXT("x"), PositionX) || !(*PositionObject)->TryGetNumberField(TEXT("y"), PositionY))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandPositionInvalid"),
				TEXT("Serialized command field 'position' must contain numeric x and y fields."));
			return false;
		}

		OutPosition = FVector2D(PositionX, PositionY);
		return true;
	}

	bool DeserializePlannedPin(
		const TSharedPtr<FJsonValue>& JsonValue,
		FVergilPlannedPin& OutPlannedPin,
		TArray<FVergilDiagnostic>* Diagnostics)
	{
		if (!JsonValue.IsValid() || JsonValue->Type != EJson::Object)
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedPlannedPinInvalid"),
				TEXT("Serialized planned pins must be JSON objects."));
			return false;
		}

		const TSharedPtr<FJsonObject> JsonObject = JsonValue->AsObject();
		if (!TryDeserializeGuidField(JsonObject, TEXT("pinId"), OutPlannedPin.PinId, Diagnostics))
		{
			return false;
		}

		TryDeserializeNameField(JsonObject, TEXT("name"), OutPlannedPin.Name);

		if (JsonObject->HasField(TEXT("isInput")) && !JsonObject->TryGetBoolField(TEXT("isInput"), OutPlannedPin.bIsInput))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedPlannedPinInvalid"),
				TEXT("Serialized planned pin field 'isInput' must be a boolean."));
			return false;
		}

		if (JsonObject->HasField(TEXT("isExec")) && !JsonObject->TryGetBoolField(TEXT("isExec"), OutPlannedPin.bIsExec))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedPlannedPinInvalid"),
				TEXT("Serialized planned pin field 'isExec' must be a boolean."));
			return false;
		}

		return true;
	}

	bool DeserializeCommand(
		const TSharedPtr<FJsonObject>& JsonObject,
		FVergilCompilerCommand& OutCommand,
		TArray<FVergilDiagnostic>* Diagnostics)
	{
		FString TypeString;
		if (!JsonObject->TryGetStringField(TEXT("type"), TypeString) || TypeString.IsEmpty())
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandTypeMissing"),
				TEXT("Serialized command entries must include a non-empty 'type' field."));
			return false;
		}

		if (!TryParseCommandTypeString(TypeString, OutCommand.Type))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandTypeInvalid"),
				FString::Printf(TEXT("Serialized command type '%s' is not supported."), *TypeString));
			return false;
		}

		TryDeserializeNameField(JsonObject, TEXT("graphName"), OutCommand.GraphName);
		TryDeserializeNameField(JsonObject, TEXT("name"), OutCommand.Name);
		TryDeserializeNameField(JsonObject, TEXT("secondaryName"), OutCommand.SecondaryName);

		if (!TryDeserializeGuidField(JsonObject, TEXT("nodeId"), OutCommand.NodeId, Diagnostics)
			|| !TryDeserializeGuidField(JsonObject, TEXT("sourceNodeId"), OutCommand.SourceNodeId, Diagnostics)
			|| !TryDeserializeGuidField(JsonObject, TEXT("sourcePinId"), OutCommand.SourcePinId, Diagnostics)
			|| !TryDeserializeGuidField(JsonObject, TEXT("targetNodeId"), OutCommand.TargetNodeId, Diagnostics)
			|| !TryDeserializeGuidField(JsonObject, TEXT("targetPinId"), OutCommand.TargetPinId, Diagnostics))
		{
			return false;
		}

		JsonObject->TryGetStringField(TEXT("stringValue"), OutCommand.StringValue);

		const TSharedPtr<FJsonObject>* AttributesObject = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("attributes"), AttributesObject) && AttributesObject != nullptr)
		{
			OutCommand.Attributes.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& AttributeEntry : (*AttributesObject)->Values)
			{
				FString AttributeValue;
				if (!AttributeEntry.Value.IsValid() || !AttributeEntry.Value->TryGetString(AttributeValue))
				{
					AddSerializationDiagnostic(
						Diagnostics,
						TEXT("SerializedCommandAttributeInvalid"),
						FString::Printf(TEXT("Serialized command attribute '%s' must use a string value."), *AttributeEntry.Key));
					return false;
				}

				OutCommand.Attributes.Add(FName(*AttributeEntry.Key), AttributeValue);
			}
		}
		else if (JsonObject->HasField(TEXT("attributes")))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedCommandAttributeInvalid"),
				TEXT("Serialized command field 'attributes' must be an object."));
			return false;
		}

		if (!TryDeserializePositionField(JsonObject, OutCommand.Position, Diagnostics))
		{
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* PlannedPinValues = nullptr;
		if (JsonObject->TryGetArrayField(TEXT("plannedPins"), PlannedPinValues) && PlannedPinValues != nullptr)
		{
			OutCommand.PlannedPins.Reset();
			for (const TSharedPtr<FJsonValue>& PlannedPinValue : *PlannedPinValues)
			{
				FVergilPlannedPin PlannedPin;
				if (!DeserializePlannedPin(PlannedPinValue, PlannedPin, Diagnostics))
				{
					return false;
				}

				OutCommand.PlannedPins.Add(PlannedPin);
			}
		}
		else if (JsonObject->HasField(TEXT("plannedPins")))
		{
			AddSerializationDiagnostic(
				Diagnostics,
				TEXT("SerializedPlannedPinInvalid"),
				TEXT("Serialized command field 'plannedPins' must be an array."));
			return false;
		}

		return true;
	}

	int32 GetDeterministicCommandPhase(const FVergilCompilerCommand& Command)
	{
		switch (Command.Type)
		{
		case EVergilCommandType::EnsureDispatcher:
		case EVergilCommandType::AddDispatcherParameter:
		case EVergilCommandType::SetBlueprintMetadata:
		case EVergilCommandType::EnsureVariable:
		case EVergilCommandType::SetVariableMetadata:
		case EVergilCommandType::SetVariableDefault:
		case EVergilCommandType::EnsureFunctionGraph:
		case EVergilCommandType::EnsureMacroGraph:
		case EVergilCommandType::EnsureComponent:
		case EVergilCommandType::AttachComponent:
		case EVergilCommandType::SetComponentProperty:
		case EVergilCommandType::EnsureInterface:
		case EVergilCommandType::RenameMember:
			return 0;

		case EVergilCommandType::EnsureGraph:
		case EVergilCommandType::AddNode:
		case EVergilCommandType::SetNodeMetadata:
		case EVergilCommandType::RemoveNode:
		case EVergilCommandType::MoveNode:
			return 1;

		case EVergilCommandType::ConnectPins:
			return 2;

		case EVergilCommandType::FinalizeNode:
			return 3;

		case EVergilCommandType::CompileBlueprint:
			return 4;

		case EVergilCommandType::SetClassDefault:
			return 5;

		default:
			return 6;
		}
	}
}

FString FVergilCompilePassRecord::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s succeeded=%s diagnostics=%d errors=%d planned=%d"),
		*LexOptionalNameString(PassName),
		LexBoolString(bSucceeded),
		DiagnosticCount,
		ErrorCount,
		PlannedCommandCount);
}

FString FVergilUndoRedoSnapshot::ToDisplayString() const
{
	return FString::Printf(
		TEXT("queue=%d undoCount=%d canUndo=%s canRedo=%s undoBufferRef=%s nextUndo={id=%s title=\"%s\" context=\"%s\" primary=\"%s\"} nextRedo={id=%s title=\"%s\" context=\"%s\" primary=\"%s\"}"),
		QueueLength,
		UndoCount,
		LexBoolString(bCanUndo),
		LexBoolString(bCanRedo),
		LexBoolString(bBlueprintReferencedByUndoBuffer),
		NextUndoTransactionId.IsValid() ? *LexGuidString(NextUndoTransactionId) : TEXT("<none>"),
		*EscapeDisplayValue(NextUndoTitle),
		*EscapeDisplayValue(NextUndoContext),
		NextUndoPrimaryObjectPath.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(NextUndoPrimaryObjectPath),
		NextRedoTransactionId.IsValid() ? *LexGuidString(NextRedoTransactionId) : TEXT("<none>"),
		*EscapeDisplayValue(NextRedoTitle),
		*EscapeDisplayValue(NextRedoContext),
		NextRedoPrimaryObjectPath.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(NextRedoPrimaryObjectPath));
}

FString FVergilTransactionAudit::ToDisplayString() const
{
	if (!bRecorded)
	{
		return TEXT("recorded=false");
	}

	return FString::Printf(
		TEXT("recorded=true nested=%s opened=%s context=\"%s\" title=\"%s\" primary=\"%s\" before={%s} recovery={required=%s attempted=%s succeeded=%s method=\"%s\" message=\"%s\" failure={%s}} after={%s}"),
		LexBoolString(bNestedInActiveTransaction),
		LexBoolString(bOpenedScopedTransaction),
		*EscapeDisplayValue(TransactionContext),
		*EscapeDisplayValue(TransactionTitle),
		PrimaryObjectPath.IsEmpty() ? TEXT("<none>") : *EscapeDisplayValue(PrimaryObjectPath),
		*BeforeState.ToDisplayString(),
		LexBoolString(bRecoveryRequired),
		LexBoolString(bRecoveryAttempted),
		LexBoolString(bRecoverySucceeded),
		*EscapeDisplayValue(RecoveryMethod),
		*EscapeDisplayValue(RecoveryMessage),
		*FailureState.ToDisplayString(),
		*AfterState.ToDisplayString());
}

int32 FVergilCompileStatistics::GetCompletedPassCount() const
{
	return CompletedPassNames.Num();
}

int32 FVergilCompileStatistics::GetTotalAccountedCommandCount() const
{
	return BlueprintDefinitionCommandCount
		+ GraphStructureCommandCount
		+ ConnectionCommandCount
		+ FinalizeCommandCount
		+ ExplicitCompileCommandCount
		+ PostBlueprintCompileCommandCount;
}

void FVergilCompileStatistics::RebuildCommandStatistics(const TArray<FVergilCompilerCommand>& Commands)
{
	CommandPlanFingerprint = BuildCommandPlanFingerprint(Commands);
	PlannedCommandCount = Commands.Num();
	BlueprintDefinitionCommandCount = 0;
	GraphStructureCommandCount = 0;
	ConnectionCommandCount = 0;
	FinalizeCommandCount = 0;
	ExplicitCompileCommandCount = 0;
	PostBlueprintCompileCommandCount = 0;

	for (const FVergilCompilerCommand& Command : Commands)
	{
		switch (GetDeterministicCommandPhase(Command))
		{
		case 0:
			++BlueprintDefinitionCommandCount;
			break;

		case 1:
			++GraphStructureCommandCount;
			break;

		case 2:
			++ConnectionCommandCount;
			break;

		case 3:
			++FinalizeCommandCount;
			break;

		case 4:
			++ExplicitCompileCommandCount;
			break;

		case 5:
			++PostBlueprintCompileCommandCount;
			break;

		default:
			break;
		}
	}
}

void FVergilCompileStatistics::SetTargetDocumentStatistics(const FVergilGraphDocument& Document)
{
	EffectiveSchemaVersion = Document.SchemaVersion;

	if (TargetGraphName == TEXT("UserConstructionScript"))
	{
		SourceNodeCount = Document.ConstructionScriptNodes.Num();
		SourceEdgeCount = Document.ConstructionScriptEdges.Num();
		return;
	}

	SourceNodeCount = Document.Nodes.Num();
	SourceEdgeCount = Document.Edges.Num();
}

FString FVergilCompileStatistics::ToDisplayString() const
{
	return FString::Printf(
		TEXT("graph=%s schema=%d->%d autoLayout=%s comments=%s applyRequested=%s executionAttempted=%s normalized=%s fingerprint=%s planCalls=%d applyCalls=%d reusedPlan=%s transaction={%s} nodes=%d edges=%d planned=%d phases={blueprint=%d graph=%d connections=%d finalize=%d compile=%d post=%d} passes=%d last=%s failed=%s"),
		*LexOptionalNameString(TargetGraphName),
		RequestedSchemaVersion,
		EffectiveSchemaVersion,
		LexBoolString(bAutoLayoutRequested),
		LexBoolString(bGenerateCommentsRequested),
		LexBoolString(bApplyRequested),
		LexBoolString(bExecutionAttempted),
		LexBoolString(bCommandPlanNormalized),
		CommandPlanFingerprint.IsEmpty() ? TEXT("<none>") : *CommandPlanFingerprint,
		PlanningInvocationCount,
		ApplyInvocationCount,
		LexBoolString(bExecutionUsedReturnedCommandPlan),
		*TransactionAudit.ToDisplayString(),
		SourceNodeCount,
		SourceEdgeCount,
		PlannedCommandCount,
		BlueprintDefinitionCommandCount,
		GraphStructureCommandCount,
		ConnectionCommandCount,
		FinalizeCommandCount,
		ExplicitCompileCommandCount,
		PostBlueprintCompileCommandCount,
		GetCompletedPassCount(),
		*LexOptionalNameString(LastCompletedPassName),
		*LexOptionalNameString(FailedPassName));
}

FString FVergilPlannedPin::ToDisplayString() const
{
	TArray<FString> Tokens;
	Tokens.Add(Name.IsNone() ? TEXT("<unnamed-pin>") : Name.ToString());
	Tokens.Add(bIsInput ? TEXT("input") : TEXT("output"));
	if (bIsExec)
	{
		Tokens.Add(TEXT("exec"));
	}
	if (PinId.IsValid())
	{
		Tokens.Add(FString::Printf(TEXT("pinId=%s"), *LexGuidString(PinId)));
	}

	return FString::Join(Tokens, TEXT(" "));
}

FString FVergilCompilerCommand::ToDisplayString() const
{
	TArray<FString> Tokens;
	Tokens.Add(LexCommandTypeString(Type));

	if (!GraphName.IsNone())
	{
		Tokens.Add(FString::Printf(TEXT("graph=%s"), *GraphName.ToString()));
	}
	if (NodeId.IsValid())
	{
		Tokens.Add(FString::Printf(TEXT("nodeId=%s"), *LexGuidString(NodeId)));
	}
	if (SourceNodeId.IsValid() || SourcePinId.IsValid())
	{
		Tokens.Add(FString::Printf(
			TEXT("source=%s/%s"),
			*LexGuidString(SourceNodeId),
			*LexGuidString(SourcePinId)));
	}
	if (TargetNodeId.IsValid() || TargetPinId.IsValid())
	{
		Tokens.Add(FString::Printf(
			TEXT("target=%s/%s"),
			*LexGuidString(TargetNodeId),
			*LexGuidString(TargetPinId)));
	}
	if (!Name.IsNone())
	{
		Tokens.Add(FString::Printf(TEXT("name=%s"), *Name.ToString()));
	}
	if (!SecondaryName.IsNone())
	{
		Tokens.Add(FString::Printf(TEXT("secondary=%s"), *SecondaryName.ToString()));
	}
	if (!StringValue.IsEmpty())
	{
		Tokens.Add(FString::Printf(TEXT("value=\"%s\""), *EscapeDisplayValue(StringValue)));
	}
	if (!Position.IsNearlyZero())
	{
		Tokens.Add(FString::Printf(TEXT("position=(%.2f, %.2f)"), Position.X, Position.Y));
	}
	if (Attributes.Num() > 0)
	{
		TArray<FName> AttributeKeys;
		Attributes.GetKeys(AttributeKeys);
		AttributeKeys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		TArray<FString> AttributeTokens;
		for (const FName AttributeKey : AttributeKeys)
		{
			AttributeTokens.Add(FString::Printf(
				TEXT("%s=%s"),
				*AttributeKey.ToString(),
				*EscapeDisplayValue(Attributes.FindRef(AttributeKey))));
		}

		Tokens.Add(FString::Printf(TEXT("attrs={%s}"), *FString::Join(AttributeTokens, TEXT(", "))));
	}
	if (PlannedPins.Num() > 0)
	{
		TArray<FString> PlannedPinTokens;
		for (const FVergilPlannedPin& PlannedPin : PlannedPins)
		{
			PlannedPinTokens.Add(PlannedPin.ToDisplayString());
		}

		Tokens.Add(FString::Printf(TEXT("pins=[%s]"), *FString::Join(PlannedPinTokens, TEXT(", "))));
	}

	return FString::Join(Tokens, TEXT(" "));
}

FString Vergil::GetCommandPlanFormatName()
{
	return CommandPlanFormatName;
}

int32 Vergil::GetCommandPlanFormatVersion()
{
	return CommandPlanFormatVersion;
}

FString Vergil::GetDiagnosticsInspectionFormatName()
{
	return DiagnosticsInspectionFormatName;
}

int32 Vergil::GetDiagnosticsInspectionFormatVersion()
{
	return DiagnosticsInspectionFormatVersion;
}

FString Vergil::GetCompileResultInspectionFormatName()
{
	return CompileResultInspectionFormatName;
}

int32 Vergil::GetCompileResultInspectionFormatVersion()
{
	return CompileResultInspectionFormatVersion;
}

FString Vergil::GetCommandPlanPreviewInspectionFormatName()
{
	return CommandPlanPreviewInspectionFormatName;
}

int32 Vergil::GetCommandPlanPreviewInspectionFormatVersion()
{
	return CommandPlanPreviewInspectionFormatVersion;
}

void Vergil::NormalizeCommandPlan(TArray<FVergilCompilerCommand>& Commands)
{
	Algo::StableSort(Commands, [](const FVergilCompilerCommand& A, const FVergilCompilerCommand& B)
	{
		return GetDeterministicCommandPhase(A) < GetDeterministicCommandPhase(B);
	});
}

FString Vergil::DescribeCommandPlan(const TArray<FVergilCompilerCommand>& Commands)
{
	if (Commands.Num() == 0)
	{
		return TEXT("<empty command plan>");
	}

	TArray<FString> Lines;
	Lines.Reserve(Commands.Num());
	for (int32 CommandIndex = 0; CommandIndex < Commands.Num(); ++CommandIndex)
	{
		Lines.Add(FString::Printf(TEXT("%d: %s"), CommandIndex, *Commands[CommandIndex].ToDisplayString()));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeCommandPlan(const TArray<FVergilCompilerCommand>& Commands, const bool bPrettyPrint)
{
	FString SerializedCommandPlan;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedCommandPlan);
		WriteCommandPlanJson(*Writer, Commands);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedCommandPlan);
		WriteCommandPlanJson(*Writer, Commands);
		Writer->Close();
	}

	return SerializedCommandPlan;
}

bool Vergil::DeserializeCommandPlan(
	const FString& SerializedCommandPlan,
	TArray<FVergilCompilerCommand>& OutCommands,
	TArray<FVergilDiagnostic>* OutDiagnostics)
{
	OutCommands.Reset();

	if (SerializedCommandPlan.TrimStartAndEnd().IsEmpty())
	{
		AddSerializationDiagnostic(
			OutDiagnostics,
			TEXT("SerializedCommandPlanEmpty"),
			TEXT("Cannot deserialize an empty serialized command plan."));
		return false;
	}

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SerializedCommandPlan);
	TSharedPtr<FJsonValue> RootValue;
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		AddSerializationDiagnostic(
			OutDiagnostics,
			TEXT("SerializedCommandPlanParseFailed"),
			TEXT("Serialized command plan must contain valid JSON."));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* CommandValues = nullptr;
	if (RootValue->Type == EJson::Array)
	{
		CommandValues = &RootValue->AsArray();
	}
	else if (RootValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> RootObject = RootValue->AsObject();

		FString FormatName;
		if (RootObject->TryGetStringField(TEXT("format"), FormatName)
			&& !FormatName.IsEmpty()
			&& FormatName != CommandPlanFormatName)
		{
			AddSerializationDiagnostic(
				OutDiagnostics,
				TEXT("SerializedCommandPlanFormatUnsupported"),
				FString::Printf(TEXT("Serialized command plan format '%s' is not supported."), *FormatName));
			return false;
		}

		double VersionNumber = static_cast<double>(CommandPlanFormatVersion);
		if (RootObject->HasField(TEXT("version")))
		{
			if (!RootObject->TryGetNumberField(TEXT("version"), VersionNumber))
			{
				AddSerializationDiagnostic(
					OutDiagnostics,
					TEXT("SerializedCommandPlanVersionInvalid"),
					TEXT("Serialized command plan field 'version' must be numeric."));
				return false;
			}

			if (VersionNumber != static_cast<double>(CommandPlanFormatVersion))
			{
				AddSerializationDiagnostic(
					OutDiagnostics,
					TEXT("SerializedCommandPlanVersionUnsupported"),
					FString::Printf(TEXT("Serialized command plan version '%g' is not supported."), VersionNumber));
				return false;
			}
		}

		if (!RootObject->TryGetArrayField(TEXT("commands"), CommandValues) || CommandValues == nullptr)
		{
			AddSerializationDiagnostic(
				OutDiagnostics,
				TEXT("SerializedCommandPlanCommandsMissing"),
				TEXT("Serialized command plan objects must contain a 'commands' array."));
			return false;
		}
	}
	else
	{
		AddSerializationDiagnostic(
			OutDiagnostics,
			TEXT("SerializedCommandPlanRootInvalid"),
			TEXT("Serialized command plan must be either a JSON array or an object containing a 'commands' array."));
		return false;
	}

	for (const TSharedPtr<FJsonValue>& CommandValue : *CommandValues)
	{
		if (!CommandValue.IsValid() || CommandValue->Type != EJson::Object)
		{
			AddSerializationDiagnostic(
				OutDiagnostics,
				TEXT("SerializedCommandEntryInvalid"),
				TEXT("Serialized command entries must be JSON objects."));
			return false;
		}

		FVergilCompilerCommand Command;
		if (!DeserializeCommand(CommandValue->AsObject(), Command, OutDiagnostics))
		{
			return false;
		}

		OutCommands.Add(Command);
	}

	return true;
}

FString Vergil::DescribeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics)
{
	if (Diagnostics.Num() == 0)
	{
		return TEXT("<no diagnostics>");
	}

	TArray<FString> Lines;
	Lines.Reserve(Diagnostics.Num());
	for (int32 DiagnosticIndex = 0; DiagnosticIndex < Diagnostics.Num(); ++DiagnosticIndex)
	{
		Lines.Add(FString::Printf(TEXT("%d: %s"), DiagnosticIndex, *Diagnostics[DiagnosticIndex].ToDisplayString()));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics, const bool bPrettyPrint)
{
	FString SerializedDiagnostics;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedDiagnostics);
		WriteDiagnosticsJson(*Writer, Diagnostics);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedDiagnostics);
		WriteDiagnosticsJson(*Writer, Diagnostics);
		Writer->Close();
	}

	return SerializedDiagnostics;
}

FString Vergil::DescribeCompileResult(const FVergilCompileResult& Result)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s version=%d succeeded=%s applied=%s executed=%d diagnostics=%d commands=%d"),
		CompileResultInspectionFormatName,
		CompileResultInspectionFormatVersion,
		LexBoolString(Result.bSucceeded),
		LexBoolString(Result.bApplied),
		Result.ExecutedCommandCount,
		Result.Diagnostics.Num(),
		Result.Commands.Num()));
	Lines.Add(FString::Printf(TEXT("compileSummary: %s"), *Vergil::SummarizeCompileResult(Result).ToDisplayString()));
	Lines.Add(FString::Printf(TEXT("applySummary: %s"), *Vergil::SummarizeApplyResult(Result).ToDisplayString()));
	Lines.Add(FString::Printf(TEXT("stats: %s"), *Result.Statistics.ToDisplayString()));

	if (Result.PassRecords.Num() == 0)
	{
		Lines.Add(TEXT("passRecords: <none>"));
	}
	else
	{
		Lines.Add(TEXT("passRecords:"));
		for (const FVergilCompilePassRecord& PassRecord : Result.PassRecords)
		{
			Lines.Add(FString::Printf(TEXT("- %s"), *PassRecord.ToDisplayString()));
		}
	}

	Lines.Add(TEXT("diagnostics:"));
	Lines.Add(Vergil::DescribeDiagnostics(Result.Diagnostics));
	Lines.Add(TEXT("commands:"));
	Lines.Add(Vergil::DescribeCommandPlan(Result.Commands));
	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeCompileResult(const FVergilCompileResult& Result, const bool bPrettyPrint)
{
	FString SerializedResult;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedResult);
		WriteCompileResultJson(*Writer, Result);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedResult);
		WriteCompileResultJson(*Writer, Result);
		Writer->Close();
	}

	return SerializedResult;
}

FVergilCommandPlanPreview Vergil::MakeCommandPlanPreview(
	const FVergilCompileRequest& Request,
	const FVergilGraphDocument& EffectiveDocument,
	const FVergilCompileResult& Result)
{
	FVergilCommandPlanPreview Preview;
	Preview.TargetBlueprintPath = ResolvePreviewBlueprintPath(Request);
	Preview.TargetGraphName = Request.TargetGraphName;
	Preview.bAutoLayout = Request.bAutoLayout;
	Preview.bGenerateComments = Request.bGenerateComments;
	Preview.SourceDocument = Request.Document;
	Preview.EffectiveDocument = EffectiveDocument;
	Preview.DocumentDiff = Vergil::DiffGraphDocuments(Request.Document, EffectiveDocument);
	Preview.Result = Result;
	return Preview;
}

FString Vergil::DescribeCommandPlanPreview(const FVergilCommandPlanPreview& Preview)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s version=%d targetBlueprint=%s targetGraph=%s autoLayout=%s generateComments=%s succeeded=%s applied=%s diffEntries=%d commands=%d diagnostics=%d"),
		CommandPlanPreviewInspectionFormatName,
		CommandPlanPreviewInspectionFormatVersion,
		Preview.TargetBlueprintPath.IsEmpty() ? TEXT("<none>") : *Preview.TargetBlueprintPath,
		Preview.TargetGraphName.IsNone() ? TEXT("<none>") : *Preview.TargetGraphName.ToString(),
		LexBoolString(Preview.bAutoLayout),
		LexBoolString(Preview.bGenerateComments),
		LexBoolString(Preview.Result.bSucceeded),
		LexBoolString(Preview.Result.bApplied),
		Preview.DocumentDiff.Entries.Num(),
		Preview.Result.Commands.Num(),
		Preview.Result.Diagnostics.Num()));
	Lines.Add(FString::Printf(TEXT("documentDiff: %s"), *Preview.DocumentDiff.ToDisplayString()));
	Lines.Add(FString::Printf(TEXT("compileSummary: %s"), *Vergil::SummarizeCompileResult(Preview.Result).ToDisplayString()));
	Lines.Add(TEXT("plannedMutations:"));
	Lines.Add(Vergil::DescribeCommandPlan(Preview.Result.Commands));
	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeCommandPlanPreview(const FVergilCommandPlanPreview& Preview, const bool bPrettyPrint)
{
	FString SerializedPreview;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedPreview);
		WriteCommandPlanPreviewJson(*Writer, Preview);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedPreview);
		WriteCommandPlanPreviewJson(*Writer, Preview);
		Writer->Close();
	}

	return SerializedPreview;
}

FString FVergilCommandPlanPreview::ToDisplayString() const
{
	return FString::Printf(
		TEXT("targetBlueprint=%s targetGraph=%s autoLayout=%s generateComments=%s diffEntries=%d commands=%d diagnostics=%d succeeded=%s applied=%s"),
		TargetBlueprintPath.IsEmpty() ? TEXT("<none>") : *TargetBlueprintPath,
		TargetGraphName.IsNone() ? TEXT("<none>") : *TargetGraphName.ToString(),
		LexBoolString(bAutoLayout),
		LexBoolString(bGenerateComments),
		DocumentDiff.Entries.Num(),
		Result.Commands.Num(),
		Result.Diagnostics.Num(),
		LexBoolString(Result.bSucceeded),
		LexBoolString(Result.bApplied));
}

int32 FVergilDiagnosticSummary::GetTotalCount() const
{
	return InfoCount + WarningCount + ErrorCount;
}

bool FVergilDiagnosticSummary::HasErrors() const
{
	return ErrorCount > 0;
}

FString FVergilDiagnosticSummary::ToDisplayString() const
{
	return FString::Printf(
		TEXT("info=%d warnings=%d errors=%d total=%d"),
		InfoCount,
		WarningCount,
		ErrorCount,
		GetTotalCount());
}

FString FVergilExecutionSummary::ToDisplayString() const
{
	const FString EffectiveLabel = Label.IsEmpty() ? TEXT("Vergil") : Label;
	return FString::Printf(
		TEXT("%s succeeded=%s applied=%s planned=%d executed=%d diagnostics={%s}"),
		*EffectiveLabel,
		LexBoolString(bSucceeded),
		LexBoolString(bApplied),
		PlannedCommandCount,
		ExecutedCommandCount,
		*Diagnostics.ToDisplayString());
}

FVergilDiagnosticSummary Vergil::SummarizeDiagnostics(const TArray<FVergilDiagnostic>& Diagnostics)
{
	FVergilDiagnosticSummary Summary;
	for (const FVergilDiagnostic& Diagnostic : Diagnostics)
	{
		switch (Diagnostic.Severity)
		{
		case EVergilDiagnosticSeverity::Info:
			++Summary.InfoCount;
			break;

		case EVergilDiagnosticSeverity::Warning:
			++Summary.WarningCount;
			break;

		case EVergilDiagnosticSeverity::Error:
			++Summary.ErrorCount;
			break;

		default:
			break;
		}
	}

	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeCompileResult(const FVergilCompileResult& Result)
{
	FVergilExecutionSummary Summary;
	const int32 PlannedCommandCount = Result.Statistics.PlannedCommandCount > 0 || Result.Commands.Num() == 0
		? Result.Statistics.PlannedCommandCount
		: Result.Commands.Num();
	Summary.Label = TEXT("Compile");
	Summary.Diagnostics = SummarizeDiagnostics(Result.Diagnostics);
	Summary.bSucceeded = Result.bSucceeded && !Summary.Diagnostics.HasErrors();
	Summary.PlannedCommandCount = PlannedCommandCount;
	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeApplyResult(const FVergilCompileResult& Result)
{
	FVergilExecutionSummary Summary;
	const int32 PlannedCommandCount = Result.Statistics.PlannedCommandCount > 0 || Result.Commands.Num() == 0
		? Result.Statistics.PlannedCommandCount
		: Result.Commands.Num();
	Summary.Label = TEXT("Apply");
	Summary.Diagnostics = SummarizeDiagnostics(Result.Diagnostics);
	Summary.bApplied = Result.bApplied;
	Summary.bSucceeded = Result.bApplied && !Summary.Diagnostics.HasErrors();
	Summary.PlannedCommandCount = PlannedCommandCount;
	Summary.ExecutedCommandCount = Result.ExecutedCommandCount;
	return Summary;
}

FVergilExecutionSummary Vergil::SummarizeTestResult(
	const FString& Label,
	const bool bSucceeded,
	const TArray<FVergilDiagnostic>& Diagnostics,
	const int32 PlannedCommandCount,
	const int32 ExecutedCommandCount)
{
	FVergilExecutionSummary Summary;
	Summary.Label = Label;
	Summary.Diagnostics = SummarizeDiagnostics(Diagnostics);
	Summary.bSucceeded = bSucceeded && !Summary.Diagnostics.HasErrors();
	Summary.bApplied = ExecutedCommandCount > 0;
	Summary.PlannedCommandCount = PlannedCommandCount;
	Summary.ExecutedCommandCount = ExecutedCommandCount;
	return Summary;
}
