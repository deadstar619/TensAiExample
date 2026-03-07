#include "VergilCompilerPasses.h"

#include "Algo/AnyOf.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#include "VergilCompilerTypes.h"
#include "VergilDiagnostic.h"
#include "VergilNodeRegistry.h"

namespace
{
	const FName EventGraphName(TEXT("EventGraph"));
	const FName ConstructionScriptGraphName(TEXT("UserConstructionScript"));
	const FString StandardMacrosBlueprintPath(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	const FName SpawnActorClassPinName(TEXT("Class"));
	const FName SpawnActorWorldContextPinName(TEXT("WorldContextObject"));
	const FName SpawnActorTransformPinName(TEXT("SpawnTransform"));
	const FName SpawnActorCollisionHandlingOverridePinName(TEXT("CollisionHandlingOverride"));
	const FName SpawnActorTransformScaleMethodPinName(TEXT("TransformScaleMethod"));
	const FName SpawnActorOwnerPinName(TEXT("Owner"));

	struct FVergilSpawnActorPinDescriptor
	{
		FName Name = NAME_None;
		EVergilPinDirection Direction = EVergilPinDirection::Input;
		bool bIsExec = false;
	};

	struct FVergilStandardMacroDescriptor
	{
		FName Descriptor;
		FName CommandName;
		FName DefaultMacroGraphName;
		FName NotFoundDiagnosticCode;
	};

	const FVergilStandardMacroDescriptor* FindStandardMacroDescriptorByDescriptor(const FName Descriptor)
	{
		static const FVergilStandardMacroDescriptor Descriptors[] =
		{
			{ TEXT("K2.ForLoop"), TEXT("Vergil.K2.ForLoop"), TEXT("ForLoop"), TEXT("ForLoopMacroNotFound") },
			{ TEXT("K2.DoOnce"), TEXT("Vergil.K2.DoOnce"), TEXT("DoOnce"), TEXT("DoOnceMacroNotFound") },
			{ TEXT("K2.FlipFlop"), TEXT("Vergil.K2.FlipFlop"), TEXT("FlipFlop"), TEXT("FlipFlopMacroNotFound") },
		};

		for (const FVergilStandardMacroDescriptor& Candidate : Descriptors)
		{
			if (Candidate.Descriptor == Descriptor)
			{
				return &Candidate;
			}
		}

		return nullptr;
	}

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

	bool IsSupportedSelectIndexPinCategory(const FString& CategoryValue)
	{
		const FString Category = CategoryValue.TrimStartAndEnd().ToLower();
		return Category == TEXT("bool")
			|| Category == TEXT("int")
			|| Category == TEXT("enum");
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

		if (Node.Descriptor == TEXT("K2.SpawnActor"))
		{
			bIsValid &= HasRequiredMetadataValue(
				Node,
				TEXT("ActorClassPath"),
				Context,
				TEXT("MissingSpawnActorClassPath"),
				TEXT("Spawn actor nodes require metadata ActorClassPath naming the actor class to spawn."));

			if (Context.GetGraphName() == ConstructionScriptGraphName)
			{
				AddNodeDiagnostic(
					TEXT("ConstructionScriptSpawnActorUnsupported"),
					TEXT("K2.SpawnActor is not supported when compiling UserConstructionScript because UE_5.7 SpawnActor nodes are incompatible with construction scripts."));
			}

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

			const FString IndexPinCategory = Node.Metadata.FindRef(TEXT("IndexPinCategory")).TrimStartAndEnd().ToLower();
			if (!IndexPinCategory.IsEmpty() && !IsSupportedSelectIndexPinCategory(IndexPinCategory))
			{
				AddNodeDiagnostic(
					TEXT("UnsupportedSelectIndexTypeCombination"),
					FString::Printf(
						TEXT("Select nodes currently support IndexPinCategory values bool, int, or enum in UE 5.7; found '%s'."),
						*IndexPinCategory));
			}
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

	void AddSymbolResolutionDiagnostic(
		FVergilCompilerContext& Context,
		const FName Code,
		const FString& Message,
		const FGuid& SourceId)
	{
		Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, Code, Message, SourceId);
	}

	UObject* ResolveObjectReference(const FString& Reference)
	{
		const FString TrimmedReference = Reference.TrimStartAndEnd();
		if (TrimmedReference.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* DirectObject = FindObject<UObject>(nullptr, *TrimmedReference))
		{
			return DirectObject;
		}

		return StaticLoadObject(UObject::StaticClass(), nullptr, *TrimmedReference, nullptr, LOAD_NoWarn);
	}

	UClass* ResolveClassReference(const FString& Reference)
	{
		const FString TrimmedReference = Reference.TrimStartAndEnd();
		if (TrimmedReference.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* DirectClass = FindObject<UClass>(nullptr, *TrimmedReference))
		{
			return DirectClass;
		}

		if (UClass* LoadedClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *TrimmedReference, nullptr, LOAD_NoWarn)))
		{
			return LoadedClass;
		}

		return LoadClass<UObject>(nullptr, *TrimmedReference, nullptr, LOAD_NoWarn);
	}

	UEdGraph* ResolveMacroGraphReference(const FString& BlueprintPath, const FName GraphName)
	{
		const FString TrimmedBlueprintPath = BlueprintPath.TrimStartAndEnd();
		if (TrimmedBlueprintPath.IsEmpty() || GraphName.IsNone())
		{
			return nullptr;
		}

		UBlueprint* const MacroBlueprint = Cast<UBlueprint>(ResolveObjectReference(TrimmedBlueprintPath));
		if (MacroBlueprint == nullptr)
		{
			return nullptr;
		}

		TArray<UEdGraph*> AllGraphs;
		MacroBlueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* CandidateGraph : AllGraphs)
		{
			if (CandidateGraph != nullptr && CandidateGraph->GetFName() == GraphName)
			{
				return CandidateGraph;
			}
		}

		return nullptr;
	}

	struct FVergilDocumentSymbolTables
	{
		TSet<FName> VariableNames;
		TMap<FName, FVergilVariableTypeReference> VariableTypes;
		TSet<FName> FunctionNames;
		TSet<FName> DispatcherNames;
		TMap<FName, int32> CustomEventCounts;
	};

	enum class EVergilVariableGetVariant : uint8
	{
		PureOnly,
		BooleanBranch,
		ValidatedObject
	};

	bool SetNormalizedMetadataValue(TMap<FName, FString>& Metadata, const FName Key, const FString& Value)
	{
		const FString TrimmedValue = Value.TrimStartAndEnd();
		if (TrimmedValue.IsEmpty())
		{
			return Metadata.Remove(Key) > 0;
		}

		const FString* ExistingValue = Metadata.Find(Key);
		if (ExistingValue != nullptr && *ExistingValue == TrimmedValue)
		{
			return false;
		}

		Metadata.Add(Key, TrimmedValue);
		return true;
	}

	EVergilVariableGetVariant GetVariableGetVariantFromCategory(const FName PinCategory, const EVergilVariableContainerType ContainerType)
	{
		if (ContainerType != EVergilVariableContainerType::None)
		{
			return EVergilVariableGetVariant::PureOnly;
		}

		if (PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return EVergilVariableGetVariant::BooleanBranch;
		}

		if (PinCategory == UEdGraphSchema_K2::PC_Object
			|| PinCategory == UEdGraphSchema_K2::PC_Class
			|| PinCategory == UEdGraphSchema_K2::PC_SoftObject
			|| PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			return EVergilVariableGetVariant::ValidatedObject;
		}

		return EVergilVariableGetVariant::PureOnly;
	}

	EVergilVariableGetVariant GetVariableGetVariantForType(const FVergilVariableTypeReference& Type)
	{
		return GetVariableGetVariantFromCategory(Type.PinCategory, Type.ContainerType);
	}

	EVergilVariableGetVariant GetVariableGetVariantForProperty(const FProperty* Property)
	{
		if (Property == nullptr)
		{
			return EVergilVariableGetVariant::PureOnly;
		}

		FEdGraphPinType PinType;
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType);
		const EVergilVariableContainerType ContainerType = PinType.ContainerType == EPinContainerType::Array
			? EVergilVariableContainerType::Array
			: PinType.ContainerType == EPinContainerType::Set
				? EVergilVariableContainerType::Set
				: PinType.ContainerType == EPinContainerType::Map
					? EVergilVariableContainerType::Map
					: EVergilVariableContainerType::None;
		return GetVariableGetVariantFromCategory(PinType.PinCategory, ContainerType);
	}

	bool HasVariableGetExecPins(const FVergilGraphNode& Node)
	{
		return Node.Pins.ContainsByPredicate([](const FVergilGraphPin& Pin)
		{
			return Pin.bIsExec;
		});
	}

	bool ValidateImpureVariableGetPinShape(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		if (!HasVariableGetExecPins(Node))
		{
			return true;
		}

		bool bHasExecuteInput = false;
		bool bHasThenOutput = false;
		bool bHasElseOutput = false;
		int32 ExecPinCount = 0;
		bool bShapeValid = true;

		for (const FVergilGraphPin& Pin : Node.Pins)
		{
			if (!Pin.bIsExec)
			{
				continue;
			}

			++ExecPinCount;
			if (Pin.Direction == EVergilPinDirection::Input && Pin.Name == UEdGraphSchema_K2::PN_Execute)
			{
				bHasExecuteInput = true;
				continue;
			}

			if (Pin.Direction == EVergilPinDirection::Output && Pin.Name == UEdGraphSchema_K2::PN_Then)
			{
				bHasThenOutput = true;
				continue;
			}

			if (Pin.Direction == EVergilPinDirection::Output && Pin.Name == UEdGraphSchema_K2::PN_Else)
			{
				bHasElseOutput = true;
				continue;
			}

			AddSymbolResolutionDiagnostic(
				Context,
				TEXT("InvalidVariableGetVariantPins"),
				FString::Printf(
					TEXT("Impure variable getter nodes under UE_5.7 only support exec pins Execute, Then, and Else; found '%s'."),
					*Pin.Name.ToString()),
				Node.Id);
			bShapeValid = false;
		}

		if (bHasExecuteInput && bHasThenOutput && bHasElseOutput && ExecPinCount == 3)
		{
			return bShapeValid;
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("InvalidVariableGetVariantPins"),
			TEXT("Impure variable getter nodes under UE_5.7 must expose exactly Execute input plus Then and Else exec outputs."),
			Node.Id);
		return false;
	}

	bool ValidateVariableGetVariant(
		const FVergilGraphNode& Node,
		FVergilCompilerContext& Context,
		const FVergilVariableTypeReference* Type,
		const FProperty* Property)
	{
		if (!Node.Descriptor.ToString().StartsWith(TEXT("K2.VarGet.")))
		{
			return true;
		}

		if (!ValidateImpureVariableGetPinShape(Node, Context))
		{
			return false;
		}

		if (!HasVariableGetExecPins(Node))
		{
			return true;
		}

		const EVergilVariableGetVariant SupportedVariant = Type != nullptr
			? GetVariableGetVariantForType(*Type)
			: GetVariableGetVariantForProperty(Property);
		if (SupportedVariant != EVergilVariableGetVariant::PureOnly)
		{
			return true;
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("UnsupportedVariableGetVariant"),
			TEXT("Impure variable getters under UE_5.7 are supported only for bool branch getters and object/class/soft reference validated getters."),
			Node.Id);
		return false;
	}

	bool SetNormalizedStringValue(FString& Value, const FString& NewValue)
	{
		const FString TrimmedValue = NewValue.TrimStartAndEnd();
		if (Value == TrimmedValue)
		{
			return false;
		}

		Value = TrimmedValue;
		return true;
	}

	bool SetNormalizedNameValue(FName& Value, const FString& NewValue)
	{
		const FString TrimmedValue = NewValue.TrimStartAndEnd();
		if (Value.ToString() == TrimmedValue)
		{
			return false;
		}

		Value = TrimmedValue.IsEmpty() ? NAME_None : FName(*TrimmedValue);
		return true;
	}

	void AddTypeResolutionDiagnostic(
		FVergilCompilerContext& Context,
		const FName Code,
		const FString& Message,
		const FGuid& SourceId = FGuid())
	{
		Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, Code, Message, SourceId);
	}

	bool IsSupportedResolvedTypeCategory(const FString& CategoryValue)
	{
		const FString Category = CategoryValue.TrimStartAndEnd().ToLower();
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

	bool TypeCategoryRequiresObjectPath(const FString& CategoryValue)
	{
		const FString Category = CategoryValue.TrimStartAndEnd().ToLower();
		return Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool ResolveNormalizedClassPath(const FString& Reference, FString& OutNormalizedPath)
	{
		if (UClass* const Class = ResolveClassReference(Reference))
		{
			OutNormalizedPath = Class->GetPathName();
			return true;
		}

		OutNormalizedPath.Reset();
		return false;
	}

	bool ResolveNormalizedEnumPath(const FString& Reference, FString& OutNormalizedPath)
	{
		if (UEnum* const Enum = Cast<UEnum>(ResolveObjectReference(Reference)))
		{
			OutNormalizedPath = Enum->GetPathName();
			return true;
		}

		OutNormalizedPath.Reset();
		return false;
	}

	bool ResolveNormalizedStructPath(const FString& Reference, FString& OutNormalizedPath)
	{
		if (UScriptStruct* const Struct = Cast<UScriptStruct>(ResolveObjectReference(Reference)))
		{
			OutNormalizedPath = Struct->GetPathName();
			return true;
		}

		OutNormalizedPath.Reset();
		return false;
	}

	bool ResolveNormalizedPinTypeMetadata(
		const FString& CategoryValue,
		const FString& ObjectPathValue,
		FString& OutNormalizedCategory,
		FString& OutNormalizedObjectPath,
		FString& OutError)
	{
		OutNormalizedCategory = CategoryValue.TrimStartAndEnd().ToLower();
		OutNormalizedObjectPath.Reset();

		if (OutNormalizedCategory.IsEmpty())
		{
			OutError = TEXT("Missing type category.");
			return false;
		}

		if (!IsSupportedResolvedTypeCategory(OutNormalizedCategory))
		{
			OutError = FString::Printf(TEXT("Unsupported pin category '%s'."), *OutNormalizedCategory);
			return false;
		}

		if (!TypeCategoryRequiresObjectPath(OutNormalizedCategory))
		{
			return true;
		}

		const FString TrimmedObjectPath = ObjectPathValue.TrimStartAndEnd();
		if (TrimmedObjectPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Type category '%s' requires an object path."), *OutNormalizedCategory);
			return false;
		}

		if (OutNormalizedCategory == TEXT("enum"))
		{
			if (ResolveNormalizedEnumPath(TrimmedObjectPath, OutNormalizedObjectPath))
			{
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve enum object '%s'."), *TrimmedObjectPath);
			return false;
		}

		if (OutNormalizedCategory == TEXT("object"))
		{
			if (ResolveNormalizedClassPath(TrimmedObjectPath, OutNormalizedObjectPath))
			{
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve object class '%s'."), *TrimmedObjectPath);
			return false;
		}

		if (OutNormalizedCategory == TEXT("class"))
		{
			if (ResolveNormalizedClassPath(TrimmedObjectPath, OutNormalizedObjectPath))
			{
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve class type '%s'."), *TrimmedObjectPath);
			return false;
		}

		if (ResolveNormalizedStructPath(TrimmedObjectPath, OutNormalizedObjectPath))
		{
			return true;
		}

		OutError = FString::Printf(TEXT("Unable to resolve struct '%s'."), *TrimmedObjectPath);
		return false;
	}

	bool ResolveAndNormalizeTypeReference(
		FVergilVariableTypeReference& Type,
		const FString& ContextLabel,
		const FName DiagnosticCode,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		FString NormalizedCategory;
		FString NormalizedObjectPath;
		FString TypeError;
		if (!ResolveNormalizedPinTypeMetadata(Type.PinCategory.ToString(), Type.ObjectPath, NormalizedCategory, NormalizedObjectPath, TypeError))
		{
			AddTypeResolutionDiagnostic(
				Context,
				DiagnosticCode,
				FString::Printf(TEXT("%s is invalid: %s"), *ContextLabel, *TypeError));
			return false;
		}

		bOutChanged |= SetNormalizedNameValue(Type.PinCategory, NormalizedCategory);
		bOutChanged |= SetNormalizedStringValue(Type.ObjectPath, NormalizedObjectPath);

		if (Type.ContainerType == EVergilVariableContainerType::Map)
		{
			FString NormalizedValueCategory;
			FString NormalizedValueObjectPath;
			if (!ResolveNormalizedPinTypeMetadata(Type.ValuePinCategory.ToString(), Type.ValueObjectPath, NormalizedValueCategory, NormalizedValueObjectPath, TypeError))
			{
				AddTypeResolutionDiagnostic(
					Context,
					DiagnosticCode,
					FString::Printf(TEXT("%s value type is invalid: %s"), *ContextLabel, *TypeError));
				return false;
			}

			bOutChanged |= SetNormalizedNameValue(Type.ValuePinCategory, NormalizedValueCategory);
			bOutChanged |= SetNormalizedStringValue(Type.ValueObjectPath, NormalizedValueObjectPath);
		}

		return true;
	}

	bool ResolveAndNormalizeDispatcherParameter(
		FVergilDispatcherParameter& Parameter,
		const FName DispatcherName,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		FString NormalizedCategory;
		FString NormalizedObjectPath;
		FString TypeError;
		if (!ResolveNormalizedPinTypeMetadata(Parameter.PinCategory.ToString(), Parameter.ObjectPath, NormalizedCategory, NormalizedObjectPath, TypeError))
		{
			AddTypeResolutionDiagnostic(
				Context,
				TEXT("DispatcherParameterTypeInvalid"),
				FString::Printf(
					TEXT("Dispatcher '%s' parameter '%s' is invalid: %s"),
					*DispatcherName.ToString(),
					*Parameter.Name.ToString(),
					*TypeError));
			return false;
		}

		bOutChanged |= SetNormalizedNameValue(Parameter.PinCategory, NormalizedCategory);
		bOutChanged |= SetNormalizedStringValue(Parameter.ObjectPath, NormalizedObjectPath);
		return true;
	}

	TArray<UClass*> BuildBlueprintSearchClasses(UBlueprint* Blueprint)
	{
		TArray<UClass*> SearchClasses;
		if (Blueprint == nullptr)
		{
			return SearchClasses;
		}

		if (Blueprint->SkeletonGeneratedClass != nullptr)
		{
			SearchClasses.AddUnique(Blueprint->SkeletonGeneratedClass);
		}
		if (Blueprint->GeneratedClass != nullptr)
		{
			SearchClasses.AddUnique(Blueprint->GeneratedClass);
		}
		if (Blueprint->ParentClass != nullptr)
		{
			SearchClasses.AddUnique(Blueprint->ParentClass);
		}

		return SearchClasses;
	}

	bool IsBlueprintSelfClass(const UBlueprint* Blueprint, const UClass* CandidateClass)
	{
		return Blueprint != nullptr
			&& CandidateClass != nullptr
			&& (CandidateClass == Blueprint->SkeletonGeneratedClass || CandidateClass == Blueprint->GeneratedClass);
	}

	const TArray<FVergilGraphNode>& GetTargetGraphNodes(const FVergilCompilerContext& Context);

	TArray<FVergilGraphNode>& GetMutableTargetGraphNodes(FVergilGraphDocument& Document, const FName GraphName)
	{
		return GraphName == ConstructionScriptGraphName
			? Document.ConstructionScriptNodes
			: Document.Nodes;
	}

	bool ResolveAndNormalizeNodePinTypeMetadata(
		FVergilGraphNode& Node,
		const FName CategoryKey,
		const FName ObjectPathKey,
		const FName DiagnosticCode,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		FString NormalizedCategory;
		FString NormalizedObjectPath;
		FString TypeError;
		if (!ResolveNormalizedPinTypeMetadata(
			Node.Metadata.FindRef(CategoryKey),
			Node.Metadata.FindRef(ObjectPathKey),
			NormalizedCategory,
			NormalizedObjectPath,
			TypeError))
		{
			AddTypeResolutionDiagnostic(Context, DiagnosticCode, TypeError, Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, CategoryKey, NormalizedCategory);
		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, ObjectPathKey, NormalizedObjectPath);
		return true;
	}

	bool ResolveAndNormalizeCastNodeType(FVergilGraphNode& Node, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		const FString TargetClassPath = Node.Metadata.FindRef(TEXT("TargetClassPath")).TrimStartAndEnd();
		FString NormalizedClassPath;
		if (!ResolveNormalizedClassPath(TargetClassPath, NormalizedClassPath))
		{
			AddTypeResolutionDiagnostic(
				Context,
				TEXT("CastTargetClassNotFound"),
				FString::Printf(TEXT("Unable to resolve cast target class '%s'."), *TargetClassPath),
				Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("TargetClassPath"), NormalizedClassPath);
		return true;
	}

	void BuildSpawnActorSupportedPins(const UClass* ActorClass, TArray<FVergilSpawnActorPinDescriptor>& OutPins)
	{
		OutPins.Reset();

		OutPins.Add({ UEdGraphSchema_K2::PN_Execute, EVergilPinDirection::Input, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_Then, EVergilPinDirection::Output, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_ReturnValue, EVergilPinDirection::Output, false });
		OutPins.Add({ SpawnActorTransformPinName, EVergilPinDirection::Input, false });
		OutPins.Add({ SpawnActorCollisionHandlingOverridePinName, EVergilPinDirection::Input, false });
		OutPins.Add({ SpawnActorTransformScaleMethodPinName, EVergilPinDirection::Input, false });
		OutPins.Add({ SpawnActorOwnerPinName, EVergilPinDirection::Input, false });

		if (ActorClass == nullptr)
		{
			return;
		}

		for (TFieldIterator<FProperty> PropertyIt(ActorClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* const Property = *PropertyIt;
			if (Property == nullptr)
			{
				continue;
			}

			const bool bIsDelegate = Property->IsA(FMulticastDelegateProperty::StaticClass());
			const bool bIsExposedToSpawn = UEdGraphSchema_K2::IsPropertyExposedOnSpawn(Property);
			const bool bIsSettableExternally = !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);

			if (bIsExposedToSpawn
				&& !Property->HasAnyPropertyFlags(CPF_Parm)
				&& bIsSettableExternally
				&& Property->HasAllPropertyFlags(CPF_BlueprintVisible)
				&& !bIsDelegate
				&& FBlueprintEditorUtils::PropertyStillExists(Property))
			{
				OutPins.Add({ Property->GetFName(), EVergilPinDirection::Input, false });
			}
		}
	}

	bool ValidateSpawnActorPins(
		const FVergilGraphNode& Node,
		const UClass* ActorClass,
		FVergilCompilerContext& Context)
	{
		TArray<FVergilSpawnActorPinDescriptor> SupportedPins;
		BuildSpawnActorSupportedPins(ActorClass, SupportedPins);

		bool bIsValid = true;
		for (const FVergilGraphPin& Pin : Node.Pins)
		{
			if (Pin.Name == SpawnActorClassPinName)
			{
				Context.AddDiagnostic(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorDynamicClassPinUnsupported"),
					TEXT("K2.SpawnActor currently uses ActorClassPath metadata as its deterministic class source; authored Class-pin connections are not supported."),
					Node.Id);
				bIsValid = false;
				continue;
			}

			if (Pin.Name == SpawnActorWorldContextPinName)
			{
				Context.AddDiagnostic(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorWorldContextPinUnsupported"),
					TEXT("K2.SpawnActor does not currently expose authored WorldContextObject pin connections."),
					Node.Id);
				bIsValid = false;
				continue;
			}

			const FVergilSpawnActorPinDescriptor* const SupportedPin = SupportedPins.FindByPredicate([&Pin](const FVergilSpawnActorPinDescriptor& Candidate)
			{
				return Candidate.Name == Pin.Name
					&& Candidate.Direction == Pin.Direction
					&& Candidate.bIsExec == Pin.bIsExec;
			});

			if (SupportedPin == nullptr)
			{
				Context.AddDiagnostic(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorPinUnsupported"),
					FString::Printf(
						TEXT("K2.SpawnActor pin '%s' is not part of the UE_5.7 SpawnActorFromClass surface for actor class '%s'."),
						*Pin.Name.ToString(),
						ActorClass != nullptr ? *ActorClass->GetPathName() : TEXT("<null>")),
					Node.Id);
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool ResolveAndNormalizeSpawnActorNodeType(FVergilGraphNode& Node, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		const FString ActorClassPath = Node.Metadata.FindRef(TEXT("ActorClassPath")).TrimStartAndEnd();
		FString NormalizedClassPath;
		if (!ResolveNormalizedClassPath(ActorClassPath, NormalizedClassPath))
		{
			AddTypeResolutionDiagnostic(
				Context,
				TEXT("SpawnActorClassNotFound"),
				FString::Printf(TEXT("Unable to resolve spawn actor class '%s'."), *ActorClassPath),
				Node.Id);
			return false;
		}

		UClass* const ActorClass = ResolveClassReference(NormalizedClassPath);
		if (ActorClass == nullptr || !ActorClass->IsChildOf(AActor::StaticClass()))
		{
			AddTypeResolutionDiagnostic(
				Context,
				TEXT("SpawnActorClassNotActor"),
				FString::Printf(TEXT("Spawn actor class '%s' must resolve to an AActor-derived class."), *NormalizedClassPath),
				Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("ActorClassPath"), NormalizedClassPath);
		return ValidateSpawnActorPins(Node, ActorClass, Context);
	}

	bool ResolveAndNormalizeSelectNodeTypes(FVergilGraphNode& Node, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		return ResolveAndNormalizeNodePinTypeMetadata(Node, TEXT("IndexPinCategory"), TEXT("IndexObjectPath"), TEXT("InvalidSelectIndexType"), Context, bOutChanged)
			&& ResolveAndNormalizeNodePinTypeMetadata(Node, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), TEXT("InvalidSelectValueType"), Context, bOutChanged);
	}

	bool ResolveAndNormalizeSwitchEnumNodeType(FVergilGraphNode& Node, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		const FString EnumPath = Node.Metadata.FindRef(TEXT("EnumPath")).TrimStartAndEnd();
		FString NormalizedEnumPath;
		if (!ResolveNormalizedEnumPath(EnumPath, NormalizedEnumPath))
		{
			AddTypeResolutionDiagnostic(
				Context,
				TEXT("SwitchEnumNotFound"),
				FString::Printf(TEXT("Unable to resolve switch enum '%s'."), *EnumPath),
				Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("EnumPath"), NormalizedEnumPath);
		return true;
	}

	bool ResolveAndNormalizeStructNodeType(
		FVergilGraphNode& Node,
		const FName DiagnosticCode,
		const TCHAR* ContextLabel,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString StructPath = Node.Metadata.FindRef(TEXT("StructPath")).TrimStartAndEnd();
		FString NormalizedStructPath;
		if (!ResolveNormalizedStructPath(StructPath, NormalizedStructPath))
		{
			AddTypeResolutionDiagnostic(
				Context,
				DiagnosticCode,
				FString::Printf(TEXT("Unable to resolve %s '%s'."), ContextLabel, *StructPath),
				Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("StructPath"), NormalizedStructPath);
		return true;
	}

	bool ResolveAndNormalizeContainerNodeType(
		FVergilGraphNode& Node,
		const FName CategoryKey,
		const FName ObjectPathKey,
		const FName DiagnosticCode,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		return ResolveAndNormalizeNodePinTypeMetadata(Node, CategoryKey, ObjectPathKey, DiagnosticCode, Context, bOutChanged);
	}

	bool ResolveDocumentTypeMetadata(FVergilGraphDocument& WorkingDocument, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		bool bIsValid = true;

		for (FVergilVariableDefinition& Variable : WorkingDocument.Variables)
		{
			bIsValid &= ResolveAndNormalizeTypeReference(
				Variable.Type,
				FString::Printf(TEXT("Variable '%s' type"), *Variable.Name.ToString()),
				TEXT("VariableTypeInvalid"),
				Context,
				bOutChanged);
		}

		for (FVergilFunctionDefinition& Function : WorkingDocument.Functions)
		{
			for (FVergilFunctionParameterDefinition& Parameter : Function.Inputs)
			{
				bIsValid &= ResolveAndNormalizeTypeReference(
					Parameter.Type,
					FString::Printf(TEXT("Function graph '%s' parameter '%s'"), *Function.Name.ToString(), *Parameter.Name.ToString()),
					TEXT("FunctionSignatureParameterTypeInvalid"),
					Context,
					bOutChanged);
			}

			for (FVergilFunctionParameterDefinition& Parameter : Function.Outputs)
			{
				bIsValid &= ResolveAndNormalizeTypeReference(
					Parameter.Type,
					FString::Printf(TEXT("Function graph '%s' parameter '%s'"), *Function.Name.ToString(), *Parameter.Name.ToString()),
					TEXT("FunctionSignatureParameterTypeInvalid"),
					Context,
					bOutChanged);
			}
		}

		for (FVergilMacroDefinition& Macro : WorkingDocument.Macros)
		{
			for (FVergilMacroParameterDefinition& Parameter : Macro.Inputs)
			{
				if (Parameter.bIsExec)
				{
					continue;
				}

				bIsValid &= ResolveAndNormalizeTypeReference(
					Parameter.Type,
					FString::Printf(TEXT("Macro graph '%s' parameter '%s'"), *Macro.Name.ToString(), *Parameter.Name.ToString()),
					TEXT("MacroSignatureParameterTypeInvalid"),
					Context,
					bOutChanged);
			}

			for (FVergilMacroParameterDefinition& Parameter : Macro.Outputs)
			{
				if (Parameter.bIsExec)
				{
					continue;
				}

				bIsValid &= ResolveAndNormalizeTypeReference(
					Parameter.Type,
					FString::Printf(TEXT("Macro graph '%s' parameter '%s'"), *Macro.Name.ToString(), *Parameter.Name.ToString()),
					TEXT("MacroSignatureParameterTypeInvalid"),
					Context,
					bOutChanged);
			}
		}

		for (FVergilDispatcherDefinition& Dispatcher : WorkingDocument.Dispatchers)
		{
			for (FVergilDispatcherParameter& Parameter : Dispatcher.Parameters)
			{
				bIsValid &= ResolveAndNormalizeDispatcherParameter(Parameter, Dispatcher.Name, Context, bOutChanged);
			}
		}

		for (FVergilComponentDefinition& Component : WorkingDocument.Components)
		{
			const FString ComponentClassPath = Component.ComponentClassPath.TrimStartAndEnd();
			FString NormalizedClassPath;
			if (!ResolveNormalizedClassPath(ComponentClassPath, NormalizedClassPath))
			{
				AddTypeResolutionDiagnostic(
					Context,
					TEXT("InvalidComponentClass"),
					FString::Printf(TEXT("Unable to resolve component class '%s'."), *ComponentClassPath));
				bIsValid = false;
				continue;
			}

			UClass* const ComponentClass = ResolveClassReference(NormalizedClassPath);
			if (ComponentClass == nullptr || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
			{
				AddTypeResolutionDiagnostic(
					Context,
					TEXT("InvalidComponentClass"),
					FString::Printf(TEXT("Unable to resolve component class '%s'."), *ComponentClassPath));
				bIsValid = false;
				continue;
			}

			bOutChanged |= SetNormalizedStringValue(Component.ComponentClassPath, NormalizedClassPath);
		}

		for (FVergilInterfaceDefinition& Interface : WorkingDocument.Interfaces)
		{
			const FString InterfaceClassPath = Interface.InterfaceClassPath.TrimStartAndEnd();
			FString NormalizedClassPath;
			if (!ResolveNormalizedClassPath(InterfaceClassPath, NormalizedClassPath))
			{
				AddTypeResolutionDiagnostic(
					Context,
					TEXT("InvalidInterfaceClass"),
					FString::Printf(TEXT("Unable to resolve interface class '%s'."), *InterfaceClassPath));
				bIsValid = false;
				continue;
			}

			UClass* const InterfaceClass = ResolveClassReference(NormalizedClassPath);
			if (InterfaceClass == nullptr || !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
			{
				AddTypeResolutionDiagnostic(
					Context,
					TEXT("InvalidInterfaceClass"),
					FString::Printf(TEXT("Unable to resolve interface class '%s'."), *InterfaceClassPath));
				bIsValid = false;
				continue;
			}

			bOutChanged |= SetNormalizedStringValue(Interface.InterfaceClassPath, NormalizedClassPath);
		}

		return bIsValid;
	}

	bool ResolveNodeTypes(FVergilGraphNode& Node, FVergilCompilerContext& Context, bool& bOutChanged)
	{
		if (Node.Descriptor == TEXT("K2.Cast"))
		{
			return ResolveAndNormalizeCastNodeType(Node, Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.SpawnActor"))
		{
			return ResolveAndNormalizeSpawnActorNodeType(Node, Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.Select"))
		{
			return ResolveAndNormalizeSelectNodeTypes(Node, Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.SwitchEnum"))
		{
			return ResolveAndNormalizeSwitchEnumNodeType(Node, Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.MakeStruct"))
		{
			return ResolveAndNormalizeStructNodeType(Node, TEXT("MakeStructTypeNotFound"), TEXT("make struct type"), Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.BreakStruct"))
		{
			return ResolveAndNormalizeStructNodeType(Node, TEXT("BreakStructTypeNotFound"), TEXT("break struct type"), Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.MakeArray"))
		{
			return ResolveAndNormalizeContainerNodeType(Node, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), TEXT("InvalidMakeArrayValueType"), Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.MakeSet"))
		{
			return ResolveAndNormalizeContainerNodeType(Node, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), TEXT("InvalidMakeSetValueType"), Context, bOutChanged);
		}

		if (Node.Descriptor == TEXT("K2.MakeMap"))
		{
			return ResolveAndNormalizeContainerNodeType(Node, TEXT("KeyPinCategory"), TEXT("KeyObjectPath"), TEXT("InvalidMakeMapKeyType"), Context, bOutChanged)
				&& ResolveAndNormalizeContainerNodeType(Node, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), TEXT("InvalidMakeMapValueType"), Context, bOutChanged);
		}

		return true;
	}

	FVergilDocumentSymbolTables BuildDocumentSymbolTables(const FVergilCompilerContext& Context)
	{
		FVergilDocumentSymbolTables SymbolTables;

		const FVergilGraphDocument& Document = Context.GetDocument();
		for (const FVergilVariableDefinition& Variable : Document.Variables)
		{
			if (!Variable.Name.IsNone())
			{
				SymbolTables.VariableNames.Add(Variable.Name);
				SymbolTables.VariableTypes.Add(Variable.Name, Variable.Type);
			}
		}

		for (const FVergilFunctionDefinition& Function : Document.Functions)
		{
			if (!Function.Name.IsNone())
			{
				SymbolTables.FunctionNames.Add(Function.Name);
			}
		}

		for (const FVergilDispatcherDefinition& Dispatcher : Document.Dispatchers)
		{
			if (!Dispatcher.Name.IsNone())
			{
				SymbolTables.DispatcherNames.Add(Dispatcher.Name);
			}
		}

		for (const FVergilGraphNode& Node : GetTargetGraphNodes(Context))
		{
			const FString CustomEventName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CustomEvent."));
			if (!CustomEventName.IsEmpty())
			{
				++SymbolTables.CustomEventCounts.FindOrAdd(*CustomEventName);
			}
		}

		return SymbolTables;
	}

	bool ResolveEventSymbol(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		const FString EventNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Event."));
		if (Context.GetGraphName() == ConstructionScriptGraphName && EventNameString == ConstructionScriptGraphName.ToString())
		{
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		UClass* const ParentClass = Blueprint != nullptr ? Blueprint->ParentClass : nullptr;
		if (ParentClass == nullptr)
		{
			AddSymbolResolutionDiagnostic(
				Context,
				TEXT("EventFunctionNotFound"),
				FString::Printf(TEXT("Unable to resolve event '%s' because the target Blueprint has no parent class."), *EventNameString),
				Node.Id);
			return false;
		}

		if (ParentClass->FindFunctionByName(*EventNameString) == nullptr)
		{
			AddSymbolResolutionDiagnostic(
				Context,
				TEXT("EventFunctionNotFound"),
				FString::Printf(TEXT("Unable to resolve event '%s' on parent class '%s'."), *EventNameString, *ParentClass->GetName()),
				Node.Id);
			return false;
		}

		return true;
	}

	bool ResolveCallSymbol(
		FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString FunctionNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.Call."));
		const FName FunctionName(*FunctionNameString);
		const FString ExplicitOwnerClassPath = Node.Metadata.FindRef(TEXT("OwnerClassPath")).TrimStartAndEnd();

		if (!ExplicitOwnerClassPath.IsEmpty())
		{
			UClass* const OwnerClass = ResolveClassReference(ExplicitOwnerClassPath);
			if (OwnerClass == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("MissingFunctionOwner"),
					FString::Printf(TEXT("Unable to resolve owner class '%s' for function '%s'."), *ExplicitOwnerClassPath, *FunctionNameString),
					Node.Id);
				return false;
			}

			UFunction* const Function = OwnerClass->FindFunctionByName(FunctionName);
			if (Function == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("FunctionNotFound"),
					FString::Printf(TEXT("Unable to resolve function '%s' on class '%s'."), *FunctionNameString, *OwnerClass->GetName()),
					Node.Id);
				return false;
			}

			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), Function->GetOwnerClass()->GetPathName());
			return true;
		}

		if (SymbolTables.FunctionNames.Contains(FunctionName))
		{
			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		for (UClass* const SearchClass : BuildBlueprintSearchClasses(Blueprint))
		{
			if (SearchClass == nullptr)
			{
				continue;
			}

			if (UFunction* const Function = SearchClass->FindFunctionByName(FunctionName))
			{
				const UClass* const OwnerClass = Function->GetOwnerClass();
				if (IsBlueprintSelfClass(Blueprint, OwnerClass))
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
				}
				else if (OwnerClass != nullptr)
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), OwnerClass->GetPathName());
				}

				return true;
			}
		}

		if (Blueprint == nullptr)
		{
			AddSymbolResolutionDiagnostic(
				Context,
				TEXT("MissingFunctionOwner"),
				FString::Printf(TEXT("Unable to resolve function '%s' because the target Blueprint is missing."), *FunctionNameString),
				Node.Id);
			return false;
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("FunctionNotFound"),
			FString::Printf(TEXT("Unable to resolve function '%s' on the compiled document, Blueprint, or parent class."), *FunctionNameString),
			Node.Id);
		return false;
	}

	bool ResolveVariableSymbol(
		FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString VariableNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarGet."));
		const FString AlternateVariableNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.VarSet."));
		const bool bIsVariableGet = !VariableNameString.IsEmpty();
		const FName VariableName = !VariableNameString.IsEmpty()
			? FName(*VariableNameString)
			: FName(*AlternateVariableNameString);
		const FString DisplayVariableName = !VariableNameString.IsEmpty() ? VariableNameString : AlternateVariableNameString;
		const FString ExplicitOwnerClassPath = Node.Metadata.FindRef(TEXT("OwnerClassPath")).TrimStartAndEnd();

		if (!ExplicitOwnerClassPath.IsEmpty())
		{
			UClass* const OwnerClass = ResolveClassReference(ExplicitOwnerClassPath);
			if (OwnerClass == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("VariableOwnerClassNotFound"),
					FString::Printf(TEXT("Unable to resolve variable owner class '%s'."), *ExplicitOwnerClassPath),
					Node.Id);
				return false;
			}

			FProperty* const Property = FindFProperty<FProperty>(OwnerClass, VariableName);
			if (Property == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("VariablePropertyNotFound"),
					FString::Printf(TEXT("Unable to resolve property '%s' on class '%s' for variable node."), *DisplayVariableName, *OwnerClass->GetName()),
					Node.Id);
				return false;
			}

			if (bIsVariableGet && !ValidateVariableGetVariant(Node, Context, nullptr, Property))
			{
				return false;
			}

			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), Property->GetOwnerClass()->GetPathName());
			return true;
		}

		if (SymbolTables.VariableNames.Contains(VariableName))
		{
			if (bIsVariableGet)
			{
				const FVergilVariableTypeReference* const VariableType = SymbolTables.VariableTypes.Find(VariableName);
				if (!ValidateVariableGetVariant(Node, Context, VariableType, nullptr))
				{
					return false;
				}
			}

			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		for (UClass* const SearchClass : BuildBlueprintSearchClasses(Blueprint))
		{
			if (FProperty* const Property = FindFProperty<FProperty>(SearchClass, VariableName))
			{
				if (bIsVariableGet && !ValidateVariableGetVariant(Node, Context, nullptr, Property))
				{
					return false;
				}

				const UClass* const OwnerClass = Property->GetOwnerClass();
				if (IsBlueprintSelfClass(Blueprint, OwnerClass))
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
				}
				else if (OwnerClass != nullptr)
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), OwnerClass->GetPathName());
				}

				return true;
			}
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("VariablePropertyNotFound"),
			FString::Printf(TEXT("Unable to resolve property '%s' on the target Blueprint or parent class for variable node."), *DisplayVariableName),
			Node.Id);
		return false;
	}

	bool ResolveDelegateSymbol(
		FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString DescriptorString = Node.Descriptor.ToString();
		FString PropertyNameString;
		if (DescriptorString.StartsWith(TEXT("K2.BindDelegate.")))
		{
			PropertyNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.BindDelegate."));
		}
		else if (DescriptorString.StartsWith(TEXT("K2.RemoveDelegate.")))
		{
			PropertyNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.RemoveDelegate."));
		}
		else if (DescriptorString.StartsWith(TEXT("K2.ClearDelegate.")))
		{
			PropertyNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.ClearDelegate."));
		}
		else
		{
			PropertyNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CallDelegate."));
		}

		const FName PropertyName(*PropertyNameString);
		const FString ExplicitOwnerClassPath = Node.Metadata.FindRef(TEXT("OwnerClassPath")).TrimStartAndEnd();

		if (!ExplicitOwnerClassPath.IsEmpty())
		{
			UClass* const OwnerClass = ResolveClassReference(ExplicitOwnerClassPath);
			if (OwnerClass == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("DelegateOwnerClassNotFound"),
					FString::Printf(TEXT("Unable to resolve delegate owner class '%s'."), *ExplicitOwnerClassPath),
					Node.Id);
				return false;
			}

			FMulticastDelegateProperty* const DelegateProperty = FindFProperty<FMulticastDelegateProperty>(OwnerClass, PropertyName);
			if (DelegateProperty == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("DelegatePropertyNotFound"),
					FString::Printf(TEXT("Unable to resolve multicast delegate property '%s' on class '%s'."), *PropertyNameString, *OwnerClass->GetName()),
					Node.Id);
				return false;
			}

			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), DelegateProperty->GetOwnerClass()->GetPathName());
			return true;
		}

		if (SymbolTables.DispatcherNames.Contains(PropertyName))
		{
			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		for (UClass* const SearchClass : BuildBlueprintSearchClasses(Blueprint))
		{
			if (FMulticastDelegateProperty* const DelegateProperty = FindFProperty<FMulticastDelegateProperty>(SearchClass, PropertyName))
			{
				const UClass* const OwnerClass = DelegateProperty->GetOwnerClass();
				if (IsBlueprintSelfClass(Blueprint, OwnerClass))
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), FString());
				}
				else if (OwnerClass != nullptr)
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("OwnerClassPath"), OwnerClass->GetPathName());
				}

				return true;
			}
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("DelegatePropertyNotFound"),
			FString::Printf(TEXT("Unable to resolve multicast delegate property '%s' on the target Blueprint or parent class."), *PropertyNameString),
			Node.Id);
		return false;
	}

	bool ResolveCustomEventDelegateSignature(
		FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString DelegatePropertyName = Node.Metadata.FindRef(TEXT("DelegatePropertyName")).TrimStartAndEnd();
		if (DelegatePropertyName.IsEmpty())
		{
			return true;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("DelegatePropertyName"), DelegatePropertyName);

		const FName PropertyName(*DelegatePropertyName);
		const FString ExplicitOwnerClassPath = Node.Metadata.FindRef(TEXT("DelegateOwnerClassPath")).TrimStartAndEnd();
		if (!ExplicitOwnerClassPath.IsEmpty())
		{
			UClass* const OwnerClass = ResolveClassReference(ExplicitOwnerClassPath);
			if (OwnerClass == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("DelegateOwnerClassNotFound"),
					FString::Printf(TEXT("Unable to resolve delegate owner class '%s'."), *ExplicitOwnerClassPath),
					Node.Id);
				return false;
			}

			FMulticastDelegateProperty* const DelegateProperty = FindFProperty<FMulticastDelegateProperty>(OwnerClass, PropertyName);
			if (DelegateProperty == nullptr)
			{
				AddSymbolResolutionDiagnostic(
					Context,
					TEXT("DelegatePropertyNotFound"),
					FString::Printf(TEXT("Unable to resolve multicast delegate property '%s' on class '%s'."), *DelegatePropertyName, *OwnerClass->GetName()),
					Node.Id);
				return false;
			}

			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("DelegateOwnerClassPath"), DelegateProperty->GetOwnerClass()->GetPathName());
			return true;
		}

		if (SymbolTables.DispatcherNames.Contains(PropertyName))
		{
			bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("DelegateOwnerClassPath"), FString());
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		for (UClass* const SearchClass : BuildBlueprintSearchClasses(Blueprint))
		{
			if (FMulticastDelegateProperty* const DelegateProperty = FindFProperty<FMulticastDelegateProperty>(SearchClass, PropertyName))
			{
				const UClass* const OwnerClass = DelegateProperty->GetOwnerClass();
				if (IsBlueprintSelfClass(Blueprint, OwnerClass))
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("DelegateOwnerClassPath"), FString());
				}
				else if (OwnerClass != nullptr)
				{
					bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("DelegateOwnerClassPath"), OwnerClass->GetPathName());
				}

				return true;
			}
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("DelegatePropertyNotFound"),
			FString::Printf(TEXT("Unable to resolve multicast delegate property '%s' for custom event '%s'."), *DelegatePropertyName, *GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CustomEvent."))),
			Node.Id);
		return false;
	}

	bool ResolveCreateDelegateSymbol(
		const FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context)
	{
		const FString FunctionNameString = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CreateDelegate."));
		const FName FunctionName(*FunctionNameString);
		const int32 CustomEventCount = SymbolTables.CustomEventCounts.FindRef(FunctionName);
		const bool bHasDocumentFunction = SymbolTables.FunctionNames.Contains(FunctionName);

		if (CustomEventCount > 1 || (CustomEventCount > 0 && bHasDocumentFunction))
		{
			AddSymbolResolutionDiagnostic(
				Context,
				TEXT("CreateDelegateFunctionAmbiguous"),
				FString::Printf(TEXT("CreateDelegate target '%s' is ambiguous within the compiled document."), *FunctionNameString),
				Node.Id);
			return false;
		}

		if (CustomEventCount == 1 || bHasDocumentFunction)
		{
			return true;
		}

		UBlueprint* const Blueprint = Context.GetBlueprint();
		for (UClass* const SearchClass : BuildBlueprintSearchClasses(Blueprint))
		{
			if (SearchClass != nullptr && SearchClass->FindFunctionByName(FunctionName) != nullptr)
			{
				return true;
			}
		}

		AddSymbolResolutionDiagnostic(
			Context,
			TEXT("CreateDelegateFunctionNotFound"),
			FString::Printf(TEXT("Unable to resolve create-delegate target '%s' on the compiled document, Blueprint, or parent class."), *FunctionNameString),
			Node.Id);
		return false;
	}

	void NormalizeStandardMacroPlannedPins(
		const FVergilStandardMacroDescriptor& MacroDescriptor,
		FVergilCompilerCommand& Command)
	{
		if (MacroDescriptor.Descriptor == TEXT("K2.DoOnce"))
		{
			for (FVergilPlannedPin& PlannedPin : Command.PlannedPins)
			{
				if (PlannedPin.Name == TEXT("StartClosed"))
				{
					PlannedPin.Name = TEXT("Start Closed");
				}
			}
		}
	}

	bool ResolveStandardMacroSymbol(
		const FVergilStandardMacroDescriptor& MacroDescriptor,
		FVergilGraphNode& Node,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		FString MacroBlueprintPath = Node.Metadata.FindRef(TEXT("MacroBlueprintPath")).TrimStartAndEnd();
		FString MacroGraphNameString = Node.Metadata.FindRef(TEXT("MacroGraphName")).TrimStartAndEnd();
		if (MacroBlueprintPath.IsEmpty())
		{
			MacroBlueprintPath = StandardMacrosBlueprintPath;
		}
		if (MacroGraphNameString.IsEmpty())
		{
			MacroGraphNameString = MacroDescriptor.DefaultMacroGraphName.ToString();
		}

		if (ResolveMacroGraphReference(MacroBlueprintPath, FName(*MacroGraphNameString)) == nullptr)
		{
			AddSymbolResolutionDiagnostic(
				Context,
				MacroDescriptor.NotFoundDiagnosticCode,
				FString::Printf(TEXT("Unable to resolve macro graph '%s' from '%s'."), *MacroGraphNameString, *MacroBlueprintPath),
				Node.Id);
			return false;
		}

		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("MacroBlueprintPath"), MacroBlueprintPath);
		bOutChanged |= SetNormalizedMetadataValue(Node.Metadata, TEXT("MacroGraphName"), MacroGraphNameString);
		return true;
	}

	bool ResolveNodeSymbols(
		FVergilGraphNode& Node,
		const FVergilDocumentSymbolTables& SymbolTables,
		FVergilCompilerContext& Context,
		bool& bOutChanged)
	{
		const FString DescriptorString = Node.Descriptor.ToString();
		if (DescriptorString.StartsWith(TEXT("K2.Event.")))
		{
			return ResolveEventSymbol(Node, Context);
		}

		if (DescriptorString.StartsWith(TEXT("K2.Call.")))
		{
			return ResolveCallSymbol(Node, SymbolTables, Context, bOutChanged);
		}

		if (DescriptorString.StartsWith(TEXT("K2.VarGet.")) || DescriptorString.StartsWith(TEXT("K2.VarSet.")))
		{
			return ResolveVariableSymbol(Node, SymbolTables, Context, bOutChanged);
		}

		if (DescriptorString.StartsWith(TEXT("K2.BindDelegate."))
			|| DescriptorString.StartsWith(TEXT("K2.RemoveDelegate."))
			|| DescriptorString.StartsWith(TEXT("K2.ClearDelegate."))
			|| DescriptorString.StartsWith(TEXT("K2.CallDelegate.")))
		{
			return ResolveDelegateSymbol(Node, SymbolTables, Context, bOutChanged);
		}

		if (DescriptorString.StartsWith(TEXT("K2.CustomEvent.")))
		{
			return ResolveCustomEventDelegateSignature(Node, SymbolTables, Context, bOutChanged);
		}

		if (DescriptorString.StartsWith(TEXT("K2.CreateDelegate.")))
		{
			return ResolveCreateDelegateSymbol(Node, SymbolTables, Context);
		}

		if (const FVergilStandardMacroDescriptor* const StandardMacroDescriptor = FindStandardMacroDescriptorByDescriptor(Node.Descriptor))
		{
			return ResolveStandardMacroSymbol(*StandardMacroDescriptor, Node, Context, bOutChanged);
		}

		return true;
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

	bool IsCommentNode(const FVergilGraphNode& Node)
	{
		return Node.Kind == EVergilNodeKind::Comment;
	}

	const TCHAR* LexBoolString(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString LexCommentMoveModeString(const EVergilCommentMoveMode MoveMode)
	{
		switch (MoveMode)
		{
		case EVergilCommentMoveMode::NoGroupMovement:
			return TEXT("NoGroupMovement");

		case EVergilCommentMoveMode::GroupMovement:
		default:
			return TEXT("GroupMovement");
		}
	}

	bool HasTrimmedMetadataValue(const FVergilGraphNode& Node, const FName Key)
	{
		return !Node.Metadata.FindRef(Key).TrimStartAndEnd().IsEmpty();
	}

	void EmitNodeMetadataCommand(
		const FGuid NodeId,
		const FName GraphName,
		const FName MetadataKey,
		const FString& MetadataValue,
		TFunctionRef<void(const FVergilCompilerCommand&)> EmitCommand)
	{
		FVergilCompilerCommand MetadataCommand;
		MetadataCommand.Type = EVergilCommandType::SetNodeMetadata;
		MetadataCommand.GraphName = GraphName;
		MetadataCommand.NodeId = NodeId;
		MetadataCommand.Name = MetadataKey;
		MetadataCommand.StringValue = MetadataValue;
		EmitCommand(MetadataCommand);
	}

	void AddDefaultCommentMetadataCommands(
		const FVergilGraphNode& Node,
		const FName GraphName,
		const FVergilCommentGenerationSettings& Settings,
		TFunctionRef<void(const FVergilCompilerCommand&)> EmitCommand)
	{
		if (!HasTrimmedMetadataValue(Node, TEXT("CommentWidth")) && !HasTrimmedMetadataValue(Node, TEXT("NodeWidth")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("CommentWidth"),
				LexToString(FMath::Max(Settings.DefaultWidth, 16.0f)),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("CommentHeight")) && !HasTrimmedMetadataValue(Node, TEXT("NodeHeight")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("CommentHeight"),
				LexToString(FMath::Max(Settings.DefaultHeight, 16.0f)),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("FontSize")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("FontSize"),
				LexToString(FMath::Max(Settings.DefaultFontSize, 1)),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("CommentColor")) && !HasTrimmedMetadataValue(Node, TEXT("Color")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("CommentColor"),
				Settings.DefaultColor.ToFColor(true).ToHex(),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("ShowBubbleWhenZoomed")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("ShowBubbleWhenZoomed"),
				LexBoolString(Settings.bShowBubbleWhenZoomed),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("ColorBubble")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("ColorBubble"),
				LexBoolString(Settings.bColorBubble),
				EmitCommand);
		}

		if (!HasTrimmedMetadataValue(Node, TEXT("MoveMode")))
		{
			EmitNodeMetadataCommand(
				Node.Id,
				GraphName,
				TEXT("MoveMode"),
				LexCommentMoveModeString(Settings.MoveMode),
				EmitCommand);
		}
	}

	void AddNodeMetadataCommands(
		const FVergilGraphNode& Node,
		const FName GraphName,
		TFunctionRef<void(const FVergilCompilerCommand&)> EmitCommand)
	{
		TArray<FName> MetadataKeys;
		Node.Metadata.GetKeys(MetadataKeys);
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
			MetadataCommand.Type = EVergilCommandType::SetNodeMetadata;
			MetadataCommand.GraphName = GraphName;
			MetadataCommand.NodeId = Node.Id;
			MetadataCommand.Name = MetadataKey;
			MetadataCommand.StringValue = Node.Metadata.FindRef(MetadataKey);
			EmitCommand(MetadataCommand);
		}
	}

	void AddNodeMetadataCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		AddNodeMetadataCommands(Node, Context.GetGraphName(), [&Context](const FVergilCompilerCommand& Command)
		{
			Context.AddCommand(Command);
		});
	}

	void BuildCommentNodeCommands(
		const FVergilGraphNode& Node,
		const FName GraphName,
		const FVergilCommentGenerationSettings& Settings,
		TFunctionRef<void(const FVergilCompilerCommand&)> EmitCommand)
	{
		FVergilCompilerCommand AddNodeCommand;
		AddNodeCommand.Type = EVergilCommandType::AddNode;
		AddNodeCommand.GraphName = GraphName;
		AddNodeCommand.NodeId = Node.Id;
		AddNodeCommand.Name = TEXT("Vergil.Comment");
		AddNodeCommand.SecondaryName = Node.Descriptor;
		AddNodeCommand.Position = Node.Position;
		AddNodeCommand.StringValue = Node.Metadata.FindRef(TEXT("CommentText"));
		if (AddNodeCommand.StringValue.IsEmpty())
		{
			AddNodeCommand.StringValue = Node.Metadata.FindRef(TEXT("Title"));
		}
		CopyPlannedPins(Node, AddNodeCommand);
		EmitCommand(AddNodeCommand);
		AddDefaultCommentMetadataCommands(Node, GraphName, Settings, EmitCommand);
		AddNodeMetadataCommands(Node, GraphName, EmitCommand);
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

	struct FCompilerPlannedPinInfo
	{
		FGuid NodeId;
		FName GraphName = EventGraphName;
		FName Name = NAME_None;
		bool bIsInput = true;
		bool bIsExec = false;
	};

	FName ResolveCompilerCommandGraphName(const FVergilCompilerCommand& Command)
	{
		return Command.GraphName.IsNone() ? EventGraphName : Command.GraphName;
	}

	FString DescribeGraphNode(const FVergilGraphNode* Node)
	{
		if (Node == nullptr)
		{
			return TEXT("<unknown-node>");
		}

		return Node->Descriptor.IsNone() ? Node->Id.ToString() : Node->Descriptor.ToString();
	}

	FString DescribePlannedPin(const FCompilerPlannedPinInfo* PinInfo, const FGuid& PinId)
	{
		if (PinInfo == nullptr)
		{
			return PinId.ToString();
		}

		return PinInfo->Name.IsNone() ? PinId.ToString() : PinInfo->Name.ToString();
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
			return IsCommentNode(Node);
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			BuildCommentNodeCommands(Node, Context.GetGraphName(), FVergilCommentGenerationSettings(), [&Context](const FVergilCompilerCommand& Command)
			{
				Context.AddCommand(Command);
			});
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

	class FVergilStandardMacroNodeHandler final : public IVergilNodeHandler
	{
	public:
		explicit FVergilStandardMacroNodeHandler(const FVergilStandardMacroDescriptor& InDescriptor)
			: Descriptor(InDescriptor)
		{
		}

		virtual FName GetDescriptor() const override
		{
			return Descriptor.CommandName;
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == Descriptor.Descriptor;
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
				Command.Attributes.Add(TEXT("MacroBlueprintPath"), StandardMacrosBlueprintPath);
			}
			if (!Command.Attributes.Contains(TEXT("MacroGraphName")))
			{
				Command.Attributes.Add(TEXT("MacroGraphName"), Descriptor.DefaultMacroGraphName.ToString());
			}
			CopyPlannedPins(Node, Command);
			NormalizeStandardMacroPlannedPins(Descriptor, Command);
			Context.AddCommand(Command);
			return true;
		}

	private:
		FVergilStandardMacroDescriptor Descriptor;
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

	class FVergilSpawnActorNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Vergil.K2.SpawnActor");
		}

		virtual bool CanHandle(const FVergilGraphNode& Node) const override
		{
			return Node.Descriptor == TEXT("K2.SpawnActor");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = GetDescriptor();
			Command.StringValue = Node.Metadata.FindRef(TEXT("ActorClassPath"));
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
			Command.StringValue = Node.Metadata.FindRef(TEXT("TargetClassPath"));
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
			AddNodeMetadataCommands(Node, Context);

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
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilStandardMacroNodeHandler, ESPMode::ThreadSafe>(*FindStandardMacroDescriptorByDescriptor(TEXT("K2.ForLoop"))));
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilStandardMacroNodeHandler, ESPMode::ThreadSafe>(*FindStandardMacroDescriptorByDescriptor(TEXT("K2.DoOnce"))));
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilStandardMacroNodeHandler, ESPMode::ThreadSafe>(*FindStandardMacroDescriptorByDescriptor(TEXT("K2.FlipFlop"))));
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilDelayNodeHandler, ESPMode::ThreadSafe>());
		FVergilNodeRegistry::Get().RegisterFallbackHandler(MakeShared<FVergilSpawnActorNodeHandler, ESPMode::ThreadSafe>());
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

	bool AddPostCompileFinalizeCommand(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		if (!Node.Descriptor.ToString().StartsWith(TEXT("K2.CreateDelegate.")))
		{
			return true;
		}

		const FString FunctionName = GetDescriptorSuffix(Node.Descriptor, TEXT("K2.CreateDelegate."));
		if (FunctionName.IsEmpty())
		{
			Context.AddDiagnostic(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingCreateDelegateFinalizeFunction"),
				TEXT("Post-compile finalize pass requires a descriptor suffix naming the selected CreateDelegate function."),
				Node.Id);
			return false;
		}

		FVergilCompilerCommand FinalizeCommand;
		FinalizeCommand.Type = EVergilCommandType::FinalizeNode;
		FinalizeCommand.GraphName = Context.GetGraphName();
		FinalizeCommand.NodeId = Node.Id;
		FinalizeCommand.Name = TEXT("Vergil.K2.CreateDelegate");
		FinalizeCommand.SecondaryName = *FunctionName;
		FinalizeCommand.Attributes = Node.Metadata;
		Context.AddPostCompileFinalizeCommand(FinalizeCommand);
		return true;
	}

	bool LowerNode(const FVergilGraphNode& Node, FVergilCompilerContext& Context)
	{
		const TSharedPtr<IVergilNodeHandler, ESPMode::ThreadSafe> Handler = FVergilNodeRegistry::Get().FindHandler(Node);
		if (!Handler.IsValid())
		{
			Context.AddDiagnostic(
				EVergilDiagnosticSeverity::Error,
				TEXT("NodeLoweringHandlerMissing"),
				FString::Printf(
					TEXT("Node lowering pass could not find a handler for descriptor '%s'."),
					*Node.Descriptor.ToString()),
				Node.Id);
			return false;
		}

		if (!Handler->BuildCommands(Node, Context))
		{
			Context.AddDiagnostic(
				EVergilDiagnosticSeverity::Error,
				TEXT("NodeLoweringFailed"),
				FString::Printf(
					TEXT("Node lowering pass failed for node '%s' using handler '%s'."),
					*Node.Descriptor.ToString(),
					*Handler->GetDescriptor().ToString()),
				Node.Id);
			return false;
		}

		return true;
	}

	bool BuildPostCompileFinalizeCommands(FVergilCompilerContext& Context)
	{
		Context.ResetPostCompileFinalizeCommands();

		bool bIsValid = true;
		for (const FVergilGraphNode& Node : GetTargetGraphNodes(Context))
		{
			if (!AddPostCompileFinalizeCommand(Node, Context))
			{
				Context.AddDiagnostic(
					EVergilDiagnosticSeverity::Error,
					TEXT("PostCompileFinalizeFailed"),
					FString::Printf(
						TEXT("Post-compile finalize pass failed for node '%s'."),
						*Node.Descriptor.ToString()),
					Node.Id);
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool BuildPostCommentCommands(const FVergilCompileRequest& Request, FVergilCompilerContext& Context)
	{
		Context.ResetPostCommentCommands();
		if (!Request.bGenerateComments)
		{
			return true;
		}

		for (const FVergilGraphNode& Node : GetTargetGraphNodes(Context))
		{
			if (!IsCommentNode(Node))
			{
				continue;
			}

			BuildCommentNodeCommands(Node, Context.GetGraphName(), Request.CommentGeneration, [&Context](const FVergilCompilerCommand& Command)
			{
				Context.AddPostCommentCommand(Command);
			});
		}

		return true;
	}

	struct FVergilAutoLayoutNodeState
	{
		const FVergilGraphNode* Node = nullptr;
		TArray<int32> Incoming;
		TArray<int32> Outgoing;
		int32 Column = 0;
		int32 Row = 0;
		bool bPlaced = false;
	};

	bool IsGuidLexicallyLess(const FGuid& A, const FGuid& B)
	{
		if (A.A != B.A)
		{
			return A.A < B.A;
		}

		if (A.B != B.B)
		{
			return A.B < B.B;
		}

		if (A.C != B.C)
		{
			return A.C < B.C;
		}

		return A.D < B.D;
	}

	int32 GetAutoLayoutNodePriority(const FVergilGraphNode& Node)
	{
		if (Node.Kind == EVergilNodeKind::Event || Node.Descriptor.ToString().StartsWith(TEXT("K2.CustomEvent.")))
		{
			return 0;
		}

		if (IsCommentNode(Node))
		{
			return 2;
		}

		return 1;
	}

	bool IsAutoLayoutNodeLess(const FVergilGraphNode& A, const FVergilGraphNode& B)
	{
		const int32 PriorityA = GetAutoLayoutNodePriority(A);
		const int32 PriorityB = GetAutoLayoutNodePriority(B);
		if (PriorityA != PriorityB)
		{
			return PriorityA < PriorityB;
		}

		if (!FMath::IsNearlyEqual(A.Position.Y, B.Position.Y))
		{
			return A.Position.Y < B.Position.Y;
		}

		if (!FMath::IsNearlyEqual(A.Position.X, B.Position.X))
		{
			return A.Position.X < B.Position.X;
		}

		if (A.Descriptor != B.Descriptor)
		{
			return A.Descriptor.LexicalLess(B.Descriptor);
		}

		return IsGuidLexicallyLess(A.Id, B.Id);
	}

	float SanitizeLayoutSpacing(const float Value, const float Minimum)
	{
		return FMath::Max(Value, Minimum);
	}

	float GetCommentDimension(const FVergilGraphNode& Node, const FName PrimaryKey, const FName SecondaryKey, const float DefaultValue)
	{
		float ParsedValue = 0.0f;
		const FString PrimaryValue = Node.Metadata.FindRef(PrimaryKey).TrimStartAndEnd();
		if (!PrimaryValue.IsEmpty() && LexTryParseString(ParsedValue, *PrimaryValue) && ParsedValue > 0.0f)
		{
			return ParsedValue;
		}

		const FString SecondaryValue = Node.Metadata.FindRef(SecondaryKey).TrimStartAndEnd();
		if (!SecondaryValue.IsEmpty() && LexTryParseString(ParsedValue, *SecondaryValue) && ParsedValue > 0.0f)
		{
			return ParsedValue;
		}

		return DefaultValue;
	}

	void BuildPrimaryAutoLayoutPositions(
		const TArray<const FVergilGraphNode*>& PrimaryNodes,
		const TArray<FVergilGraphEdge>& Edges,
		const FVergilAutoLayoutSettings& Settings,
		TMap<FGuid, FVector2D>& OutPositions)
	{
		OutPositions.Reset();
		if (PrimaryNodes.Num() == 0)
		{
			return;
		}

		TArray<const FVergilGraphNode*> SortedPrimaryNodes = PrimaryNodes;
		SortedPrimaryNodes.Sort([](const FVergilGraphNode& A, const FVergilGraphNode& B)
		{
			return IsAutoLayoutNodeLess(A, B);
		});

		TArray<FVergilAutoLayoutNodeState> NodeStates;
		NodeStates.Reserve(SortedPrimaryNodes.Num());

		TMap<FGuid, int32> NodeIndices;
		for (int32 NodeIndex = 0; NodeIndex < SortedPrimaryNodes.Num(); ++NodeIndex)
		{
			FVergilAutoLayoutNodeState& NodeState = NodeStates.AddDefaulted_GetRef();
			NodeState.Node = SortedPrimaryNodes[NodeIndex];
			NodeIndices.Add(SortedPrimaryNodes[NodeIndex]->Id, NodeIndex);
		}

		for (const FVergilGraphEdge& Edge : Edges)
		{
			const int32* const SourceIndex = NodeIndices.Find(Edge.SourceNodeId);
			const int32* const TargetIndex = NodeIndices.Find(Edge.TargetNodeId);
			if (SourceIndex == nullptr || TargetIndex == nullptr || *SourceIndex == *TargetIndex)
			{
				continue;
			}

			NodeStates[*SourceIndex].Outgoing.Add(*TargetIndex);
			NodeStates[*TargetIndex].Incoming.Add(*SourceIndex);
		}

		TArray<int32> RemainingIndegree;
		RemainingIndegree.Reserve(NodeStates.Num());
		for (const FVergilAutoLayoutNodeState& NodeState : NodeStates)
		{
			RemainingIndegree.Add(NodeState.Incoming.Num());
		}

		int32 PlacedCount = 0;
		while (PlacedCount < NodeStates.Num())
		{
			int32 SelectedIndex = INDEX_NONE;
			for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
			{
				if (!NodeStates[NodeIndex].bPlaced && RemainingIndegree[NodeIndex] == 0)
				{
					SelectedIndex = NodeIndex;
					break;
				}
			}

			if (SelectedIndex == INDEX_NONE)
			{
				for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
				{
					if (!NodeStates[NodeIndex].bPlaced)
					{
						SelectedIndex = NodeIndex;
						break;
					}
				}
			}

			check(SelectedIndex != INDEX_NONE);

			int32 Column = 0;
			for (const int32 IncomingIndex : NodeStates[SelectedIndex].Incoming)
			{
				if (NodeStates[IncomingIndex].bPlaced)
				{
					Column = FMath::Max(Column, NodeStates[IncomingIndex].Column + 1);
				}
			}

			NodeStates[SelectedIndex].Column = Column;
			NodeStates[SelectedIndex].bPlaced = true;
			++PlacedCount;

			for (const int32 OutgoingIndex : NodeStates[SelectedIndex].Outgoing)
			{
				if (!NodeStates[OutgoingIndex].bPlaced && RemainingIndegree[OutgoingIndex] > 0)
				{
					--RemainingIndegree[OutgoingIndex];
				}
			}
		}

		int32 MaxColumn = 0;
		for (const FVergilAutoLayoutNodeState& NodeState : NodeStates)
		{
			MaxColumn = FMath::Max(MaxColumn, NodeState.Column);
		}

		TArray<TArray<int32>> ColumnBuckets;
		ColumnBuckets.SetNum(MaxColumn + 1);
		for (int32 NodeIndex = 0; NodeIndex < NodeStates.Num(); ++NodeIndex)
		{
			ColumnBuckets[NodeStates[NodeIndex].Column].Add(NodeIndex);
		}

		const float HorizontalSpacing = SanitizeLayoutSpacing(Settings.HorizontalSpacing, 64.0f);
		const float VerticalSpacing = SanitizeLayoutSpacing(Settings.VerticalSpacing, 64.0f);

		for (int32 ColumnIndex = 0; ColumnIndex < ColumnBuckets.Num(); ++ColumnIndex)
		{
			TArray<int32>& Bucket = ColumnBuckets[ColumnIndex];
			Bucket.Sort([&NodeStates](const int32 A, const int32 B)
			{
				double AverageIncomingRowA = 0.0;
				double AverageIncomingRowB = 0.0;
				int32 IncomingRowCountA = 0;
				int32 IncomingRowCountB = 0;

				for (const int32 IncomingIndex : NodeStates[A].Incoming)
				{
					if (NodeStates[IncomingIndex].Column < NodeStates[A].Column)
					{
						AverageIncomingRowA += NodeStates[IncomingIndex].Row;
						++IncomingRowCountA;
					}
				}

				for (const int32 IncomingIndex : NodeStates[B].Incoming)
				{
					if (NodeStates[IncomingIndex].Column < NodeStates[B].Column)
					{
						AverageIncomingRowB += NodeStates[IncomingIndex].Row;
						++IncomingRowCountB;
					}
				}

				if (IncomingRowCountA > 0)
				{
					AverageIncomingRowA /= IncomingRowCountA;
				}

				if (IncomingRowCountB > 0)
				{
					AverageIncomingRowB /= IncomingRowCountB;
				}

				if ((IncomingRowCountA > 0) != (IncomingRowCountB > 0))
				{
					return IncomingRowCountA > 0;
				}

				if (IncomingRowCountA > 0 && !FMath::IsNearlyEqual(AverageIncomingRowA, AverageIncomingRowB))
				{
					return AverageIncomingRowA < AverageIncomingRowB;
				}

				return IsAutoLayoutNodeLess(*NodeStates[A].Node, *NodeStates[B].Node);
			});

			for (int32 RowIndex = 0; RowIndex < Bucket.Num(); ++RowIndex)
			{
				FVergilAutoLayoutNodeState& NodeState = NodeStates[Bucket[RowIndex]];
				NodeState.Row = RowIndex;
				OutPositions.Add(
					NodeState.Node->Id,
					FVector2D(
						Settings.Origin.X + (ColumnIndex * HorizontalSpacing),
						Settings.Origin.Y + (RowIndex * VerticalSpacing)));
			}
		}
	}

	void BuildCommentAutoLayoutPositions(
		const TArray<const FVergilGraphNode*>& CommentNodes,
		const FVergilAutoLayoutSettings& Settings,
		const FVergilCommentGenerationSettings& CommentSettings,
		const TMap<FGuid, FVector2D>& PrimaryPositions,
		TMap<FGuid, FVector2D>& OutPositions)
	{
		if (CommentNodes.Num() == 0)
		{
			return;
		}

		float AnchorX = Settings.Origin.X;
		float AnchorY = Settings.Origin.Y;
		if (PrimaryPositions.Num() > 0)
		{
			bool bFirstPrimaryPosition = true;
			for (const TPair<FGuid, FVector2D>& PositionEntry : PrimaryPositions)
			{
				if (bFirstPrimaryPosition)
				{
					AnchorX = PositionEntry.Value.X;
					AnchorY = PositionEntry.Value.Y;
					bFirstPrimaryPosition = false;
					continue;
				}

				AnchorX = FMath::Min(AnchorX, PositionEntry.Value.X);
				AnchorY = FMath::Min(AnchorY, PositionEntry.Value.Y);
			}
		}

		TArray<const FVergilGraphNode*> SortedComments = CommentNodes;
		SortedComments.Sort([](const FVergilGraphNode& A, const FVergilGraphNode& B)
		{
			return IsAutoLayoutNodeLess(A, B);
		});

		const float CommentPadding = SanitizeLayoutSpacing(Settings.CommentPadding, 16.0f);
		float CurrentY = AnchorY;
		for (const FVergilGraphNode* CommentNode : SortedComments)
		{
			check(CommentNode != nullptr);

			const float CommentWidth = GetCommentDimension(
				*CommentNode,
				TEXT("CommentWidth"),
				TEXT("NodeWidth"),
				FMath::Max(CommentSettings.DefaultWidth, 16.0f));
			const float CommentHeight = GetCommentDimension(
				*CommentNode,
				TEXT("CommentHeight"),
				TEXT("NodeHeight"),
				FMath::Max(CommentSettings.DefaultHeight, 16.0f));
			const float CommentX = PrimaryPositions.Num() > 0
				? (AnchorX - CommentWidth - CommentPadding)
				: Settings.Origin.X;

			OutPositions.Add(CommentNode->Id, FVector2D(CommentX, CurrentY));
			CurrentY += CommentHeight + CommentPadding;
		}
	}

	void AddAutoLayoutMoveCommands(
		const TArray<const FVergilGraphNode*>& Nodes,
		const TMap<FGuid, FVector2D>& PlannedPositions,
		const FName GraphName,
		FVergilCompilerContext& Context)
	{
		for (const FVergilGraphNode* Node : Nodes)
		{
			check(Node != nullptr);

			const FVector2D* const PlannedPosition = PlannedPositions.Find(Node->Id);
			if (PlannedPosition == nullptr || Node->Position.Equals(*PlannedPosition, KINDA_SMALL_NUMBER))
			{
				continue;
			}

			FVergilCompilerCommand MoveCommand;
			MoveCommand.Type = EVergilCommandType::MoveNode;
			MoveCommand.GraphName = GraphName;
			MoveCommand.NodeId = Node->Id;
			MoveCommand.Position = *PlannedPosition;
			Context.AddPostLayoutCommand(MoveCommand);
		}
	}

	bool BuildPostLayoutCommands(const FVergilCompileRequest& Request, FVergilCompilerContext& Context)
	{
		Context.ResetPostLayoutCommands();
		if (!Request.bAutoLayout)
		{
			return true;
		}

		const TArray<FVergilGraphNode>& TargetNodes = GetTargetGraphNodes(Context);
		const TArray<FVergilGraphEdge>& TargetEdges = GetTargetGraphEdges(Context);

		TArray<const FVergilGraphNode*> PrimaryNodes;
		TArray<const FVergilGraphNode*> CommentNodes;
		PrimaryNodes.Reserve(TargetNodes.Num());
		CommentNodes.Reserve(TargetNodes.Num());

		for (const FVergilGraphNode& Node : TargetNodes)
		{
			if (IsCommentNode(Node))
			{
				if (Request.bGenerateComments)
				{
					CommentNodes.Add(&Node);
				}
				continue;
			}

			PrimaryNodes.Add(&Node);
		}

		TMap<FGuid, FVector2D> PlannedPositions;
		BuildPrimaryAutoLayoutPositions(PrimaryNodes, TargetEdges, Request.AutoLayout, PlannedPositions);
		BuildCommentAutoLayoutPositions(CommentNodes, Request.AutoLayout, Request.CommentGeneration, PlannedPositions, PlannedPositions);
		AddAutoLayoutMoveCommands(PrimaryNodes, PlannedPositions, Context.GetGraphName(), Context);
		AddAutoLayoutMoveCommands(CommentNodes, PlannedPositions, Context.GetGraphName(), Context);

		return true;
	}

	bool ValidateConnectionLegality(FVergilCompilerContext& Context)
	{
		const TArray<FVergilGraphNode>& TargetNodes = GetTargetGraphNodes(Context);
		const TArray<FVergilGraphEdge>& TargetEdges = GetTargetGraphEdges(Context);

		TMap<FGuid, const FVergilGraphNode*> AuthoredNodesById;
		AuthoredNodesById.Reserve(TargetNodes.Num());
		for (const FVergilGraphNode& Node : TargetNodes)
		{
			AuthoredNodesById.Add(Node.Id, &Node);
		}

		TMap<FGuid, const FVergilCompilerCommand*> LoweredNodesById;
		TMap<FGuid, FCompilerPlannedPinInfo> LoweredPinsById;
		for (const FVergilCompilerCommand& Command : Context.GetLoweredNodeCommands())
		{
			if (Command.Type != EVergilCommandType::AddNode || !Command.NodeId.IsValid())
			{
				continue;
			}

			LoweredNodesById.Add(Command.NodeId, &Command);

			const FName GraphName = ResolveCompilerCommandGraphName(Command);
			for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
			{
				if (!PlannedPin.PinId.IsValid())
				{
					continue;
				}

				FCompilerPlannedPinInfo PinInfo;
				PinInfo.NodeId = Command.NodeId;
				PinInfo.GraphName = GraphName;
				PinInfo.Name = PlannedPin.Name;
				PinInfo.bIsInput = PlannedPin.bIsInput;
				PinInfo.bIsExec = PlannedPin.bIsExec;
				LoweredPinsById.Add(PlannedPin.PinId, PinInfo);
			}
		}

		bool bIsValid = true;
		TMap<FGuid, FGuid> FirstIncomingEdgeByTargetPin;
		auto AddConnectionDiagnostic = [&](const FName Code, const FString& Message, const FGuid& SourceId)
		{
			bIsValid = false;
			Context.AddDiagnostic(EVergilDiagnosticSeverity::Error, Code, Message, SourceId);
		};

		for (const FVergilGraphEdge& Edge : TargetEdges)
		{
			const FVergilGraphNode* const SourceNode = AuthoredNodesById.FindRef(Edge.SourceNodeId);
			const FVergilGraphNode* const TargetNode = AuthoredNodesById.FindRef(Edge.TargetNodeId);
			const FString SourceNodeLabel = DescribeGraphNode(SourceNode);
			const FString TargetNodeLabel = DescribeGraphNode(TargetNode);

			const bool bHasLoweredSourceNode = LoweredNodesById.Contains(Edge.SourceNodeId);
			if (!bHasLoweredSourceNode)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionSourceNodeNotLowered"),
					FString::Printf(
						TEXT("Connection legality pass could not find a lowered AddNode command for source node '%s'."),
						*SourceNodeLabel),
					Edge.SourceNodeId);
			}

			const bool bHasLoweredTargetNode = LoweredNodesById.Contains(Edge.TargetNodeId);
			if (!bHasLoweredTargetNode)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetNodeNotLowered"),
					FString::Printf(
						TEXT("Connection legality pass could not find a lowered AddNode command for target node '%s'."),
						*TargetNodeLabel),
					Edge.TargetNodeId);
			}

			const FCompilerPlannedPinInfo* const SourcePinInfo = LoweredPinsById.Find(Edge.SourcePinId);
			if (bHasLoweredSourceNode && SourcePinInfo == nullptr)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionSourcePinNotLowered"),
					FString::Printf(
						TEXT("Connection legality pass could not find source pin '%s' in the lowered output for node '%s'."),
						*Edge.SourcePinId.ToString(),
						*SourceNodeLabel),
					Edge.SourceNodeId);
			}

			const FCompilerPlannedPinInfo* const TargetPinInfo = LoweredPinsById.Find(Edge.TargetPinId);
			if (bHasLoweredTargetNode && TargetPinInfo == nullptr)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetPinNotLowered"),
					FString::Printf(
						TEXT("Connection legality pass could not find target pin '%s' in the lowered output for node '%s'."),
						*Edge.TargetPinId.ToString(),
						*TargetNodeLabel),
					Edge.TargetNodeId);
			}

			if (SourcePinInfo == nullptr || TargetPinInfo == nullptr)
			{
				continue;
			}

			if (SourcePinInfo->NodeId != Edge.SourceNodeId)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionSourcePinOwnerMismatch"),
					FString::Printf(
						TEXT("Connection legality pass resolved source pin '%s' to lowered node '%s', not declared node '%s'."),
						*DescribePlannedPin(SourcePinInfo, Edge.SourcePinId),
						*SourcePinInfo->NodeId.ToString(),
						*Edge.SourceNodeId.ToString()),
					Edge.SourceNodeId);
			}

			if (TargetPinInfo->NodeId != Edge.TargetNodeId)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetPinOwnerMismatch"),
					FString::Printf(
						TEXT("Connection legality pass resolved target pin '%s' to lowered node '%s', not declared node '%s'."),
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetPinInfo->NodeId.ToString(),
						*Edge.TargetNodeId.ToString()),
					Edge.TargetNodeId);
			}

			if (SourcePinInfo->GraphName != TargetPinInfo->GraphName)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionPinGraphMismatch"),
					FString::Printf(
						TEXT("Connection legality pass rejected connection between source pin '%s' on graph '%s' and target pin '%s' on graph '%s'."),
						*DescribePlannedPin(SourcePinInfo, Edge.SourcePinId),
						*SourcePinInfo->GraphName.ToString(),
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetPinInfo->GraphName.ToString()),
					Edge.SourceNodeId);
			}

			if (SourcePinInfo->GraphName != Context.GetGraphName())
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionSourceGraphMismatch"),
					FString::Printf(
						TEXT("Connection legality pass rejected source pin '%s' on node '%s' because it lowers into graph '%s' instead of compile target '%s'."),
						*DescribePlannedPin(SourcePinInfo, Edge.SourcePinId),
						*SourceNodeLabel,
						*SourcePinInfo->GraphName.ToString(),
						*Context.GetGraphName().ToString()),
					Edge.SourceNodeId);
			}

			if (TargetPinInfo->GraphName != Context.GetGraphName())
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetGraphMismatch"),
					FString::Printf(
						TEXT("Connection legality pass rejected target pin '%s' on node '%s' because it lowers into graph '%s' instead of compile target '%s'."),
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetNodeLabel,
						*TargetPinInfo->GraphName.ToString(),
						*Context.GetGraphName().ToString()),
					Edge.TargetNodeId);
			}

			if (SourcePinInfo->bIsInput)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionSourcePinDirectionInvalid"),
					FString::Printf(
						TEXT("Connection legality pass rejected source pin '%s' on node '%s' because it is an input pin."),
						*DescribePlannedPin(SourcePinInfo, Edge.SourcePinId),
						*SourceNodeLabel),
					Edge.SourceNodeId);
			}

			if (!TargetPinInfo->bIsInput)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetPinDirectionInvalid"),
					FString::Printf(
						TEXT("Connection legality pass rejected target pin '%s' on node '%s' because it is an output pin."),
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetNodeLabel),
					Edge.TargetNodeId);
			}

			if (SourcePinInfo->bIsExec != TargetPinInfo->bIsExec)
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionExecMismatch"),
					FString::Printf(
						TEXT("Connection legality pass rejected source pin '%s' on node '%s' and target pin '%s' on node '%s' because exec and data pins cannot be mixed."),
						*DescribePlannedPin(SourcePinInfo, Edge.SourcePinId),
						*SourceNodeLabel,
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetNodeLabel),
					Edge.SourceNodeId);
			}

			if (!TargetPinInfo->bIsInput)
			{
				continue;
			}

			if (const FGuid* const FirstEdgeId = FirstIncomingEdgeByTargetPin.Find(Edge.TargetPinId))
			{
				AddConnectionDiagnostic(
					TEXT("ConnectionTargetPinMultiplyDriven"),
					FString::Printf(
						TEXT("Connection legality pass rejected target pin '%s' on node '%s' because input pins may only have one incoming edge (already driven by edge '%s')."),
						*DescribePlannedPin(TargetPinInfo, Edge.TargetPinId),
						*TargetNodeLabel,
						*FirstEdgeId->ToString()),
					Edge.TargetNodeId);
			}
			else
			{
				FirstIncomingEdgeByTargetPin.Add(Edge.TargetPinId, Edge.Id);
			}
		}

		for (const FVergilGraphNode& Node : TargetNodes)
		{
			if (Node.Descriptor != TEXT("K2.SpawnActor"))
			{
				continue;
			}

			const FVergilCompilerCommand* const LoweredSpawnActor = LoweredNodesById.FindRef(Node.Id);
			if (LoweredSpawnActor == nullptr)
			{
				continue;
			}

			const FVergilPlannedPin* const SpawnTransformPin = LoweredSpawnActor->PlannedPins.FindByPredicate([](const FVergilPlannedPin& PlannedPin)
			{
				return PlannedPin.Name == SpawnActorTransformPinName
					&& PlannedPin.bIsInput
					&& !PlannedPin.bIsExec;
			});

			if (SpawnTransformPin == nullptr || !SpawnTransformPin->PinId.IsValid())
			{
				AddConnectionDiagnostic(
					TEXT("SpawnActorTransformPinMissing"),
					TEXT("Connection legality pass requires K2.SpawnActor nodes to author a valid input pin named 'SpawnTransform'."),
					Node.Id);
				continue;
			}

			if (!FirstIncomingEdgeByTargetPin.Contains(SpawnTransformPin->PinId))
			{
				AddConnectionDiagnostic(
					TEXT("SpawnActorTransformConnectionMissing"),
					TEXT("Connection legality pass requires K2.SpawnActor nodes to drive 'SpawnTransform' with a data edge because UE_5.7 UK2Node_SpawnActorFromClass expands into by-reference transform calls."),
					Node.Id);
			}
		}

		return bIsValid;
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

FName FVergilSymbolResolutionPass::GetPassName() const
{
	return TEXT("SymbolResolution");
}

bool FVergilSymbolResolutionPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	FVergilGraphDocument WorkingDocument = Context.GetDocument();
	TArray<FVergilGraphNode>& TargetNodes = GetMutableTargetGraphNodes(WorkingDocument, Context.GetGraphName());
	const FVergilDocumentSymbolTables SymbolTables = BuildDocumentSymbolTables(Context);

	bool bIsValid = true;
	bool bMadeChanges = false;
	for (FVergilGraphNode& Node : TargetNodes)
	{
		bIsValid &= ResolveNodeSymbols(Node, SymbolTables, Context, bMadeChanges);
	}

	if (bMadeChanges)
	{
		Context.SetWorkingDocument(MoveTemp(WorkingDocument));
	}

	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilTypeResolutionPass::GetPassName() const
{
	return TEXT("TypeResolution");
}

bool FVergilTypeResolutionPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	FVergilGraphDocument WorkingDocument = Context.GetDocument();
	TArray<FVergilGraphNode>& TargetNodes = GetMutableTargetGraphNodes(WorkingDocument, Context.GetGraphName());

	bool bMadeChanges = false;
	bool bIsValid = ResolveDocumentTypeMetadata(WorkingDocument, Context, bMadeChanges);
	for (FVergilGraphNode& Node : TargetNodes)
	{
		bIsValid &= ResolveNodeTypes(Node, Context, bMadeChanges);
	}

	if (bMadeChanges)
	{
		Context.SetWorkingDocument(MoveTemp(WorkingDocument));
	}

	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilNodeLoweringPass::GetPassName() const
{
	return TEXT("NodeLowering");
}

bool FVergilNodeLoweringPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	EnsureGenericFallbackHandler();
	Context.ResetLoweredNodeCommands();
	Context.ResetPostCompileFinalizeCommands();
	Context.ResetPostCommentCommands();
	Context.ResetPostLayoutCommands();

	bool bIsValid = true;
	for (const FVergilGraphNode& Node : GetTargetGraphNodes(Context))
	{
		if (IsCommentNode(Node))
		{
			continue;
		}

		bIsValid &= LowerNode(Node, Context);
	}

	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilPostCompileFinalizePass::GetPassName() const
{
	return TEXT("PostCompileFinalize");
}

bool FVergilPostCompileFinalizePass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	const bool bIsValid = BuildPostCompileFinalizeCommands(Context);
	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilCommentPostPass::GetPassName() const
{
	return TEXT("CommentPostPass");
}

bool FVergilCommentPostPass::Run(
	const FVergilCompileRequest& Request,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	const bool bIsValid = BuildPostCommentCommands(Request, Context);
	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilLayoutPostPass::GetPassName() const
{
	return TEXT("LayoutPostPass");
}

bool FVergilLayoutPostPass::Run(
	const FVergilCompileRequest& Request,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	const bool bIsValid = BuildPostLayoutCommands(Request, Context);
	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilConnectionLegalityPass::GetPassName() const
{
	return TEXT("ConnectionLegality");
}

bool FVergilConnectionLegalityPass::Run(
	const FVergilCompileRequest& /*Request*/,
	FVergilCompilerContext& Context,
	FVergilCompileResult& Result) const
{
	const bool bIsValid = ValidateConnectionLegality(Context);
	return bIsValid && !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}

FName FVergilCommandPlanningPass::GetPassName() const
{
	return TEXT("CommandPlanning");
}

bool FVergilCommandPlanningPass::Run(const FVergilCompileRequest& /*Request*/, FVergilCompilerContext& Context, FVergilCompileResult& Result) const
{
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

	const TArray<FVergilGraphEdge>& TargetEdges = GetTargetGraphEdges(Context);

	for (const FVergilCompilerCommand& LoweredNodeCommand : Context.GetLoweredNodeCommands())
	{
		Result.Commands.Add(LoweredNodeCommand);
	}

	for (const FVergilCompilerCommand& CommentCommand : Context.GetPostCommentCommands())
	{
		Result.Commands.Add(CommentCommand);
	}

	for (const FVergilCompilerCommand& LayoutCommand : Context.GetPostLayoutCommands())
	{
		Result.Commands.Add(LayoutCommand);
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

	for (const FVergilCompilerCommand& FinalizeCommand : Context.GetPostCompileFinalizeCommands())
	{
		Result.Commands.Add(FinalizeCommand);
	}

	return !Algo::AnyOf(Result.Diagnostics, [](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	});
}
