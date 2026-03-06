#include "VergilCompilerPasses.h"

#include "Algo/AnyOf.h"

#include "VergilCompilerTypes.h"
#include "VergilDiagnostic.h"
#include "VergilNodeRegistry.h"

namespace
{
	const FName EventGraphName(TEXT("EventGraph"));
	const FName ConstructionScriptGraphName(TEXT("UserConstructionScript"));

	void CopyPlannedPins(const FVergilGraphNode& Node, FVergilCompilerCommand& Command)
	{
		Command.PlannedPins.Reset();
		for (const FVergilGraphPin& Pin : Node.Pins)
		{
			FVergilPlannedPin PlannedPin;
			PlannedPin.PinId = Pin.Id;
			PlannedPin.Name = Pin.Name;
			PlannedPin.bIsInput = (Pin.Direction == EVergilPinDirection::Input);
			PlannedPin.bIsExec = Pin.bIsExec;
			Command.PlannedPins.Add(PlannedPin);
		}
	}

	FString GetDescriptorSuffix(const FName Descriptor, const FString& Prefix)
	{
		const FString DescriptorString = Descriptor.ToString();
		if (!DescriptorString.StartsWith(Prefix))
		{
			return FString();
		}

		return DescriptorString.RightChop(Prefix.Len());
	}

	bool IsSupportedCompileTargetGraph(const FName GraphName)
	{
		return GraphName == EventGraphName || GraphName == ConstructionScriptGraphName;
	}

	void AddSemanticValidationDiagnostic(
		FVergilCompilerContext& Context,
		const FName Code,
		const FString& Message,
		const FGuid& SourceId)
	{
		Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, Code, Message, SourceId);
	}

	bool HasRequiredMetadataValue(
		const FVergilGraphNode& Node,
		const FName MetadataKey,
		FVergilCompilerContext& Context,
		const FName Code,
		const FString& Message,
		const bool bTreatWhitespaceAsEmpty = true)
	{
		const FString Value = Node.Metadata.FindRef(MetadataKey);
		const FString ValueToValidate = bTreatWhitespaceAsEmpty ? Value.TrimStartAndEnd() : Value;
		if (!ValueToValidate.IsEmpty())
		{
			return true;
		}

		AddSemanticValidationDiagnostic(Context, Code, Message, Node.Id);
		return false;
	}

	bool ValidateNodeSemanticRequirements(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		const FString DescriptorString = Node.Descriptor.ToString();
		bool bIsValid = true;

		auto AddNodeDiagnostic = [&](const FName Code, const FString& Message)
		{
			AddSemanticValidationDiagnostic(Context, Code, Message, Node.Id);
			bIsValid = false;
		};

		if (DescriptorString.StartsWith(TEXT("K2.Event.")))
		{
			if (Node.Kind != EVergilNodeKind::Event)
			{
				AddNodeDiagnostic(
					TEXT("EventNodeKindInvalid"),
					FString::Printf(TEXT("Descriptor '%s' requires node kind Event."), *DescriptorString));
			}

			const FString EventName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Event."));
			if (EventName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingEventName"), TEXT("Event nodes require a descriptor suffix naming the bound function."));
			}
			else if (Context.GetGraphName() == ConstructionScriptGraphName)
			{
				if (EventName != ConstructionScriptGraphName.ToString())
				{
					AddNodeDiagnostic(
						TEXT("ConstructionScriptEventInvalid"),
						TEXT("Construction-script graphs only support K2.Event.UserConstructionScript as their authored event entry."));
				}
			}
			else if (EventName == ConstructionScriptGraphName.ToString())
			{
				AddNodeDiagnostic(
					TEXT("ConstructionScriptEventGraphMismatch"),
					TEXT("K2.Event.UserConstructionScript is only valid when compiling the UserConstructionScript graph."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.CustomEvent.")))
		{
			const FString EventName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CustomEvent."));
			if (EventName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingCustomEventName"), TEXT("Custom event nodes require a descriptor suffix naming the event."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.Call.")))
		{
			if (Node.Kind != EVergilNodeKind::Call)
			{
				AddNodeDiagnostic(
					TEXT("CallNodeKindInvalid"),
					FString::Printf(TEXT("Descriptor '%s' requires node kind Call."), *DescriptorString));
			}

			const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Call."));
			if (FunctionName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingFunctionName"), TEXT("Call nodes require a descriptor suffix naming the function."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.VarGet.")))
		{
			if (Node.Kind != EVergilNodeKind::VariableGet)
			{
				AddNodeDiagnostic(
					TEXT("VariableGetNodeKindInvalid"),
					FString::Printf(TEXT("Descriptor '%s' requires node kind VariableGet."), *DescriptorString));
			}

			const FString VariableName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarGet."));
			if (VariableName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingVariableName"), TEXT("Variable get nodes require a descriptor suffix naming the variable."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.VarSet.")))
		{
			if (Node.Kind != EVergilNodeKind::VariableSet)
			{
				AddNodeDiagnostic(
					TEXT("VariableSetNodeKindInvalid"),
					FString::Printf(TEXT("Descriptor '%s' requires node kind VariableSet."), *DescriptorString));
			}

			const FString VariableName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarSet."));
			if (VariableName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingVariableName"), TEXT("Variable set nodes require a descriptor suffix naming the variable."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.BindDelegate.")))
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.BindDelegate."));
			if (PropertyName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingBindDelegateMetadata"), TEXT("Bind delegate nodes require a descriptor suffix naming the property."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.RemoveDelegate.")))
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.RemoveDelegate."));
			if (PropertyName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingRemoveDelegateMetadata"), TEXT("Remove delegate nodes require a descriptor suffix naming the property."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.ClearDelegate.")))
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.ClearDelegate."));
			if (PropertyName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingClearDelegateMetadata"), TEXT("Clear delegate nodes require a descriptor suffix naming the property."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.CallDelegate.")))
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CallDelegate."));
			if (PropertyName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingCallDelegateMetadata"), TEXT("Call delegate nodes require a descriptor suffix naming the property."));
			}

			return bIsValid;
		}

		if (DescriptorString.StartsWith(TEXT("K2.CreateDelegate.")))
		{
			const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CreateDelegate."));
			if (FunctionName.IsEmpty())
			{
				AddNodeDiagnostic(TEXT("MissingCreateDelegateFunction"), TEXT("CreateDelegate nodes require a descriptor suffix naming the selected function."));
			}

			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.Cast"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("TargetClassPath"),
				Context,
				TEXT("MissingTargetClassPath"),
				TEXT("Cast nodes require metadata TargetClassPath naming the target class."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.Select"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("IndexPinCategory"),
				Context,
				TEXT("MissingSelectIndexCategory"),
				TEXT("Select nodes require metadata IndexPinCategory."));
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("ValuePinCategory"),
				Context,
				TEXT("MissingSelectValueCategory"),
				TEXT("Select nodes require metadata ValuePinCategory."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.SwitchEnum"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("EnumPath"),
				Context,
				TEXT("MissingSwitchEnumPath"),
				TEXT("Switch enum nodes require metadata EnumPath."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.FormatText"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("FormatPattern"),
				Context,
				TEXT("MissingFormatPattern"),
				TEXT("Format text nodes require metadata FormatPattern."),
				false);
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.MakeStruct"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("StructPath"),
				Context,
				TEXT("MissingMakeStructPath"),
				TEXT("Make struct nodes require metadata StructPath."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.BreakStruct"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("StructPath"),
				Context,
				TEXT("MissingBreakStructPath"),
				TEXT("Break struct nodes require metadata StructPath."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.MakeArray"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("ValuePinCategory"),
				Context,
				TEXT("MissingMakeArrayValueCategory"),
				TEXT("Make array nodes require metadata ValuePinCategory."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.MakeSet"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("ValuePinCategory"),
				Context,
				TEXT("MissingMakeSetValueCategory"),
				TEXT("Make set nodes require metadata ValuePinCategory."));
			return bIsValid;
		}

		if (Node.Descriptor == TEXT("K2.MakeMap"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("KeyPinCategory"),
				Context,
				TEXT("MissingMakeMapKeyCategory"),
				TEXT("Make map nodes require metadata KeyPinCategory."));
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("ValuePinCategory"),
				Context,
				TEXT("MissingMakeMapValueCategory"),
				TEXT("Make map nodes require metadata ValuePinCategory."));
			return bIsValid;
		}

		switch (Node.Kind)
		{
		case EVergilNodeKind::Event:
			AddNodeDiagnostic(
				TEXT("EventNodeDescriptorInvalid"),
				FString::Printf(TEXT("Event nodes must use descriptor K2.Event.<FunctionName>; found '%s'."), *DescriptorString));
			break;

		case EVergilNodeKind::Call:
			AddNodeDiagnostic(
				TEXT("CallNodeDescriptorInvalid"),
				FString::Printf(TEXT("Call nodes must use descriptor K2.Call.<FunctionName>; found '%s'."), *DescriptorString));
			break;

		case EVergilNodeKind::VariableGet:
			AddNodeDiagnostic(
				TEXT("VariableGetDescriptorInvalid"),
				FString::Printf(TEXT("Variable get nodes must use descriptor K2.VarGet.<VariableName>; found '%s'."), *DescriptorString));
			break;

		case EVergilNodeKind::VariableSet:
			AddNodeDiagnostic(
				TEXT("VariableSetDescriptorInvalid"),
				FString::Printf(TEXT("Variable set nodes must use descriptor K2.VarSet.<VariableName>; found '%s'."), *DescriptorString));
			break;

		default:
			break;
		}

		return bIsValid;
	}

	FString GetVariableContainerTypeString(const EVergilVariableContainerType ContainerType)
	{
		switch (ContainerType)
		{
		case EVergilVariableContainerType::Array:
			return TEXT("Array");

		case EVergilVariableContainerType::Set:
			return TEXT("Set");

		case EVergilVariableContainerType::Map:
			return TEXT("Map");

		case EVergilVariableContainerType::None:
		default:
			return TEXT("None");
		}
	}

	void AddVariableTypeAttributes(TMap<FName, FString>& Attributes, const FVergilVariableTypeReference& Type)
	{
		Attributes.Add(TEXT("PinCategory"), Type.PinCategory.ToString());
		Attributes.Add(TEXT("ContainerType"), GetVariableContainerTypeString(Type.ContainerType));

		if (!Type.PinSubCategory.IsNone())
		{
			Attributes.Add(TEXT("PinSubCategory"), Type.PinSubCategory.ToString());
		}

		if (!Type.ObjectPath.IsEmpty())
		{
			Attributes.Add(TEXT("ObjectPath"), Type.ObjectPath);
		}

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			Attributes.Add(TEXT("ValuePinCategory"), Type.ValuePinCategory.ToString());

			if (!Type.ValuePinSubCategory.IsNone())
			{
				Attributes.Add(TEXT("ValuePinSubCategory"), Type.ValuePinSubCategory.ToString());
			}

			if (!Type.ValueObjectPath.IsEmpty())
			{
				Attributes.Add(TEXT("ValueObjectPath"), Type.ValueObjectPath);
			}
		}
	}

	void AddVariableFlagAttributes(TMap<FName, FString>& Attributes, const FVergilVariableFlags& Flags)
	{
		Attributes.Add(TEXT("bInstanceEditable"), Flags.bInstanceEditable ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bBlueprintReadOnly"), Flags.bBlueprintReadOnly ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bExposeOnSpawn"), Flags.bExposeOnSpawn ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bPrivate"), Flags.bPrivate ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bTransient"), Flags.bTransient ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bSaveGame"), Flags.bSaveGame ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bAdvancedDisplay"), Flags.bAdvancedDisplay ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bDeprecated"), Flags.bDeprecated ? TEXT("true") : TEXT("false"));
		Attributes.Add(TEXT("bExposeToCinematics"), Flags.bExposeToCinematics ? TEXT("true") : TEXT("false"));
	}

	FName MakeSignatureAttributeKey(const TCHAR* Prefix, const int32 Index, const TCHAR* Suffix)
	{
		return *FString::Printf(TEXT("%s_%d_%s"), Prefix, Index, Suffix);
	}

	void AddFunctionParameterAttributes(
		TMap<FName, FString>& Attributes,
		const TCHAR* Prefix,
		const TArray<FVergilFunctionParameterDefinition>& Parameters)
	{
		Attributes.Add(*FString::Printf(TEXT("%sCount"), Prefix), FString::FromInt(Parameters.Num()));
		for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
		{
			const FVergilFunctionParameterDefinition& Parameter = Parameters[ParameterIndex];
			Attributes.Add(MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("Name")), Parameter.Name.ToString());

			TMap<FName, FString> TypeAttributes;
			AddVariableTypeAttributes(TypeAttributes, Parameter.Type);
			for (const TPair<FName, FString>& TypeAttribute : TypeAttributes)
			{
				Attributes.Add(
					MakeSignatureAttributeKey(Prefix, ParameterIndex, *TypeAttribute.Key.ToString()),
					TypeAttribute.Value);
			}
		}
	}

	void AddFunctionDefinitionAttributes(TMap<FName, FString>& Attributes, const FVergilFunctionDefinition& Function)
	{
		Attributes.Add(TEXT("bPure"), Function.bPure ? TEXT("true") : TEXT("false"));

		FString AccessSpecifier = TEXT("Public");
		switch (Function.AccessSpecifier)
		{
		case EVergilFunctionAccessSpecifier::Protected:
			AccessSpecifier = TEXT("Protected");
			break;

		case EVergilFunctionAccessSpecifier::Private:
			AccessSpecifier = TEXT("Private");
			break;

		case EVergilFunctionAccessSpecifier::Public:
		default:
			break;
		}

		Attributes.Add(TEXT("AccessSpecifier"), AccessSpecifier);
		AddFunctionParameterAttributes(Attributes, TEXT("Input"), Function.Inputs);
		AddFunctionParameterAttributes(Attributes, TEXT("Output"), Function.Outputs);
	}

	void AddMacroParameterAttributes(
		TMap<FName, FString>& Attributes,
		const TCHAR* Prefix,
		const TArray<FVergilMacroParameterDefinition>& Parameters)
	{
		Attributes.Add(*FString::Printf(TEXT("%sCount"), Prefix), FString::FromInt(Parameters.Num()));
		for (int32 ParameterIndex = 0; ParameterIndex < Parameters.Num(); ++ParameterIndex)
		{
			const FVergilMacroParameterDefinition& Parameter = Parameters[ParameterIndex];
			Attributes.Add(MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("Name")), Parameter.Name.ToString());
			Attributes.Add(MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("bExec")), Parameter.bIsExec ? TEXT("true") : TEXT("false"));

			if (Parameter.bIsExec)
			{
				continue;
			}

			TMap<FName, FString> TypeAttributes;
			AddVariableTypeAttributes(TypeAttributes, Parameter.Type);
			for (const TPair<FName, FString>& TypeAttribute : TypeAttributes)
			{
				Attributes.Add(
					MakeSignatureAttributeKey(Prefix, ParameterIndex, *TypeAttribute.Key.ToString()),
					TypeAttribute.Value);
			}
		}
	}

	void AddMacroDefinitionAttributes(TMap<FName, FString>& Attributes, const FVergilMacroDefinition& Macro)
	{
		AddMacroParameterAttributes(Attributes, TEXT("Input"), Macro.Inputs);
		AddMacroParameterAttributes(Attributes, TEXT("Output"), Macro.Outputs);
	}

	void AddComponentTransformCommands(TArray<FVergilCompilerCommand>& Commands, const FVergilComponentDefinition& Component)
	{
		auto AddTransformCommand = [&Commands, &Component](const FName PropertyName, const FString& Value)
		{
			FVergilCompilerCommand PropertyCommand;
			PropertyCommand.Type = EVergilCommandType::SetComponentProperty;
			PropertyCommand.SecondaryName = Component.Name;
			PropertyCommand.Name = PropertyName;
			PropertyCommand.StringValue = Value;
			Commands.Add(PropertyCommand);
		};

		if (Component.RelativeTransform.bHasRelativeLocation)
		{
			AddTransformCommand(TEXT("RelativeLocation"), Component.RelativeTransform.RelativeLocation.ToString());
		}

		if (Component.RelativeTransform.bHasRelativeRotation)
		{
			AddTransformCommand(TEXT("RelativeRotation"), Component.RelativeTransform.RelativeRotation.ToString());
		}

		if (Component.RelativeTransform.bHasRelativeScale)
		{
			AddTransformCommand(TEXT("RelativeScale3D"), Component.RelativeTransform.RelativeScale3D.ToString());
		}
	}

	void AddComponentTemplatePropertyCommands(TArray<FVergilCompilerCommand>& Commands, const FVergilComponentDefinition& Component)
	{
		TArray<FName> PropertyNames;
		Component.TemplateProperties.GetKeys(PropertyNames);
		PropertyNames.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName PropertyName : PropertyNames)
		{
			if (PropertyName.IsNone())
			{
				continue;
			}

			FVergilCompilerCommand PropertyCommand;
			PropertyCommand.Type = EVergilCommandType::SetComponentProperty;
			PropertyCommand.SecondaryName = Component.Name;
			PropertyCommand.Name = PropertyName;
			PropertyCommand.StringValue = Component.TemplateProperties.FindRef(PropertyName);
			Commands.Add(PropertyCommand);
		}
	}

	void AddClassDefaultCommands(TArray<FVergilCompilerCommand>& Commands, const TMap<FName, FString>& ClassDefaults)
	{
		TArray<FName> PropertyNames;
		ClassDefaults.GetKeys(PropertyNames);
		PropertyNames.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName PropertyName : PropertyNames)
		{
			if (PropertyName.IsNone())
			{
				continue;
			}

			FVergilCompilerCommand ClassDefaultCommand;
			ClassDefaultCommand.Type = EVergilCommandType::SetClassDefault;
			ClassDefaultCommand.Name = PropertyName;
			ClassDefaultCommand.StringValue = ClassDefaults.FindRef(PropertyName);
			Commands.Add(ClassDefaultCommand);
		}
	}

	void AddBlueprintMetadataCommands(TArray<FVergilCompilerCommand>& Commands, const TMap<FName, FString>& Metadata)
	{
		TArray<FName> MetadataKeys;
		Metadata.GetKeys(MetadataKeys);
		MetadataKeys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName MetadataKey : MetadataKeys)
		{
			if (MetadataKey.IsNone())
			{
				continue;
			}

			FVergilCompilerCommand MetadataCommand;
			MetadataCommand.Type = EVergilCommandType::SetBlueprintMetadata;
			MetadataCommand.Name = MetadataKey;
			MetadataCommand.StringValue = Metadata.FindRef(MetadataKey);
			Commands.Add(MetadataCommand);
		}
	}

	const TArray<FVergilGraphNode>& GetTargetGraphNodes(const FVergilCompilerContext& Context)
	{
		const FVergilGraphDocument& Document = Context.GetDocument();
		return Context.GetGraphName() == ConstructionScriptGraphName
			? Document.ConstructionScriptNodes
			: Document.Nodes;
	}

	const TArray<FVergilGraphEdge>& GetTargetGraphEdges(const FVergilCompilerContext& Context)
	{
		const FVergilGraphDocument& Document = Context.GetDocument();
		return Context.GetGraphName() == ConstructionScriptGraphName
			? Document.ConstructionScriptEdges
			: Document.Edges;
	}

	class FVergilCommentNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.Comment");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Kind == EVergilNodeKind::Comment;
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand AddNodeCommand;
			AddNodeCommand.Type = EVergilCommandType::AddNode;
			AddNodeCommand.GraphName = Context.GetGraphName();
			AddNodeCommand.NodeId = Node.Id;
			AddNodeCommand.Name = GetDescriptor();
			AddNodeCommand.SecondaryName = Node.Descriptor;
			AddNodeCommand.Position = Node.Position;
			AddNodeCommand.StringValue = Node.Metadata.FindRef(TEXT("CommentText"));
			if (AddNodeCommand.StringValue.IsEmpty())
			{
				AddNodeCommand.StringValue = Node.Metadata.FindRef(TEXT("Title"));
			}
			CopyPlannedPins(Node, AddNodeCommand);
			Context.AddCommand(AddNodeCommand);

			for (const TPair<FName, FString>& MetadataEntry : Node.Metadata)
			{
				FVergilCompilerCommand MetadataCommand;
				MetadataCommand.Type = EVergilCommandType::SetNodeMetadata;
				MetadataCommand.GraphName = Context.GetGraphName();
				MetadataCommand.NodeId = Node.Id;
				MetadataCommand.Name = MetadataEntry.Key;
				MetadataCommand.StringValue = MetadataEntry.Value;
				Context.AddCommand(MetadataCommand);
			}

			return true;
		}
	};

	class FVergilEventNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Event");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Kind == EVergilNodeKind::Event && Node.Descriptor.ToString().StartsWith(TEXT("K2.Event."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString EventName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Event."));
			if (EventName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingEventName"), TEXT("Event nodes require a descriptor suffix naming the bound function."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *EventName;
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilCustomEventNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.CustomEvent");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.CustomEvent."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString EventName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CustomEvent."));
			if (EventName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingCustomEventName"), TEXT("Custom event nodes require a descriptor suffix naming the event."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *EventName;
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilBranchNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Branch");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Branch");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSequenceNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Sequence");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Sequence");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			int32 NumOutputs = 0;
			for (const FVergilGraphPin& Pin : Node.Pins)
			{
				if (!Pin.bIsExec || Pin.Direction != EVergilPinDirection::Output)
				{
					continue;
				}

				if (Pin.Name.ToString().StartsWith(TEXT("Then_")))
				{
					++NumOutputs;
				}
			}

			if (NumOutputs < 2)
			{
				NumOutputs = 2;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.StringValue = FString::FromInt(NumOutputs);
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilForLoopNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.ForLoop");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.ForLoop");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			if (!Command.Attributes.Contains(TEXT("MacroBlueprintPath")))
			{
				Command.Attributes.Add(TEXT("MacroBlueprintPath"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
			}
			if (!Command.Attributes.Contains(TEXT("MacroGraphName")))
			{
				Command.Attributes.Add(TEXT("MacroGraphName"), TEXT("ForLoop"));
			}
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilDelayNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Delay");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Delay");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilBindDelegateNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.BindDelegate");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.BindDelegate."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.BindDelegate."));
			if (PropertyName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingBindDelegateMetadata"), TEXT("Bind delegate nodes require a descriptor suffix naming the property."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *PropertyName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilRemoveDelegateNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.RemoveDelegate");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.RemoveDelegate."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.RemoveDelegate."));
			if (PropertyName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingRemoveDelegateMetadata"), TEXT("Remove delegate nodes require a descriptor suffix naming the property."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *PropertyName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilClearDelegateNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.ClearDelegate");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.ClearDelegate."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.ClearDelegate."));
			if (PropertyName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingClearDelegateMetadata"), TEXT("Clear delegate nodes require a descriptor suffix naming the property."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *PropertyName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilCallDelegateNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.CallDelegate");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.CallDelegate."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString PropertyName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CallDelegate."));
			if (PropertyName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingCallDelegateMetadata"), TEXT("Call delegate nodes require a descriptor suffix naming the property."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *PropertyName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilCreateDelegateNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.CreateDelegate");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor.ToString().StartsWith(TEXT("K2.CreateDelegate."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CreateDelegate."));
			if (FunctionName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingCreateDelegateFunction"), TEXT("CreateDelegate nodes require a descriptor suffix naming the selected function."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *FunctionName;
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilCallNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Call");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Kind == EVergilNodeKind::Call && Node.Descriptor.ToString().StartsWith(TEXT("K2.Call."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Call."));
			if (FunctionName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingFunctionName"), TEXT("Call nodes require a descriptor suffix naming the function."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *FunctionName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilVariableGetNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.VariableGet");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Kind == EVergilNodeKind::VariableGet && Node.Descriptor.ToString().StartsWith(TEXT("K2.VarGet."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString VariableName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarGet."));
			if (VariableName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingVariableName"), TEXT("Variable get nodes require a descriptor suffix naming the variable."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *VariableName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilVariableSetNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.VariableSet");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Kind == EVergilNodeKind::VariableSet && Node.Descriptor.ToString().StartsWith(TEXT("K2.VarSet."));
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString VariableName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarSet."));
			if (VariableName.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingVariableName"), TEXT("Variable set nodes require a descriptor suffix naming the variable."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.SecondaryName = *VariableName;
			Command.StringValue = Node.Metadata.FindRef(TEXT("OwnerClassPath"));
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSelfNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Self");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Self");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilCastNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Cast");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Cast");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString TargetClassPath = Node.Metadata.FindRef(TEXT("TargetClassPath"));
			if (TargetClassPath.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingTargetClassPath"), TEXT("Cast nodes require metadata TargetClassPath naming the target class."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.StringValue = TargetClassPath;
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilRerouteNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Reroute");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Reroute");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSelectNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.Select");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.Select");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString IndexCategory = Node.Metadata.FindRef(TEXT("IndexPinCategory"));
			const FString ValueCategory = Node.Metadata.FindRef(TEXT("ValuePinCategory"));
			if (IndexCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingSelectIndexCategory"), TEXT("Select nodes require metadata IndexPinCategory."), Node.Id);
				return false;
			}

			if (ValueCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingSelectValueCategory"), TEXT("Select nodes require metadata ValuePinCategory."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSwitchIntNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.SwitchInt");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.SwitchInt");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSwitchStringNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.SwitchString");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.SwitchString");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilSwitchEnumNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.SwitchEnum");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.SwitchEnum");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString EnumPath = Node.Metadata.FindRef(TEXT("EnumPath"));
			if (EnumPath.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingSwitchEnumPath"), TEXT("Switch enum nodes require metadata EnumPath."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilFormatTextNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.FormatText");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.FormatText");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString FormatPattern = Node.Metadata.FindRef(TEXT("FormatPattern"));
			if (FormatPattern.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingFormatPattern"), TEXT("Format text nodes require metadata FormatPattern."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilMakeStructNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.MakeStruct");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.MakeStruct");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString StructPath = Node.Metadata.FindRef(TEXT("StructPath"));
			if (StructPath.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingMakeStructPath"), TEXT("Make struct nodes require metadata StructPath."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilBreakStructNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.BreakStruct");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.BreakStruct");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString StructPath = Node.Metadata.FindRef(TEXT("StructPath"));
			if (StructPath.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingBreakStructPath"), TEXT("Break struct nodes require metadata StructPath."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilMakeArrayNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.MakeArray");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.MakeArray");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString ValueCategory = Node.Metadata.FindRef(TEXT("ValuePinCategory"));
			if (ValueCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingMakeArrayValueCategory"), TEXT("Make array nodes require metadata ValuePinCategory."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilMakeSetNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.MakeSet");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.MakeSet");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString ValueCategory = Node.Metadata.FindRef(TEXT("ValuePinCategory"));
			if (ValueCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingMakeSetValueCategory"), TEXT("Make set nodes require metadata ValuePinCategory."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilMakeMapNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.MakeMap");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.MakeMap");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			const FString KeyCategory = Node.Metadata.FindRef(TEXT("KeyPinCategory"));
			if (KeyCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingMakeMapKeyCategory"), TEXT("Make map nodes require metadata KeyPinCategory."), Node.Id);
				return false;
			}

			const FString ValueCategory = Node.Metadata.FindRef(TEXT("ValuePinCategory"));
			if (ValueCategory.IsEmpty())
			{
				Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, TEXT("MissingMakeMapValueCategory"), TEXT("Make map nodes require metadata ValuePinCategory."), Node.Id);
				return false;
			}

			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.Position = Node.Position;
			Command.Attributes = Node.Metadata;
			CopyPlannedPins(Node, Command);
			Context.AddCommand(Command);
			return true;
		}
	};

	class FVergilGenericNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.Generic");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return !Node.Descriptor.IsNone();
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand AddNodeCommand;
			AddNodeCommand.Type = EVergilCommandType::AddNode;
			AddNodeCommand.GraphName = Context.GetGraphName();
			AddNodeCommand.NodeId = Node.Id;
			AddNodeCommand.Name = Node.Descriptor;
			AddNodeCommand.SecondaryName = StaticEnum<EVergilNodeKind>()->GetNameByValue(static_cast<int64>(Node.Kind));
			AddNodeCommand.Position = Node.Position;
			CopyPlannedPins(Node, AddNodeCommand);
			Context.AddCommand(AddNodeCommand);

			for (const TPair<FName, FString>& MetadataEntry : Node.Metadata)
			{
				FVergilCompilerCommand MetadataCommand;
				MetadataCommand.Type = EVergilCommandType::SetNodeMetadata;
				MetadataCommand.GraphName = Context.GetGraphName();
				MetadataCommand.NodeId = Node.Id;
				MetadataCommand.Name = MetadataEntry.Key;
				MetadataCommand.StringValue = MetadataEntry.Value;
				Context.AddCommand(MetadataCommand);
			}

			return true;
		}
	};

	void EnsureGenericFallbackHandler()
	{
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCommentNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilEventNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCustomEventNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilBranchNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSequenceNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilForLoopNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilDelayNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilBindDelegateNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilRemoveDelegateNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilClearDelegateNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCallDelegateNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCreateDelegateNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCallNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilVariableGetNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilVariableSetNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSelfNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilCastNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilRerouteNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSelectNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSwitchIntNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSwitchStringNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSwitchEnumNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilFormatTextNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilMakeStructNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilBreakStructNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilMakeArrayNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilMakeSetNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilMakeMapNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilGenericNodeHandler, ESPMode::ThreadSafe>());
	}
}

FName FVergilSchemaMigrationPass::GetPassName() const
{
	return TEXT("SchemaMigration");
}

bool FVergilSchemaMigrationPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	const FVergilGraphDocument& SourceDocument = Context.GetDocument();
	if (SourceDocument.SchemaVersion >= Vergil::SchemaVersion)
	{
		return true;
	}

	FVergilGraphDocument MigratedDocument;
	if (!Vergil::MigrateDocumentToCurrentSchema(SourceDocument, MigratedDocument, &Result.Diagnostics))
	{
		return false;
	}

	Context.SetWorkingDocument(MoveTemp(MigratedDocument));
	return !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilStructuralValidationPass::GetPassName() const
{
	return TEXT("StructuralValidation");
}

bool FVergilStructuralValidationPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	Context.GetDocument().IsStructurallyValid(&Result.Diagnostics);
	return !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilSemanticValidationPass::GetPassName() const
{
	return TEXT("SemanticValidation");
}

bool FVergilSemanticValidationPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	EnsureGenericFallbackHandler();

	bool bIsValid = true;
	if (!IsSupportedCompileTargetGraph(Context.GetGraphName()))
	{
		AddSemanticValidationDiagnostic(
			Context,
			TEXT("UnsupportedCompileTargetGraph"),
			FString::Printf(
				TEXT("Compile target graph '%s' is not supported by the current scaffold. Use EventGraph or UserConstructionScript."),
				*Context.GetGraphName().ToString()),
			FGuid());
		bIsValid = false;
	}

	for (const FVergilGraphNode& Node : GetTargetGraphNodes(Context))
	{
		bIsValid &= ValidateNodeSemanticRequirements(Node, Context);
	}

	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilCommandPlanningPass::GetPassName() const
{
	return TEXT("CommandPlanning");
}

bool FVergilCommandPlanningPass::Run(const FVergilCompileRequest& Request, FVergilCompilerContext& Context, FVergilCompileResult& Result) const
{
	EnsureGenericFallbackHandler();

	const FVergilGraphDocument& Document = Context.GetDocument();

	AddBlueprintMetadataCommands(Result.Commands, Document.Metadata);

	for (const FVergilVariableDefinition& Variable : Document.Variables)
	{
		FVergilCompilerCommand EnsureVariableCommand;
		EnsureVariableCommand.Type = EVergilCommandType::EnsureVariable;
		EnsureVariableCommand.SecondaryName = Variable.Name;
		EnsureVariableCommand.StringValue = Variable.DefaultValue;
		AddVariableTypeAttributes(EnsureVariableCommand.Attributes, Variable.Type);
		AddVariableFlagAttributes(EnsureVariableCommand.Attributes, Variable.Flags);
		EnsureVariableCommand.Attributes.Add(TEXT("Category"), Variable.Category);
		Result.Commands.Add(EnsureVariableCommand);

		TArray<FName> MetadataKeys;
		Variable.Metadata.GetKeys(MetadataKeys);
		MetadataKeys.Sort([](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		});

		for (const FName MetadataKey : MetadataKeys)
		{
			FVergilCompilerCommand MetadataCommand;
			MetadataCommand.Type = EVergilCommandType::SetVariableMetadata;
			MetadataCommand.SecondaryName = Variable.Name;
			MetadataCommand.Name = MetadataKey;
			MetadataCommand.StringValue = Variable.Metadata.FindRef(MetadataKey);
			Result.Commands.Add(MetadataCommand);
		}

		FVergilCompilerCommand DefaultCommand;
		DefaultCommand.Type = EVergilCommandType::SetVariableDefault;
		DefaultCommand.SecondaryName = Variable.Name;
		DefaultCommand.StringValue = Variable.DefaultValue;
		Result.Commands.Add(DefaultCommand);
	}

	for (const FVergilFunctionDefinition& Function : Document.Functions)
	{
		FVergilCompilerCommand EnsureFunctionGraphCommand;
		EnsureFunctionGraphCommand.Type = EVergilCommandType::EnsureFunctionGraph;
		EnsureFunctionGraphCommand.GraphName = Function.Name;
		EnsureFunctionGraphCommand.SecondaryName = Function.Name;
		AddFunctionDefinitionAttributes(EnsureFunctionGraphCommand.Attributes, Function);
		Result.Commands.Add(EnsureFunctionGraphCommand);
	}

	for (const FVergilMacroDefinition& Macro : Document.Macros)
	{
		FVergilCompilerCommand EnsureMacroGraphCommand;
		EnsureMacroGraphCommand.Type = EVergilCommandType::EnsureMacroGraph;
		EnsureMacroGraphCommand.GraphName = Macro.Name;
		EnsureMacroGraphCommand.SecondaryName = Macro.Name;
		AddMacroDefinitionAttributes(EnsureMacroGraphCommand.Attributes, Macro);
		Result.Commands.Add(EnsureMacroGraphCommand);
	}

	for (const FVergilDispatcherDefinition& Dispatcher : Document.Dispatchers)
	{
		FVergilCompilerCommand EnsureDispatcherCommand;
		EnsureDispatcherCommand.Type = EVergilCommandType::EnsureDispatcher;
		EnsureDispatcherCommand.SecondaryName = Dispatcher.Name;
		Result.Commands.Add(EnsureDispatcherCommand);

		for (const FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
		{
			FVergilCompilerCommand ParameterCommand;
			ParameterCommand.Type = EVergilCommandType::AddDispatcherParameter;
			ParameterCommand.SecondaryName = Dispatcher.Name;
			ParameterCommand.Name = Parameter.Name;
			ParameterCommand.Attributes.Add(TEXT("PinCategory"), Parameter.PinCategory.ToString());
			if (!Parameter.PinSubCategory.IsNone())
			{
				ParameterCommand.Attributes.Add(TEXT("PinSubCategory"), Parameter.PinSubCategory.ToString());
			}
			if (!Parameter.ObjectPath.IsEmpty())
			{
				ParameterCommand.Attributes.Add(TEXT("ObjectPath"), Parameter.ObjectPath);
			}
			if (Parameter.bIsArray)
			{
				ParameterCommand.Attributes.Add(TEXT("bIsArray"), TEXT("true"));
			}
			Result.Commands.Add(ParameterCommand);
		}
	}

	for (const FVergilComponentDefinition& Component : Document.Components)
	{
		FVergilCompilerCommand EnsureComponentCommand;
		EnsureComponentCommand.Type = EVergilCommandType::EnsureComponent;
		EnsureComponentCommand.SecondaryName = Component.Name;
		EnsureComponentCommand.StringValue = Component.ComponentClassPath;
		Result.Commands.Add(EnsureComponentCommand);
	}

	for (const FVergilComponentDefinition& Component : Document.Components)
	{
		if (!Component.ParentComponentName.IsNone())
		{
			FVergilCompilerCommand AttachComponentCommand;
			AttachComponentCommand.Type = EVergilCommandType::AttachComponent;
			AttachComponentCommand.SecondaryName = Component.Name;
			AttachComponentCommand.Name = Component.ParentComponentName;
			AttachComponentCommand.StringValue = Component.AttachSocketName.ToString();
			Result.Commands.Add(AttachComponentCommand);
		}

		AddComponentTemplatePropertyCommands(Result.Commands, Component);
		AddComponentTransformCommands(Result.Commands, Component);
	}

	for (const FVergilInterfaceDefinition& Interface : Document.Interfaces)
	{
		FVergilCompilerCommand EnsureInterfaceCommand;
		EnsureInterfaceCommand.Type = EVergilCommandType::EnsureInterface;
		EnsureInterfaceCommand.StringValue = Interface.InterfaceClassPath.TrimStartAndEnd();
		Result.Commands.Add(EnsureInterfaceCommand);
	}

	AddClassDefaultCommands(Result.Commands, Document.ClassDefaults);

	FVergilCompilerCommand EnsureGraphCommand;
	EnsureGraphCommand.Type = EVergilCommandType::EnsureGraph;
	EnsureGraphCommand.GraphName = Context.GetGraphName();
	EnsureGraphCommand.Name = Context.GetGraphName();
	Result.Commands.Add(EnsureGraphCommand);

	const TArray<FVergilGraphNode>& TargetNodes = GetTargetGraphNodes(Context);
	const TArray<FVergilGraphEdge>& TargetEdges = GetTargetGraphEdges(Context);

	for (const FVergilGraphNode& Node : TargetNodes)
	{
		const TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe> Handler = FVergilNodeRegistry::Get().FindHandler(Node);
		if (!Handler.IsValid())
		{
			Result.Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("UnhandledNodeDescriptor"),
				FString::Printf(TEXT("No registered handler exists for descriptor '%s'."), *Node.Descriptor.ToString()),
				Node.Id));
			continue;
		}

		if (!Handler->BuildCommands(Node, Context))
		{
			Result.Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("CommandPlanningFailed"),
				FString::Printf(TEXT("Handler '%s' failed while planning node '%s'."), *Handler->GetDescriptor().ToString(), *Node.Descriptor.ToString()),
				Node.Id));
		}
	}

	for (const FVergilGraphEdge& Edge : TargetEdges)
	{
		FVergilCompilerCommand ConnectCommand;
		ConnectCommand.Type = EVergilCommandType::ConnectPins;
		ConnectCommand.GraphName = Context.GetGraphName();
		ConnectCommand.SourceNodeId = Edge.SourceNodeId;
		ConnectCommand.SourcePinId = Edge.SourcePinId;
		ConnectCommand.TargetNodeId = Edge.TargetNodeId;
		ConnectCommand.TargetPinId = Edge.TargetPinId;
		Result.Commands.Add(ConnectCommand);
	}

	for (const FVergilGraphNode& Node : TargetNodes)
	{
		if (!Node.Descriptor.ToString().StartsWith(TEXT("K2.CreateDelegate.")))
		{
			continue;
		}

		const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CreateDelegate."));
		if (FunctionName.IsEmpty())
		{
			Result.Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingCreateDelegateFinalizeFunction"),
				TEXT("CreateDelegate finalization requires a descriptor suffix naming the selected function."),
				Node.Id));
			continue;
		}

		FVergilCompilerCommand FinalizeCommand;
		FinalizeCommand.Type = EVergilCommandType::FinalizeNode;
		FinalizeCommand.GraphName = Context.GetGraphName();
		FinalizeCommand.NodeId = Node.Id;
		FinalizeCommand.Name = TEXT("Vergil.K2.CreateDelegate");
		FinalizeCommand.SecondaryName = *FunctionName;
		FinalizeCommand.Attributes = Node.Metadata;
		Result.Commands.Add(FinalizeCommand);
	}

	return !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}
