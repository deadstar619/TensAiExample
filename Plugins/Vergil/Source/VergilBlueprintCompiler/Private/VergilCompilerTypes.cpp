#include "VergilCompilerTypes.h"

#include "Algo/StableSort.h"
#include "Dom/JsonObject.h"
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
		TEXT("graph=%s schema=%d->%d autoLayout=%s comments=%s applyRequested=%s executionAttempted=%s normalized=%s fingerprint=%s planCalls=%d applyCalls=%d reusedPlan=%s nodes=%d edges=%d planned=%d phases={blueprint=%d graph=%d connections=%d finalize=%d compile=%d post=%d} passes=%d last=%s failed=%s"),
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
