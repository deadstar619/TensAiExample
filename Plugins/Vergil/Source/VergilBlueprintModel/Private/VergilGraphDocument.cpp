#include "VergilGraphDocument.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonWriter.h"

namespace
{
	inline constexpr TCHAR DocumentInspectionFormatName[] = TEXT("Vergil.GraphDocument");
	inline constexpr int32 DocumentInspectionFormatVersion = 1;

	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
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

	const TCHAR* LexPinDirectionString(const EVergilPinDirection Direction)
	{
		switch (Direction)
		{
		case EVergilPinDirection::Input:
			return TEXT("Input");

		case EVergilPinDirection::Output:
			return TEXT("Output");

		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LexNodeKindString(const EVergilNodeKind Kind)
	{
		switch (Kind)
		{
		case EVergilNodeKind::Event:
			return TEXT("Event");

		case EVergilNodeKind::Call:
			return TEXT("Call");

		case EVergilNodeKind::VariableGet:
			return TEXT("VariableGet");

		case EVergilNodeKind::VariableSet:
			return TEXT("VariableSet");

		case EVergilNodeKind::ControlFlow:
			return TEXT("ControlFlow");

		case EVergilNodeKind::Macro:
			return TEXT("Macro");

		case EVergilNodeKind::Comment:
			return TEXT("Comment");

		case EVergilNodeKind::Native:
			return TEXT("Native");

		case EVergilNodeKind::Custom:
			return TEXT("Custom");

		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LexVariableContainerTypeString(const EVergilVariableContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EVergilVariableContainerType::None:
			return TEXT("None");

		case EVergilVariableContainerType::Array:
			return TEXT("Array");

		case EVergilVariableContainerType::Set:
			return TEXT("Set");

		case EVergilVariableContainerType::Map:
			return TEXT("Map");

		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* LexFunctionAccessSpecifierString(const EVergilFunctionAccessSpecifier AccessSpecifier)
	{
		switch (AccessSpecifier)
		{
		case EVergilFunctionAccessSpecifier::Public:
			return TEXT("Public");

		case EVergilFunctionAccessSpecifier::Protected:
			return TEXT("Protected");

		case EVergilFunctionAccessSpecifier::Private:
			return TEXT("Private");

		default:
			return TEXT("Unknown");
		}
	}

	FString DescribeNameValueMap(const TMap<FName, FString>& Values)
	{
		if (Values.Num() == 0)
		{
			return TEXT("{}");
		}

		TArray<FName> Keys;
		Values.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		TArray<FString> Tokens;
		Tokens.Reserve(Keys.Num());
		for (const FName Key : Keys)
		{
			Tokens.Add(FString::Printf(
				TEXT("%s=\"%s\""),
				Key.IsNone() ? TEXT("<none>") : *Key.ToString(),
				*EscapeDisplayValue(Values.FindRef(Key))));
		}

		return FString::Printf(TEXT("{%s}"), *FString::Join(Tokens, TEXT(", ")));
	}

	FString DescribeLeafType(const FName PinCategory, const FString& ObjectPath)
	{
		if (PinCategory.IsNone())
		{
			return TEXT("<none>");
		}

		const FString TrimmedObjectPath = ObjectPath.TrimStartAndEnd();
		return TrimmedObjectPath.IsEmpty()
			? PinCategory.ToString()
			: FString::Printf(TEXT("%s:%s"), *PinCategory.ToString(), *TrimmedObjectPath);
	}

	FString DescribeTypeReference(const FVergilVariableTypeReference& Type)
	{
		const FString BaseType = DescribeLeafType(Type.PinCategory, Type.ObjectPath);
		switch (Type.ContainerType)
		{
		case EVergilVariableContainerType::Array:
			return FString::Printf(TEXT("array<%s>"), *BaseType);

		case EVergilVariableContainerType::Set:
			return FString::Printf(TEXT("set<%s>"), *BaseType);

		case EVergilVariableContainerType::Map:
			return FString::Printf(
				TEXT("map<%s, %s>"),
				*BaseType,
				*DescribeLeafType(Type.ValuePinCategory, Type.ValueObjectPath));

		case EVergilVariableContainerType::None:
		default:
			return BaseType;
		}
	}

	FString DescribeVariableFlags(const FVergilVariableFlags& Flags)
	{
		TArray<FString> Tokens;
		if (Flags.bInstanceEditable)
		{
			Tokens.Add(TEXT("InstanceEditable"));
		}
		if (Flags.bBlueprintReadOnly)
		{
			Tokens.Add(TEXT("BlueprintReadOnly"));
		}
		if (Flags.bExposeOnSpawn)
		{
			Tokens.Add(TEXT("ExposeOnSpawn"));
		}
		if (Flags.bPrivate)
		{
			Tokens.Add(TEXT("Private"));
		}
		if (Flags.bTransient)
		{
			Tokens.Add(TEXT("Transient"));
		}
		if (Flags.bSaveGame)
		{
			Tokens.Add(TEXT("SaveGame"));
		}
		if (Flags.bAdvancedDisplay)
		{
			Tokens.Add(TEXT("AdvancedDisplay"));
		}
		if (Flags.bDeprecated)
		{
			Tokens.Add(TEXT("Deprecated"));
		}
		if (Flags.bExposeToCinematics)
		{
			Tokens.Add(TEXT("ExposeToCinematics"));
		}

		return Tokens.Num() == 0
			? TEXT("{}")
			: FString::Printf(TEXT("{%s}"), *FString::Join(Tokens, TEXT(", ")));
	}

	FString DescribeFunctionParameter(const FVergilFunctionParameterDefinition& Parameter)
	{
		return FString::Printf(TEXT("%s:%s"), *LexOptionalNameString(Parameter.Name), *DescribeTypeReference(Parameter.Type));
	}

	FString DescribeMacroParameter(const FVergilMacroParameterDefinition& Parameter)
	{
		return Parameter.bIsExec
			? FString::Printf(TEXT("%s:exec"), *LexOptionalNameString(Parameter.Name))
			: FString::Printf(TEXT("%s:%s"), *LexOptionalNameString(Parameter.Name), *DescribeTypeReference(Parameter.Type));
	}

	FString DescribeDispatcherParameter(const FVergilDispatcherParameter& Parameter)
	{
		FString Description = FString::Printf(
			TEXT("%s:%s"),
			*LexOptionalNameString(Parameter.Name),
			*DescribeLeafType(Parameter.PinCategory, Parameter.ObjectPath));
		if (Parameter.bIsArray)
		{
			Description = FString::Printf(TEXT("array<%s>"), *Description);
		}

		if (!Parameter.PinSubCategory.IsNone())
		{
			Description += FString::Printf(TEXT(" sub=%s"), *Parameter.PinSubCategory.ToString());
		}

		return Description;
	}

	FString DescribeGraphPin(const FVergilGraphPin& Pin)
	{
		TArray<FString> Tokens;
		Tokens.Add(LexOptionalNameString(Pin.Name));
		Tokens.Add(LexPinDirectionString(Pin.Direction));
		if (Pin.bIsExec)
		{
			Tokens.Add(TEXT("exec"));
		}
		if (Pin.bIsArray)
		{
			Tokens.Add(TEXT("array"));
		}
		if (!Pin.TypeName.IsNone())
		{
			Tokens.Add(FString::Printf(TEXT("type=%s"), *Pin.TypeName.ToString()));
		}
		if (!Pin.DefaultValue.IsEmpty())
		{
			Tokens.Add(FString::Printf(TEXT("default=\"%s\""), *EscapeDisplayValue(Pin.DefaultValue)));
		}
		if (Pin.Id.IsValid())
		{
			Tokens.Add(FString::Printf(TEXT("pinId=%s"), *LexGuidString(Pin.Id)));
		}

		return FString::Join(Tokens, TEXT(" "));
	}

	FString DescribeGraphNode(const FVergilGraphNode& Node)
	{
		TArray<FString> Tokens;
		Tokens.Add(FString::Printf(TEXT("id=%s"), *LexGuidString(Node.Id)));
		Tokens.Add(FString::Printf(TEXT("kind=%s"), LexNodeKindString(Node.Kind)));
		Tokens.Add(FString::Printf(TEXT("descriptor=%s"), *LexOptionalNameString(Node.Descriptor)));
		Tokens.Add(FString::Printf(TEXT("position=(%.2f, %.2f)"), Node.Position.X, Node.Position.Y));

		if (Node.Pins.Num() > 0)
		{
			TArray<FString> PinTokens;
			PinTokens.Reserve(Node.Pins.Num());
			for (const FVergilGraphPin& Pin : Node.Pins)
			{
				PinTokens.Add(DescribeGraphPin(Pin));
			}

			Tokens.Add(FString::Printf(TEXT("pins=[%s]"), *FString::Join(PinTokens, TEXT(", "))));
		}
		else
		{
			Tokens.Add(TEXT("pins=[]"));
		}

		Tokens.Add(FString::Printf(TEXT("metadata=%s"), *DescribeNameValueMap(Node.Metadata)));
		return FString::Join(Tokens, TEXT(" "));
	}

	FString DescribeGraphEdge(const FVergilGraphEdge& Edge)
	{
		return FString::Printf(
			TEXT("id=%s source=%s/%s target=%s/%s"),
			*LexGuidString(Edge.Id),
			*LexGuidString(Edge.SourceNodeId),
			*LexGuidString(Edge.SourcePinId),
			*LexGuidString(Edge.TargetNodeId),
			*LexGuidString(Edge.TargetPinId));
	}

	FString DescribeComponentTransform(const FVergilComponentTransformDefinition& Transform)
	{
		TArray<FString> Tokens;
		if (Transform.bHasRelativeLocation)
		{
			Tokens.Add(FString::Printf(
				TEXT("location=(%.2f, %.2f, %.2f)"),
				Transform.RelativeLocation.X,
				Transform.RelativeLocation.Y,
				Transform.RelativeLocation.Z));
		}
		if (Transform.bHasRelativeRotation)
		{
			Tokens.Add(FString::Printf(
				TEXT("rotation=(%.2f, %.2f, %.2f)"),
				Transform.RelativeRotation.Roll,
				Transform.RelativeRotation.Pitch,
				Transform.RelativeRotation.Yaw));
		}
		if (Transform.bHasRelativeScale)
		{
			Tokens.Add(FString::Printf(
				TEXT("scale=(%.2f, %.2f, %.2f)"),
				Transform.RelativeScale3D.X,
				Transform.RelativeScale3D.Y,
				Transform.RelativeScale3D.Z));
		}

		return Tokens.Num() == 0
			? TEXT("{}")
			: FString::Printf(TEXT("{%s}"), *FString::Join(Tokens, TEXT(", ")));
	}

	FString DescribeDispatcherDefinition(const FVergilDispatcherDefinition& Dispatcher)
	{
		TArray<FString> ParameterTokens;
		ParameterTokens.Reserve(Dispatcher.Parameters.Num());
		for (const FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
		{
			ParameterTokens.Add(DescribeDispatcherParameter(Parameter));
		}

		return FString::Printf(
			TEXT("%s parameters=[%s]"),
			*LexOptionalNameString(Dispatcher.Name),
			*FString::Join(ParameterTokens, TEXT(", ")));
	}

	FString DescribeVariableDefinition(const FVergilVariableDefinition& Variable)
	{
		return FString::Printf(
			TEXT("%s type=%s category=\"%s\" flags=%s metadata=%s default=\"%s\""),
			*LexOptionalNameString(Variable.Name),
			*DescribeTypeReference(Variable.Type),
			*EscapeDisplayValue(Variable.Category),
			*DescribeVariableFlags(Variable.Flags),
			*DescribeNameValueMap(Variable.Metadata),
			*EscapeDisplayValue(Variable.DefaultValue));
	}

	FString DescribeFunctionDefinition(const FVergilFunctionDefinition& Function)
	{
		TArray<FString> InputTokens;
		InputTokens.Reserve(Function.Inputs.Num());
		for (const FVergilFunctionParameterDefinition& Input : Function.Inputs)
		{
			InputTokens.Add(DescribeFunctionParameter(Input));
		}

		TArray<FString> OutputTokens;
		OutputTokens.Reserve(Function.Outputs.Num());
		for (const FVergilFunctionParameterDefinition& Output : Function.Outputs)
		{
			OutputTokens.Add(DescribeFunctionParameter(Output));
		}

		return FString::Printf(
			TEXT("%s pure=%s access=%s inputs=[%s] outputs=[%s]"),
			*LexOptionalNameString(Function.Name),
			LexBoolString(Function.bPure),
			LexFunctionAccessSpecifierString(Function.AccessSpecifier),
			*FString::Join(InputTokens, TEXT(", ")),
			*FString::Join(OutputTokens, TEXT(", ")));
	}

	FString DescribeMacroDefinition(const FVergilMacroDefinition& Macro)
	{
		TArray<FString> InputTokens;
		InputTokens.Reserve(Macro.Inputs.Num());
		for (const FVergilMacroParameterDefinition& Input : Macro.Inputs)
		{
			InputTokens.Add(DescribeMacroParameter(Input));
		}

		TArray<FString> OutputTokens;
		OutputTokens.Reserve(Macro.Outputs.Num());
		for (const FVergilMacroParameterDefinition& Output : Macro.Outputs)
		{
			OutputTokens.Add(DescribeMacroParameter(Output));
		}

		return FString::Printf(
			TEXT("%s inputs=[%s] outputs=[%s]"),
			*LexOptionalNameString(Macro.Name),
			*FString::Join(InputTokens, TEXT(", ")),
			*FString::Join(OutputTokens, TEXT(", ")));
	}

	FString DescribeComponentDefinition(const FVergilComponentDefinition& Component)
	{
		const FString TrimmedClassPath = Component.ComponentClassPath.TrimStartAndEnd();
		return FString::Printf(
			TEXT("%s class=%s parent=%s socket=%s transform=%s templateProperties=%s"),
			*LexOptionalNameString(Component.Name),
			TrimmedClassPath.IsEmpty() ? TEXT("<none>") : *TrimmedClassPath,
			*LexOptionalNameString(Component.ParentComponentName),
			*LexOptionalNameString(Component.AttachSocketName),
			*DescribeComponentTransform(Component.RelativeTransform),
			*DescribeNameValueMap(Component.TemplateProperties));
	}

	FString DescribeInterfaceDefinition(const FVergilInterfaceDefinition& InterfaceDefinition)
	{
		const FString TrimmedInterfacePath = InterfaceDefinition.InterfaceClassPath.TrimStartAndEnd();
		return TrimmedInterfacePath.IsEmpty() ? TEXT("<none>") : TrimmedInterfacePath;
	}

	template <typename PrintPolicy>
	void WriteNameValueMapJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const TMap<FName, FString>& Values)
	{
		Writer.WriteObjectStart(FieldName);

		TArray<FName> Keys;
		Values.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName Key : Keys)
		{
			Writer.WriteValue(Key.ToString(), Values.FindRef(Key));
		}

		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteVector2DJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVector2D& Value)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("x"), Value.X);
		Writer.WriteValue(TEXT("y"), Value.Y);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteVectorJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVector& Value)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("x"), Value.X);
		Writer.WriteValue(TEXT("y"), Value.Y);
		Writer.WriteValue(TEXT("z"), Value.Z);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteRotatorJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FRotator& Value)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("roll"), Value.Roll);
		Writer.WriteValue(TEXT("pitch"), Value.Pitch);
		Writer.WriteValue(TEXT("yaw"), Value.Yaw);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteVariableTypeReferenceJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilVariableTypeReference& Type)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("pinCategory"), LexNameString(Type.PinCategory));
		Writer.WriteValue(TEXT("pinSubCategory"), LexNameString(Type.PinSubCategory));
		Writer.WriteValue(TEXT("objectPath"), Type.ObjectPath);
		Writer.WriteValue(TEXT("containerType"), LexVariableContainerTypeString(Type.ContainerType));
		Writer.WriteValue(TEXT("valuePinCategory"), LexNameString(Type.ValuePinCategory));
		Writer.WriteValue(TEXT("valuePinSubCategory"), LexNameString(Type.ValuePinSubCategory));
		Writer.WriteValue(TEXT("valueObjectPath"), Type.ValueObjectPath);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteVariableFlagsJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilVariableFlags& Flags)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("instanceEditable"), Flags.bInstanceEditable);
		Writer.WriteValue(TEXT("blueprintReadOnly"), Flags.bBlueprintReadOnly);
		Writer.WriteValue(TEXT("exposeOnSpawn"), Flags.bExposeOnSpawn);
		Writer.WriteValue(TEXT("private"), Flags.bPrivate);
		Writer.WriteValue(TEXT("transient"), Flags.bTransient);
		Writer.WriteValue(TEXT("saveGame"), Flags.bSaveGame);
		Writer.WriteValue(TEXT("advancedDisplay"), Flags.bAdvancedDisplay);
		Writer.WriteValue(TEXT("deprecated"), Flags.bDeprecated);
		Writer.WriteValue(TEXT("exposeToCinematics"), Flags.bExposeToCinematics);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteGraphPinJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilGraphPin& Pin)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("id"), LexGuidString(Pin.Id));
		Writer.WriteValue(TEXT("name"), LexNameString(Pin.Name));
		Writer.WriteValue(TEXT("direction"), LexPinDirectionString(Pin.Direction));
		Writer.WriteValue(TEXT("typeName"), LexNameString(Pin.TypeName));
		Writer.WriteValue(TEXT("isExec"), Pin.bIsExec);
		Writer.WriteValue(TEXT("isArray"), Pin.bIsArray);
		Writer.WriteValue(TEXT("defaultValue"), Pin.DefaultValue);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteGraphNodeJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilGraphNode& Node)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("id"), LexGuidString(Node.Id));
		Writer.WriteValue(TEXT("kind"), LexNodeKindString(Node.Kind));
		Writer.WriteValue(TEXT("descriptor"), LexNameString(Node.Descriptor));
		WriteVector2DJson(Writer, TEXT("position"), Node.Position);

		Writer.WriteArrayStart(TEXT("pins"));
		for (const FVergilGraphPin& Pin : Node.Pins)
		{
			WriteGraphPinJson(Writer, Pin);
		}
		Writer.WriteArrayEnd();

		WriteNameValueMapJson(Writer, TEXT("metadata"), Node.Metadata);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteGraphEdgeJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilGraphEdge& Edge)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("id"), LexGuidString(Edge.Id));
		Writer.WriteValue(TEXT("sourceNodeId"), LexGuidString(Edge.SourceNodeId));
		Writer.WriteValue(TEXT("sourcePinId"), LexGuidString(Edge.SourcePinId));
		Writer.WriteValue(TEXT("targetNodeId"), LexGuidString(Edge.TargetNodeId));
		Writer.WriteValue(TEXT("targetPinId"), LexGuidString(Edge.TargetPinId));
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDispatcherParameterJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilDispatcherParameter& Parameter)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Parameter.Name));
		Writer.WriteValue(TEXT("pinCategory"), LexNameString(Parameter.PinCategory));
		Writer.WriteValue(TEXT("pinSubCategory"), LexNameString(Parameter.PinSubCategory));
		Writer.WriteValue(TEXT("objectPath"), Parameter.ObjectPath);
		Writer.WriteValue(TEXT("isArray"), Parameter.bIsArray);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteDispatcherDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilDispatcherDefinition& Dispatcher)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Dispatcher.Name));
		Writer.WriteArrayStart(TEXT("parameters"));
		for (const FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
		{
			WriteDispatcherParameterJson(Writer, Parameter);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteVariableDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilVariableDefinition& Variable)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Variable.Name));
		WriteVariableTypeReferenceJson(Writer, TEXT("type"), Variable.Type);
		WriteVariableFlagsJson(Writer, TEXT("flags"), Variable.Flags);
		Writer.WriteValue(TEXT("category"), Variable.Category);
		WriteNameValueMapJson(Writer, TEXT("metadata"), Variable.Metadata);
		Writer.WriteValue(TEXT("defaultValue"), Variable.DefaultValue);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteFunctionParameterDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilFunctionParameterDefinition& Parameter)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Parameter.Name));
		WriteVariableTypeReferenceJson(Writer, TEXT("type"), Parameter.Type);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteFunctionDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilFunctionDefinition& Function)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Function.Name));
		Writer.WriteValue(TEXT("pure"), Function.bPure);
		Writer.WriteValue(TEXT("accessSpecifier"), LexFunctionAccessSpecifierString(Function.AccessSpecifier));
		Writer.WriteArrayStart(TEXT("inputs"));
		for (const FVergilFunctionParameterDefinition& Input : Function.Inputs)
		{
			WriteFunctionParameterDefinitionJson(Writer, Input);
		}
		Writer.WriteArrayEnd();
		Writer.WriteArrayStart(TEXT("outputs"));
		for (const FVergilFunctionParameterDefinition& Output : Function.Outputs)
		{
			WriteFunctionParameterDefinitionJson(Writer, Output);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteMacroParameterDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilMacroParameterDefinition& Parameter)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Parameter.Name));
		Writer.WriteValue(TEXT("isExec"), Parameter.bIsExec);
		WriteVariableTypeReferenceJson(Writer, TEXT("type"), Parameter.Type);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteMacroDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilMacroDefinition& Macro)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Macro.Name));
		Writer.WriteArrayStart(TEXT("inputs"));
		for (const FVergilMacroParameterDefinition& Input : Macro.Inputs)
		{
			WriteMacroParameterDefinitionJson(Writer, Input);
		}
		Writer.WriteArrayEnd();
		Writer.WriteArrayStart(TEXT("outputs"));
		for (const FVergilMacroParameterDefinition& Output : Macro.Outputs)
		{
			WriteMacroParameterDefinitionJson(Writer, Output);
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteComponentTransformJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const FVergilComponentTransformDefinition& Transform)
	{
		Writer.WriteObjectStart(FieldName);
		Writer.WriteValue(TEXT("hasRelativeLocation"), Transform.bHasRelativeLocation);
		WriteVectorJson(Writer, TEXT("relativeLocation"), Transform.RelativeLocation);
		Writer.WriteValue(TEXT("hasRelativeRotation"), Transform.bHasRelativeRotation);
		WriteRotatorJson(Writer, TEXT("relativeRotation"), Transform.RelativeRotation);
		Writer.WriteValue(TEXT("hasRelativeScale"), Transform.bHasRelativeScale);
		WriteVectorJson(Writer, TEXT("relativeScale3D"), Transform.RelativeScale3D);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteComponentDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilComponentDefinition& Component)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("name"), LexNameString(Component.Name));
		Writer.WriteValue(TEXT("componentClassPath"), Component.ComponentClassPath);
		Writer.WriteValue(TEXT("parentComponentName"), LexNameString(Component.ParentComponentName));
		Writer.WriteValue(TEXT("attachSocketName"), LexNameString(Component.AttachSocketName));
		WriteComponentTransformJson(Writer, TEXT("relativeTransform"), Component.RelativeTransform);
		WriteNameValueMapJson(Writer, TEXT("templateProperties"), Component.TemplateProperties);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteInterfaceDefinitionJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilInterfaceDefinition& InterfaceDefinition)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("interfaceClassPath"), InterfaceDefinition.InterfaceClassPath);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteGraphDocumentJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilGraphDocument& Document)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("format"), DocumentInspectionFormatName);
		Writer.WriteValue(TEXT("version"), DocumentInspectionFormatVersion);
		Writer.WriteValue(TEXT("schemaVersion"), Document.SchemaVersion);
		Writer.WriteValue(TEXT("blueprintPath"), Document.BlueprintPath);
		WriteNameValueMapJson(Writer, TEXT("metadata"), Document.Metadata);

		Writer.WriteArrayStart(TEXT("variables"));
		for (const FVergilVariableDefinition& Variable : Document.Variables)
		{
			WriteVariableDefinitionJson(Writer, Variable);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("functions"));
		for (const FVergilFunctionDefinition& Function : Document.Functions)
		{
			WriteFunctionDefinitionJson(Writer, Function);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("dispatchers"));
		for (const FVergilDispatcherDefinition& Dispatcher : Document.Dispatchers)
		{
			WriteDispatcherDefinitionJson(Writer, Dispatcher);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("macros"));
		for (const FVergilMacroDefinition& Macro : Document.Macros)
		{
			WriteMacroDefinitionJson(Writer, Macro);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("components"));
		for (const FVergilComponentDefinition& Component : Document.Components)
		{
			WriteComponentDefinitionJson(Writer, Component);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("interfaces"));
		for (const FVergilInterfaceDefinition& InterfaceDefinition : Document.Interfaces)
		{
			WriteInterfaceDefinitionJson(Writer, InterfaceDefinition);
		}
		Writer.WriteArrayEnd();

		WriteNameValueMapJson(Writer, TEXT("classDefaults"), Document.ClassDefaults);

		Writer.WriteArrayStart(TEXT("constructionScriptNodes"));
		for (const FVergilGraphNode& Node : Document.ConstructionScriptNodes)
		{
			WriteGraphNodeJson(Writer, Node);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("constructionScriptEdges"));
		for (const FVergilGraphEdge& Edge : Document.ConstructionScriptEdges)
		{
			WriteGraphEdgeJson(Writer, Edge);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("nodes"));
		for (const FVergilGraphNode& Node : Document.Nodes)
		{
			WriteGraphNodeJson(Writer, Node);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("edges"));
		for (const FVergilGraphEdge& Edge : Document.Edges)
		{
			WriteGraphEdgeJson(Writer, Edge);
		}
		Writer.WriteArrayEnd();

		Writer.WriteArrayStart(TEXT("tags"));
		for (const FName Tag : Document.Tags)
		{
			Writer.WriteValue(LexNameString(Tag));
		}
		Writer.WriteArrayEnd();
		Writer.WriteObjectEnd();
	}

	void AddDiagnostic(
		TArray<FVergilDiagnostic>* OutDiagnostics,
		const EVergilDiagnosticSeverity Severity,
		const FName Code,
		const FString& Message,
		const FGuid& SourceId = FGuid())
	{
		if (OutDiagnostics == nullptr)
		{
			return;
		}

		OutDiagnostics->Add(FVergilDiagnostic::Make(Severity, Code, Message, SourceId));
	}

	bool HasSchemaMigrationStep(const int32 SourceSchemaVersion)
	{
		switch (SourceSchemaVersion)
		{
		case 1:
		case 2:
			return true;

		default:
			return false;
		}
	}

	bool ApplySchemaMigrationStep(const int32 SourceSchemaVersion, FVergilGraphDocument& InOutDocument)
	{
		if (!HasSchemaMigrationStep(SourceSchemaVersion))
		{
			return false;
		}

		switch (SourceSchemaVersion)
		{
		case 1:
			// Schema 2 formalizes the expanded whole-asset document surface. Schema 1 remains
			// structurally compatible because the added fields are all additive/default-empty.
			static_cast<void>(InOutDocument);
			return true;

		case 2:
			// Schema 3 adds Blueprint-level metadata as another additive/default-empty document
			// surface, so forward migration only needs to advance the schema stamp.
			static_cast<void>(InOutDocument);
			return true;

		default:
			return false;
		}
	}

	bool IsSupportedBlueprintMetadataKey(const FName MetadataKey)
	{
		return MetadataKey == TEXT("BlueprintDisplayName")
			|| MetadataKey == TEXT("BlueprintDescription")
			|| MetadataKey == TEXT("BlueprintCategory")
			|| MetadataKey == TEXT("HideCategories");
	}

	bool IsSupportedTypeCategory(const FName PinCategory)
	{
		const FString Category = PinCategory.ToString().ToLower();
		return Category == TEXT("bool")
			|| Category == TEXT("int")
			|| Category == TEXT("float")
			|| Category == TEXT("double")
			|| Category == TEXT("string")
			|| Category == TEXT("name")
			|| Category == TEXT("text")
			|| Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool TypeCategoryRequiresObjectPath(const FName PinCategory)
	{
		const FString Category = PinCategory.ToString().ToLower();
		return Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	bool IsFiniteRotator(const FRotator& Value)
	{
		return FMath::IsFinite(Value.Roll)
			&& FMath::IsFinite(Value.Pitch)
			&& FMath::IsFinite(Value.Yaw);
	}

	bool ValidateTypeReference(
		const FName PinCategory,
		const FString& ObjectPath,
		const FString& ContextLabel,
		const FName MissingCategoryCode,
		const FName UnsupportedCategoryCode,
		const FName MissingObjectPathCode,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		if (PinCategory.IsNone())
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				MissingCategoryCode,
				FString::Printf(TEXT("%s must declare a type category."), *ContextLabel));
			return false;
		}

		if (!IsSupportedTypeCategory(PinCategory))
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				UnsupportedCategoryCode,
				FString::Printf(TEXT("%s declares unsupported type category '%s'."), *ContextLabel, *PinCategory.ToString()));
			return false;
		}

		const FString TrimmedObjectPath = ObjectPath.TrimStartAndEnd();
		if (TypeCategoryRequiresObjectPath(PinCategory) && TrimmedObjectPath.IsEmpty())
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				MissingObjectPathCode,
				FString::Printf(TEXT("%s type category '%s' requires an object path."), *ContextLabel, *PinCategory.ToString()));
			return false;
		}

		return true;
	}

	bool ValidateVariableTypeReference(
		const FVergilVariableTypeReference& Type,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		bool bIsValid = ValidateTypeReference(
			Type.PinCategory,
			Type.ObjectPath,
			ContextLabel,
			TEXT("VariableTypeCategoryMissing"),
			TEXT("VariableTypeCategoryUnsupported"),
			TEXT("VariableTypeObjectPathMissing"),
			OutDiagnostics);

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			if (Type.ValuePinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("VariableMapValueCategoryMissing"),
					FString::Printf(TEXT("%s map definitions must declare a value type category."), *ContextLabel));
			}
			else
			{
				bIsValid &= ValidateTypeReference(
					Type.ValuePinCategory,
					Type.ValueObjectPath,
					FString::Printf(TEXT("%s value type"), *ContextLabel),
					TEXT("VariableTypeCategoryMissing"),
					TEXT("VariableTypeCategoryUnsupported"),
					TEXT("VariableTypeObjectPathMissing"),
					OutDiagnostics);
			}
		}
		else if (!Type.ValuePinCategory.IsNone()
			|| !Type.ValuePinSubCategory.IsNone()
			|| !Type.ValueObjectPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableValueTypeUnexpected"),
				FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel));
		}

		return bIsValid;
	}

	bool ValidateFunctionParameterTypeReference(
		const FVergilVariableTypeReference& Type,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		bool bIsValid = ValidateTypeReference(
			Type.PinCategory,
			Type.ObjectPath,
			ContextLabel,
			TEXT("FunctionParameterTypeCategoryMissing"),
			TEXT("FunctionParameterTypeCategoryUnsupported"),
			TEXT("FunctionParameterTypeObjectPathMissing"),
			OutDiagnostics);

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			if (Type.ValuePinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterMapValueCategoryMissing"),
					FString::Printf(TEXT("%s map definitions must declare a value type category."), *ContextLabel));
			}
			else
			{
				bIsValid &= ValidateTypeReference(
					Type.ValuePinCategory,
					Type.ValueObjectPath,
					FString::Printf(TEXT("%s value type"), *ContextLabel),
					TEXT("FunctionParameterTypeCategoryMissing"),
					TEXT("FunctionParameterTypeCategoryUnsupported"),
					TEXT("FunctionParameterTypeObjectPathMissing"),
					OutDiagnostics);
			}
		}
		else if (!Type.ValuePinCategory.IsNone()
			|| !Type.ValuePinSubCategory.IsNone()
			|| !Type.ValueObjectPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionParameterValueTypeUnexpected"),
				FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel));
		}

		return bIsValid;
	}

	bool ValidateDispatcherParameterTypeReference(
		const FVergilDispatcherParameter& Parameter,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		return ValidateTypeReference(
			Parameter.PinCategory,
			Parameter.ObjectPath,
			ContextLabel,
			TEXT("DispatcherParameterCategoryMissing"),
			TEXT("DispatcherParameterCategoryUnsupported"),
			TEXT("DispatcherParameterObjectPathMissing"),
			OutDiagnostics);
	}

	bool ValidateMacroParameterTypeReference(
		const FVergilMacroParameterDefinition& Parameter,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		if (Parameter.bIsExec)
		{
			if (!Parameter.Type.PinCategory.IsNone()
				|| !Parameter.Type.PinSubCategory.IsNone()
				|| !Parameter.Type.ObjectPath.IsEmpty()
				|| Parameter.Type.ContainerType != EVergilVariableContainerType::None
				|| !Parameter.Type.ValuePinCategory.IsNone()
				|| !Parameter.Type.ValuePinSubCategory.IsNone()
				|| !Parameter.Type.ValueObjectPath.IsEmpty())
			{
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroExecPinTypeUnexpected"),
					FString::Printf(TEXT("%s cannot declare type metadata for exec pins."), *ContextLabel));
				return false;
			}

			return true;
		}

		bool bIsValid = ValidateTypeReference(
			Parameter.Type.PinCategory,
			Parameter.Type.ObjectPath,
			ContextLabel,
			TEXT("MacroParameterTypeCategoryMissing"),
			TEXT("MacroParameterTypeCategoryUnsupported"),
			TEXT("MacroParameterTypeObjectPathMissing"),
			OutDiagnostics);

		if (Parameter.Type.ContainerType == EVergilVariableContainerType::Map)
		{
			if (Parameter.Type.ValuePinCategory.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroParameterMapValueCategoryMissing"),
					FString::Printf(TEXT("%s map definitions must declare a value type category."), *ContextLabel));
			}
			else
			{
				bIsValid &= ValidateTypeReference(
					Parameter.Type.ValuePinCategory,
					Parameter.Type.ValueObjectPath,
					FString::Printf(TEXT("%s value type"), *ContextLabel),
					TEXT("MacroParameterTypeCategoryMissing"),
					TEXT("MacroParameterTypeCategoryUnsupported"),
					TEXT("MacroParameterTypeObjectPathMissing"),
					OutDiagnostics);
			}
		}
		else if (!Parameter.Type.ValuePinCategory.IsNone()
			|| !Parameter.Type.ValuePinSubCategory.IsNone()
			|| !Parameter.Type.ValueObjectPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroParameterValueTypeUnexpected"),
				FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel));
		}

		return bIsValid;
	}

	bool ValidateGraphDefinition(
		const TArray<FVergilGraphNode>& GraphNodes,
		const TArray<FVergilGraphEdge>& GraphEdges,
		const FString& GraphLabel,
		TSet<FGuid>& GlobalNodeIds,
		TSet<FGuid>& GlobalPinIds,
		TArray<FVergilDiagnostic>* OutDiagnostics)
	{
		bool bIsValid = true;
		TSet<FGuid> LocalNodeIds;
		TSet<FGuid> LocalPinIds;
		TMap<FGuid, TSet<FGuid>> NodePinIds;

		for (const FVergilGraphNode& Node : GraphNodes)
		{
			if (!Node.Id.IsValid())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("NodeIdMissing"),
					FString::Printf(TEXT("%s nodes must have a valid GUID."), *GraphLabel),
					Node.Id);
			}
			else if (GlobalNodeIds.Contains(Node.Id))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("NodeIdDuplicate"),
					FString::Printf(TEXT("%s reuses node id %s."), *GraphLabel, *Node.Id.ToString()),
					Node.Id);
			}
			else
			{
				GlobalNodeIds.Add(Node.Id);
				LocalNodeIds.Add(Node.Id);
			}

			if (Node.Descriptor.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("NodeDescriptorMissing"),
					FString::Printf(TEXT("%s nodes must declare a descriptor."), *GraphLabel),
					Node.Id);
			}

			for (const FVergilGraphPin& Pin : Node.Pins)
			{
				if (!Pin.Id.IsValid())
				{
					bIsValid = false;
					AddDiagnostic(
						OutDiagnostics,
						EVergilDiagnosticSeverity::Error,
						TEXT("PinIdMissing"),
						FString::Printf(TEXT("%s pins must have a valid GUID."), *GraphLabel),
						Node.Id);
				}
				else if (GlobalPinIds.Contains(Pin.Id))
				{
					bIsValid = false;
					AddDiagnostic(
						OutDiagnostics,
						EVergilDiagnosticSeverity::Error,
						TEXT("PinIdDuplicate"),
						FString::Printf(TEXT("%s reuses pin id %s."), *GraphLabel, *Pin.Id.ToString()),
						Node.Id);
				}
				else
				{
					GlobalPinIds.Add(Pin.Id);
					LocalPinIds.Add(Pin.Id);
					NodePinIds.FindOrAdd(Node.Id).Add(Pin.Id);
				}
			}
		}

		for (const FVergilGraphEdge& Edge : GraphEdges)
		{
			const bool bHasValidSourceNode = LocalNodeIds.Contains(Edge.SourceNodeId);
			const bool bHasValidTargetNode = LocalNodeIds.Contains(Edge.TargetNodeId);
			const bool bHasValidSourcePin = LocalPinIds.Contains(Edge.SourcePinId);
			const bool bHasValidTargetPin = LocalPinIds.Contains(Edge.TargetPinId);

			if (!bHasValidSourceNode || !bHasValidTargetNode || !bHasValidSourcePin || !bHasValidTargetPin)
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("EdgeReferenceInvalid"),
					FString::Printf(TEXT("%s edges must reference nodes and pins authored in the same graph definition."), *GraphLabel),
					Edge.Id);
				continue;
			}

			const TSet<FGuid>* const SourceNodePins = NodePinIds.Find(Edge.SourceNodeId);
			const TSet<FGuid>* const TargetNodePins = NodePinIds.Find(Edge.TargetNodeId);
			if (SourceNodePins == nullptr
				|| TargetNodePins == nullptr
				|| !SourceNodePins->Contains(Edge.SourcePinId)
				|| !TargetNodePins->Contains(Edge.TargetPinId))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("EdgePinNodeMismatch"),
					FString::Printf(TEXT("%s edges must connect pins owned by their declared source and target nodes."), *GraphLabel),
					Edge.Id);
			}
		}

		return bIsValid;
	}
}

bool FVergilGraphDocument::IsStructurallyValid(TArray<FVergilDiagnostic>* OutDiagnostics) const
{
	bool bIsValid = true;

	if (SchemaVersion <= 0)
	{
		bIsValid = false;
		AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("InvalidSchemaVersion"), TEXT("SchemaVersion must be greater than zero."));
	}

	for (const TPair<FName, FString>& MetadataEntry : Metadata)
	{
		if (MetadataEntry.Key.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("BlueprintMetadataKeyMissing"),
				TEXT("Blueprint metadata entries must use a non-empty key."));
			continue;
		}

		if (!IsSupportedBlueprintMetadataKey(MetadataEntry.Key))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("BlueprintMetadataKeyUnsupported"),
				FString::Printf(TEXT("Blueprint metadata key '%s' is not currently supported."), *MetadataEntry.Key.ToString()));
		}
	}

	TSet<FGuid> NodeIds;
	TSet<FGuid> PinIds;
	TSet<FName> VariableNames;
	TSet<FName> DispatcherNames;
	TSet<FName> AllComponentNames;

	for (const FVergilComponentDefinition& Component : Components)
	{
		if (!Component.Name.IsNone())
		{
			AllComponentNames.Add(Component.Name);
		}
	}

	for (const FVergilDispatcherDefinition& Dispatcher : Dispatchers)
	{
		if (Dispatcher.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherNameMissing"), TEXT("Every dispatcher must declare a name."));
			continue;
		}

		if (DispatcherNames.Contains(Dispatcher.Name))
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherNameDuplicate"), FString::Printf(TEXT("Duplicate dispatcher name '%s'."), *Dispatcher.Name.ToString()));
			continue;
		}

		DispatcherNames.Add(Dispatcher.Name);

		TSet<FName> ParameterNames;
		for (const FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
		{
			const FString ParameterLabel = Parameter.Name.IsNone()
				? FString::Printf(TEXT("Dispatcher '%s' parameter"), *Dispatcher.Name.ToString())
				: FString::Printf(TEXT("Dispatcher '%s' parameter '%s'"), *Dispatcher.Name.ToString(), *Parameter.Name.ToString());

			if (Parameter.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherParameterNameMissing"), FString::Printf(TEXT("Dispatcher '%s' contains a parameter without a name."), *Dispatcher.Name.ToString()));
				continue;
			}

			if (ParameterNames.Contains(Parameter.Name))
			{
				bIsValid = false;
				AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("DispatcherParameterNameDuplicate"), FString::Printf(TEXT("Dispatcher '%s' contains duplicate parameter '%s'."), *Dispatcher.Name.ToString(), *Parameter.Name.ToString()));
				continue;
			}

			ParameterNames.Add(Parameter.Name);
			bIsValid &= ValidateDispatcherParameterTypeReference(Parameter, ParameterLabel, OutDiagnostics);
		}
	}

	for (const FVergilVariableDefinition& Variable : Variables)
	{
		const FString VariableLabel = Variable.Name.IsNone()
			? FString(TEXT("Variable"))
			: FString::Printf(TEXT("Variable '%s'"), *Variable.Name.ToString());

		if (Variable.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("VariableNameMissing"), TEXT("Every variable must declare a name."));
			continue;
		}

		if (VariableNames.Contains(Variable.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableNameDuplicate"),
				FString::Printf(TEXT("Duplicate variable name '%s'."), *Variable.Name.ToString()));
			continue;
		}

		if (DispatcherNames.Contains(Variable.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Variable '%s' conflicts with a dispatcher of the same name."), *Variable.Name.ToString()));
			continue;
		}

		VariableNames.Add(Variable.Name);

		bIsValid &= ValidateVariableTypeReference(Variable.Type, VariableLabel, OutDiagnostics);

		for (const TPair<FName, FString>& MetadataEntry : Variable.Metadata)
		{
			if (MetadataEntry.Key.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("VariableMetadataKeyMissing"),
					FString::Printf(TEXT("%s contains metadata with an empty key."), *VariableLabel));
			}
		}

		if (Variable.Flags.bExposeOnSpawn && !Variable.Flags.bInstanceEditable)
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableExposeOnSpawnRequiresInstanceEditable"),
				FString::Printf(TEXT("%s cannot enable ExposeOnSpawn unless it is also instance editable."), *VariableLabel));
		}
	}

	TSet<FName> FunctionNames;
	for (const FVergilFunctionDefinition& Function : Functions)
	{
		const FString FunctionLabel = Function.Name.IsNone()
			? FString(TEXT("Function"))
			: FString::Printf(TEXT("Function '%s'"), *Function.Name.ToString());

		if (Function.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("FunctionNameMissing"), TEXT("Every function must declare a name."));
			continue;
		}

		if (FunctionNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameDuplicate"),
				FString::Printf(TEXT("Duplicate function name '%s'."), *Function.Name.ToString()));
			continue;
		}

		if (VariableNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameConflictsWithVariable"),
				FString::Printf(TEXT("Function '%s' conflicts with a variable of the same name."), *Function.Name.ToString()));
			continue;
		}

		if (DispatcherNames.Contains(Function.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Function '%s' conflicts with a dispatcher of the same name."), *Function.Name.ToString()));
			continue;
		}

		FunctionNames.Add(Function.Name);

		TSet<FName> SignatureNames;
		for (const FVergilFunctionParameterDefinition& Input : Function.Inputs)
		{
			const FString InputLabel = Input.Name.IsNone()
				? FString::Printf(TEXT("%s input"), *FunctionLabel)
				: FString::Printf(TEXT("%s input '%s'"), *FunctionLabel, *Input.Name.ToString());

			if (Input.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionInputNameMissing"),
					FString::Printf(TEXT("%s contains an input without a name."), *FunctionLabel));
				continue;
			}

			if (SignatureNames.Contains(Input.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *FunctionLabel, *Input.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Input.Name);
			bIsValid &= ValidateFunctionParameterTypeReference(Input.Type, InputLabel, OutDiagnostics);
		}

		for (const FVergilFunctionParameterDefinition& Output : Function.Outputs)
		{
			const FString OutputLabel = Output.Name.IsNone()
				? FString::Printf(TEXT("%s output"), *FunctionLabel)
				: FString::Printf(TEXT("%s output '%s'"), *FunctionLabel, *Output.Name.ToString());

			if (Output.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionOutputNameMissing"),
					FString::Printf(TEXT("%s contains an output without a name."), *FunctionLabel));
				continue;
			}

			if (SignatureNames.Contains(Output.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *FunctionLabel, *Output.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Output.Name);
			bIsValid &= ValidateFunctionParameterTypeReference(Output.Type, OutputLabel, OutDiagnostics);
		}
	}

	TSet<FName> MacroNames;
	for (const FVergilMacroDefinition& Macro : Macros)
	{
		const FString MacroLabel = Macro.Name.IsNone()
			? FString(TEXT("Macro"))
			: FString::Printf(TEXT("Macro '%s'"), *Macro.Name.ToString());

		if (Macro.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("MacroNameMissing"), TEXT("Every macro must declare a name."));
			continue;
		}

		if (MacroNames.Contains(Macro.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroNameDuplicate"),
				FString::Printf(TEXT("Duplicate macro name '%s'."), *Macro.Name.ToString()));
			continue;
		}

		if (VariableNames.Contains(Macro.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroNameConflictsWithVariable"),
				FString::Printf(TEXT("Macro '%s' conflicts with a variable of the same name."), *Macro.Name.ToString()));
			continue;
		}

		if (FunctionNames.Contains(Macro.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroNameConflictsWithFunction"),
				FString::Printf(TEXT("Macro '%s' conflicts with a function of the same name."), *Macro.Name.ToString()));
			continue;
		}

		if (DispatcherNames.Contains(Macro.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Macro '%s' conflicts with a dispatcher of the same name."), *Macro.Name.ToString()));
			continue;
		}

		if (AllComponentNames.Contains(Macro.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroNameConflictsWithComponent"),
				FString::Printf(TEXT("Macro '%s' conflicts with a component of the same name."), *Macro.Name.ToString()));
			continue;
		}

		MacroNames.Add(Macro.Name);

		TSet<FName> SignatureNames;
		for (const FVergilMacroParameterDefinition& Input : Macro.Inputs)
		{
			const FString InputLabel = Input.Name.IsNone()
				? FString::Printf(TEXT("%s input"), *MacroLabel)
				: FString::Printf(TEXT("%s input '%s'"), *MacroLabel, *Input.Name.ToString());

			if (Input.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroInputNameMissing"),
					FString::Printf(TEXT("%s contains an input without a name."), *MacroLabel));
				continue;
			}

			if (SignatureNames.Contains(Input.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *MacroLabel, *Input.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Input.Name);
			bIsValid &= ValidateMacroParameterTypeReference(Input, InputLabel, OutDiagnostics);
		}

		for (const FVergilMacroParameterDefinition& Output : Macro.Outputs)
		{
			const FString OutputLabel = Output.Name.IsNone()
				? FString::Printf(TEXT("%s output"), *MacroLabel)
				: FString::Printf(TEXT("%s output '%s'"), *MacroLabel, *Output.Name.ToString());

			if (Output.Name.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroOutputNameMissing"),
					FString::Printf(TEXT("%s contains an output without a name."), *MacroLabel));
				continue;
			}

			if (SignatureNames.Contains(Output.Name))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroParameterNameDuplicate"),
					FString::Printf(TEXT("%s contains duplicate signature member '%s'."), *MacroLabel, *Output.Name.ToString()));
				continue;
			}

			SignatureNames.Add(Output.Name);
			bIsValid &= ValidateMacroParameterTypeReference(Output, OutputLabel, OutDiagnostics);
		}
	}

	TSet<FName> ComponentNames;
	TSet<FName> SelfParentComponentNames;
	TMap<FName, FName> ComponentParents;
	for (const FVergilComponentDefinition& Component : Components)
	{
		const FString ComponentLabel = Component.Name.IsNone()
			? FString(TEXT("Component"))
			: FString::Printf(TEXT("Component '%s'"), *Component.Name.ToString());

		if (Component.Name.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(OutDiagnostics, EVergilDiagnosticSeverity::Error, TEXT("ComponentNameMissing"), TEXT("Every component must declare a name."));
			continue;
		}

		if (ComponentNames.Contains(Component.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentNameDuplicate"),
				FString::Printf(TEXT("Duplicate component name '%s'."), *Component.Name.ToString()));
			continue;
		}

		ComponentNames.Add(Component.Name);
		ComponentParents.Add(Component.Name, Component.ParentComponentName);

		if (VariableNames.Contains(Component.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentNameConflictsWithVariable"),
				FString::Printf(TEXT("Component '%s' conflicts with a variable of the same name."), *Component.Name.ToString()));
		}

		if (FunctionNames.Contains(Component.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentNameConflictsWithFunction"),
				FString::Printf(TEXT("Component '%s' conflicts with a function of the same name."), *Component.Name.ToString()));
		}

		if (DispatcherNames.Contains(Component.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentNameConflictsWithDispatcher"),
				FString::Printf(TEXT("Component '%s' conflicts with a dispatcher of the same name."), *Component.Name.ToString()));
		}

		if (MacroNames.Contains(Component.Name))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentNameConflictsWithMacro"),
				FString::Printf(TEXT("Component '%s' conflicts with a macro of the same name."), *Component.Name.ToString()));
		}

		if (Component.ComponentClassPath.TrimStartAndEnd().IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentClassPathMissing"),
				FString::Printf(TEXT("%s must declare a component class path."), *ComponentLabel));
		}

		if (Component.ParentComponentName == Component.Name)
		{
			bIsValid = false;
			SelfParentComponentNames.Add(Component.Name);
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentParentSelfReference"),
				FString::Printf(TEXT("%s cannot name itself as its parent component."), *ComponentLabel));
		}
		else if (!Component.ParentComponentName.IsNone() && !AllComponentNames.Contains(Component.ParentComponentName))
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Warning,
				TEXT("ComponentParentMissing"),
				FString::Printf(
					TEXT("%s references parent '%s' which is not authored in this document. This may be intentional for inherited components, but Vergil cannot validate that attachment target locally."),
					*ComponentLabel,
					*Component.ParentComponentName.ToString()));
		}

		if (!Component.AttachSocketName.IsNone() && Component.ParentComponentName.IsNone())
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Warning,
				TEXT("ComponentAttachSocketWithoutParent"),
				FString::Printf(TEXT("%s declares attach socket '%s' without naming a parent component."), *ComponentLabel, *Component.AttachSocketName.ToString()));
		}

		if (Component.RelativeTransform.bHasRelativeLocation && !IsFiniteVector(Component.RelativeTransform.RelativeLocation))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentRelativeLocationInvalid"),
				FString::Printf(TEXT("%s declares a non-finite relative location."), *ComponentLabel));
		}

		if (Component.RelativeTransform.bHasRelativeRotation && !IsFiniteRotator(Component.RelativeTransform.RelativeRotation))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentRelativeRotationInvalid"),
				FString::Printf(TEXT("%s declares a non-finite relative rotation."), *ComponentLabel));
		}

		if (Component.RelativeTransform.bHasRelativeScale && !IsFiniteVector(Component.RelativeTransform.RelativeScale3D))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentRelativeScaleInvalid"),
				FString::Printf(TEXT("%s declares a non-finite relative scale."), *ComponentLabel));
		}

		for (const TPair<FName, FString>& TemplateProperty : Component.TemplateProperties)
		{
			if (TemplateProperty.Key.IsNone())
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentTemplatePropertyNameMissing"),
					FString::Printf(TEXT("%s contains a template property with an empty name."), *ComponentLabel));
			}
		}
	}

	for (const TPair<FName, FName>& ComponentParentPair : ComponentParents)
	{
		if (ComponentParentPair.Value.IsNone() || SelfParentComponentNames.Contains(ComponentParentPair.Key))
		{
			continue;
		}

		TSet<FName> VisitedComponents;
		FName CurrentComponentName = ComponentParentPair.Key;
		while (!CurrentComponentName.IsNone())
		{
			if (VisitedComponents.Contains(CurrentComponentName))
			{
				bIsValid = false;
				AddDiagnostic(
					OutDiagnostics,
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentParentCycle"),
					FString::Printf(TEXT("Component '%s' participates in a circular parent chain."), *ComponentParentPair.Key.ToString()));
				break;
			}

			VisitedComponents.Add(CurrentComponentName);

			const FName* ParentComponentName = ComponentParents.Find(CurrentComponentName);
			if (ParentComponentName == nullptr || ParentComponentName->IsNone() || !AllComponentNames.Contains(*ParentComponentName))
			{
				break;
			}

			CurrentComponentName = *ParentComponentName;
		}
	}

	TSet<FString> InterfaceClassPaths;
	for (const FVergilInterfaceDefinition& Interface : Interfaces)
	{
		const FString TrimmedInterfaceClassPath = Interface.InterfaceClassPath.TrimStartAndEnd();
		if (TrimmedInterfaceClassPath.IsEmpty())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("InterfaceClassPathMissing"),
				TEXT("Every implemented interface must declare an interface class path."));
			continue;
		}

		if (InterfaceClassPaths.Contains(TrimmedInterfaceClassPath))
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("InterfaceClassPathDuplicate"),
				FString::Printf(TEXT("Duplicate implemented interface class path '%s'."), *TrimmedInterfaceClassPath));
			continue;
		}

		InterfaceClassPaths.Add(TrimmedInterfaceClassPath);
	}

	for (const TPair<FName, FString>& ClassDefault : ClassDefaults)
	{
		if (ClassDefault.Key.IsNone())
		{
			bIsValid = false;
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("ClassDefaultPropertyNameMissing"),
				TEXT("Class defaults must declare a non-empty property name."));
		}
	}

	bIsValid &= ValidateGraphDefinition(Nodes, Edges, TEXT("Target graph"), NodeIds, PinIds, OutDiagnostics);
	bIsValid &= ValidateGraphDefinition(
		ConstructionScriptNodes,
		ConstructionScriptEdges,
		TEXT("Construction script"),
		NodeIds,
		PinIds,
		OutDiagnostics);

	if (SchemaVersion > Vergil::SchemaVersion)
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Warning,
			TEXT("SchemaVersionFuture"),
			FString::Printf(TEXT("Document schema %d is newer than compiler schema %d."), SchemaVersion, Vergil::SchemaVersion));
	}

	return bIsValid;
}

FString Vergil::GetDocumentInspectionFormatName()
{
	return DocumentInspectionFormatName;
}

int32 Vergil::GetDocumentInspectionFormatVersion()
{
	return DocumentInspectionFormatVersion;
}

FString Vergil::DescribeGraphDocument(const FVergilGraphDocument& Document)
{
	const FString TrimmedBlueprintPath = Document.BlueprintPath.TrimStartAndEnd();
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s version=%d schema=%d blueprint=%s metadata=%d variables=%d functions=%d dispatchers=%d macros=%d components=%d interfaces=%d classDefaults=%d constructionScript=%d/%d graph=%d/%d tags=%d"),
		DocumentInspectionFormatName,
		DocumentInspectionFormatVersion,
		Document.SchemaVersion,
		TrimmedBlueprintPath.IsEmpty() ? TEXT("<none>") : *TrimmedBlueprintPath,
		Document.Metadata.Num(),
		Document.Variables.Num(),
		Document.Functions.Num(),
		Document.Dispatchers.Num(),
		Document.Macros.Num(),
		Document.Components.Num(),
		Document.Interfaces.Num(),
		Document.ClassDefaults.Num(),
		Document.ConstructionScriptNodes.Num(),
		Document.ConstructionScriptEdges.Num(),
		Document.Nodes.Num(),
		Document.Edges.Num(),
		Document.Tags.Num()));

	auto AppendSection = [&Lines](const TCHAR* Title, const TArray<FString>& Entries)
	{
		if (Entries.Num() == 0)
		{
			return;
		}

		Lines.Add(Title);
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			Lines.Add(FString::Printf(TEXT("  %d: %s"), EntryIndex, *Entries[EntryIndex]));
		}
	};

	if (Document.Metadata.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("metadata: %s"), *DescribeNameValueMap(Document.Metadata)));
	}

	if (Document.ClassDefaults.Num() > 0)
	{
		Lines.Add(FString::Printf(TEXT("classDefaults: %s"), *DescribeNameValueMap(Document.ClassDefaults)));
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Variables.Num());
		for (const FVergilVariableDefinition& Variable : Document.Variables)
		{
			Entries.Add(DescribeVariableDefinition(Variable));
		}
		AppendSection(TEXT("variables:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Functions.Num());
		for (const FVergilFunctionDefinition& Function : Document.Functions)
		{
			Entries.Add(DescribeFunctionDefinition(Function));
		}
		AppendSection(TEXT("functions:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Dispatchers.Num());
		for (const FVergilDispatcherDefinition& Dispatcher : Document.Dispatchers)
		{
			Entries.Add(DescribeDispatcherDefinition(Dispatcher));
		}
		AppendSection(TEXT("dispatchers:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Macros.Num());
		for (const FVergilMacroDefinition& Macro : Document.Macros)
		{
			Entries.Add(DescribeMacroDefinition(Macro));
		}
		AppendSection(TEXT("macros:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Components.Num());
		for (const FVergilComponentDefinition& Component : Document.Components)
		{
			Entries.Add(DescribeComponentDefinition(Component));
		}
		AppendSection(TEXT("components:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Interfaces.Num());
		for (const FVergilInterfaceDefinition& InterfaceDefinition : Document.Interfaces)
		{
			Entries.Add(DescribeInterfaceDefinition(InterfaceDefinition));
		}
		AppendSection(TEXT("interfaces:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.ConstructionScriptNodes.Num());
		for (const FVergilGraphNode& Node : Document.ConstructionScriptNodes)
		{
			Entries.Add(DescribeGraphNode(Node));
		}
		AppendSection(TEXT("constructionScriptNodes:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.ConstructionScriptEdges.Num());
		for (const FVergilGraphEdge& Edge : Document.ConstructionScriptEdges)
		{
			Entries.Add(DescribeGraphEdge(Edge));
		}
		AppendSection(TEXT("constructionScriptEdges:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Nodes.Num());
		for (const FVergilGraphNode& Node : Document.Nodes)
		{
			Entries.Add(DescribeGraphNode(Node));
		}
		AppendSection(TEXT("nodes:"), Entries);
	}

	{
		TArray<FString> Entries;
		Entries.Reserve(Document.Edges.Num());
		for (const FVergilGraphEdge& Edge : Document.Edges)
		{
			Entries.Add(DescribeGraphEdge(Edge));
		}
		AppendSection(TEXT("edges:"), Entries);
	}

	if (Document.Tags.Num() > 0)
	{
		TArray<FString> TagEntries;
		TagEntries.Reserve(Document.Tags.Num());
		for (const FName Tag : Document.Tags)
		{
			TagEntries.Add(LexOptionalNameString(Tag));
		}
		AppendSection(TEXT("tags:"), TagEntries);
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeGraphDocument(const FVergilGraphDocument& Document, const bool bPrettyPrint)
{
	FString SerializedDocument;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedDocument);
		WriteGraphDocumentJson(*Writer, Document);
		Writer->Close();
	}
	else
	{
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedDocument);
		WriteGraphDocumentJson(*Writer, Document);
		Writer->Close();
	}

	return SerializedDocument;
}

bool Vergil::CanMigrateSchemaVersion(const int32 SourceSchemaVersion, const int32 TargetSchemaVersion)
{
	if (SourceSchemaVersion <= 0 || TargetSchemaVersion <= 0 || SourceSchemaVersion > TargetSchemaVersion)
	{
		return false;
	}

	for (int32 Version = SourceSchemaVersion; Version < TargetSchemaVersion; ++Version)
	{
		if (!HasSchemaMigrationStep(Version))
		{
			return false;
		}
	}

	return true;
}

TArray<FString> Vergil::GetSupportedSchemaMigrationPaths()
{
	TArray<FString> Paths;

	for (int32 Version = 1; Version < Vergil::SchemaVersion; ++Version)
	{
		if (HasSchemaMigrationStep(Version))
		{
			Paths.Add(FString::Printf(TEXT("%d->%d"), Version, Version + 1));
		}
	}

	return Paths;
}

bool Vergil::MigrateDocumentSchema(
	const FVergilGraphDocument& SourceDocument,
	FVergilGraphDocument& OutDocument,
	TArray<FVergilDiagnostic>* OutDiagnostics,
	const int32 TargetSchemaVersion)
{
	OutDocument = SourceDocument;

	if (TargetSchemaVersion <= 0)
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Error,
			TEXT("SchemaMigrationTargetInvalid"),
			FString::Printf(TEXT("Target schema %d must be greater than zero."), TargetSchemaVersion));
		return false;
	}

	if (SourceDocument.SchemaVersion <= 0)
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Error,
			TEXT("InvalidSchemaVersion"),
			TEXT("SchemaVersion must be greater than zero."));
		return false;
	}

	if (SourceDocument.SchemaVersion == TargetSchemaVersion)
	{
		return true;
	}

	if (SourceDocument.SchemaVersion > TargetSchemaVersion)
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Error,
			TEXT("SchemaMigrationDowngradeUnsupported"),
			FString::Printf(
				TEXT("Schema migration only supports forward upgrades. Cannot migrate schema %d to older schema %d."),
				SourceDocument.SchemaVersion,
				TargetSchemaVersion));
		return false;
	}

	if (!CanMigrateSchemaVersion(SourceDocument.SchemaVersion, TargetSchemaVersion))
	{
		AddDiagnostic(
			OutDiagnostics,
			EVergilDiagnosticSeverity::Error,
			TEXT("SchemaMigrationPathMissing"),
			FString::Printf(
				TEXT("No schema migration path exists from schema %d to schema %d."),
				SourceDocument.SchemaVersion,
				TargetSchemaVersion));
		return false;
	}

	for (int32 Version = SourceDocument.SchemaVersion; Version < TargetSchemaVersion; ++Version)
	{
		if (!ApplySchemaMigrationStep(Version, OutDocument))
		{
			AddDiagnostic(
				OutDiagnostics,
				EVergilDiagnosticSeverity::Error,
				TEXT("SchemaMigrationStepFailed"),
				FString::Printf(TEXT("Schema migration step %d to %d failed."), Version, Version + 1));
			return false;
		}

		OutDocument.SchemaVersion = Version + 1;
	}

	AddDiagnostic(
		OutDiagnostics,
		EVergilDiagnosticSeverity::Info,
		TEXT("SchemaMigrationApplied"),
		FString::Printf(
			TEXT("Migrated document schema %d to schema %d."),
			SourceDocument.SchemaVersion,
			TargetSchemaVersion));

	return true;
}

bool Vergil::MigrateDocumentToCurrentSchema(
	const FVergilGraphDocument& SourceDocument,
	FVergilGraphDocument& OutDocument,
	TArray<FVergilDiagnostic>* OutDiagnostics)
{
	return MigrateDocumentSchema(SourceDocument, OutDocument, OutDiagnostics, SchemaVersion);
}
