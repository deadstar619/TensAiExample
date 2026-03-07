#include "VergilCommandExecutor.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddComponentByClass.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_ConvertAsset.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GetClassDefaults.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_LoadAsset.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Message.h"
#include "K2Node_Tunnel.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "VergilLog.h"

namespace
{
	const FName ConstructionScriptGraphName = UEdGraphSchema_K2::FN_UserConstructionScript;
	const FString StandardMacrosBlueprintPath(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
	const FName AddComponentClassPinName(TEXT("Class"));
	const FName AddComponentRelativeTransformPinName(TEXT("RelativeTransform"));
	const FName AddComponentManualAttachmentPinName(TEXT("bManualAttachment"));
	const FName ComponentLookupClassPinName(TEXT("ComponentClass"));
	const FName ComponentLookupTagPinName(TEXT("Tag"));
	const FName ActorGetComponentByClassFunctionName(TEXT("GetComponentByClass"));
	const FName ActorFindComponentByTagFunctionName(TEXT("FindComponentByTag"));
	const FName SpawnActorClassPinName(TEXT("Class"));
	const FName SpawnActorWorldContextPinName(TEXT("WorldContextObject"));
	const FName SpawnActorTransformPinName(TEXT("SpawnTransform"));
	const FName SpawnActorCollisionHandlingOverridePinName(TEXT("CollisionHandlingOverride"));
	const FName SpawnActorTransformScaleMethodPinName(TEXT("TransformScaleMethod"));
	const FName SpawnActorOwnerPinName(TEXT("Owner"));
	const FName GetClassDefaultsClassPinName(TEXT("Class"));
	const FName ConvertAssetInputPinName(TEXT("Input"));
	const FName ConvertAssetOutputPinName(TEXT("Output"));

	struct FVergilExecutionState
	{
		TMap<FName, UEdGraph*> GraphsByName;
		TMap<FGuid, UEdGraphNode*> NodesById;
		TMap<FGuid, UEdGraphPin*> PinsById;
		TSet<FGuid> RemovedNodeIds;
	};

	struct FVergilSignaturePinPlan
	{
		FName Name = NAME_None;
		FEdGraphPinType Type;
	};

	struct FVergilStandardMacroCommand
	{
		FName CommandName;
		FName DefaultMacroGraphName;
		FName NotFoundDiagnosticCode;
	};

	struct FVergilSupportedPinDescriptor
	{
		FName Name = NAME_None;
		bool bIsInput = true;
		bool bIsExec = false;
	};

	struct FVergilSpawnActorPinDescriptor
	{
		FName Name = NAME_None;
		bool bIsInput = true;
		bool bIsExec = false;
	};

	struct FVergilComponentLookupCommand
	{
		FName CommandName;
		FName FunctionName;
		bool bSupportsTag = false;
	};

	struct FVergilInterfaceInvocationCommand
	{
		FName CommandName;
		bool bIsMessage = false;
	};

	struct FVergilLoadAssetCommand
	{
		FName CommandName;
		FName InputPinName;
		FName OutputPinName;
		bool bIsArray = false;
		bool bIsClassAsset = false;
	};

	const FVergilStandardMacroCommand* FindStandardMacroCommand(const FName CommandName)
	{
		static const FVergilStandardMacroCommand Commands[] =
		{
			{ TEXT("Vergil.K2.ForLoop"), TEXT("ForLoop"), TEXT("ForLoopMacroNotFound") },
			{ TEXT("Vergil.K2.ForLoopWithBreak"), TEXT("ForLoopWithBreak"), TEXT("ForLoopWithBreakMacroNotFound") },
			{ TEXT("Vergil.K2.DoOnce"), TEXT("DoOnce"), TEXT("DoOnceMacroNotFound") },
			{ TEXT("Vergil.K2.FlipFlop"), TEXT("FlipFlop"), TEXT("FlipFlopMacroNotFound") },
			{ TEXT("Vergil.K2.Gate"), TEXT("Gate"), TEXT("GateMacroNotFound") },
			{ TEXT("Vergil.K2.WhileLoop"), TEXT("WhileLoop"), TEXT("WhileLoopMacroNotFound") },
		};

		for (const FVergilStandardMacroCommand& Candidate : Commands)
		{
			if (Candidate.CommandName == CommandName)
			{
				return &Candidate;
			}
		}

		return nullptr;
	}

	const FVergilComponentLookupCommand* FindComponentLookupCommand(const FName CommandName)
	{
		static const FVergilComponentLookupCommand Commands[] =
		{
			{ TEXT("Vergil.K2.GetComponentByClass"), ActorGetComponentByClassFunctionName, false },
			{ TEXT("Vergil.K2.GetComponentsByClass"), GET_FUNCTION_NAME_CHECKED(AActor, K2_GetComponentsByClass), false },
			{ TEXT("Vergil.K2.FindComponentByTag"), ActorFindComponentByTagFunctionName, true },
			{ TEXT("Vergil.K2.GetComponentsByTag"), GET_FUNCTION_NAME_CHECKED(AActor, GetComponentsByTag), true },
		};

		for (const FVergilComponentLookupCommand& Candidate : Commands)
		{
			if (Candidate.CommandName == CommandName)
			{
				return &Candidate;
			}
		}

		return nullptr;
	}

	const FVergilInterfaceInvocationCommand* FindInterfaceInvocationCommand(const FName CommandName)
	{
		static const FVergilInterfaceInvocationCommand Commands[] =
		{
			{ TEXT("Vergil.K2.InterfaceCall"), false },
			{ TEXT("Vergil.K2.InterfaceMessage"), true },
		};

		for (const FVergilInterfaceInvocationCommand& Candidate : Commands)
		{
			if (Candidate.CommandName == CommandName)
			{
				return &Candidate;
			}
		}

		return nullptr;
	}

	const FVergilLoadAssetCommand* FindLoadAssetCommand(const FName CommandName)
	{
		static const FVergilLoadAssetCommand Commands[] =
		{
			{ TEXT("Vergil.K2.LoadAsset"), TEXT("Asset"), TEXT("Object"), false, false },
			{ TEXT("Vergil.K2.LoadAssetClass"), TEXT("AssetClass"), TEXT("Class"), false, true },
			{ TEXT("Vergil.K2.LoadAssets"), TEXT("Assets"), TEXT("Objects"), true, false },
		};

		for (const FVergilLoadAssetCommand& Candidate : Commands)
		{
			if (Candidate.CommandName == CommandName)
			{
				return &Candidate;
			}
		}

		return nullptr;
	}

	bool ExecuteStandardMacroNode(
		const FVergilStandardMacroCommand& MacroCommand,
		UEdGraph* Graph,
		const FVergilCompilerCommand& Command,
		UEdGraphNode*& OutNewNode,
		TArray<FVergilDiagnostic>& Diagnostics);

	UClass* ResolveClassReference(const FString& Reference);

	FName ResolveCommandGraphName(const FVergilCompilerCommand& Command)
	{
		if ((Command.Type == EVergilCommandType::EnsureFunctionGraph || Command.Type == EVergilCommandType::EnsureMacroGraph)
			&& !Command.SecondaryName.IsNone()
			&& (Command.GraphName.IsNone() || Command.GraphName == TEXT("EventGraph")))
		{
			return Command.SecondaryName;
		}

		if (!Command.GraphName.IsNone())
		{
			return Command.GraphName;
		}

		if ((Command.Type == EVergilCommandType::EnsureFunctionGraph
			|| Command.Type == EVergilCommandType::EnsureMacroGraph
			|| Command.Type == EVergilCommandType::EnsureGraph)
			&& !Command.SecondaryName.IsNone())
		{
			return Command.SecondaryName;
		}

		return FName(TEXT("EventGraph"));
	}

	UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FName GraphName)
	{
		if (Blueprint == nullptr)
		{
			return nullptr;
		}

		if (GraphName == TEXT("EventGraph"))
		{
			if (UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint))
			{
				return EventGraph;
			}
		}
		else if (GraphName == ConstructionScriptGraphName)
		{
			if (UEdGraph* ConstructionScriptGraph = FBlueprintEditorUtils::FindUserConstructionScript(Blueprint))
			{
				return ConstructionScriptGraph;
			}
		}

		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph != nullptr && Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}

		return nullptr;
	}

	UEdGraphNode* FindGraphNodeByGuid(UEdGraph* Graph, const FGuid& NodeId)
	{
		if (Graph == nullptr || !NodeId.IsValid())
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node != nullptr && Node->NodeGuid == NodeId)
			{
				return Node;
			}
		}

		return nullptr;
	}

	UEdGraph* FindDelegateGraph(UBlueprint* Blueprint, const FName DispatcherName)
	{
		if (Blueprint == nullptr || DispatcherName.IsNone())
		{
			return nullptr;
		}

		return FBlueprintEditorUtils::GetDelegateSignatureGraphByName(Blueprint, DispatcherName);
	}

	UK2Node_FunctionEntry* FindFunctionEntryNode(UEdGraph* Graph)
	{
		if (Graph == nullptr)
		{
			return nullptr;
		}

		if (UK2Node_FunctionEntry* const EntryNode = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)))
		{
			return EntryNode;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* const EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				return EntryNode;
			}
		}

		return nullptr;
	}

	UK2Node_EditablePinBase* FindEditableGraphEntryNode(UEdGraph* Graph)
	{
		TWeakObjectPtr<UK2Node_EditablePinBase> EntryNode;
		TWeakObjectPtr<UK2Node_EditablePinBase> ResultNode;
		FBlueprintEditorUtils::GetEntryAndResultNodes(Graph, EntryNode, ResultNode);
		return EntryNode.Get();
	}

	UK2Node_FunctionResult* FindFunctionResultNode(UEdGraph* Graph)
	{
		if (Graph == nullptr)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				return ResultNode;
			}
		}

		return nullptr;
	}

	UK2Node_EditablePinBase* FindEditableGraphResultNode(UEdGraph* Graph)
	{
		TWeakObjectPtr<UK2Node_EditablePinBase> EntryNode;
		TWeakObjectPtr<UK2Node_EditablePinBase> ResultNode;
		FBlueprintEditorUtils::GetEntryAndResultNodes(Graph, EntryNode, ResultNode);
		return ResultNode.Get();
	}

	bool HasBlueprintMemberVariable(UBlueprint* Blueprint, const FName VariableName)
	{
		if (Blueprint == nullptr || VariableName.IsNone())
		{
			return false;
		}

		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName == VariableName)
			{
				return true;
			}
		}

		return false;
	}

	FBPVariableDescription* FindBlueprintMemberVariable(UBlueprint* Blueprint, const FName VariableName)
	{
		if (Blueprint == nullptr || VariableName.IsNone())
		{
			return nullptr;
		}

		for (FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName == VariableName)
			{
				return &Variable;
			}
		}

		return nullptr;
	}

	const FBPVariableDescription* FindBlueprintMemberVariable(const UBlueprint* Blueprint, const FName VariableName)
	{
		if (Blueprint == nullptr || VariableName.IsNone())
		{
			return nullptr;
		}

		for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
		{
			if (Variable.VarName == VariableName)
			{
				return &Variable;
			}
		}

		return nullptr;
	}

	USCS_Node* FindComponentNode(UBlueprint* Blueprint, const FName ComponentName)
	{
		if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr || ComponentName.IsNone())
		{
			return nullptr;
		}

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node != nullptr && Node->GetVariableName() == ComponentName)
			{
				return Node;
			}
		}

		return nullptr;
	}

	FString CanonicalizeLookupName(FString Value, const bool bStripBoolPrefix)
	{
		Value.ReplaceInline(TEXT("_"), TEXT(""));
		Value.ReplaceInline(TEXT(" "), TEXT(""));
		if (bStripBoolPrefix && Value.Len() > 1 && Value[0] == TCHAR('b') && FChar::IsUpper(Value[1]))
		{
			Value.RightChopInline(1, EAllowShrinking::No);
		}

		return Value.ToLower();
	}

	bool IsEquivalentLookupName(const FName RequestedName, const FName CandidateName)
	{
		if (RequestedName == CandidateName)
		{
			return true;
		}

		const FString RequestedRaw = RequestedName.ToString();
		const FString CandidateRaw = CandidateName.ToString();
		if (RequestedRaw.Equals(CandidateRaw, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString RequestedCanonical = CanonicalizeLookupName(RequestedRaw, false);
		const FString CandidateCanonical = CanonicalizeLookupName(CandidateRaw, false);
		if (RequestedCanonical == CandidateCanonical)
		{
			return true;
		}

		return CanonicalizeLookupName(RequestedRaw, true) == CandidateCanonical
			|| RequestedCanonical == CanonicalizeLookupName(CandidateRaw, true)
			|| CanonicalizeLookupName(RequestedRaw, true) == CanonicalizeLookupName(CandidateRaw, true);
	}

	FProperty* FindPropertyFlexible(UStruct* Owner, const FName PropertyName)
	{
		if (Owner == nullptr || PropertyName.IsNone())
		{
			return nullptr;
		}

		if (FProperty* DirectProperty = FindFProperty<FProperty>(Owner, PropertyName))
		{
			return DirectProperty;
		}

		for (TFieldIterator<FProperty> It(Owner, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (FProperty* CandidateProperty = *It; CandidateProperty != nullptr && IsEquivalentLookupName(PropertyName, CandidateProperty->GetFName()))
			{
				return CandidateProperty;
			}
		}

		return nullptr;
	}

	bool ImportPropertyValue(UObject* TargetObject, FProperty* Property, const FString& SerializedValue)
	{
		if (TargetObject == nullptr || Property == nullptr)
		{
			return false;
		}

		void* const ValueAddress = Property->ContainerPtrToValuePtr<void>(TargetObject);
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == TBaseStructure<FVector>::Get())
			{
				FVector ParsedVector;
				if (ParsedVector.InitFromString(SerializedValue))
				{
					*Property->ContainerPtrToValuePtr<FVector>(TargetObject) = ParsedVector;
					return true;
				}
			}
			else if (StructProperty->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator ParsedRotator;
				if (ParsedRotator.InitFromString(SerializedValue))
				{
					*Property->ContainerPtrToValuePtr<FRotator>(TargetObject) = ParsedRotator;
					return true;
				}
			}
		}

		if (Property->ImportText_Direct(*SerializedValue, ValueAddress, TargetObject, PPF_None) != nullptr)
		{
			return true;
		}

		if (CastField<FStructProperty>(Property) != nullptr && !SerializedValue.StartsWith(TEXT("(")))
		{
			const FString WrappedImportText = FString::Printf(TEXT("(%s)"), *SerializedValue);
			return Property->ImportText_Direct(*WrappedImportText, ValueAddress, TargetObject, PPF_None) != nullptr;
		}

		return false;
	}

	UEdGraph* ResolveOrCreateGraph(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		const FName GraphName = ResolveCommandGraphName(Command);
		if (UEdGraph** ExistingGraph = State.GraphsByName.Find(GraphName))
		{
			return *ExistingGraph;
		}

		UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
		if (Graph == nullptr
			&& (Command.Type == EVergilCommandType::EnsureGraph
				|| Command.Type == EVergilCommandType::EnsureFunctionGraph
				|| Command.Type == EVergilCommandType::EnsureMacroGraph))
		{
			if (GraphName == ConstructionScriptGraphName)
			{
				FKismetEditorUtilities::CreateUserConstructionScript(Blueprint);
				Graph = FindGraphByName(Blueprint, GraphName);
			}
			else
			{
				Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
				if (Graph != nullptr)
				{
					if (Command.Type == EVergilCommandType::EnsureMacroGraph)
					{
						FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, true, static_cast<UClass*>(nullptr));
					}
					else if (Command.Type == EVergilCommandType::EnsureFunctionGraph)
					{
						FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
					}
					else if (GraphName == TEXT("EventGraph"))
					{
						FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
					}
					else
					{
						FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
					}
				}
			}
		}

		if (Graph == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("GraphResolutionFailed"),
				FString::Printf(TEXT("Unable to resolve graph '%s'."), *GraphName.ToString()),
				Command.NodeId));
			return nullptr;
		}

		State.GraphsByName.Add(GraphName, Graph);
		return Graph;
	}

	bool IsCommentAddCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::AddNode && Command.Name == TEXT("Vergil.Comment");
	}

	bool IsBlueprintDefinitionCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::EnsureDispatcher
			|| Command.Type == EVergilCommandType::AddDispatcherParameter
			|| Command.Type == EVergilCommandType::SetBlueprintMetadata
			|| Command.Type == EVergilCommandType::EnsureVariable
			|| Command.Type == EVergilCommandType::SetVariableMetadata
			|| Command.Type == EVergilCommandType::SetVariableDefault
			|| Command.Type == EVergilCommandType::EnsureFunctionGraph
			|| Command.Type == EVergilCommandType::EnsureMacroGraph
			|| Command.Type == EVergilCommandType::EnsureComponent
			|| Command.Type == EVergilCommandType::AttachComponent
			|| Command.Type == EVergilCommandType::SetComponentProperty
			|| Command.Type == EVergilCommandType::EnsureInterface
			|| Command.Type == EVergilCommandType::RenameMember;
	}

	bool IsPostBlueprintCompileCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::SetClassDefault;
	}

	bool IsPostCompileFinalizeCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::FinalizeNode;
	}

	bool IsExplicitCompileCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::CompileBlueprint;
	}

	bool IsSupportedBlueprintMetadataKey(const FName MetadataKey)
	{
		return MetadataKey == TEXT("BlueprintDisplayName")
			|| MetadataKey == TEXT("BlueprintDescription")
			|| MetadataKey == TEXT("BlueprintCategory")
			|| MetadataKey == TEXT("HideCategories");
	}

	void ParseBlueprintHideCategories(const FString& InValue, TArray<FString>& OutCategories)
	{
		OutCategories.Reset();

		const FString TrimmedValue = InValue.TrimStartAndEnd();
		if (TrimmedValue.IsEmpty())
		{
			return;
		}

		FString DelimitedValue = TrimmedValue;
		DelimitedValue.ReplaceInline(TEXT(";"), TEXT(","));
		DelimitedValue.ReplaceInline(TEXT("\r\n"), TEXT(","));
		DelimitedValue.ReplaceInline(TEXT("\n"), TEXT(","));

		TArray<FString> RawCategories;
		DelimitedValue.ParseIntoArray(RawCategories, TEXT(","), true);
		if (RawCategories.Num() == 0)
		{
			RawCategories.Add(DelimitedValue);
		}

		for (const FString& RawCategory : RawCategories)
		{
			const FString Category = RawCategory.TrimStartAndEnd();
			if (!Category.IsEmpty())
			{
				OutCategories.AddUnique(Category);
			}
		}

		OutCategories.Sort();
	}

	bool IsExecConnectionCommand(const FVergilCompilerCommand& Command, const FVergilExecutionState& State)
	{
		if (Command.Type != EVergilCommandType::ConnectPins)
		{
			return false;
		}

		const UEdGraphPin* const SourcePin = State.PinsById.FindRef(Command.SourcePinId);
		const UEdGraphPin* const TargetPin = State.PinsById.FindRef(Command.TargetPinId);
		return (SourcePin != nullptr && SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			|| (TargetPin != nullptr && TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
	}

	bool TryParseFloat(const FString& InValue, float& OutValue)
	{
		const FString TrimmedValue = InValue.TrimStartAndEnd();
		if (TrimmedValue.IsEmpty())
		{
			return false;
		}

		return LexTryParseString(OutValue, *TrimmedValue);
	}

	bool TryParseInt(const FString& InValue, int32& OutValue)
	{
		const FString TrimmedValue = InValue.TrimStartAndEnd();
		if (TrimmedValue.IsEmpty())
		{
			return false;
		}

		TCHAR* EndPtr = nullptr;
		const int64 ParsedValue = FCString::Strtoi64(*TrimmedValue, &EndPtr, 10);
		if (EndPtr == *TrimmedValue || EndPtr == nullptr || *EndPtr != '\0')
		{
			return false;
		}

		OutValue = static_cast<int32>(ParsedValue);
		return true;
	}

	FString GetCommandAttribute(const FVergilCompilerCommand& Command, const FName Key)
	{
		if (const FString* Value = Command.Attributes.Find(Key))
		{
			return *Value;
		}

		return FString();
	}

	bool IsSupportedCommandPinCategory(const FString& PinCategory)
	{
		const FString Category = PinCategory.TrimStartAndEnd().ToLower();
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

	bool CommandPinCategoryRequiresObjectPath(const FString& PinCategory)
	{
		const FString Category = PinCategory.TrimStartAndEnd().ToLower();
		return Category == TEXT("enum")
			|| Category == TEXT("object")
			|| Category == TEXT("class")
			|| Category == TEXT("struct");
	}

	bool ValidateCommandTypeShape(
		const FString& PinCategoryValue,
		const FString& ObjectPathValue,
		const FString& ContextLabel,
		const FName MissingCategoryCode,
		const FName UnsupportedCategoryCode,
		const FName MissingObjectPathCode,
		const FGuid& SourceId,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		const FString PinCategory = PinCategoryValue.TrimStartAndEnd();
		const FString ObjectPath = ObjectPathValue.TrimStartAndEnd();
		if (PinCategory.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				MissingCategoryCode,
				FString::Printf(TEXT("%s must declare a type category."), *ContextLabel),
				SourceId));
			return false;
		}

		if (!IsSupportedCommandPinCategory(PinCategory))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				UnsupportedCategoryCode,
				FString::Printf(TEXT("%s declares unsupported type category '%s'."), *ContextLabel, *PinCategory),
				SourceId));
			return false;
		}

		if (CommandPinCategoryRequiresObjectPath(PinCategory) && ObjectPath.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				MissingObjectPathCode,
				FString::Printf(TEXT("%s type category '%s' requires an object path."), *ContextLabel, *PinCategory),
				SourceId));
			return false;
		}

		return true;
	}

	bool IsSupportedSelectIndexCategory(const FString& PinCategoryValue)
	{
		const FString PinCategory = PinCategoryValue.TrimStartAndEnd().ToLower();
		return PinCategory == TEXT("bool")
			|| PinCategory == TEXT("int")
			|| PinCategory == TEXT("enum");
	}

	void BuildSpawnActorSupportedPins(const UClass* ActorClass, TArray<FVergilSpawnActorPinDescriptor>& OutPins)
	{
		OutPins.Reset();

		OutPins.Add({ UEdGraphSchema_K2::PN_Execute, true, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_Then, false, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_ReturnValue, false, false });
		OutPins.Add({ SpawnActorTransformPinName, true, false });
		OutPins.Add({ SpawnActorCollisionHandlingOverridePinName, true, false });
		OutPins.Add({ SpawnActorTransformScaleMethodPinName, true, false });
		OutPins.Add({ SpawnActorOwnerPinName, true, false });

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
				OutPins.Add({ Property->GetFName(), true, false });
			}
		}
	}

	bool ResolveSpawnActorClass(
		const FString& Reference,
		UClass*& OutActorClass,
		TArray<FVergilDiagnostic>& Diagnostics,
		const FGuid& SourceId)
	{
		const FString ActorClassPath = Reference.TrimStartAndEnd();
		if (ActorClassPath.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("SpawnActorClassMissing"),
				TEXT("Spawn actor execution requires StringValue to contain an actor class path."),
				SourceId));
			return false;
		}

		OutActorClass = ResolveClassReference(ActorClassPath);
		if (OutActorClass == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("SpawnActorClassNotFound"),
				FString::Printf(TEXT("Unable to resolve spawn actor class '%s'."), *ActorClassPath),
				SourceId));
			return false;
		}

		if (!OutActorClass->IsChildOf(AActor::StaticClass()))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("SpawnActorClassNotActor"),
				FString::Printf(TEXT("Spawn actor class '%s' must resolve to an AActor-derived class."), *OutActorClass->GetPathName()),
				SourceId));
			OutActorClass = nullptr;
			return false;
		}

		return true;
	}

	bool ValidateSpawnActorPlannedPins(
		const FVergilCompilerCommand& Command,
		const UClass* ActorClass,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		TArray<FVergilSpawnActorPinDescriptor> SupportedPins;
		BuildSpawnActorSupportedPins(ActorClass, SupportedPins);

		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.Name == SpawnActorClassPinName)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorDynamicClassPinUnsupported"),
					TEXT("Vergil.K2.SpawnActor currently uses a fixed actor class path and does not support planned Class-pin connections."),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			if (PlannedPin.Name == SpawnActorWorldContextPinName)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorWorldContextPinUnsupported"),
					TEXT("Vergil.K2.SpawnActor does not currently expose planned WorldContextObject pin connections."),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			const FVergilSpawnActorPinDescriptor* const SupportedPin = SupportedPins.FindByPredicate([&PlannedPin](const FVergilSpawnActorPinDescriptor& Candidate)
			{
				return Candidate.Name == PlannedPin.Name
					&& Candidate.bIsInput == PlannedPin.bIsInput
					&& Candidate.bIsExec == PlannedPin.bIsExec;
			});

			if (SupportedPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorPinUnsupported"),
					FString::Printf(
						TEXT("Vergil.K2.SpawnActor pin '%s' is not part of the UE_5.7 SpawnActorFromClass surface for actor class '%s'."),
						*PlannedPin.Name.ToString(),
						ActorClass != nullptr ? *ActorClass->GetPathName() : TEXT("<null>")),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool ResolveActorComponentClass(
		const FString& Reference,
		const FName MissingDiagnosticCode,
		const FName NotFoundDiagnosticCode,
		const FName InvalidDiagnosticCode,
		const TCHAR* ContextLabel,
		UClass*& OutComponentClass,
		TArray<FVergilDiagnostic>& Diagnostics,
		const FGuid& SourceId)
	{
		const FString ComponentClassPath = Reference.TrimStartAndEnd();
		if (ComponentClassPath.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				MissingDiagnosticCode,
				FString::Printf(TEXT("%s requires a component class path."), ContextLabel),
				SourceId));
			return false;
		}

		OutComponentClass = ResolveClassReference(ComponentClassPath);
		if (OutComponentClass == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				NotFoundDiagnosticCode,
				FString::Printf(TEXT("Unable to resolve %s '%s'."), ContextLabel, *ComponentClassPath),
				SourceId));
			return false;
		}

		if (!OutComponentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				InvalidDiagnosticCode,
				FString::Printf(TEXT("%s '%s' must resolve to a UActorComponent-derived class."), ContextLabel, *OutComponentClass->GetPathName()),
				SourceId));
			OutComponentClass = nullptr;
			return false;
		}

		return true;
	}

	bool ResolveNodeClassReference(
		const FString& Reference,
		const FName MissingDiagnosticCode,
		const FName NotFoundDiagnosticCode,
		const TCHAR* ContextLabel,
		UClass*& OutClass,
		TArray<FVergilDiagnostic>& Diagnostics,
		const FGuid& SourceId)
	{
		const FString ClassPath = Reference.TrimStartAndEnd();
		if (ClassPath.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				MissingDiagnosticCode,
				FString::Printf(TEXT("%s requires a class path."), ContextLabel),
				SourceId));
			return false;
		}

		OutClass = ResolveClassReference(ClassPath);
		if (OutClass == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				NotFoundDiagnosticCode,
				FString::Printf(TEXT("Unable to resolve %s '%s'."), ContextLabel, *ClassPath),
				SourceId));
			return false;
		}

		return true;
	}

	bool IsUnsafeGetClassDefaultsObjectContainerProperty(const FProperty* Property)
	{
		if (const FArrayProperty* const ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return ArrayProperty->Inner != nullptr
				&& ArrayProperty->Inner->IsA(FObjectProperty::StaticClass())
				&& !ArrayProperty->Inner->IsA(FClassProperty::StaticClass());
		}

		if (const FSetProperty* const SetProperty = CastField<FSetProperty>(Property))
		{
			return SetProperty->ElementProp != nullptr
				&& SetProperty->ElementProp->IsA(FObjectProperty::StaticClass())
				&& !SetProperty->ElementProp->IsA(FClassProperty::StaticClass());
		}

		if (const FMapProperty* const MapProperty = CastField<FMapProperty>(Property))
		{
			const bool bUnsafeKeyObject = MapProperty->KeyProp != nullptr
				&& MapProperty->KeyProp->IsA(FObjectProperty::StaticClass())
				&& !MapProperty->KeyProp->IsA(FClassProperty::StaticClass());
			const bool bUnsafeValueObject = MapProperty->ValueProp != nullptr
				&& MapProperty->ValueProp->IsA(FObjectProperty::StaticClass())
				&& !MapProperty->ValueProp->IsA(FClassProperty::StaticClass());
			return bUnsafeKeyObject || bUnsafeValueObject;
		}

		return false;
	}

	bool CanExposeGetClassDefaultsProperty(const FProperty* Property)
	{
		return Property != nullptr
			&& Property->HasAllPropertyFlags(CPF_BlueprintVisible)
			&& !Property->HasAnyPropertyFlags(CPF_Parm)
			&& !IsUnsafeGetClassDefaultsObjectContainerProperty(Property)
			&& FBlueprintEditorUtils::PropertyStillExists(const_cast<FProperty*>(Property));
	}

	bool ValidateGetClassDefaultsPlannedPins(
		const FVergilCompilerCommand& Command,
		const UClass* SourceClass,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.Name == GetClassDefaultsClassPinName)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("GetClassDefaultsDynamicClassPinUnsupported"),
					TEXT("Vergil.K2.GetClassDefaults currently uses a fixed ClassPath metadata source and does not support planned Class-pin connections."),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			if (PlannedPin.bIsInput || PlannedPin.bIsExec)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("GetClassDefaultsPinUnsupported"),
					FString::Printf(TEXT("Vergil.K2.GetClassDefaults pin '%s' is not part of the deterministic supported surface."), *PlannedPin.Name.ToString()),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			const FProperty* const Property = SourceClass != nullptr ? FindFProperty<FProperty>(SourceClass, PlannedPin.Name) : nullptr;
			if (!CanExposeGetClassDefaultsProperty(Property))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("GetClassDefaultsPinUnsupported"),
					FString::Printf(
						TEXT("Vergil.K2.GetClassDefaults pin '%s' is not an exposed class-default output on class '%s'."),
						*PlannedPin.Name.ToString(),
						SourceClass != nullptr ? *SourceClass->GetPathName() : TEXT("<null>")),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	void BuildLoadAssetSupportedPins(const FVergilLoadAssetCommand& LoadAssetCommand, TArray<FVergilSupportedPinDescriptor>& OutPins)
	{
		OutPins.Reset();
		OutPins.Add({ UEdGraphSchema_K2::PN_Execute, true, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_Then, false, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_Completed, false, true });
		OutPins.Add({ LoadAssetCommand.InputPinName, true, false });
		OutPins.Add({ LoadAssetCommand.OutputPinName, false, false });
	}

	bool ResolveLoadAssetClass(
		const FString& Reference,
		UClass*& OutAssetClass,
		TArray<FVergilDiagnostic>& Diagnostics,
		const FGuid& SourceId)
	{
		return ResolveNodeClassReference(
			Reference,
			TEXT("LoadAssetClassMissing"),
			TEXT("LoadAssetClassNotFound"),
			TEXT("load-asset node execution"),
			OutAssetClass,
			Diagnostics,
			SourceId);
	}

	bool ValidateLoadAssetPlannedPins(
		const FVergilCompilerCommand& Command,
		const FVergilLoadAssetCommand& LoadAssetCommand,
		const UClass* AssetClass,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		TArray<FVergilSupportedPinDescriptor> SupportedPins;
		BuildLoadAssetSupportedPins(LoadAssetCommand, SupportedPins);

		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			const FVergilSupportedPinDescriptor* const SupportedPin = SupportedPins.FindByPredicate([&PlannedPin](const FVergilSupportedPinDescriptor& Candidate)
			{
				return Candidate.Name == PlannedPin.Name
					&& Candidate.bIsInput == PlannedPin.bIsInput
					&& Candidate.bIsExec == PlannedPin.bIsExec;
			});

			if (SupportedPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("LoadAssetPinUnsupported"),
					FString::Printf(
						TEXT("%s pin '%s' is not part of the UE_5.7 deterministic surface for asset class '%s'."),
						*LoadAssetCommand.CommandName.ToString(),
						*PlannedPin.Name.ToString(),
						AssetClass != nullptr ? *AssetClass->GetPathName() : TEXT("<null>")),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool ValidateConvertAssetPlannedPins(const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			const bool bSupportedPin = !PlannedPin.bIsExec
				&& ((PlannedPin.bIsInput && PlannedPin.Name == ConvertAssetInputPinName)
					|| (!PlannedPin.bIsInput && PlannedPin.Name == ConvertAssetOutputPinName));
			if (!bSupportedPin)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ConvertAssetPinUnsupported"),
					FString::Printf(TEXT("Vergil.K2.ConvertAsset pin '%s' is not part of the deterministic supported surface."), *PlannedPin.Name.ToString()),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	void BuildAddComponentSupportedPins(const UClass* ComponentClass, TArray<FVergilSupportedPinDescriptor>& OutPins)
	{
		OutPins.Reset();

		OutPins.Add({ UEdGraphSchema_K2::PN_Execute, true, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_Then, false, true });
		OutPins.Add({ UEdGraphSchema_K2::PN_ReturnValue, false, false });
		OutPins.Add({ UEdGraphSchema_K2::PN_Self, true, false });

		if (ComponentClass == nullptr)
		{
			return;
		}

		if (ComponentClass->IsChildOf(USceneComponent::StaticClass()))
		{
			OutPins.Add({ AddComponentManualAttachmentPinName, true, false });
			OutPins.Add({ AddComponentRelativeTransformPinName, true, false });
		}

		for (TFieldIterator<FProperty> PropertyIt(ComponentClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
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
				OutPins.Add({ Property->GetFName(), true, false });
			}
		}
	}

	bool ValidateAddComponentPlannedPins(
		const FVergilCompilerCommand& Command,
		const UClass* ComponentClass,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		TArray<FVergilSupportedPinDescriptor> SupportedPins;
		BuildAddComponentSupportedPins(ComponentClass, SupportedPins);

		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.Name == AddComponentClassPinName)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("AddComponentDynamicClassPinUnsupported"),
					TEXT("Vergil.K2.AddComponentByClass currently uses ComponentClassPath metadata as its deterministic class source and does not support planned Class-pin connections."),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			const FVergilSupportedPinDescriptor* const SupportedPin = SupportedPins.FindByPredicate([&PlannedPin](const FVergilSupportedPinDescriptor& Candidate)
			{
				return Candidate.Name == PlannedPin.Name
					&& Candidate.bIsInput == PlannedPin.bIsInput
					&& Candidate.bIsExec == PlannedPin.bIsExec;
			});

			if (SupportedPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("AddComponentPinUnsupported"),
					FString::Printf(
						TEXT("Vergil.K2.AddComponentByClass pin '%s' is not part of the UE_5.7 AddComponentByClass surface for component class '%s'."),
						*PlannedPin.Name.ToString(),
						ComponentClass != nullptr ? *ComponentClass->GetPathName() : TEXT("<null>")),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	void BuildComponentLookupSupportedPins(const bool bSupportsTag, TArray<FVergilSupportedPinDescriptor>& OutPins)
	{
		OutPins.Reset();
		OutPins.Add({ UEdGraphSchema_K2::PN_Self, true, false });
		OutPins.Add({ UEdGraphSchema_K2::PN_ReturnValue, false, false });

		if (bSupportsTag)
		{
			OutPins.Add({ ComponentLookupTagPinName, true, false });
		}
	}

	bool ValidateComponentLookupPlannedPins(
		const FVergilCompilerCommand& Command,
		const FVergilComponentLookupCommand& LookupCommand,
		const UClass* ComponentClass,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		TArray<FVergilSupportedPinDescriptor> SupportedPins;
		BuildComponentLookupSupportedPins(LookupCommand.bSupportsTag, SupportedPins);

		bool bIsValid = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.Name == ComponentLookupClassPinName)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentLookupDynamicClassPinUnsupported"),
					FString::Printf(
						TEXT("%s currently uses ComponentClassPath metadata as its deterministic class source and does not support planned ComponentClass-pin connections."),
						*LookupCommand.CommandName.ToString()),
					Command.NodeId));
				bIsValid = false;
				continue;
			}

			const FVergilSupportedPinDescriptor* const SupportedPin = SupportedPins.FindByPredicate([&PlannedPin](const FVergilSupportedPinDescriptor& Candidate)
			{
				return Candidate.Name == PlannedPin.Name
					&& Candidate.bIsInput == PlannedPin.bIsInput
					&& Candidate.bIsExec == PlannedPin.bIsExec;
			});

			if (SupportedPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentLookupPinUnsupported"),
					FString::Printf(
						TEXT("%s pin '%s' is not part of the UE_5.7 deterministic surface for component class '%s'."),
						*LookupCommand.CommandName.ToString(),
						*PlannedPin.Name.ToString(),
						ComponentClass != nullptr ? *ComponentClass->GetPathName() : TEXT("<null>")),
					Command.NodeId));
				bIsValid = false;
			}
		}

		return bIsValid;
	}

	bool ValidateCommandVariableTypeShape(
		const FVergilCompilerCommand& Command,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		bool bIsValid = ValidateCommandTypeShape(
			GetCommandAttribute(Command, TEXT("PinCategory")),
			GetCommandAttribute(Command, TEXT("ObjectPath")),
			ContextLabel,
			TEXT("VariableTypeCategoryMissing"),
			TEXT("VariableTypeCategoryUnsupported"),
			TEXT("VariableTypeObjectPathMissing"),
			Command.NodeId,
			Diagnostics);

		const FString ContainerType = GetCommandAttribute(Command, TEXT("ContainerType")).TrimStartAndEnd().ToLower();
		if (ContainerType.IsEmpty() || ContainerType == TEXT("none"))
		{
			if (!GetCommandAttribute(Command, TEXT("ValuePinCategory")).TrimStartAndEnd().IsEmpty()
				|| !GetCommandAttribute(Command, TEXT("ValueObjectPath")).TrimStartAndEnd().IsEmpty())
			{
				bIsValid = false;
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("VariableValueTypeUnexpected"),
					FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel),
					Command.NodeId));
			}

			return bIsValid;
		}

		if (ContainerType == TEXT("array") || ContainerType == TEXT("set"))
		{
			if (!GetCommandAttribute(Command, TEXT("ValuePinCategory")).TrimStartAndEnd().IsEmpty()
				|| !GetCommandAttribute(Command, TEXT("ValueObjectPath")).TrimStartAndEnd().IsEmpty())
			{
				bIsValid = false;
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("VariableValueTypeUnexpected"),
					FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel),
					Command.NodeId));
			}

			return bIsValid;
		}

		if (ContainerType != TEXT("map"))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableContainerTypeInvalid"),
				FString::Printf(TEXT("%s uses unsupported container type '%s'."), *ContextLabel, *ContainerType),
				Command.NodeId));
			return false;
		}

		return ValidateCommandTypeShape(
				GetCommandAttribute(Command, TEXT("ValuePinCategory")),
				GetCommandAttribute(Command, TEXT("ValueObjectPath")),
				FString::Printf(TEXT("%s value type"), *ContextLabel),
				TEXT("VariableMapValueCategoryMissing"),
				TEXT("VariableTypeCategoryUnsupported"),
				TEXT("VariableTypeObjectPathMissing"),
				Command.NodeId,
				Diagnostics)
			&& bIsValid;
	}

	bool TryGetCommandCountAttribute(const FVergilCompilerCommand& Command, const TCHAR* Key, int32& OutCount, TArray<FVergilDiagnostic>& Diagnostics, const FName InvalidCode)
	{
		const FString CountValue = GetCommandAttribute(Command, Key);
		if (CountValue.IsEmpty())
		{
			OutCount = 0;
			return true;
		}

		if (!TryParseInt(CountValue, OutCount))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				InvalidCode,
				FString::Printf(TEXT("Command attribute '%s' has invalid integer value '%s'."), Key, *CountValue),
				Command.NodeId));
			return false;
		}

		return true;
	}

	FName MakeSignatureAttributeKey(const TCHAR* Prefix, const int32 Index, const TCHAR* Suffix);

	bool ValidateCommandSignatureTypeShape(
		const FVergilCompilerCommand& Command,
		const TCHAR* Prefix,
		const int32 ParameterIndex,
		const FString& ContextLabel,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		bool bIsValid = ValidateCommandTypeShape(
			GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("PinCategory"))),
			GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ObjectPath"))),
			ContextLabel,
			TEXT("FunctionParameterTypeCategoryMissing"),
			TEXT("FunctionParameterTypeCategoryUnsupported"),
			TEXT("FunctionParameterTypeObjectPathMissing"),
			Command.NodeId,
			Diagnostics);

		const FString ContainerType = GetCommandAttribute(
			Command,
			MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ContainerType"))).TrimStartAndEnd().ToLower();
		if (ContainerType.IsEmpty() || ContainerType == TEXT("none"))
		{
			if (!GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValuePinCategory"))).TrimStartAndEnd().IsEmpty()
				|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValueObjectPath"))).TrimStartAndEnd().IsEmpty())
			{
				bIsValid = false;
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterValueTypeUnexpected"),
					FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel),
					Command.NodeId));
			}

			return bIsValid;
		}

		if (ContainerType == TEXT("array") || ContainerType == TEXT("set"))
		{
			if (!GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValuePinCategory"))).TrimStartAndEnd().IsEmpty()
				|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValueObjectPath"))).TrimStartAndEnd().IsEmpty())
			{
				bIsValid = false;
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionParameterValueTypeUnexpected"),
					FString::Printf(TEXT("%s may only declare value-type fields for map containers."), *ContextLabel),
					Command.NodeId));
			}

			return bIsValid;
		}

		if (ContainerType != TEXT("map"))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionParameterContainerTypeInvalid"),
				FString::Printf(TEXT("%s uses unsupported container type '%s'."), *ContextLabel, *ContainerType),
				Command.NodeId));
			return false;
		}

		return ValidateCommandTypeShape(
				GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValuePinCategory"))),
				GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValueObjectPath"))),
				FString::Printf(TEXT("%s value type"), *ContextLabel),
				TEXT("FunctionParameterMapValueCategoryMissing"),
				TEXT("FunctionParameterTypeCategoryUnsupported"),
				TEXT("FunctionParameterTypeObjectPathMissing"),
				Command.NodeId,
				Diagnostics)
			&& bIsValid;
	}

	FName MakeSignatureAttributeKey(const TCHAR* Prefix, const int32 Index, const TCHAR* Suffix)
	{
		return *FString::Printf(TEXT("%s_%d_%s"), Prefix, Index, Suffix);
	}

	enum class EVergilVariableGetVariant : uint8
	{
		PureOnly,
		BooleanBranch,
		ValidatedObject
	};

	EVergilVariableGetVariant GetVariableGetVariantFromPinType(const FEdGraphPinType& PinType)
	{
		if (PinType.ContainerType != EPinContainerType::None)
		{
			return EVergilVariableGetVariant::PureOnly;
		}

		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			return EVergilVariableGetVariant::BooleanBranch;
		}

		if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object
			|| PinType.PinCategory == UEdGraphSchema_K2::PC_Class
			|| PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject
			|| PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			return EVergilVariableGetVariant::ValidatedObject;
		}

		return EVergilVariableGetVariant::PureOnly;
	}

	EVergilVariableGetVariant GetVariableGetVariantForProperty(const FProperty* Property)
	{
		if (Property == nullptr)
		{
			return EVergilVariableGetVariant::PureOnly;
		}

		FEdGraphPinType PinType;
		GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType);
		return GetVariableGetVariantFromPinType(PinType);
	}

	bool HasPlannedExecPins(const FVergilCompilerCommand& Command)
	{
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.bIsExec)
			{
				return true;
			}
		}

		return false;
	}

	TArray<FName> GetPlannedExecOutputPinNames(const FVergilCompilerCommand& Command, const bool bExcludeDefault = true)
	{
		TArray<FName> PinNames;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (PlannedPin.bIsInput || !PlannedPin.bIsExec || PlannedPin.Name.IsNone())
			{
				continue;
			}

			if (bExcludeDefault && PlannedPin.Name == TEXT("Default"))
			{
				continue;
			}

			PinNames.Add(PlannedPin.Name);
		}

		return PinNames;
	}

	bool ValidateImpureVariableGetPinShape(const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (!HasPlannedExecPins(Command))
		{
			return true;
		}

		bool bHasExecuteInput = false;
		bool bHasThenOutput = false;
		bool bHasElseOutput = false;
		int32 ExecPinCount = 0;
		bool bShapeValid = true;

		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (!PlannedPin.bIsExec)
			{
				continue;
			}

			++ExecPinCount;
			if (PlannedPin.bIsInput && PlannedPin.Name == UEdGraphSchema_K2::PN_Execute)
			{
				bHasExecuteInput = true;
				continue;
			}

			if (!PlannedPin.bIsInput && PlannedPin.Name == UEdGraphSchema_K2::PN_Then)
			{
				bHasThenOutput = true;
				continue;
			}

			if (!PlannedPin.bIsInput && PlannedPin.Name == UEdGraphSchema_K2::PN_Else)
			{
				bHasElseOutput = true;
				continue;
			}

			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidVariableGetVariantPins"),
				FString::Printf(
					TEXT("Impure variable getter nodes under UE_5.7 only support exec pins Execute, Then, and Else; found '%s'."),
					*PlannedPin.Name.ToString()),
				Command.NodeId));
			bShapeValid = false;
		}

		if (bHasExecuteInput && bHasThenOutput && bHasElseOutput && ExecPinCount == 3)
		{
			return bShapeValid;
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("InvalidVariableGetVariantPins"),
			TEXT("Impure variable getter nodes under UE_5.7 must expose exactly Execute input plus Then and Else exec outputs."),
			Command.NodeId));
		return false;
	}

	bool ValidateVariableGetVariantSupport(
		const FVergilCompilerCommand& Command,
		const FProperty* Property,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (!HasPlannedExecPins(Command))
		{
			return true;
		}

		if (GetVariableGetVariantForProperty(Property) != EVergilVariableGetVariant::PureOnly)
		{
			return true;
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("UnsupportedVariableGetVariant"),
			TEXT("Impure variable getters under UE_5.7 are supported only for bool branch getters and object/class/soft reference validated getters."),
			Command.NodeId));
		return false;
	}

	bool ApplyVariableGetVariantToNode(
		UK2Node_VariableGet* Node,
		const FProperty* Property,
		const FGuid& NodeId,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Node == nullptr)
		{
			return false;
		}

		const EVergilVariableGetVariant SupportedVariant = GetVariableGetVariantForProperty(Property);
		if (SupportedVariant == EVergilVariableGetVariant::PureOnly)
		{
			return true;
		}

		const EGetNodeVariation DesiredVariation = SupportedVariant == EVergilVariableGetVariant::BooleanBranch
			? EGetNodeVariation::Branch
			: EGetNodeVariation::ValidatedObject;
		FProperty* const VariationProperty = UK2Node_VariableGet::StaticClass()->FindPropertyByName(TEXT("CurrentVariation"));
		if (VariationProperty == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableGetVariantConfigurationFailed"),
				TEXT("Unable to configure the UE_5.7 variable getter variation because CurrentVariation was not found."),
				NodeId));
			return false;
		}

		Node->Modify();
		if (FEnumProperty* const EnumProperty = CastField<FEnumProperty>(VariationProperty))
		{
			void* const ValuePtr = EnumProperty->ContainerPtrToValuePtr<void>(Node);
			EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, static_cast<int64>(DesiredVariation));
		}
		else if (FByteProperty* const ByteProperty = CastField<FByteProperty>(VariationProperty))
		{
			ByteProperty->SetPropertyValue_InContainer(Node, static_cast<uint8>(DesiredVariation));
		}
		else
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableGetVariantConfigurationFailed"),
				TEXT("Unable to configure the UE_5.7 variable getter variation because CurrentVariation uses an unexpected property type."),
				NodeId));
			return false;
		}

		Node->ReconstructNode();
		return true;
	}

	UEdGraphPin* FindMatchingPin(const FVergilCompilerCommand& Command, UEdGraphNode* Node, const FVergilPlannedPin& PlannedPin)
	{
		if (Node == nullptr || PlannedPin.Name.IsNone())
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin == nullptr)
			{
				continue;
			}

			if (Pin->Direction != (PlannedPin.bIsInput ? EGPD_Input : EGPD_Output))
			{
				continue;
			}

			if (Pin->PinName == PlannedPin.Name)
			{
				return Pin;
			}
		}

		if ((Command.Name == TEXT("Vergil.K2.DoOnce") || Command.Name == TEXT("Vergil.K2.Gate")) && PlannedPin.Name == TEXT("StartClosed"))
		{
			const FName EnginePinName = Command.Name == TEXT("Vergil.K2.Gate")
				? FName(TEXT("bStartClosed"))
				: FName(TEXT("Start Closed"));

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr
					&& Pin->Direction == (PlannedPin.bIsInput ? EGPD_Input : EGPD_Output)
					&& Pin->PinName == EnginePinName)
				{
					return Pin;
				}
			}
		}

		if (Command.Name == TEXT("Vergil.K2.Gate")
			&& PlannedPin.bIsInput
			&& PlannedPin.bIsExec
			&& (PlannedPin.Name == UEdGraphSchema_K2::PN_Execute || PlannedPin.Name == TEXT("Execute") || PlannedPin.Name == TEXT("Exec")))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr
					&& Pin->Direction == EGPD_Input
					&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
					&& Pin->PinName == TEXT("Enter"))
				{
					return Pin;
				}
			}
		}

		if (Command.Name == TEXT("Vergil.K2.FlipFlop")
			&& PlannedPin.bIsInput
			&& PlannedPin.bIsExec
			&& (PlannedPin.Name == UEdGraphSchema_K2::PN_Execute || PlannedPin.Name == TEXT("Execute") || PlannedPin.Name == TEXT("Exec")))
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin != nullptr
					&& Pin->Direction == EGPD_Input
					&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
					&& Pin->PinName.IsNone())
				{
					return Pin;
				}
			}
		}

		const FString StructPath = GetCommandAttribute(Command, TEXT("StructPath"));
		const EEdGraphPinDirection PlannedDirection = PlannedPin.bIsInput ? EGPD_Input : EGPD_Output;
		if (!StructPath.IsEmpty() && Node->IsA<UK2Node_CallFunction>())
		{
			if (Command.Name == TEXT("Vergil.K2.MakeStruct") && !PlannedPin.bIsInput && !PlannedPin.bIsExec)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin != nullptr
						&& Pin->Direction == PlannedDirection
						&& Pin->PinName == UEdGraphSchema_K2::PN_ReturnValue
						&& Pin->PinType.PinSubCategoryObject.IsValid()
						&& Pin->PinType.PinSubCategoryObject->GetPathName() == StructPath)
					{
						return Pin;
					}
				}
			}
			else if (Command.Name == TEXT("Vergil.K2.BreakStruct") && PlannedPin.bIsInput && !PlannedPin.bIsExec)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin != nullptr
						&& Pin->Direction == PlannedDirection
						&& Pin->PinType.PinSubCategoryObject.IsValid()
						&& Pin->PinType.PinSubCategoryObject->GetPathName() == StructPath)
					{
						return Pin;
					}
				}
			}
		}

		return nullptr;
	}

	bool RegisterPlannedPins(
		const FVergilCompilerCommand& Command,
		UEdGraphNode* Node,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Node == nullptr)
		{
			return false;
		}

		bool bAllPinsResolved = true;
		for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
		{
			if (!PlannedPin.PinId.IsValid())
			{
				continue;
			}

			UEdGraphPin* MatchedPin = FindMatchingPin(Command, Node, PlannedPin);
			if (MatchedPin == nullptr)
			{
				TArray<FString> AvailablePins;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin == nullptr)
					{
						continue;
					}

					const TCHAR* DirectionLabel = Pin->Direction == EGPD_Input ? TEXT("In") : TEXT("Out");
					AvailablePins.Add(FString::Printf(TEXT("%s:%s"), DirectionLabel, *Pin->PinName.ToString()));
				}

				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("PinRegistrationFailed"),
					FString::Printf(
						TEXT("Node '%s' does not expose an exact pin named '%s'. Available pins: [%s]."),
						*Command.Name.ToString(),
						*PlannedPin.Name.ToString(),
						*FString::Join(AvailablePins, TEXT(", "))),
					Command.NodeId));
				bAllPinsResolved = false;
				continue;
			}

			State.PinsById.Add(PlannedPin.PinId, MatchedPin);
		}

		return bAllPinsResolved;
	}

	FString DescribeRawPinTypeForDiagnostic(
		const FName Category,
		const FName SubCategory,
		const TWeakObjectPtr<UObject>& SubCategoryObject)
	{
		if (Category == UEdGraphSchema_K2::PC_Exec)
		{
			return TEXT("exec");
		}

		if ((Category == UEdGraphSchema_K2::PC_Byte || Category == TEXT("enum"))
			&& SubCategoryObject.IsValid()
			&& SubCategoryObject->IsA<UEnum>())
		{
			return FString::Printf(TEXT("enum '%s'"), *SubCategoryObject->GetPathName());
		}

		if ((Category == UEdGraphSchema_K2::PC_Object
			|| Category == UEdGraphSchema_K2::PC_Class
			|| Category == UEdGraphSchema_K2::PC_Struct
			|| Category == TEXT("enum"))
			&& SubCategoryObject.IsValid())
		{
			return FString::Printf(TEXT("%s '%s'"), *Category.ToString(), *SubCategoryObject->GetPathName());
		}

		if (!Category.IsNone())
		{
			FString Description = Category.ToString();
			if (!SubCategory.IsNone())
			{
				Description += FString::Printf(TEXT(":%s"), *SubCategory.ToString());
			}
			return Description;
		}

		return TEXT("unknown");
	}

	FString DescribePinTypeForDiagnostic(const FEdGraphPinType& PinType)
	{
		const FString BaseType = DescribeRawPinTypeForDiagnostic(
			PinType.PinCategory,
			PinType.PinSubCategory,
			PinType.PinSubCategoryObject);

		if (PinType.ContainerType == EPinContainerType::Array)
		{
			return FString::Printf(TEXT("array<%s>"), *BaseType);
		}

		if (PinType.ContainerType == EPinContainerType::Set)
		{
			return FString::Printf(TEXT("set<%s>"), *BaseType);
		}

		if (PinType.ContainerType == EPinContainerType::Map)
		{
			const FString ValueType = DescribeRawPinTypeForDiagnostic(
				PinType.PinValueType.TerminalCategory,
				PinType.PinValueType.TerminalSubCategory,
				PinType.PinValueType.TerminalSubCategoryObject);
			return FString::Printf(TEXT("map<%s, %s>"), *BaseType, *ValueType);
		}

		return BaseType;
	}

	FString DescribePinForDiagnostic(const UEdGraphPin* Pin)
	{
		if (Pin == nullptr)
		{
			return TEXT("unknown pin");
		}

		FString NodeTitle;
		if (const UEdGraphNode* const OwningNode = Pin->GetOwningNode())
		{
			NodeTitle = OwningNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (NodeTitle.IsEmpty())
			{
				NodeTitle = OwningNode->GetClass()->GetName();
			}
		}
		else
		{
			NodeTitle = TEXT("UnknownNode");
		}

		return FString::Printf(TEXT("pin '%s' on node '%s'"), *Pin->PinName.ToString(), *NodeTitle);
	}

	bool IsSelectOptionPin(const UK2Node_Select* SelectNode, const UEdGraphPin* Pin)
	{
		if (SelectNode == nullptr || Pin == nullptr)
		{
			return false;
		}

		TArray<UEdGraphPin*> OptionPins;
		SelectNode->GetOptionPins(OptionPins);
		return OptionPins.Contains(const_cast<UEdGraphPin*>(Pin));
	}

	FString BuildPinTypeMismatchDiagnosticMessage(
		const TCHAR* ContextLabel,
		const UEdGraphPin* ConstrainedPin,
		const UEdGraphPin* ConnectedPin)
	{
		return FString::Printf(
			TEXT("%s %s expects type %s, but %s has type %s."),
			ContextLabel,
			*DescribePinForDiagnostic(ConstrainedPin),
			*DescribePinTypeForDiagnostic(ConstrainedPin->PinType),
			*DescribePinForDiagnostic(ConnectedPin),
			*DescribePinTypeForDiagnostic(ConnectedPin->PinType));
	}

	bool TryBuildSelectSwitchTypeMismatchDiagnostic(
		const UEdGraphPin* SourcePin,
		const UEdGraphPin* TargetPin,
		FName& OutCode,
		FString& OutMessage)
	{
		if (SourcePin == nullptr
			|| TargetPin == nullptr
			|| SourcePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
			|| TargetPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			return false;
		}

		if (const UK2Node_SwitchInteger* const SwitchNode = Cast<UK2Node_SwitchInteger>(TargetPin->GetOwningNode()))
		{
			if (TargetPin == SwitchNode->GetSelectionPin() && TargetPin->PinType != SourcePin->PinType)
			{
				OutCode = TEXT("UnsupportedSwitchIntSelectionTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Switch int selection"), TargetPin, SourcePin);
				return true;
			}
		}

		if (const UK2Node_SwitchString* const SwitchNode = Cast<UK2Node_SwitchString>(TargetPin->GetOwningNode()))
		{
			if (TargetPin == SwitchNode->GetSelectionPin() && TargetPin->PinType != SourcePin->PinType)
			{
				OutCode = TEXT("UnsupportedSwitchStringSelectionTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Switch string selection"), TargetPin, SourcePin);
				return true;
			}
		}

		if (const UK2Node_SwitchEnum* const SwitchNode = Cast<UK2Node_SwitchEnum>(TargetPin->GetOwningNode()))
		{
			if (TargetPin == SwitchNode->GetSelectionPin() && TargetPin->PinType != SourcePin->PinType)
			{
				OutCode = TEXT("UnsupportedSwitchEnumSelectionTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Switch enum selection"), TargetPin, SourcePin);
				return true;
			}
		}

		if (const UK2Node_Select* const SelectNode = Cast<UK2Node_Select>(TargetPin->GetOwningNode()))
		{
			if (TargetPin == SelectNode->GetIndexPin() && TargetPin->PinType != SourcePin->PinType)
			{
				OutCode = TEXT("UnsupportedSelectIndexTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Select index"), TargetPin, SourcePin);
				return true;
			}

			if (IsSelectOptionPin(SelectNode, TargetPin) && TargetPin->PinType != SourcePin->PinType)
			{
				OutCode = TEXT("UnsupportedSelectValueTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Select value"), TargetPin, SourcePin);
				return true;
			}
		}

		if (const UK2Node_Select* const SelectNode = Cast<UK2Node_Select>(SourcePin->GetOwningNode()))
		{
			if (SourcePin == SelectNode->GetReturnValuePin() && SourcePin->PinType != TargetPin->PinType)
			{
				OutCode = TEXT("UnsupportedSelectValueTypeCombination");
				OutMessage = BuildPinTypeMismatchDiagnosticMessage(TEXT("Select value"), SourcePin, TargetPin);
				return true;
			}
		}

		return false;
	}

	bool RefreshRegisteredPins(
		UBlueprint* Blueprint,
		const TArray<FVergilCompilerCommand>& Commands,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		State.NodesById.Reset();
		State.PinsById.Reset();

		bool bAllPinsResolved = true;
		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.Type != EVergilCommandType::AddNode)
			{
				continue;
			}

			if (State.RemovedNodeIds.Contains(Command.NodeId))
			{
				continue;
			}

			UEdGraph* Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
			if (Graph == nullptr)
			{
				bAllPinsResolved = false;
				continue;
			}

			UEdGraphNode* Node = nullptr;
			Node = FindGraphNodeByGuid(Graph, Command.NodeId);

			if (Node == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("NodeRegistrationFailed"),
					FString::Printf(TEXT("Unable to resolve node '%s' by guid in graph '%s'."), *Command.Name.ToString(), *Graph->GetName()),
					Command.NodeId));
				bAllPinsResolved = false;
				continue;
			}

			State.NodesById.Add(Command.NodeId, Node);
			bAllPinsResolved &= RegisterPlannedPins(Command, Node, State, Diagnostics);
		}

		return bAllPinsResolved;
	}

	void FinalizePlacedNode(UEdGraph* Graph, UEdGraphNode* Node, const FVector2D& Position, const FGuid& DesiredNodeGuid = FGuid())
	{
		Node->SetFlags(RF_Transactional);
		if (DesiredNodeGuid.IsValid())
		{
			Node->NodeGuid = DesiredNodeGuid;
		}
		else
		{
			Node->CreateNewGuid();
		}
		Node->AllocateDefaultPins();
		Node->PostPlacedNewNode();
		Node->NodePosX = FMath::RoundToInt(Position.X);
		Node->NodePosY = FMath::RoundToInt(Position.Y);
		Graph->AddNode(Node, false, false);
	}

	UFunction* ResolveCallFunction(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingFunctionName"),
				TEXT("Call node execution requires SecondaryName to contain the function name."),
				Command.NodeId));
			return nullptr;
		}

		auto ResolveOwnerClass = [&Blueprint](const FString& Reference) -> UClass*
		{
			if (Reference.IsEmpty())
			{
				return nullptr;
			}

			if (UClass* DirectClass = FindObject<UClass>(nullptr, *Reference))
			{
				return DirectClass;
			}

			if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Reference))
			{
				return LoadedClass;
			}

			if (UClass* StaticLoadedClass = LoadClass<UObject>(nullptr, *Reference))
			{
				return StaticLoadedClass;
			}

			return nullptr;
		};

		if (!Command.StringValue.IsEmpty())
		{
			if (UClass* const OwnerClass = ResolveOwnerClass(Command.StringValue))
			{
				if (UFunction* const Func = OwnerClass->FindFunctionByName(Command.SecondaryName))
				{
					return Func;
				}

				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionNotFound"),
					FString::Printf(TEXT("Unable to resolve function '%s' on class '%s'."), *Command.SecondaryName.ToString(), *OwnerClass->GetName()),
					Command.NodeId));
				return nullptr;
			}
			if (UFunction* DirectFunction = FindObject<UFunction>(nullptr, *Command.StringValue))
			{
				return DirectFunction;
			}

			if (UFunction* LoadedFunction = LoadObject<UFunction>(nullptr, *Command.StringValue))
			{
				return LoadedFunction;
			}

			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingFunctionOwner"),
				FString::Printf(TEXT("Unable to resolve owner class '%s' for function '%s'."), *Command.StringValue, *Command.SecondaryName.ToString()),
				Command.NodeId));
			return nullptr;
		}

		TArray<UClass*> SearchClasses;
		auto AddSearchClass = [&SearchClasses](UClass* CandidateClass)
		{
			if (CandidateClass != nullptr)
			{
				SearchClasses.AddUnique(CandidateClass);
			}
		};

		if (Blueprint != nullptr)
		{
			AddSearchClass(Blueprint->SkeletonGeneratedClass);
			AddSearchClass(Blueprint->GeneratedClass);
			AddSearchClass(Blueprint->ParentClass);
		}

		for (UClass* SearchClass : SearchClasses)
		{
			if (UFunction* const Func = SearchClass->FindFunctionByName(Command.SecondaryName))
			{
				return Func;
			}
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("FunctionNotFound"),
			FString::Printf(TEXT("Unable to resolve function '%s' on the target Blueprint or its parent class."), *Command.SecondaryName.ToString()),
			Command.NodeId));
		return nullptr;
	}

	UFunction* ResolveInterfaceFunction(const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingFunctionName"),
				TEXT("Interface node execution requires SecondaryName to contain the function name."),
				Command.NodeId));
			return nullptr;
		}

		const FString InterfaceClassPath = (Command.StringValue.IsEmpty()
			? GetCommandAttribute(Command, TEXT("InterfaceClassPath"))
			: Command.StringValue).TrimStartAndEnd();
		if (InterfaceClassPath.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingInterfaceClass"),
				FString::Printf(TEXT("Interface node '%s' requires StringValue or InterfaceClassPath metadata to name the interface owner."), *Command.Name.ToString()),
				Command.NodeId));
			return nullptr;
		}

		UClass* const InterfaceClass = ResolveClassReference(InterfaceClassPath);
		if (InterfaceClass == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingInterfaceClass"),
				FString::Printf(TEXT("Unable to resolve interface class '%s' for function '%s'."), *InterfaceClassPath, *Command.SecondaryName.ToString()),
				Command.NodeId));
			return nullptr;
		}

		if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidInterfaceClass"),
				FString::Printf(TEXT("Interface node '%s' requires interface owner '%s' to resolve to a UInterface-derived class."), *Command.Name.ToString(), *InterfaceClassPath),
				Command.NodeId));
			return nullptr;
		}

		UFunction* const InterfaceFunction = InterfaceClass->FindFunctionByName(Command.SecondaryName);
		if (InterfaceFunction == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InterfaceFunctionNotFound"),
				FString::Printf(TEXT("Unable to resolve interface function '%s' on class '%s'."), *Command.SecondaryName.ToString(), *InterfaceClass->GetName()),
				Command.NodeId));
			return nullptr;
		}

		const UClass* const OwnerClass = InterfaceFunction->GetOwnerClass();
		if (OwnerClass == nullptr || !OwnerClass->HasAnyClassFlags(CLASS_Interface))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InterfaceFunctionOwnerInvalid"),
				FString::Printf(TEXT("Resolved interface function '%s' did not report an interface owner class."), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return nullptr;
		}

		return InterfaceFunction;
	}

	UObject* ResolveObjectReference(const FString& Reference)
	{
		if (Reference.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* DirectObject = FindObject<UObject>(nullptr, *Reference))
		{
			return DirectObject;
		}

		return LoadObject<UObject>(nullptr, *Reference);
	}

	UClass* ResolveClassReference(const FString& Reference)
	{
		if (Reference.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* DirectClass = FindObject<UClass>(nullptr, *Reference))
		{
			return DirectClass;
		}

		if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Reference))
		{
			return LoadedClass;
		}

		if (UClass* StaticLoadedClass = LoadClass<UObject>(nullptr, *Reference))
		{
			return StaticLoadedClass;
		}

		return nullptr;
	}

	UScriptStruct* ResolveStructReference(const FString& Reference)
	{
		return Cast<UScriptStruct>(ResolveObjectReference(Reference));
	}

	UEdGraph* ResolveMacroGraphReference(const FString& BlueprintPath, const FName GraphName)
	{
		if (BlueprintPath.IsEmpty() || GraphName.IsNone())
		{
			return nullptr;
		}

		UBlueprint* const MacroBlueprint = Cast<UBlueprint>(ResolveObjectReference(BlueprintPath));
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

	bool SetObjectPropertyByName(UObject* Object, const FName PropertyName, UObject* Value)
	{
		if (Object == nullptr)
		{
			return false;
		}

		FObjectPropertyBase* const Property = FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName);
		if (Property == nullptr)
		{
			return false;
		}

		Property->SetObjectPropertyValue_InContainer(Object, Value);
		return true;
	}

	bool FinalizePlacedStructNode(
		UEdGraph* Graph,
		UEdGraphNode* Node,
		UScriptStruct* StructType,
		const FVector2D& Position,
		const FGuid& DesiredNodeGuid = FGuid())
	{
		if (Graph == nullptr || Node == nullptr || StructType == nullptr)
		{
			return false;
		}

		if (!SetObjectPropertyByName(Node, TEXT("StructType"), StructType))
		{
			return false;
		}

		Node->SetFlags(RF_Transactional);
		if (DesiredNodeGuid.IsValid())
		{
			Node->NodeGuid = DesiredNodeGuid;
		}
		else
		{
			Node->CreateNewGuid();
		}
		Node->AllocateDefaultPins();
		Node->PostPlacedNewNode();
		Node->NodePosX = FMath::RoundToInt(Position.X);
		Node->NodePosY = FMath::RoundToInt(Position.Y);
		Graph->AddNode(Node, false, false);
		return true;
	}

	bool BuildPinTypeFromAttributes(
		const FVergilCompilerCommand& Command,
		const FName CategoryKey,
		const FName ObjectPathKey,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		const FString Category = GetCommandAttribute(Command, CategoryKey).ToLower();
		const FString ObjectPath = GetCommandAttribute(Command, ObjectPathKey);
		OutPinType = FEdGraphPinType();

		if (Category.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Missing required attribute '%s'."), *CategoryKey.ToString());
			return false;
		}

		if (Category == TEXT("bool"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}

		if (Category == TEXT("int"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}

		if (Category == TEXT("float"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
			return true;
		}

		if (Category == TEXT("double"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}

		if (Category == TEXT("string"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}

		if (Category == TEXT("name"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}

		if (Category == TEXT("text"))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}

		if (Category == TEXT("enum"))
		{
			if (UEnum* Enum = Cast<UEnum>(ResolveObjectReference(ObjectPath)))
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
				OutPinType.PinSubCategoryObject = Enum;
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve enum object '%s'."), *ObjectPath);
			return false;
		}

		if (Category == TEXT("object"))
		{
			if (UClass* Class = ResolveClassReference(ObjectPath))
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				OutPinType.PinSubCategoryObject = Class;
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve object class '%s'."), *ObjectPath);
			return false;
		}

		if (Category == TEXT("class"))
		{
			if (UClass* Class = ResolveClassReference(ObjectPath))
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
				OutPinType.PinSubCategoryObject = Class;
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve class type '%s'."), *ObjectPath);
			return false;
		}

		if (Category == TEXT("struct"))
		{
			if (UScriptStruct* Struct = Cast<UScriptStruct>(ResolveObjectReference(ObjectPath)))
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				OutPinType.PinSubCategoryObject = Struct;
				return true;
			}

			OutError = FString::Printf(TEXT("Unable to resolve struct '%s'."), *ObjectPath);
			return false;
		}

		OutError = FString::Printf(TEXT("Unsupported pin category '%s'."), *Category);
		return false;
	}

	bool BuildPinTypeFromAttributes(
		const FVergilCompilerCommand& Command,
		const TCHAR* CategoryKey,
		const TCHAR* ObjectPathKey,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		return BuildPinTypeFromAttributes(Command, FName(CategoryKey), FName(ObjectPathKey), OutPinType, OutError);
	}

	bool TryParseBoolAttribute(const FVergilCompilerCommand& Command, const FName Key, bool& OutValue)
	{
		const FString Value = GetCommandAttribute(Command, Key).ToLower();
		if (Value.IsEmpty())
		{
			return false;
		}

		if (Value == TEXT("true") || Value == TEXT("1"))
		{
			OutValue = true;
			return true;
		}

		if (Value == TEXT("false") || Value == TEXT("0"))
		{
			OutValue = false;
			return true;
		}

		return false;
	}

	bool TryParseBoolString(const FString& Value, bool& OutValue)
	{
		const FString LowerValue = Value.TrimStartAndEnd().ToLower();
		if (LowerValue.IsEmpty())
		{
			return false;
		}

		if (LowerValue == TEXT("true") || LowerValue == TEXT("1"))
		{
			OutValue = true;
			return true;
		}

		if (LowerValue == TEXT("false") || LowerValue == TEXT("0"))
		{
			OutValue = false;
			return true;
		}

		return false;
	}

	bool TryParseCommentMoveMode(const FString& Value, ECommentBoxMode::Type& OutValue)
	{
		const FString NormalizedValue = Value.TrimStartAndEnd();
		if (NormalizedValue.IsEmpty())
		{
			return false;
		}

		if (NormalizedValue.Equals(TEXT("GroupMovement"), ESearchCase::IgnoreCase))
		{
			OutValue = ECommentBoxMode::GroupMovement;
			return true;
		}

		if (NormalizedValue.Equals(TEXT("NoGroupMovement"), ESearchCase::IgnoreCase)
			|| NormalizedValue.Equals(TEXT("Comment"), ESearchCase::IgnoreCase))
		{
			OutValue = ECommentBoxMode::NoGroupMovement;
			return true;
		}

		return false;
	}

	bool BuildVariablePinTypeFromCommand(
		const FVergilCompilerCommand& Command,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		if (!BuildPinTypeFromAttributes(Command, TEXT("PinCategory"), TEXT("ObjectPath"), OutPinType, OutError))
		{
			return false;
		}

		const FString ContainerType = GetCommandAttribute(Command, TEXT("ContainerType")).ToLower();
		if (ContainerType.IsEmpty() || ContainerType == TEXT("none"))
		{
			return true;
		}

		if (ContainerType == TEXT("array"))
		{
			OutPinType.ContainerType = EPinContainerType::Array;
			return true;
		}

		if (ContainerType == TEXT("set"))
		{
			OutPinType.ContainerType = EPinContainerType::Set;
			return true;
		}

		if (ContainerType == TEXT("map"))
		{
			FEdGraphPinType ValuePinType;
			if (!BuildPinTypeFromAttributes(Command, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), ValuePinType, OutError))
			{
				return false;
			}

			OutPinType.ContainerType = EPinContainerType::Map;
			OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(ValuePinType);
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported container type '%s'."), *ContainerType);
		return false;
	}

	bool BuildVariablePinTypeFromSignatureAttributes(
		const FVergilCompilerCommand& Command,
		const TCHAR* Prefix,
		const int32 ParameterIndex,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		if (!BuildPinTypeFromAttributes(
				Command,
				MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("PinCategory")),
				MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ObjectPath")),
				OutPinType,
				OutError))
		{
			return false;
		}

		const FString ContainerType = GetCommandAttribute(
			Command,
			MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ContainerType"))).ToLower();
		if (ContainerType.IsEmpty() || ContainerType == TEXT("none"))
		{
			return true;
		}

		if (ContainerType == TEXT("array"))
		{
			OutPinType.ContainerType = EPinContainerType::Array;
			return true;
		}

		if (ContainerType == TEXT("set"))
		{
			OutPinType.ContainerType = EPinContainerType::Set;
			return true;
		}

		if (ContainerType == TEXT("map"))
		{
			FEdGraphPinType ValuePinType;
			if (!BuildPinTypeFromAttributes(
					Command,
					MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValuePinCategory")),
					MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("ValueObjectPath")),
					ValuePinType,
					OutError))
			{
				return false;
			}

			OutPinType.ContainerType = EPinContainerType::Map;
			OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(ValuePinType);
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported container type '%s'."), *ContainerType);
		return false;
	}

	bool HasFunctionSignatureAttributes(const FVergilCompilerCommand& Command)
	{
		return Command.Attributes.Contains(TEXT("bPure"))
			|| Command.Attributes.Contains(TEXT("AccessSpecifier"))
			|| Command.Attributes.Contains(TEXT("InputCount"))
			|| Command.Attributes.Contains(TEXT("OutputCount"));
	}

	bool HasMacroSignatureAttributes(const FVergilCompilerCommand& Command)
	{
		return Command.Attributes.Contains(TEXT("InputCount"))
			|| Command.Attributes.Contains(TEXT("OutputCount"));
	}

	bool TryGetFunctionParameterPlans(
		const FVergilCompilerCommand& Command,
		const TCHAR* Prefix,
		TArray<FVergilSignaturePinPlan>& OutParameters,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		OutParameters.Reset();

		int32 ParameterCount = 0;
		const FString CountValue = GetCommandAttribute(Command, *FString::Printf(TEXT("%sCount"), Prefix));
		if (!CountValue.IsEmpty() && !TryParseInt(CountValue, ParameterCount))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionSignatureCountInvalid"),
				FString::Printf(TEXT("Function graph '%s' has an invalid %s count '%s'."), *ResolveCommandGraphName(Command).ToString(), Prefix, *CountValue),
				Command.NodeId));
			return false;
		}

		if (ParameterCount < 0)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionSignatureCountNegative"),
				FString::Printf(TEXT("Function graph '%s' cannot declare a negative %s count."), *ResolveCommandGraphName(Command).ToString(), Prefix),
				Command.NodeId));
			return false;
		}

		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FVergilSignaturePinPlan Parameter;
			Parameter.Name = *GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("Name")));
			if (Parameter.Name.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionSignatureParameterNameMissing"),
					FString::Printf(TEXT("Function graph '%s' %s parameter %d is missing a name."), *ResolveCommandGraphName(Command).ToString(), Prefix, ParameterIndex),
					Command.NodeId));
				return false;
			}

			FString TypeError;
			if (!BuildVariablePinTypeFromSignatureAttributes(Command, Prefix, ParameterIndex, Parameter.Type, TypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionSignatureParameterTypeInvalid"),
					FString::Printf(TEXT("Function graph '%s' parameter '%s' is invalid: %s"), *ResolveCommandGraphName(Command).ToString(), *Parameter.Name.ToString(), *TypeError),
					Command.NodeId));
				return false;
			}

			OutParameters.Add(Parameter);
		}

		return true;
	}

	bool TryGetMacroPinPlans(
		const FVergilCompilerCommand& Command,
		const TCHAR* Prefix,
		TArray<FVergilSignaturePinPlan>& OutParameters,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		OutParameters.Reset();

		int32 ParameterCount = 0;
		const FString CountValue = GetCommandAttribute(Command, *FString::Printf(TEXT("%sCount"), Prefix));
		if (!CountValue.IsEmpty() && !TryParseInt(CountValue, ParameterCount))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroSignatureCountInvalid"),
				FString::Printf(TEXT("Macro graph '%s' has an invalid %s count '%s'."), *ResolveCommandGraphName(Command).ToString(), Prefix, *CountValue),
				Command.NodeId));
			return false;
		}

		if (ParameterCount < 0)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroSignatureCountNegative"),
				FString::Printf(TEXT("Macro graph '%s' cannot declare a negative %s count."), *ResolveCommandGraphName(Command).ToString(), Prefix),
				Command.NodeId));
			return false;
		}

		for (int32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FVergilSignaturePinPlan Parameter;
			Parameter.Name = *GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("Name")));
			if (Parameter.Name.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroSignatureParameterNameMissing"),
					FString::Printf(TEXT("Macro graph '%s' %s parameter %d is missing a name."), *ResolveCommandGraphName(Command).ToString(), Prefix, ParameterIndex),
					Command.NodeId));
				return false;
			}

			bool bIsExec = false;
			if (TryParseBoolAttribute(Command, MakeSignatureAttributeKey(Prefix, ParameterIndex, TEXT("bExec")), bIsExec) && bIsExec)
			{
				Parameter.Type.PinCategory = UEdGraphSchema_K2::PC_Exec;
				OutParameters.Add(Parameter);
				continue;
			}

			FString TypeError;
			if (!BuildVariablePinTypeFromSignatureAttributes(Command, Prefix, ParameterIndex, Parameter.Type, TypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MacroSignatureParameterTypeInvalid"),
					FString::Printf(TEXT("Macro graph '%s' parameter '%s' is invalid: %s"), *ResolveCommandGraphName(Command).ToString(), *Parameter.Name.ToString(), *TypeError),
					Command.NodeId));
				return false;
			}

			OutParameters.Add(Parameter);
		}

		return true;
	}

	bool EditablePinsMatch(
		UK2Node_EditablePinBase* Node,
		const TArray<FVergilSignaturePinPlan>& DesiredPins,
		const EEdGraphPinDirection DesiredDirection)
	{
		if (Node == nullptr || Node->UserDefinedPins.Num() != DesiredPins.Num())
		{
			return false;
		}

		for (int32 PinIndex = 0; PinIndex < DesiredPins.Num(); ++PinIndex)
		{
			const TSharedPtr<FUserPinInfo>& UserPin = Node->UserDefinedPins[PinIndex];
			if (!UserPin.IsValid()
				|| UserPin->DesiredPinDirection != DesiredDirection
				|| UserPin->PinName != DesiredPins[PinIndex].Name
				|| !(UserPin->PinType == DesiredPins[PinIndex].Type))
			{
				return false;
			}
		}

		return true;
	}

	bool RebuildEditablePins(
		UK2Node_EditablePinBase* Node,
		const TArray<FVergilSignaturePinPlan>& DesiredPins,
		const EEdGraphPinDirection DesiredDirection,
		const FName FailureCode,
		const TCHAR* GraphKind,
		const TCHAR* PinKind,
		const FVergilCompilerCommand& Command,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Node == nullptr)
		{
			return false;
		}

		if (EditablePinsMatch(Node, DesiredPins, DesiredDirection))
		{
			return true;
		}

		Node->Modify();
		for (int32 PinIndex = Node->UserDefinedPins.Num() - 1; PinIndex >= 0; --PinIndex)
		{
			const TSharedPtr<FUserPinInfo>& UserPin = Node->UserDefinedPins[PinIndex];
			if (UserPin.IsValid())
			{
				Node->RemoveUserDefinedPin(UserPin);
			}
		}

		for (const FVergilSignaturePinPlan& DesiredPin : DesiredPins)
		{
			if (Node->CreateUserDefinedPin(DesiredPin.Name, DesiredPin.Type, DesiredDirection, false) == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					FailureCode,
					FString::Printf(TEXT("Unable to create %s %s pin '%s' on graph '%s'."), GraphKind, PinKind, *DesiredPin.Name.ToString(), *ResolveCommandGraphName(Command).ToString()),
					Command.NodeId));
				return false;
			}
		}

		return true;
	}

	bool ExecuteEnsureFunctionGraph(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraph* const Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		if (!HasFunctionSignatureAttributes(Command))
		{
			return true;
		}

		UK2Node_FunctionEntry* const EntryNode = FindFunctionEntryNode(Graph);
		if (EntryNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionGraphEntryMissing"),
				FString::Printf(TEXT("Function graph '%s' does not contain an entry node."), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		TArray<FVergilSignaturePinPlan> InputParameters;
		TArray<FVergilSignaturePinPlan> OutputParameters;
		if (!TryGetFunctionParameterPlans(Command, TEXT("Input"), InputParameters, Diagnostics)
			|| !TryGetFunctionParameterPlans(Command, TEXT("Output"), OutputParameters, Diagnostics))
		{
			return false;
		}

		bool bPure = false;
		if (!TryParseBoolAttribute(Command, TEXT("bPure"), bPure))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionPurityMissing"),
				FString::Printf(TEXT("Function graph '%s' is missing the bPure attribute."), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		const FString AccessSpecifier = GetCommandAttribute(Command, TEXT("AccessSpecifier")).ToLower();
		int32 AccessFlags = FUNC_Public;
		if (AccessSpecifier.IsEmpty() || AccessSpecifier == TEXT("public"))
		{
			AccessFlags = FUNC_Public;
		}
		else if (AccessSpecifier == TEXT("protected"))
		{
			AccessFlags = FUNC_Protected;
		}
		else if (AccessSpecifier == TEXT("private"))
		{
			AccessFlags = FUNC_Private;
		}
		else
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionAccessSpecifierInvalid"),
				FString::Printf(TEXT("Function graph '%s' uses unsupported access specifier '%s'."), *Graph->GetName(), *AccessSpecifier),
				Command.NodeId));
			return false;
		}

		Graph->Modify();
		Blueprint->Modify();
		EntryNode->Modify();

		const int32 ExistingFlags = EntryNode->GetExtraFlags();
		const int32 DesiredFlags = (ExistingFlags & ~(FUNC_BlueprintPure | FUNC_Public | FUNC_Protected | FUNC_Private))
			| AccessFlags
			| (bPure ? FUNC_BlueprintPure : 0);
		if (DesiredFlags != ExistingFlags)
		{
			EntryNode->SetExtraFlags(DesiredFlags);
		}

		if (!RebuildEditablePins(
				EntryNode,
				InputParameters,
				EGPD_Output,
				TEXT("FunctionSignaturePinCreateFailed"),
				TEXT("function"),
				TEXT("input"),
				Command,
				Diagnostics))
		{
			return false;
		}

		UK2Node_FunctionResult* ResultNode = FindFunctionResultNode(Graph);
		if (ResultNode == nullptr && OutputParameters.Num() > 0)
		{
			ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
		}

		if (OutputParameters.Num() > 0 && ResultNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FunctionResultNodeMissing"),
				FString::Printf(TEXT("Function graph '%s' could not create a result node for outputs."), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		if (ResultNode != nullptr)
		{
			if (!RebuildEditablePins(
					ResultNode,
					OutputParameters,
					EGPD_Input,
					TEXT("FunctionSignaturePinCreateFailed"),
					TEXT("function"),
					TEXT("output"),
					Command,
					Diagnostics))
			{
				return false;
			}
		}

		EntryNode->ReconstructNode();
		if (ResultNode != nullptr)
		{
			ResultNode->ReconstructNode();
		}

		return true;
	}

	bool ExecuteEnsureMacroGraph(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraph* const Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		if (!HasMacroSignatureAttributes(Command))
		{
			return true;
		}

		UK2Node_EditablePinBase* const EntryNode = FindEditableGraphEntryNode(Graph);
		UK2Node_EditablePinBase* const ResultNode = FindEditableGraphResultNode(Graph);
		if (EntryNode == nullptr || ResultNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MacroGraphTerminatorMissing"),
				FString::Printf(TEXT("Macro graph '%s' does not contain editable entry and exit tunnel nodes."), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		TArray<FVergilSignaturePinPlan> InputParameters;
		TArray<FVergilSignaturePinPlan> OutputParameters;
		if (!TryGetMacroPinPlans(Command, TEXT("Input"), InputParameters, Diagnostics)
			|| !TryGetMacroPinPlans(Command, TEXT("Output"), OutputParameters, Diagnostics))
		{
			return false;
		}

		Graph->Modify();
		Blueprint->Modify();

		if (!RebuildEditablePins(
				EntryNode,
				InputParameters,
				EGPD_Output,
				TEXT("MacroSignaturePinCreateFailed"),
				TEXT("macro"),
				TEXT("input"),
				Command,
				Diagnostics))
		{
			return false;
		}

		if (!RebuildEditablePins(
				ResultNode,
				OutputParameters,
				EGPD_Input,
				TEXT("MacroSignaturePinCreateFailed"),
				TEXT("macro"),
				TEXT("output"),
				Command,
				Diagnostics))
		{
			return false;
		}

		EntryNode->ReconstructNode();
		ResultNode->ReconstructNode();
		return true;
	}

	bool ExecuteEnsureDispatcher(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidEnsureDispatcherCommand"),
				TEXT("Dispatcher creation requires a target blueprint and dispatcher name."),
				Command.NodeId));
			return false;
		}

		if (Blueprint->BlueprintType == BPTYPE_MacroLibrary)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("DispatcherNotSupportedOnMacroLibrary"),
				TEXT("Event dispatchers cannot be added to macro libraries."),
				Command.NodeId));
			return false;
		}

		Blueprint->Modify();

		const FName DispatcherName = Command.SecondaryName;
		const bool bHadVariable = HasBlueprintMemberVariable(Blueprint, DispatcherName);
		if (!bHadVariable)
		{
			FEdGraphPinType DelegateType;
			DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;

			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, DispatcherName, DelegateType))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("AddDispatcherVariableFailed"),
					FString::Printf(TEXT("Unable to add dispatcher variable '%s'."), *DispatcherName.ToString()),
					Command.NodeId));
				return false;
			}
		}

		if (FindDelegateGraph(Blueprint, DispatcherName) != nullptr)
		{
			return true;
		}

		UEdGraph* const NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			DispatcherName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());

		if (NewGraph == nullptr)
		{
			if (!bHadVariable)
			{
				FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, DispatcherName);
			}

			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("CreateDispatcherGraphFailed"),
				FString::Printf(TEXT("Unable to create delegate signature graph '%s'."), *DispatcherName.ToString()),
				Command.NodeId));
			return false;
		}

		const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
		NewGraph->bEditable = false;
		K2Schema->CreateDefaultNodesForGraph(*NewGraph);
		K2Schema->CreateFunctionGraphTerminators(*NewGraph, static_cast<UClass*>(nullptr));
		K2Schema->AddExtraFunctionFlags(NewGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
		K2Schema->MarkFunctionEntryAsEditable(NewGraph, true);

		Blueprint->DelegateSignatureGraphs.Add(NewGraph);
		return true;
	}

	bool ExecuteAddDispatcherParameter(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone() || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidAddDispatcherParameterCommand"),
				TEXT("Dispatcher parameter commands require a target blueprint, dispatcher name, and parameter name."),
				Command.NodeId));
			return false;
		}

		UEdGraph* const DelegateGraph = FindDelegateGraph(Blueprint, Command.SecondaryName);
		if (DelegateGraph == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("DispatcherGraphNotFound"),
				FString::Printf(TEXT("Unable to resolve dispatcher '%s' while adding parameter '%s'."), *Command.SecondaryName.ToString(), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		UK2Node_FunctionEntry* EntryNode = nullptr;
		for (UEdGraphNode* GraphNode : DelegateGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(GraphNode))
			{
				EntryNode = Candidate;
				break;
			}
		}

		if (EntryNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("DispatcherEntryNodeMissing"),
				FString::Printf(TEXT("Dispatcher '%s' has no function entry node."), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return false;
		}

		for (UEdGraphPin* Pin : EntryNode->Pins)
		{
			if (Pin != nullptr
				&& Pin->Direction == EGPD_Output
				&& Pin->PinName == Command.Name
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return true;
			}
		}

		FEdGraphPinType PinType;
		FString PinTypeError;
		if (!BuildPinTypeFromAttributes(Command, TEXT("PinCategory"), TEXT("ObjectPath"), PinType, PinTypeError))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("DispatcherParameterTypeInvalid"),
				PinTypeError,
				Command.NodeId));
			return false;
		}

		bool bIsArray = false;
		if (TryParseBoolAttribute(Command, TEXT("bIsArray"), bIsArray) && bIsArray)
		{
			PinType.ContainerType = EPinContainerType::Array;
		}

		EntryNode->Modify();
		DelegateGraph->Modify();

		if (EntryNode->CreateUserDefinedPin(Command.Name, PinType, EGPD_Output) == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("DispatcherParameterCreateFailed"),
				FString::Printf(TEXT("Unable to create dispatcher parameter '%s' on '%s'."), *Command.Name.ToString(), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return false;
		}

		return true;
	}

	bool ExecuteSetBlueprintMetadata(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidSetBlueprintMetadataCommand"),
				TEXT("Blueprint metadata commands require a target blueprint and metadata key."),
				Command.NodeId));
			return false;
		}

		if (!IsSupportedBlueprintMetadataKey(Command.Name))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("BlueprintMetadataKeyUnsupported"),
				FString::Printf(TEXT("Blueprint metadata key '%s' is not currently supported."), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		Blueprint->Modify();

		if (Command.Name == TEXT("BlueprintDisplayName"))
		{
			Blueprint->BlueprintDisplayName = Command.StringValue;
			return true;
		}

		if (Command.Name == TEXT("BlueprintDescription"))
		{
			Blueprint->BlueprintDescription = Command.StringValue;
			return true;
		}

		if (Command.Name == TEXT("BlueprintCategory"))
		{
			Blueprint->BlueprintCategory = Command.StringValue;
			return true;
		}

		if (Command.Name == TEXT("HideCategories"))
		{
			ParseBlueprintHideCategories(Command.StringValue, Blueprint->HideCategories);
			return true;
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("BlueprintMetadataKeyUnsupported"),
			FString::Printf(TEXT("Blueprint metadata key '%s' is not currently supported."), *Command.Name.ToString()),
			Command.NodeId));
		return false;
	}

	bool ExecuteEnsureVariable(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidEnsureVariableCommand"),
				TEXT("Variable creation requires a target blueprint and variable name."),
				Command.NodeId));
			return false;
		}

		if (Blueprint->BlueprintType == BPTYPE_MacroLibrary)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableNotSupportedOnMacroLibrary"),
				TEXT("Member variables cannot be added to macro libraries."),
				Command.NodeId));
			return false;
		}

		FEdGraphPinType VariableType;
		FString VariableTypeError;
		if (!BuildVariablePinTypeFromCommand(Command, VariableType, VariableTypeError))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableTypeInvalid"),
				VariableTypeError,
				Command.NodeId));
			return false;
		}

		Blueprint->Modify();

		const FName VariableName = Command.SecondaryName;
		const FBPVariableDescription* ExistingVariable = FindBlueprintMemberVariable(Blueprint, VariableName);
		if (ExistingVariable == nullptr)
		{
			if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, VariableType, Command.StringValue))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("AddVariableFailed"),
					FString::Printf(TEXT("Unable to add variable '%s'."), *VariableName.ToString()),
					Command.NodeId));
				return false;
			}
		}
		else if (!(ExistingVariable->VarType == VariableType))
		{
			FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, VariableName, VariableType);
		}

		bool bInstanceEditable = false;
		TryParseBoolAttribute(Command, TEXT("bInstanceEditable"), bInstanceEditable);
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VariableName, !bInstanceEditable);

		bool bBlueprintReadOnly = false;
		TryParseBoolAttribute(Command, TEXT("bBlueprintReadOnly"), bBlueprintReadOnly);
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VariableName, bBlueprintReadOnly);

		bool bExposeToCinematics = false;
		TryParseBoolAttribute(Command, TEXT("bExposeToCinematics"), bExposeToCinematics);
		FBlueprintEditorUtils::SetInterpFlag(Blueprint, VariableName, bExposeToCinematics);

		bool bTransient = false;
		TryParseBoolAttribute(Command, TEXT("bTransient"), bTransient);
		FBlueprintEditorUtils::SetVariableTransientFlag(Blueprint, VariableName, bTransient);

		bool bSaveGame = false;
		TryParseBoolAttribute(Command, TEXT("bSaveGame"), bSaveGame);
		FBlueprintEditorUtils::SetVariableSaveGameFlag(Blueprint, VariableName, bSaveGame);

		bool bAdvancedDisplay = false;
		TryParseBoolAttribute(Command, TEXT("bAdvancedDisplay"), bAdvancedDisplay);
		FBlueprintEditorUtils::SetVariableAdvancedDisplayFlag(Blueprint, VariableName, bAdvancedDisplay);

		bool bDeprecated = false;
		TryParseBoolAttribute(Command, TEXT("bDeprecated"), bDeprecated);
		FBlueprintEditorUtils::SetVariableDeprecatedFlag(Blueprint, VariableName, bDeprecated);

		bool bExposeOnSpawn = false;
		TryParseBoolAttribute(Command, TEXT("bExposeOnSpawn"), bExposeOnSpawn);
		if (bExposeOnSpawn)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		}
		else
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn);
		}

		bool bPrivate = false;
		TryParseBoolAttribute(Command, TEXT("bPrivate"), bPrivate);
		if (bPrivate)
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
		}
		else
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, VariableName, nullptr, FBlueprintMetadata::MD_Private);
		}

		FBlueprintEditorUtils::SetBlueprintVariableCategory(
			Blueprint,
			VariableName,
			nullptr,
			FText::FromString(GetCommandAttribute(Command, TEXT("Category"))),
			true);

		return true;
	}

	bool ExecuteSetVariableMetadata(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone() || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidSetVariableMetadataCommand"),
				TEXT("Variable metadata commands require a target blueprint, variable name, and metadata key."),
				Command.NodeId));
			return false;
		}

		if (!HasBlueprintMemberVariable(Blueprint, Command.SecondaryName))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableMetadataTargetMissing"),
				FString::Printf(TEXT("Unable to resolve variable '%s' for metadata key '%s'."), *Command.SecondaryName.ToString(), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		if (Command.StringValue.IsEmpty())
		{
			FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint, Command.SecondaryName, nullptr, Command.Name);
		}
		else
		{
			FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Command.SecondaryName, nullptr, Command.Name, Command.StringValue);
		}

		return true;
	}

	bool ExecuteSetVariableDefault(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidSetVariableDefaultCommand"),
				TEXT("Variable default commands require a target blueprint and variable name."),
				Command.NodeId));
			return false;
		}

		FBPVariableDescription* const Variable = FindBlueprintMemberVariable(Blueprint, Command.SecondaryName);
		if (Variable == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("VariableDefaultTargetMissing"),
				FString::Printf(TEXT("Unable to resolve variable '%s' while applying a default value."), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return false;
		}

		if (Variable->DefaultValue == Command.StringValue)
		{
			return true;
		}

		Blueprint->Modify();
		Variable->DefaultValue = Command.StringValue;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return true;
	}

	bool ExecuteEnsureComponent(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone() || Command.StringValue.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidEnsureComponentCommand"),
				TEXT("Component creation requires a target blueprint, component name, and component class path."),
				Command.NodeId));
			return false;
		}

		if (Blueprint->SimpleConstructionScript == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("BlueprintMissingSimpleConstructionScript"),
				TEXT("Component commands require a blueprint with a simple construction script."),
				Command.NodeId));
			return false;
		}

		UClass* const ComponentClass = ResolveClassReference(Command.StringValue);
		if (ComponentClass == nullptr || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidComponentClass"),
				FString::Printf(TEXT("Unable to resolve component class '%s'."), *Command.StringValue),
				Command.NodeId));
			return false;
		}

		if (USCS_Node* ExistingNode = FindComponentNode(Blueprint, Command.SecondaryName))
		{
			if (ExistingNode->ComponentTemplate == nullptr || ExistingNode->ComponentTemplate->GetClass() != ComponentClass)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentClassMismatch"),
					FString::Printf(
						TEXT("Component '%s' already exists with class '%s', not requested class '%s'."),
						*Command.SecondaryName.ToString(),
						ExistingNode->ComponentTemplate != nullptr ? *ExistingNode->ComponentTemplate->GetClass()->GetPathName() : TEXT("<null>"),
						*ComponentClass->GetPathName()),
					Command.NodeId));
				return false;
			}

			return true;
		}

		Blueprint->Modify();
		Blueprint->SimpleConstructionScript->Modify();

		USCS_Node* const NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, Command.SecondaryName);
		if (NewNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("CreateComponentNodeFailed"),
				FString::Printf(TEXT("Unable to create component '%s'."), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return false;
		}

		Blueprint->SimpleConstructionScript->AddNode(NewNode);
		return true;
	}

	bool ExecuteAttachComponent(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone() || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidAttachComponentCommand"),
				TEXT("Component attachment requires a target blueprint, child component name, and parent component name."),
				Command.NodeId));
			return false;
		}

		USCS_Node* const ChildNode = FindComponentNode(Blueprint, Command.SecondaryName);
		USCS_Node* const ParentNode = FindComponentNode(Blueprint, Command.Name);
		if (ChildNode == nullptr || ParentNode == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentAttachmentTargetMissing"),
				FString::Printf(
					TEXT("Unable to resolve child '%s' or parent '%s' for component attachment."),
					*Command.SecondaryName.ToString(),
					*Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		if (ChildNode == ParentNode)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentSelfAttachment"),
				FString::Printf(TEXT("Component '%s' cannot be attached to itself."), *Command.SecondaryName.ToString()),
				Command.NodeId));
			return false;
		}

		if (Cast<USceneComponent>(ChildNode->ComponentTemplate) == nullptr || Cast<USceneComponent>(ParentNode->ComponentTemplate) == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentAttachmentRequiresSceneComponents"),
				TEXT("Component attachment requires both child and parent to derive from USceneComponent."),
				Command.NodeId));
			return false;
		}

		USimpleConstructionScript* const SimpleConstructionScript = Blueprint->SimpleConstructionScript;
		USCS_Node* const ExistingParentNode = SimpleConstructionScript != nullptr ? SimpleConstructionScript->FindParentNode(ChildNode) : nullptr;
		const bool bWasRootNode = SimpleConstructionScript != nullptr && SimpleConstructionScript->GetRootNodes().Contains(ChildNode);

		Blueprint->Modify();
		if (SimpleConstructionScript != nullptr)
		{
			SimpleConstructionScript->Modify();
		}
		ChildNode->Modify();
		ParentNode->Modify();

		if (ExistingParentNode != nullptr && ExistingParentNode != ParentNode)
		{
			ExistingParentNode->Modify();
			ExistingParentNode->RemoveChildNode(ChildNode, false);
		}
		else if (bWasRootNode && SimpleConstructionScript != nullptr)
		{
			SimpleConstructionScript->RemoveNode(ChildNode, false);
		}

		if (ExistingParentNode != ParentNode)
		{
			ParentNode->AddChildNode(ChildNode, bWasRootNode);
		}

		ChildNode->bIsParentComponentNative = false;
		ChildNode->ParentComponentOrVariableName = NAME_None;
		ChildNode->ParentComponentOwnerClassName = NAME_None;
		ChildNode->AttachToName = Command.StringValue.IsEmpty() ? NAME_None : FName(*Command.StringValue);
		if (SimpleConstructionScript != nullptr)
		{
			SimpleConstructionScript->ValidateSceneRootNodes();
		}
		return true;
	}

	bool ExecuteSetComponentProperty(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.SecondaryName.IsNone() || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidSetComponentPropertyCommand"),
				TEXT("Component property commands require a target blueprint, component name, and property name."),
				Command.NodeId));
			return false;
		}

		USCS_Node* const ComponentNode = FindComponentNode(Blueprint, Command.SecondaryName);
		if (ComponentNode == nullptr || ComponentNode->ComponentTemplate == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentPropertyTargetMissing"),
				FString::Printf(TEXT("Unable to resolve component '%s' while setting property '%s'."), *Command.SecondaryName.ToString(), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		FProperty* const Property = FindPropertyFlexible(ComponentNode->ComponentTemplate->GetClass(), Command.Name);
		if (Property == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentPropertyMissing"),
				FString::Printf(TEXT("Component '%s' does not expose property '%s'."), *Command.SecondaryName.ToString(), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		Blueprint->Modify();
		ComponentNode->Modify();
		ComponentNode->ComponentTemplate->Modify();
		if (!ImportPropertyValue(ComponentNode->ComponentTemplate, Property, Command.StringValue))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ComponentPropertyImportFailed"),
				FString::Printf(
					TEXT("Unable to import value '%s' into component '%s' property '%s'."),
					*Command.StringValue,
					*Command.SecondaryName.ToString(),
					*Property->GetName()),
				Command.NodeId));
			return false;
		}

		return true;
	}

	bool ExecuteEnsureInterface(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.StringValue.IsEmpty())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidEnsureInterfaceCommand"),
				TEXT("Interface commands require a target blueprint and interface class path."),
				Command.NodeId));
			return false;
		}

		UClass* const InterfaceClass = ResolveClassReference(Command.StringValue);
		if (InterfaceClass == nullptr || !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidInterfaceClass"),
				FString::Printf(TEXT("Unable to resolve interface class '%s'."), *Command.StringValue),
				Command.NodeId));
			return false;
		}

		TArray<UClass*> ImplementedInterfaces;
		FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, true, ImplementedInterfaces);
		if (ImplementedInterfaces.Contains(InterfaceClass))
		{
			return true;
		}

		Blueprint->Modify();
		if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName()))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ImplementInterfaceFailed"),
				FString::Printf(TEXT("Unable to implement interface '%s'."), *InterfaceClass->GetPathName()),
				Command.NodeId));
			return false;
		}

		return true;
	}

	bool ExecuteSetClassDefault(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.Name.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidSetClassDefaultCommand"),
				TEXT("Class default commands require a target blueprint and property name."),
				Command.NodeId));
			return false;
		}

		if (Blueprint->GeneratedClass == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("GeneratedClassMissing"),
				TEXT("Class default commands require a compiled generated class."),
				Command.NodeId));
			return false;
		}

		UObject* const ClassDefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
		FProperty* const Property = FindPropertyFlexible(Blueprint->GeneratedClass, Command.Name);
		if (ClassDefaultObject == nullptr || Property == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ClassDefaultPropertyMissing"),
				FString::Printf(TEXT("Unable to resolve class default property '%s'."), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		ClassDefaultObject->Modify();
		if (!ImportPropertyValue(ClassDefaultObject, Property, Command.StringValue))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("ClassDefaultImportFailed"),
				FString::Printf(TEXT("Unable to import value '%s' into class default property '%s'."), *Command.StringValue, *Property->GetName()),
				Command.NodeId));
			return false;
		}

		return true;
	}

	bool ExecuteRemoveNode(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (State.RemovedNodeIds.Contains(Command.NodeId))
		{
			return true;
		}

		UEdGraph* const Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		UEdGraphNode* Node = State.NodesById.FindRef(Command.NodeId);
		if (Node == nullptr || Node->GetGraph() != Graph)
		{
			Node = FindGraphNodeByGuid(Graph, Command.NodeId);
		}

		if (Node == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("RemoveNodeTargetMissing"),
				FString::Printf(TEXT("Unable to resolve node '%s' in graph '%s' for removal."), *Command.NodeId.ToString(), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		Graph->Modify();
		Node->Modify();
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);

		State.NodesById.Remove(Command.NodeId);
		State.RemovedNodeIds.Add(Command.NodeId);
		for (auto PinIt = State.PinsById.CreateIterator(); PinIt; ++PinIt)
		{
			if (PinIt.Value() != nullptr && PinIt.Value()->GetOwningNode() == Node)
			{
				PinIt.RemoveCurrent();
			}
		}

		return true;
	}

	bool ExecuteRenameMember(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr || Command.Name.IsNone() || Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidRenameMemberCommand"),
				TEXT("Member rename commands require a target blueprint, old name, and new name."),
				Command.NodeId));
			return false;
		}

		if (Command.Name == Command.SecondaryName)
		{
			return true;
		}

		const FString MemberType = GetCommandAttribute(Command, TEXT("MemberType")).ToLower();
		Blueprint->Modify();

		if (MemberType == TEXT("variable") || MemberType == TEXT("dispatcher"))
		{
			if (!HasBlueprintMemberVariable(Blueprint, Command.Name))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("RenameVariableTargetMissing"),
					FString::Printf(TEXT("Unable to resolve member variable '%s' for rename."), *Command.Name.ToString()),
					Command.NodeId));
				return false;
			}

			FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Command.Name, Command.SecondaryName);
			return true;
		}

		if (MemberType == TEXT("functiongraph") || MemberType == TEXT("macrograph"))
		{
			UEdGraph* const Graph = FindGraphByName(Blueprint, Command.Name);
			if (Graph == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("RenameGraphTargetMissing"),
					FString::Printf(TEXT("Unable to resolve graph '%s' for rename."), *Command.Name.ToString()),
					Command.NodeId));
				return false;
			}

			FBlueprintEditorUtils::RenameGraph(Graph, Command.SecondaryName.ToString());
			return true;
		}

		if (MemberType == TEXT("component"))
		{
			USCS_Node* const ComponentNode = FindComponentNode(Blueprint, Command.Name);
			if (ComponentNode == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("RenameComponentTargetMissing"),
					FString::Printf(TEXT("Unable to resolve component '%s' for rename."), *Command.Name.ToString()),
					Command.NodeId));
				return false;
			}

			FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, ComponentNode, Command.SecondaryName);
			return true;
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("UnsupportedRenameMemberType"),
			FString::Printf(TEXT("Unsupported rename member type '%s'."), *MemberType),
			Command.NodeId));
		return false;
	}

	bool ExecuteMoveNode(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraph* const Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		UEdGraphNode* Node = State.NodesById.FindRef(Command.NodeId);
		if (Node == nullptr || Node->GetGraph() != Graph)
		{
			Node = FindGraphNodeByGuid(Graph, Command.NodeId);
		}

		if (Node == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MoveNodeTargetMissing"),
				FString::Printf(TEXT("Unable to resolve node '%s' in graph '%s' for movement."), *Command.NodeId.ToString(), *Graph->GetName()),
				Command.NodeId));
			return false;
		}

		Node->Modify();
		Node->NodePosX = FMath::RoundToInt(Command.Position.X);
		Node->NodePosY = FMath::RoundToInt(Command.Position.Y);
		return true;
	}

	bool ExecuteCompileBlueprint(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("InvalidCompileBlueprintCommand"),
				TEXT("Compile commands require a target blueprint."),
				Command.NodeId));
			return false;
		}

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return true;
	}

	bool ExecuteFinalizeNode(
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraphNode* const Node = State.NodesById.FindRef(Command.NodeId);
		if (Node == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("FinalizeNodeMissing"),
				FString::Printf(TEXT("Unable to resolve node '%s' during finalization."), *Command.NodeId.ToString()),
				Command.NodeId));
			return false;
		}

		if (Command.Name == TEXT("Vergil.K2.CreateDelegate"))
		{
			UK2Node_CreateDelegate* const CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node);
			if (CreateDelegateNode == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FinalizeCreateDelegateWrongNodeType"),
					TEXT("CreateDelegate finalization target was not a UK2Node_CreateDelegate."),
					Command.NodeId));
				return false;
			}

			if (Command.SecondaryName.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FinalizeCreateDelegateMissingFunction"),
					TEXT("CreateDelegate finalization requires SecondaryName to contain the function name."),
					Command.NodeId));
				return false;
			}

			CreateDelegateNode->Modify();
			CreateDelegateNode->SelectedFunctionName = Command.SecondaryName;
			CreateDelegateNode->SelectedFunctionGuid.Invalidate();
			CreateDelegateNode->SetFunction(Command.SecondaryName);
			return true;
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("UnsupportedFinalizeNode"),
			FString::Printf(TEXT("Unsupported finalize node command '%s'."), *Command.Name.ToString()),
			Command.NodeId));
		return false;
	}

	FProperty* ResolveVariableProperty(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingVariableName"),
				TEXT("Variable node execution requires SecondaryName to contain the variable name."),
				Command.NodeId));
			return nullptr;
		}

		TArray<UClass*> SearchClasses;
		if (!Command.StringValue.IsEmpty())
		{
			if (UClass* ExplicitOwnerClass = ResolveClassReference(Command.StringValue))
			{
				SearchClasses.Add(ExplicitOwnerClass);
			}
		}

		if (Blueprint != nullptr)
		{
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
		}

		for (UClass* SearchClass : SearchClasses)
		{
			if (SearchClass == nullptr)
			{
				continue;
			}

			if (FProperty* Property = FindFProperty<FProperty>(SearchClass, Command.SecondaryName))
			{
				return Property;
			}
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("VariablePropertyNotFound"),
			FString::Printf(TEXT("Unable to resolve property '%s' for variable node."), *Command.SecondaryName.ToString()),
			Command.NodeId));
		return nullptr;
	}

	FMulticastDelegateProperty* ResolveDelegateProperty(UBlueprint* Blueprint, const FVergilCompilerCommand& Command, TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Command.SecondaryName.IsNone())
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingDelegatePropertyName"),
				TEXT("Delegate node execution requires SecondaryName to contain the delegate property name."),
				Command.NodeId));
			return nullptr;
		}

		TArray<UClass*> SearchClasses;
		auto AddSearchClass = [&SearchClasses](UClass* CandidateClass)
		{
			if (CandidateClass != nullptr)
			{
				SearchClasses.AddUnique(CandidateClass);
			}
		};

		if (!Command.StringValue.IsEmpty())
		{
			if (UClass* ExplicitOwnerClass = ResolveClassReference(Command.StringValue))
			{
				AddSearchClass(ExplicitOwnerClass);
			}
			else
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("DelegateOwnerClassNotFound"),
					FString::Printf(TEXT("Unable to resolve delegate owner class '%s'."), *Command.StringValue),
					Command.NodeId));
				return nullptr;
			}
		}

		if (Blueprint != nullptr)
		{
			AddSearchClass(Blueprint->SkeletonGeneratedClass);
			AddSearchClass(Blueprint->GeneratedClass);
			AddSearchClass(Blueprint->ParentClass);
		}

		for (UClass* SearchClass : SearchClasses)
		{
			if (FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(SearchClass, Command.SecondaryName))
			{
				return DelegateProperty;
			}
		}

		const FString OwnerDescription = Command.StringValue.IsEmpty()
			? TEXT("the target blueprint or its parent class")
			: FString::Printf(TEXT("'%s'"), *Command.StringValue);

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("DelegatePropertyNotFound"),
			FString::Printf(TEXT("Unable to resolve multicast delegate property '%s' on %s."), *Command.SecondaryName.ToString(), *OwnerDescription),
			Command.NodeId));
		return nullptr;
	}

	bool ExecuteAddNode(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraph* Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		Graph->Modify();
		UEdGraphNode* NewNode = nullptr;

		if (IsCommentAddCommand(Command))
		{
			UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
			CommentNode->NodeWidth = 400;
			CommentNode->NodeHeight = 100;
			CommentNode->NodeComment = Command.StringValue.IsEmpty() ? TEXT("Vergil Comment") : Command.StringValue;
			FinalizePlacedNode(Graph, CommentNode, Command.Position, Command.NodeId);
			NewNode = CommentNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Event"))
		{
			if (Command.SecondaryName == ConstructionScriptGraphName)
			{
				UK2Node_FunctionEntry* const EntryNode = FindFunctionEntryNode(Graph);
				if (EntryNode == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("ConstructionScriptEntryMissing"),
						FString::Printf(TEXT("Construction script graph '%s' does not contain a function entry node."), *Graph->GetName()),
						Command.NodeId));
					return false;
				}

				EntryNode->Modify();
				if (Command.NodeId.IsValid())
				{
					EntryNode->NodeGuid = Command.NodeId;
				}
				EntryNode->NodePosX = FMath::RoundToInt(Command.Position.X);
				EntryNode->NodePosY = FMath::RoundToInt(Command.Position.Y);
				NewNode = EntryNode;
			}
			else if (Blueprint == nullptr || Blueprint->ParentClass == nullptr || Command.SecondaryName.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidEventCommand"),
					TEXT("Event node execution requires a parent class and event function name."),
					Command.NodeId));
				return false;
			}

			else
			{
				UFunction* Func = Blueprint->ParentClass->FindFunctionByName(Command.SecondaryName);
				if (Func == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("EventFunctionNotFound"),
						FString::Printf(TEXT("Unable to resolve event '%s' on parent class '%s'."), *Command.SecondaryName.ToString(), *Blueprint->ParentClass->GetName()),
						Command.NodeId));
					return false;
				}

				UK2Node_Event* EventNode = NewObject<UK2Node_Event>(Graph);
				EventNode->EventReference.SetExternalMember(Command.SecondaryName, Blueprint->ParentClass);
				EventNode->bOverrideFunction = true;
				FinalizePlacedNode(Graph, EventNode, Command.Position, Command.NodeId);
				NewNode = EventNode;
			}
		}
		else if (Command.Name == TEXT("Vergil.K2.CustomEvent"))
		{
			if (Command.SecondaryName.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidCustomEventCommand"),
					TEXT("Custom event execution requires the event name in SecondaryName."),
					Command.NodeId));
				return false;
			}

			UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
			EventNode->CustomFunctionName = Command.SecondaryName;
			FinalizePlacedNode(Graph, EventNode, Command.Position, Command.NodeId);

			const FString DelegateOwnerClassPath = GetCommandAttribute(Command, TEXT("DelegateOwnerClassPath"));
			const FString DelegatePropertyName = GetCommandAttribute(Command, TEXT("DelegatePropertyName"));
			if (!DelegatePropertyName.IsEmpty())
			{
				FVergilCompilerCommand DelegateSignatureCommand = Command;
				DelegateSignatureCommand.StringValue = DelegateOwnerClassPath;
				DelegateSignatureCommand.SecondaryName = FName(*DelegatePropertyName);

				FMulticastDelegateProperty* const DelegateProperty = ResolveDelegateProperty(Blueprint, DelegateSignatureCommand, Diagnostics);
				if (DelegateProperty == nullptr || DelegateProperty->SignatureFunction == nullptr)
				{
					return false;
				}

				EventNode->SetDelegateSignature(DelegateProperty->SignatureFunction);
				EventNode->ReconstructNode();
			}

			NewNode = EventNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Branch"))
		{
			UK2Node_IfThenElse* BranchNode = NewObject<UK2Node_IfThenElse>(Graph);
			FinalizePlacedNode(Graph, BranchNode, Command.Position, Command.NodeId);
			NewNode = BranchNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Sequence"))
		{
			UK2Node_ExecutionSequence* SequenceNode = NewObject<UK2Node_ExecutionSequence>(Graph);
			FinalizePlacedNode(Graph, SequenceNode, Command.Position, Command.NodeId);

			int32 NumOutputs = 2;
			TryParseInt(Command.StringValue, NumOutputs);
			if (NumOutputs < 2)
			{
				NumOutputs = 2;
			}

			for (int32 OutputIndex = 2; OutputIndex < NumOutputs; ++OutputIndex)
			{
				SequenceNode->AddInputPin();
			}

			NewNode = SequenceNode;
		}
		else if (const FVergilStandardMacroCommand* const MacroCommand = FindStandardMacroCommand(Command.Name))
		{
			if (!ExecuteStandardMacroNode(*MacroCommand, Graph, Command, NewNode, Diagnostics))
			{
				return false;
			}
		}
		else if (Command.Name == TEXT("Vergil.K2.Delay"))
		{
			UFunction* const DelayFunction = UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
			if (DelayFunction == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("DelayFunctionNotFound"),
					TEXT("Unable to resolve UKismetSystemLibrary::Delay."),
					Command.NodeId));
				return false;
			}

			UK2Node_CallFunction* DelayNode = NewObject<UK2Node_CallFunction>(Graph);
			DelayNode->SetFromFunction(DelayFunction);
			FinalizePlacedNode(Graph, DelayNode, Command.Position, Command.NodeId);
			NewNode = DelayNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.SpawnActor"))
		{
			UClass* ActorClass = nullptr;
			if (!ResolveSpawnActorClass(
				Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ActorClassPath")) : Command.StringValue,
				ActorClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UK2Node_SpawnActorFromClass* SpawnActorNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
			FinalizePlacedNode(Graph, SpawnActorNode, Command.Position, Command.NodeId);

			UEdGraphPin* const ClassPin = SpawnActorNode->GetClassPin();
			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (ClassPin == nullptr || K2Schema == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("SpawnActorClassPinMissing"),
					TEXT("Spawn actor node execution could not access the Class pin."),
					Command.NodeId));
				return false;
			}

			K2Schema->TrySetDefaultObject(*ClassPin, ActorClass, false);

			if (UEdGraphPin* const SpawnTransformPin = SpawnActorNode->FindPin(SpawnActorTransformPinName))
			{
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(SpawnTransformPin);
			}

			NewNode = SpawnActorNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.AddComponentByClass"))
		{
			UClass* ComponentClass = nullptr;
			if (!ResolveActorComponentClass(
				Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ComponentClassPath")) : Command.StringValue,
				TEXT("AddComponentClassMissing"),
				TEXT("AddComponentClassNotFound"),
				TEXT("AddComponentClassNotComponent"),
				TEXT("AddComponentByClass node execution"),
				ComponentClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UK2Node_AddComponentByClass* AddComponentNode = NewObject<UK2Node_AddComponentByClass>(Graph);
			FinalizePlacedNode(Graph, AddComponentNode, Command.Position, Command.NodeId);

			UEdGraphPin* const ClassPin = AddComponentNode->GetClassPin();
			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (ClassPin == nullptr || K2Schema == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("AddComponentClassPinMissing"),
					TEXT("Add component by class execution could not access the Class pin."),
					Command.NodeId));
				return false;
			}

			K2Schema->TrySetDefaultObject(*ClassPin, ComponentClass, false);
			AddComponentNode->PinDefaultValueChanged(ClassPin);

			NewNode = AddComponentNode;
		}
		else if (const FVergilComponentLookupCommand* const LookupCommand = FindComponentLookupCommand(Command.Name))
		{
			UClass* ComponentClass = nullptr;
			if (!ResolveActorComponentClass(
				Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ComponentClassPath")) : Command.StringValue,
				TEXT("ComponentLookupClassMissing"),
				TEXT("ComponentLookupClassNotFound"),
				TEXT("ComponentLookupClassNotComponent"),
				TEXT("Component lookup node execution"),
				ComponentClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UFunction* const LookupFunction = AActor::StaticClass()->FindFunctionByName(LookupCommand->FunctionName);
			if (LookupFunction == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentLookupFunctionNotFound"),
					FString::Printf(TEXT("Unable to resolve AActor::%s for component lookup execution."), *LookupCommand->FunctionName.ToString()),
					Command.NodeId));
				return false;
			}

			UK2Node_CallFunction* LookupNode = NewObject<UK2Node_CallFunction>(Graph);
			LookupNode->SetFromFunction(LookupFunction);
			FinalizePlacedNode(Graph, LookupNode, Command.Position, Command.NodeId);

			UEdGraphPin* const ComponentClassPin = LookupNode->FindPin(ComponentLookupClassPinName);
			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (ComponentClassPin == nullptr || K2Schema == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ComponentLookupClassPinMissing"),
					FString::Printf(TEXT("Component lookup node '%s' did not expose a ComponentClass pin."), *Command.Name.ToString()),
					Command.NodeId));
				return false;
			}

			K2Schema->TrySetDefaultObject(*ComponentClassPin, ComponentClass, false);
			LookupNode->PinDefaultValueChanged(ComponentClassPin);
			NewNode = LookupNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Call"))
		{
			UFunction* Func = ResolveCallFunction(Blueprint, Command, Diagnostics);
			if (Func == nullptr)
			{
				return false;
			}

			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
			const bool bIsOwnBlueprintFunction = Blueprint != nullptr
				&& ((Blueprint->SkeletonGeneratedClass != nullptr && Func->GetOwnerClass() == Blueprint->SkeletonGeneratedClass)
					|| (Blueprint->GeneratedClass != nullptr && Func->GetOwnerClass() == Blueprint->GeneratedClass));

			if (bIsOwnBlueprintFunction)
			{
				CallNode->FunctionReference.SetSelfMember(Func->GetFName());
			}
			else
			{
				CallNode->SetFromFunction(Func);
			}

			FinalizePlacedNode(Graph, CallNode, Command.Position, Command.NodeId);
			NewNode = CallNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.InterfaceCall"))
		{
			UFunction* const InterfaceFunction = ResolveInterfaceFunction(Command, Diagnostics);
			if (InterfaceFunction == nullptr)
			{
				return false;
			}

			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
			CallNode->FunctionReference.SetExternalMember(Command.SecondaryName, InterfaceFunction->GetOwnerClass());
			FinalizePlacedNode(Graph, CallNode, Command.Position, Command.NodeId);
			NewNode = CallNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.InterfaceMessage"))
		{
			UFunction* const InterfaceFunction = ResolveInterfaceFunction(Command, Diagnostics);
			if (InterfaceFunction == nullptr)
			{
				return false;
			}

			UK2Node_Message* MessageNode = NewObject<UK2Node_Message>(Graph);
			MessageNode->FunctionReference.SetExternalMember(Command.SecondaryName, InterfaceFunction->GetOwnerClass());
			FinalizePlacedNode(Graph, MessageNode, Command.Position, Command.NodeId);
			NewNode = MessageNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.GetClassDefaults"))
		{
			UClass* SourceClass = nullptr;
			if (!ResolveNodeClassReference(
				Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ClassPath")) : Command.StringValue,
				TEXT("GetClassDefaultsClassMissing"),
				TEXT("GetClassDefaultsClassNotFound"),
				TEXT("GetClassDefaults node execution"),
				SourceClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UK2Node_GetClassDefaults* GetClassDefaultsNode = NewObject<UK2Node_GetClassDefaults>(Graph);
			FinalizePlacedNode(Graph, GetClassDefaultsNode, Command.Position, Command.NodeId);

			UEdGraphPin* const ClassPin = GetClassDefaultsNode->FindClassPin();
			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			if (ClassPin == nullptr || K2Schema == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("GetClassDefaultsClassPinMissing"),
					TEXT("GetClassDefaults node execution could not access the Class pin."),
					Command.NodeId));
				return false;
			}

			K2Schema->TrySetDefaultObject(*ClassPin, SourceClass, false);
			GetClassDefaultsNode->PinDefaultValueChanged(ClassPin);
			NewNode = GetClassDefaultsNode;
		}
		else if (const FVergilLoadAssetCommand* const LoadAssetCommand = FindLoadAssetCommand(Command.Name))
		{
			UClass* AssetClass = nullptr;
			if (!ResolveLoadAssetClass(
				Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("AssetClassPath")) : Command.StringValue,
				AssetClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UK2Node* LoadNode = nullptr;
			if (LoadAssetCommand->CommandName == TEXT("Vergil.K2.LoadAsset"))
			{
				LoadNode = NewObject<UK2Node_LoadAsset>(Graph);
			}
			else if (LoadAssetCommand->CommandName == TEXT("Vergil.K2.LoadAssetClass"))
			{
				LoadNode = NewObject<UK2Node_LoadAssetClass>(Graph);
			}
			else
			{
				LoadNode = NewObject<UK2Node_LoadAssets>(Graph);
			}

			FinalizePlacedNode(Graph, LoadNode, Command.Position, Command.NodeId);

			UEdGraphPin* const InputPin = LoadNode->FindPin(LoadAssetCommand->InputPinName);
			UEdGraphPin* const OutputPin = LoadNode->FindPin(LoadAssetCommand->OutputPinName);
			if (InputPin == nullptr || OutputPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("LoadAssetPinMissing"),
					FString::Printf(TEXT("%s node execution could not access the typed input/output pins."), *Command.Name.ToString()),
					Command.NodeId));
				return false;
			}

			InputPin->PinType.PinSubCategoryObject = AssetClass;
			if (LoadAssetCommand->bIsClassAsset)
			{
				OutputPin->PinType.PinSubCategoryObject = AssetClass;
			}
			NewNode = LoadNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.ConvertAsset"))
		{
			UK2Node_ConvertAsset* ConvertAssetNode = NewObject<UK2Node_ConvertAsset>(Graph);
			FinalizePlacedNode(Graph, ConvertAssetNode, Command.Position, Command.NodeId);
			NewNode = ConvertAssetNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.ClassCast"))
		{
			UClass* TargetClass = nullptr;
			if (!ResolveNodeClassReference(
				Command.StringValue,
				TEXT("ClassCastTargetClassMissing"),
				TEXT("ClassCastTargetClassNotFound"),
				TEXT("class-cast target class"),
				TargetClass,
				Diagnostics,
				Command.NodeId))
			{
				return false;
			}

			UK2Node_ClassDynamicCast* CastNode = NewObject<UK2Node_ClassDynamicCast>(Graph);
			CastNode->TargetType = TargetClass;
			FinalizePlacedNode(Graph, CastNode, Command.Position, Command.NodeId);

			if (!HasPlannedExecPins(Command))
			{
				CastNode->SetPurity(true);
			}

			NewNode = CastNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.CallDelegate"))
		{
			FMulticastDelegateProperty* const DelegateProperty = ResolveDelegateProperty(Blueprint, Command, Diagnostics);
			if (DelegateProperty == nullptr)
			{
				return false;
			}

			UK2Node_CallDelegate* CallDelegateNode = NewObject<UK2Node_CallDelegate>(Graph);
			CallDelegateNode->SetFromProperty(DelegateProperty, false, DelegateProperty->GetOwnerClass());
			FinalizePlacedNode(Graph, CallDelegateNode, Command.Position, Command.NodeId);
			NewNode = CallDelegateNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.CreateDelegate"))
		{
			UK2Node_CreateDelegate* CreateDelegateNode = NewObject<UK2Node_CreateDelegate>(Graph);
			CreateDelegateNode->SelectedFunctionName = Command.SecondaryName;
			CreateDelegateNode->SelectedFunctionGuid.Invalidate();
			FinalizePlacedNode(Graph, CreateDelegateNode, Command.Position, Command.NodeId);
			NewNode = CreateDelegateNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.BindDelegate"))
		{
			FMulticastDelegateProperty* const DelegateProperty = ResolveDelegateProperty(Blueprint, Command, Diagnostics);
			if (DelegateProperty == nullptr)
			{
				return false;
			}

			UK2Node_AddDelegate* BindNode = NewObject<UK2Node_AddDelegate>(Graph);
			BindNode->SetFromProperty(DelegateProperty, false, DelegateProperty->GetOwnerClass());
			FinalizePlacedNode(Graph, BindNode, Command.Position, Command.NodeId);
			NewNode = BindNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.RemoveDelegate"))
		{
			FMulticastDelegateProperty* const DelegateProperty = ResolveDelegateProperty(Blueprint, Command, Diagnostics);
			if (DelegateProperty == nullptr)
			{
				return false;
			}

			UK2Node_RemoveDelegate* RemoveNode = NewObject<UK2Node_RemoveDelegate>(Graph);
			RemoveNode->SetFromProperty(DelegateProperty, false, DelegateProperty->GetOwnerClass());
			FinalizePlacedNode(Graph, RemoveNode, Command.Position, Command.NodeId);
			NewNode = RemoveNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.ClearDelegate"))
		{
			FMulticastDelegateProperty* const DelegateProperty = ResolveDelegateProperty(Blueprint, Command, Diagnostics);
			if (DelegateProperty == nullptr)
			{
				return false;
			}

			UK2Node_ClearDelegate* ClearNode = NewObject<UK2Node_ClearDelegate>(Graph);
			ClearNode->SetFromProperty(DelegateProperty, false, DelegateProperty->GetOwnerClass());
			FinalizePlacedNode(Graph, ClearNode, Command.Position, Command.NodeId);
			NewNode = ClearNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Self"))
		{
			UK2Node_Self* SelfNode = NewObject<UK2Node_Self>(Graph);
			FinalizePlacedNode(Graph, SelfNode, Command.Position, Command.NodeId);
			NewNode = SelfNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Cast"))
		{
			UClass* TargetClass = ResolveClassReference(Command.StringValue);
			if (TargetClass == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("CastTargetClassNotFound"),
					FString::Printf(TEXT("Unable to resolve cast target class '%s'."), *Command.StringValue),
					Command.NodeId));
				return false;
			}

			UK2Node_DynamicCast* CastNode = NewObject<UK2Node_DynamicCast>(Graph);
			CastNode->TargetType = TargetClass;
			FinalizePlacedNode(Graph, CastNode, Command.Position, Command.NodeId);

			if (!HasPlannedExecPins(Command))
			{
				CastNode->SetPurity(true);
			}

			NewNode = CastNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Reroute"))
		{
			UK2Node_Knot* KnotNode = NewObject<UK2Node_Knot>(Graph);
			FinalizePlacedNode(Graph, KnotNode, Command.Position, Command.NodeId);
			NewNode = KnotNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Select"))
		{
			const FString IndexCategory = GetCommandAttribute(Command, TEXT("IndexPinCategory")).TrimStartAndEnd().ToLower();
			if (IndexCategory.IsEmpty())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingSelectIndexCategory"),
					TEXT("Select node execution requires attribute IndexPinCategory."),
					Command.NodeId));
				return false;
			}

			if (!IsSupportedSelectIndexCategory(IndexCategory))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("UnsupportedSelectIndexTypeCombination"),
					FString::Printf(
						TEXT("Select nodes currently support IndexPinCategory values bool, int, or enum in UE 5.7; found '%s'."),
						*IndexCategory),
					Command.NodeId));
				return false;
			}

			UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);

			if (IndexCategory == TEXT("enum"))
			{
				const FString EnumPath = GetCommandAttribute(Command, TEXT("IndexObjectPath"));
				UEnum* Enum = Cast<UEnum>(ResolveObjectReference(EnumPath));
				if (Enum == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("SelectEnumNotFound"),
						FString::Printf(TEXT("Unable to resolve select enum '%s'."), *EnumPath),
						Command.NodeId));
					return false;
				}

				SelectNode->SetEnum(Enum, true);
			}

			FinalizePlacedNode(Graph, SelectNode, Command.Position, Command.NodeId);

			if (IndexCategory != TEXT("enum"))
			{
				FEdGraphPinType IndexPinType;
				FString IndexTypeError;
				if (!BuildPinTypeFromAttributes(Command, TEXT("IndexPinCategory"), TEXT("IndexObjectPath"), IndexPinType, IndexTypeError))
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("InvalidSelectIndexType"),
						IndexTypeError,
						Command.NodeId));
					return false;
				}

				UEdGraphPin* IndexPin = SelectNode->GetIndexPin();
				if (IndexPin == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("MissingSelectIndexPin"),
						TEXT("Select node did not expose an index pin."),
						Command.NodeId));
					return false;
				}

				IndexPin->PinType = IndexPinType;
				SelectNode->ChangePinType(IndexPin);
			}

			if (IndexCategory == TEXT("bool"))
			{
				int32 RequestedOptions = 2;
				if (TryParseInt(GetCommandAttribute(Command, TEXT("NumOptions")), RequestedOptions) && RequestedOptions != 2)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("InvalidBoolSelectOptionCount"),
						TEXT("Bool select nodes always require exactly 2 options."),
						Command.NodeId));
					return false;
				}
			}
			else if (IndexCategory != TEXT("enum"))
			{
				int32 RequestedOptions = 0;
				if (!TryParseInt(GetCommandAttribute(Command, TEXT("NumOptions")), RequestedOptions) || RequestedOptions < 2)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("InvalidSelectOptionCount"),
						TEXT("Non-bool, non-enum select nodes require NumOptions >= 2."),
						Command.NodeId));
					return false;
				}

				TArray<UEdGraphPin*> ExistingOptionPins;
				SelectNode->GetOptionPins(ExistingOptionPins);
				for (int32 OptionIndex = ExistingOptionPins.Num(); OptionIndex < RequestedOptions; ++OptionIndex)
				{
					SelectNode->AddInputPin();
				}
			}

			FEdGraphPinType ValuePinType;
			FString ValueTypeError;
			if (!BuildPinTypeFromAttributes(Command, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), ValuePinType, ValueTypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidSelectValueType"),
					ValueTypeError,
					Command.NodeId));
				return false;
			}

			TArray<UEdGraphPin*> OptionPins;
			SelectNode->GetOptionPins(OptionPins);
			if (OptionPins.Num() == 0 || OptionPins[0] == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingSelectOptionPins"),
					TEXT("Select node did not expose option pins after configuration."),
					Command.NodeId));
				return false;
			}

			OptionPins[0]->PinType = ValuePinType;
			SelectNode->ChangePinType(OptionPins[0]);
			NewNode = SelectNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.FormatText"))
		{
			const FString FormatPattern = GetCommandAttribute(Command, TEXT("FormatPattern"));
			if (FormatPattern.IsEmpty())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingFormatPattern"),
					TEXT("Format text node execution requires attribute FormatPattern."),
					Command.NodeId));
				return false;
			}

			UK2Node_FormatText* FormatTextNode = NewObject<UK2Node_FormatText>(Graph);
			FinalizePlacedNode(Graph, FormatTextNode, Command.Position, Command.NodeId);

			UEdGraphPin* const FormatPin = FormatTextNode->GetFormatPin();
			if (FormatPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingFormatTextPin"),
					TEXT("Format text node did not expose a Format pin."),
					Command.NodeId));
				return false;
			}

			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			K2Schema->TrySetDefaultText(*FormatPin, FText::FromString(FormatPattern), true);
			FormatTextNode->PinDefaultValueChanged(FormatPin);
			NewNode = FormatTextNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.SwitchInt"))
		{
			const TArray<FName> CasePins = GetPlannedExecOutputPinNames(Command);
			if (CasePins.Num() == 0)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingSwitchIntCases"),
					TEXT("Switch int nodes require at least one planned case exec pin."),
					Command.NodeId));
				return false;
			}

			TArray<int32> CaseValues;
			for (const FName CasePin : CasePins)
			{
				int32 ParsedValue = 0;
				if (!TryParseInt(CasePin.ToString(), ParsedValue))
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("InvalidSwitchIntCaseName"),
						FString::Printf(TEXT("Switch int case pin '%s' is not a valid integer."), *CasePin.ToString()),
						Command.NodeId));
					return false;
				}

				CaseValues.Add(ParsedValue);
			}

			CaseValues.Sort();
			const int32 StartIndex = CaseValues[0];
			for (int32 CaseIndex = 1; CaseIndex < CaseValues.Num(); ++CaseIndex)
			{
				if (CaseValues[CaseIndex] != StartIndex + CaseIndex)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("NonContiguousSwitchIntCases"),
						TEXT("Switch int cases must form a contiguous ascending integer range."),
						Command.NodeId));
					return false;
				}
			}

			UK2Node_SwitchInteger* SwitchNode = NewObject<UK2Node_SwitchInteger>(Graph);
			SwitchNode->StartIndex = StartIndex;
			FinalizePlacedNode(Graph, SwitchNode, Command.Position, Command.NodeId);
			for (int32 CaseIndex = 0; CaseIndex < CasePins.Num(); ++CaseIndex)
			{
				SwitchNode->AddPinToSwitchNode();
			}

			NewNode = SwitchNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.SwitchString"))
		{
			const TArray<FName> CasePins = GetPlannedExecOutputPinNames(Command);
			if (CasePins.Num() == 0)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingSwitchStringCases"),
					TEXT("Switch string nodes require at least one planned case exec pin."),
					Command.NodeId));
				return false;
			}

			UK2Node_SwitchString* SwitchNode = NewObject<UK2Node_SwitchString>(Graph);
			SwitchNode->PinNames = CasePins;

			bool bCaseSensitive = false;
			if (TryParseBoolAttribute(Command, TEXT("CaseSensitive"), bCaseSensitive))
			{
				SwitchNode->bIsCaseSensitive = bCaseSensitive;
			}

			FinalizePlacedNode(Graph, SwitchNode, Command.Position, Command.NodeId);
			NewNode = SwitchNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.SwitchEnum"))
		{
			const FString EnumPath = GetCommandAttribute(Command, TEXT("EnumPath"));
			UEnum* Enum = Cast<UEnum>(ResolveObjectReference(EnumPath));
			if (Enum == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("SwitchEnumNotFound"),
					FString::Printf(TEXT("Unable to resolve switch enum '%s'."), *EnumPath),
					Command.NodeId));
				return false;
			}

			UK2Node_SwitchEnum* SwitchNode = NewObject<UK2Node_SwitchEnum>(Graph);
			SwitchNode->SetEnum(Enum);
			FinalizePlacedNode(Graph, SwitchNode, Command.Position, Command.NodeId);
			NewNode = SwitchNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.MakeStruct"))
		{
			const FString StructPath = GetCommandAttribute(Command, TEXT("StructPath"));
			UScriptStruct* const StructType = ResolveStructReference(StructPath);
			if (StructType == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MakeStructTypeNotFound"),
					FString::Printf(TEXT("Unable to resolve make struct type '%s'."), *StructPath),
					Command.NodeId));
				return false;
			}

			const FString NativeMakePath = StructType->GetMetaData(FBlueprintMetadata::MD_NativeMakeFunction);
			if (!NativeMakePath.IsEmpty())
			{
				UFunction* const NativeMakeFunction = Cast<UFunction>(ResolveObjectReference(NativeMakePath));
				if (NativeMakeFunction == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("NativeMakeFunctionNotFound"),
						FString::Printf(TEXT("Unable to resolve native make function '%s' for struct '%s'."), *NativeMakePath, *StructType->GetPathName()),
						Command.NodeId));
					return false;
				}

				UK2Node_CallFunction* MakeStructCallNode = NewObject<UK2Node_CallFunction>(Graph);
				MakeStructCallNode->SetFromFunction(NativeMakeFunction);
				FinalizePlacedNode(Graph, MakeStructCallNode, Command.Position, Command.NodeId);
				NewNode = MakeStructCallNode;
			}
			else
			{
				UK2Node_MakeStruct* MakeStructNode = NewObject<UK2Node_MakeStruct>(Graph);
				if (!FinalizePlacedStructNode(Graph, MakeStructNode, StructType, Command.Position, Command.NodeId))
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("MakeStructInitializationFailed"),
						FString::Printf(TEXT("Unable to initialize make struct node for '%s'."), *StructType->GetPathName()),
						Command.NodeId));
					return false;
				}

				NewNode = MakeStructNode;
			}
		}
		else if (Command.Name == TEXT("Vergil.K2.BreakStruct"))
		{
			const FString StructPath = GetCommandAttribute(Command, TEXT("StructPath"));
			UScriptStruct* const StructType = ResolveStructReference(StructPath);
			if (StructType == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("BreakStructTypeNotFound"),
					FString::Printf(TEXT("Unable to resolve break struct type '%s'."), *StructPath),
					Command.NodeId));
				return false;
			}

			const FString NativeBreakPath = StructType->GetMetaData(FBlueprintMetadata::MD_NativeBreakFunction);
			if (!NativeBreakPath.IsEmpty())
			{
				UFunction* const NativeBreakFunction = Cast<UFunction>(ResolveObjectReference(NativeBreakPath));
				if (NativeBreakFunction == nullptr)
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("NativeBreakFunctionNotFound"),
						FString::Printf(TEXT("Unable to resolve native break function '%s' for struct '%s'."), *NativeBreakPath, *StructType->GetPathName()),
						Command.NodeId));
					return false;
				}

				UK2Node_CallFunction* BreakStructCallNode = NewObject<UK2Node_CallFunction>(Graph);
				BreakStructCallNode->SetFromFunction(NativeBreakFunction);
				FinalizePlacedNode(Graph, BreakStructCallNode, Command.Position, Command.NodeId);
				NewNode = BreakStructCallNode;
			}
			else
			{
				UK2Node_BreakStruct* BreakStructNode = NewObject<UK2Node_BreakStruct>(Graph);
				if (!FinalizePlacedStructNode(Graph, BreakStructNode, StructType, Command.Position, Command.NodeId))
				{
					Diagnostics.Add(FVergilDiagnostic::Make(
						EVergilDiagnosticSeverity::Error,
						TEXT("BreakStructInitializationFailed"),
						FString::Printf(TEXT("Unable to initialize break struct node for '%s'."), *StructType->GetPathName()),
						Command.NodeId));
					return false;
				}

				NewNode = BreakStructNode;
			}
		}
		else if (Command.Name == TEXT("Vergil.K2.MakeArray"))
		{
			FEdGraphPinType ElementPinType;
			FString ElementTypeError;
			if (!BuildPinTypeFromAttributes(Command, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), ElementPinType, ElementTypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeArrayValueType"),
					ElementTypeError,
					Command.NodeId));
				return false;
			}

			int32 NumInputs = 1;
			TryParseInt(GetCommandAttribute(Command, TEXT("NumInputs")), NumInputs);
			if (NumInputs < 1)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeArrayInputCount"),
					TEXT("Make array nodes require NumInputs >= 1."),
					Command.NodeId));
				return false;
			}

			UK2Node_MakeArray* MakeArrayNode = NewObject<UK2Node_MakeArray>(Graph);
			FinalizePlacedNode(Graph, MakeArrayNode, Command.Position, Command.NodeId);

			UEdGraphPin* const OutputPin = MakeArrayNode->GetOutputPin();
			if (OutputPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingMakeArrayOutputPin"),
					TEXT("Make array node did not expose an output pin."),
					Command.NodeId));
				return false;
			}

			OutputPin->PinType = ElementPinType;
			OutputPin->PinType.ContainerType = EPinContainerType::Array;

			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			for (UEdGraphPin* Pin : MakeArrayNode->Pins)
			{
				if (Pin == nullptr || Pin == OutputPin || Pin->Direction != EGPD_Input || Pin->ParentPin != nullptr)
				{
					continue;
				}

				Pin->PinType = ElementPinType;
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}

			for (int32 InputIndex = 1; InputIndex < NumInputs; ++InputIndex)
			{
				MakeArrayNode->AddInputPin();
			}

			NewNode = MakeArrayNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.MakeSet"))
		{
			FEdGraphPinType ElementPinType;
			FString ElementTypeError;
			if (!BuildPinTypeFromAttributes(Command, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), ElementPinType, ElementTypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeSetValueType"),
					ElementTypeError,
					Command.NodeId));
				return false;
			}

			int32 NumInputs = 1;
			TryParseInt(GetCommandAttribute(Command, TEXT("NumInputs")), NumInputs);
			if (NumInputs < 1)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeSetInputCount"),
					TEXT("Make set nodes require NumInputs >= 1."),
					Command.NodeId));
				return false;
			}

			UK2Node_MakeSet* MakeSetNode = NewObject<UK2Node_MakeSet>(Graph);
			FinalizePlacedNode(Graph, MakeSetNode, Command.Position, Command.NodeId);

			UEdGraphPin* const OutputPin = MakeSetNode->GetOutputPin();
			if (OutputPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingMakeSetOutputPin"),
					TEXT("Make set node did not expose an output pin."),
					Command.NodeId));
				return false;
			}

			OutputPin->PinType = ElementPinType;
			OutputPin->PinType.ContainerType = EPinContainerType::Set;

			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			for (UEdGraphPin* Pin : MakeSetNode->Pins)
			{
				if (Pin == nullptr || Pin == OutputPin || Pin->Direction != EGPD_Input || Pin->ParentPin != nullptr)
				{
					continue;
				}

				Pin->PinType = ElementPinType;
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}

			for (int32 InputIndex = 1; InputIndex < NumInputs; ++InputIndex)
			{
				MakeSetNode->AddInputPin();
			}

			NewNode = MakeSetNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.MakeMap"))
		{
			FEdGraphPinType KeyPinType;
			FString KeyTypeError;
			if (!BuildPinTypeFromAttributes(Command, TEXT("KeyPinCategory"), TEXT("KeyObjectPath"), KeyPinType, KeyTypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeMapKeyType"),
					KeyTypeError,
					Command.NodeId));
				return false;
			}

			FEdGraphPinType ValuePinType;
			FString ValueTypeError;
			if (!BuildPinTypeFromAttributes(Command, TEXT("ValuePinCategory"), TEXT("ValueObjectPath"), ValuePinType, ValueTypeError))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeMapValueType"),
					ValueTypeError,
					Command.NodeId));
				return false;
			}

			int32 NumPairs = 1;
			TryParseInt(GetCommandAttribute(Command, TEXT("NumPairs")), NumPairs);
			if (NumPairs < 1)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidMakeMapPairCount"),
					TEXT("Make map nodes require NumPairs >= 1."),
					Command.NodeId));
				return false;
			}

			UK2Node_MakeMap* MakeMapNode = NewObject<UK2Node_MakeMap>(Graph);
			FinalizePlacedNode(Graph, MakeMapNode, Command.Position, Command.NodeId);

			UEdGraphPin* const OutputPin = MakeMapNode->GetOutputPin();
			if (OutputPin == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingMakeMapOutputPin"),
					TEXT("Make map node did not expose an output pin."),
					Command.NodeId));
				return false;
			}

			OutputPin->PinType = KeyPinType;
			OutputPin->PinType.ContainerType = EPinContainerType::Map;
			OutputPin->PinType.PinValueType = FEdGraphTerminalType::FromPinType(ValuePinType);

			TArray<UEdGraphPin*> KeyPins;
			TArray<UEdGraphPin*> ValuePins;
			MakeMapNode->GetKeyAndValuePins(KeyPins, ValuePins);

			const UEdGraphSchema_K2* const K2Schema = GetDefault<UEdGraphSchema_K2>();
			for (UEdGraphPin* Pin : KeyPins)
			{
				if (Pin == nullptr)
				{
					continue;
				}

				Pin->PinType = KeyPinType;
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}

			for (UEdGraphPin* Pin : ValuePins)
			{
				if (Pin == nullptr)
				{
					continue;
				}

				Pin->PinType = ValuePinType;
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}

			for (int32 PairIndex = 1; PairIndex < NumPairs; ++PairIndex)
			{
				MakeMapNode->AddInputPin();
			}

			NewNode = MakeMapNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.VariableGet"))
		{
			FProperty* Property = ResolveVariableProperty(Blueprint, Command, Diagnostics);
			if (Property == nullptr)
			{
				return false;
			}

			if (!ValidateImpureVariableGetPinShape(Command, Diagnostics))
			{
				return false;
			}

			if (!ValidateVariableGetVariantSupport(Command, Property, Diagnostics))
			{
				return false;
			}

			UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
			GetNode->SetFromProperty(Property, true, Property->GetOwnerClass());
			FinalizePlacedNode(Graph, GetNode, Command.Position, Command.NodeId);
			if (HasPlannedExecPins(Command))
			{
				if (!ApplyVariableGetVariantToNode(GetNode, Property, Command.NodeId, Diagnostics))
				{
					return false;
				}
			}
			NewNode = GetNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.VariableSet"))
		{
			FProperty* Property = ResolveVariableProperty(Blueprint, Command, Diagnostics);
			if (Property == nullptr)
			{
				return false;
			}

			UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(Graph);
			SetNode->SetFromProperty(Property, true, Property->GetOwnerClass());
			FinalizePlacedNode(Graph, SetNode, Command.Position, Command.NodeId);
			NewNode = SetNode;
		}
		else
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("UnsupportedNodeExecution"),
				FString::Printf(TEXT("Execution is not implemented for node descriptor '%s'."), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		State.NodesById.Add(Command.NodeId, NewNode);
		return RegisterPlannedPins(Command, NewNode, State, Diagnostics);
	}

	bool ExecuteSetNodeMetadata(
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraphNode** NodePtr = State.NodesById.Find(Command.NodeId);
		if (NodePtr == nullptr || *NodePtr == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MetadataTargetMissing"),
				FString::Printf(TEXT("No executed node exists for metadata command '%s'."), *Command.Name.ToString()),
				Command.NodeId));
			return false;
		}

		UEdGraphNode* Node = *NodePtr;
		Node->Modify();

		if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
		{
			if (Command.Name == TEXT("CommentText") || Command.Name == TEXT("Title"))
			{
				CommentNode->NodeComment = Command.StringValue;
				return true;
			}

			if (Command.Name == TEXT("NodeWidth") || Command.Name == TEXT("CommentWidth"))
			{
				float Width = 0.0f;
				if (TryParseFloat(Command.StringValue, Width))
				{
					CommentNode->NodeWidth = Width;
					return true;
				}
			}

			if (Command.Name == TEXT("NodeHeight") || Command.Name == TEXT("CommentHeight"))
			{
				float Height = 0.0f;
				if (TryParseFloat(Command.StringValue, Height))
				{
					CommentNode->NodeHeight = Height;
					return true;
				}
			}

			if (Command.Name == TEXT("FontSize"))
			{
				int32 FontSize = 0;
				if (TryParseInt(Command.StringValue, FontSize))
				{
					CommentNode->FontSize = FontSize;
					return true;
				}
			}

			if (Command.Name == TEXT("Color") || Command.Name == TEXT("CommentColor"))
			{
				FColor ParsedColor = FColor::FromHex(Command.StringValue);
				CommentNode->CommentColor = FLinearColor(ParsedColor);
				return true;
			}

			if (Command.Name == TEXT("ShowBubbleWhenZoomed"))
			{
				bool bShowBubbleWhenZoomed = false;
				if (TryParseBoolString(Command.StringValue, bShowBubbleWhenZoomed))
				{
					CommentNode->bCommentBubbleVisible_InDetailsPanel = bShowBubbleWhenZoomed;
					CommentNode->bCommentBubbleVisible = bShowBubbleWhenZoomed;
					CommentNode->bCommentBubblePinned = bShowBubbleWhenZoomed;
					return true;
				}
			}

			if (Command.Name == TEXT("ColorBubble"))
			{
				bool bColorBubble = false;
				if (TryParseBoolString(Command.StringValue, bColorBubble))
				{
					CommentNode->bColorCommentBubble = bColorBubble;
					return true;
				}
			}

			if (Command.Name == TEXT("MoveMode"))
			{
				ECommentBoxMode::Type MoveMode = ECommentBoxMode::GroupMovement;
				if (TryParseCommentMoveMode(Command.StringValue, MoveMode))
				{
					CommentNode->MoveMode = MoveMode;
					return true;
				}
			}
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Warning,
			TEXT("MetadataIgnored"),
			FString::Printf(TEXT("Metadata key '%s' was ignored for the executed node."), *Command.Name.ToString()),
			Command.NodeId));
		return true;
	}

	bool ExecuteConnectPins(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		UEdGraph* Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
		if (Graph == nullptr)
		{
			return false;
		}

		UEdGraphPin* const* SourcePinPtr = State.PinsById.Find(Command.SourcePinId);
		UEdGraphPin* const* TargetPinPtr = State.PinsById.Find(Command.TargetPinId);
		if (SourcePinPtr == nullptr || TargetPinPtr == nullptr || *SourcePinPtr == nullptr || *TargetPinPtr == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("PinResolutionFailed"),
				TEXT("Unable to resolve source or target pins for connection command."),
				Command.SourceNodeId));
			return false;
		}

		if ((*SourcePinPtr)->LinkedTo.Contains(*TargetPinPtr))
		{
			return true;
		}

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("GraphSchemaMissing"),
				FString::Printf(TEXT("Graph '%s' has no schema for pin connection."), *Graph->GetName()),
				Command.SourceNodeId));
			return false;
		}

		const FPinConnectionResponse Response = Schema->CanCreateConnection(*SourcePinPtr, *TargetPinPtr);
		if (Response.Response == ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW)
		{
			FName SpecializedCode = NAME_None;
			FString SpecializedMessage;
			if (TryBuildSelectSwitchTypeMismatchDiagnostic(*SourcePinPtr, *TargetPinPtr, SpecializedCode, SpecializedMessage))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					SpecializedCode,
					SpecializedMessage,
					Command.SourceNodeId));
				return false;
			}

			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("PinConnectionRejected"),
				Response.Message.ToString(),
				Command.SourceNodeId));
			return false;
		}

		if (!Schema->TryCreateConnection(*SourcePinPtr, *TargetPinPtr))
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("PinConnectionFailed"),
				TEXT("Schema rejected connection creation."),
				Command.SourceNodeId));
			return false;
		}

		return true;
	}

	bool ExecuteStandardMacroNode(
		const FVergilStandardMacroCommand& MacroCommand,
		UEdGraph* Graph,
		const FVergilCompilerCommand& Command,
		UEdGraphNode*& OutNewNode,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		const FString MacroBlueprintPath = GetCommandAttribute(Command, TEXT("MacroBlueprintPath")).IsEmpty()
			? StandardMacrosBlueprintPath
			: GetCommandAttribute(Command, TEXT("MacroBlueprintPath"));
		const FName MacroGraphName = GetCommandAttribute(Command, TEXT("MacroGraphName")).IsEmpty()
			? MacroCommand.DefaultMacroGraphName
			: FName(*GetCommandAttribute(Command, TEXT("MacroGraphName")));

		UEdGraph* const MacroGraph = ResolveMacroGraphReference(MacroBlueprintPath, MacroGraphName);
		if (MacroGraph == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				MacroCommand.NotFoundDiagnosticCode,
				FString::Printf(TEXT("Unable to resolve macro graph '%s' from '%s'."), *MacroGraphName.ToString(), *MacroBlueprintPath),
				Command.NodeId));
			return false;
		}

		UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
		MacroNode->SetMacroGraph(MacroGraph);
		FinalizePlacedNode(Graph, MacroNode, Command.Position, Command.NodeId);
		MacroNode->ReconstructNode();
		OutNewNode = MacroNode;
		return true;
	}

	bool ValidateCommandPlan(
		UBlueprint* Blueprint,
		const TArray<FVergilCompilerCommand>& Commands,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		if (Blueprint == nullptr)
		{
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("MissingBlueprint"),
				TEXT("Cannot execute a command plan without a target blueprint.")));
			return false;
		}

		bool bIsValid = true;
		TMap<FGuid, int32> PlannedNodeIndices;
		TMap<FGuid, FName> PlannedNodeGraphs;
		TMap<FGuid, int32> PlannedPinIndices;
		TMap<FGuid, FGuid> PlannedPinOwners;
		TMap<FGuid, FName> PlannedPinGraphs;

		auto AddValidationError = [&](const FName Code, const FString& Message, const FGuid& SourceId = FGuid())
		{
			bIsValid = false;
			Diagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Error, Code, Message, SourceId));
		};

		auto ValidateOptionalBoolAttribute = [&](const FVergilCompilerCommand& Command, const FName Key, const FName ErrorCode) -> void
		{
			const FString RawValue = GetCommandAttribute(Command, Key);
			if (RawValue.IsEmpty())
			{
				return;
			}

			bool bBoolValue = false;
			if (!TryParseBoolAttribute(Command, Key, bBoolValue))
			{
				AddValidationError(
					ErrorCode,
					FString::Printf(TEXT("Command attribute '%s' must be 'true', 'false', '1', or '0'."), *Key.ToString()),
					Command.NodeId);
			}
		};

		auto ValidateGraphBackedEnsureName = [&](const FVergilCompilerCommand& Command, const TCHAR* ContextLabel) -> void
		{
			const bool bHasExplicitGraphName = !Command.SecondaryName.IsNone()
				|| (!Command.GraphName.IsNone() && Command.GraphName != TEXT("EventGraph"));
			if (!bHasExplicitGraphName)
			{
				AddValidationError(
					TEXT("CommandValidationGraphNameMissing"),
					FString::Printf(TEXT("%s commands require an explicit graph name via GraphName or SecondaryName."), ContextLabel),
					Command.NodeId);
			}
		};

		auto ValidateSignatureCount = [&](const FVergilCompilerCommand& Command,
			const TCHAR* Key,
			const FName InvalidCode,
			const FName NegativeCode,
			const FString& ContextLabel,
			const TCHAR* CountLabel,
			int32& OutCount) -> bool
		{
			if (!TryGetCommandCountAttribute(Command, Key, OutCount, Diagnostics, InvalidCode))
			{
				bIsValid = false;
				return false;
			}

			if (OutCount < 0)
			{
				AddValidationError(
					NegativeCode,
					FString::Printf(TEXT("%s cannot declare a negative %s count."), *ContextLabel, CountLabel),
					Command.NodeId);
				return false;
			}

			return true;
		};

		auto ValidatePlannedNodeGraph = [&](const FVergilCompilerCommand& Command, const FName ErrorCode, const TCHAR* ContextLabel) -> void
		{
			const FName* const PlannedGraph = PlannedNodeGraphs.Find(Command.NodeId);
			if (PlannedGraph != nullptr && *PlannedGraph != ResolveCommandGraphName(Command))
			{
				AddValidationError(
					ErrorCode,
					FString::Printf(
						TEXT("%s targets graph '%s', but node '%s' belongs to graph '%s'."),
						ContextLabel,
						*ResolveCommandGraphName(Command).ToString(),
						*Command.NodeId.ToString(),
						*PlannedGraph->ToString()),
					Command.NodeId);
			}
		};

		for (int32 CommandIndex = 0; CommandIndex < Commands.Num(); ++CommandIndex)
		{
			const FVergilCompilerCommand& Command = Commands[CommandIndex];
			if (Command.Type != EVergilCommandType::AddNode)
			{
				continue;
			}

			if (!Command.NodeId.IsValid())
			{
				AddValidationError(
					TEXT("CommandValidationNodeIdMissing"),
					FString::Printf(TEXT("AddNode command '%s' requires a valid node id."), *Command.Name.ToString()),
					Command.NodeId);
			}
			else if (PlannedNodeIndices.Contains(Command.NodeId))
			{
				AddValidationError(
					TEXT("CommandValidationNodeIdDuplicate"),
					FString::Printf(TEXT("Command plan reuses node id '%s' across multiple AddNode commands."), *Command.NodeId.ToString()),
					Command.NodeId);
			}
			else
			{
				PlannedNodeIndices.Add(Command.NodeId, CommandIndex);
				PlannedNodeGraphs.Add(Command.NodeId, ResolveCommandGraphName(Command));
			}

			for (const FVergilPlannedPin& PlannedPin : Command.PlannedPins)
			{
				if (!PlannedPin.PinId.IsValid())
				{
					continue;
				}

				if (PlannedPin.Name.IsNone())
				{
					AddValidationError(
						TEXT("CommandValidationPinNameMissing"),
						FString::Printf(TEXT("AddNode command '%s' contains a planned pin with a valid id but no name."), *Command.Name.ToString()),
						Command.NodeId);
					continue;
				}

				if (PlannedPinIndices.Contains(PlannedPin.PinId))
				{
					AddValidationError(
						TEXT("CommandValidationPinIdDuplicate"),
						FString::Printf(TEXT("Command plan reuses pin id '%s' across multiple planned pins."), *PlannedPin.PinId.ToString()),
						Command.NodeId);
					continue;
				}

				PlannedPinIndices.Add(PlannedPin.PinId, CommandIndex);
				PlannedPinOwners.Add(PlannedPin.PinId, Command.NodeId);
				PlannedPinGraphs.Add(PlannedPin.PinId, ResolveCommandGraphName(Command));
			}
		}

		for (int32 CommandIndex = 0; CommandIndex < Commands.Num(); ++CommandIndex)
		{
			const FVergilCompilerCommand& Command = Commands[CommandIndex];
			const FName GraphName = ResolveCommandGraphName(Command);

			switch (Command.Type)
			{
			case EVergilCommandType::EnsureDispatcher:
				if (Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidEnsureDispatcherCommand"), TEXT("Dispatcher creation requires a target blueprint and dispatcher name."), Command.NodeId);
				}
				break;

			case EVergilCommandType::AddDispatcherParameter:
				if (Command.SecondaryName.IsNone() || Command.Name.IsNone())
				{
					AddValidationError(TEXT("InvalidAddDispatcherParameterCommand"), TEXT("Dispatcher parameter commands require a target blueprint, dispatcher name, and parameter name."), Command.NodeId);
				}
				else
				{
					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("PinCategory")),
						GetCommandAttribute(Command, TEXT("ObjectPath")),
						FString::Printf(TEXT("Dispatcher '%s' parameter '%s'"), *Command.SecondaryName.ToString(), *Command.Name.ToString()),
						TEXT("DispatcherParameterCategoryMissing"),
						TEXT("DispatcherParameterCategoryUnsupported"),
						TEXT("DispatcherParameterObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}
				}
				ValidateOptionalBoolAttribute(Command, TEXT("bIsArray"), TEXT("DispatcherParameterArrayFlagInvalid"));
				break;

			case EVergilCommandType::SetBlueprintMetadata:
				if (Command.Name.IsNone())
				{
					AddValidationError(TEXT("InvalidSetBlueprintMetadataCommand"), TEXT("Blueprint metadata commands require a metadata key in Name."), Command.NodeId);
				}
				else if (!IsSupportedBlueprintMetadataKey(Command.Name))
				{
					AddValidationError(TEXT("BlueprintMetadataKeyUnsupported"), FString::Printf(TEXT("Blueprint metadata key '%s' is not currently supported."), *Command.Name.ToString()), Command.NodeId);
				}
				break;

			case EVergilCommandType::EnsureVariable:
				if (Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidEnsureVariableCommand"), TEXT("Variable creation requires a target blueprint and variable name."), Command.NodeId);
				}
				else
				{
					if (!ValidateCommandVariableTypeShape(
						Command,
						FString::Printf(TEXT("Variable '%s'"), *Command.SecondaryName.ToString()),
						Diagnostics))
					{
						bIsValid = false;
					}
				}
				ValidateOptionalBoolAttribute(Command, TEXT("bInstanceEditable"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bBlueprintReadOnly"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bExposeOnSpawn"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bPrivate"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bTransient"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bSaveGame"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bAdvancedDisplay"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bDeprecated"), TEXT("VariableFlagInvalid"));
				ValidateOptionalBoolAttribute(Command, TEXT("bExposeToCinematics"), TEXT("VariableFlagInvalid"));
				break;

			case EVergilCommandType::SetVariableMetadata:
				if (Command.SecondaryName.IsNone() || Command.Name.IsNone())
				{
					AddValidationError(TEXT("InvalidSetVariableMetadataCommand"), TEXT("Variable metadata commands require a target blueprint, variable name, and metadata key."), Command.NodeId);
				}
				break;

			case EVergilCommandType::SetVariableDefault:
				if (Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidSetVariableDefaultCommand"), TEXT("Variable default commands require a target blueprint and variable name."), Command.NodeId);
				}
				break;

			case EVergilCommandType::EnsureFunctionGraph:
			{
				ValidateGraphBackedEnsureName(Command, TEXT("Function graph"));
				if (!HasFunctionSignatureAttributes(Command))
				{
					break;
				}

				int32 InputCount = 0;
				int32 OutputCount = 0;
				const FString GraphLabel = FString::Printf(TEXT("Function graph '%s'"), *GraphName.ToString());
				if (!ValidateSignatureCount(Command, TEXT("InputCount"), TEXT("FunctionSignatureCountInvalid"), TEXT("FunctionSignatureCountNegative"), GraphLabel, TEXT("input"), InputCount)
					|| !ValidateSignatureCount(Command, TEXT("OutputCount"), TEXT("FunctionSignatureCountInvalid"), TEXT("FunctionSignatureCountNegative"), GraphLabel, TEXT("output"), OutputCount))
				{
					break;
				}

				for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
				{
					const FName ParameterName = *GetCommandAttribute(Command, MakeSignatureAttributeKey(TEXT("Input"), InputIndex, TEXT("Name")));
					if (ParameterName.IsNone())
					{
						AddValidationError(TEXT("FunctionSignatureParameterNameMissing"), FString::Printf(TEXT("Function graph '%s' input parameter %d is missing a name."), *GraphName.ToString(), InputIndex), Command.NodeId);
						continue;
					}

					if (!ValidateCommandSignatureTypeShape(
						Command,
						TEXT("Input"),
						InputIndex,
						FString::Printf(TEXT("Function graph '%s' input '%s'"), *GraphName.ToString(), *ParameterName.ToString()),
						Diagnostics))
					{
						bIsValid = false;
					}
				}

				for (int32 OutputIndex = 0; OutputIndex < OutputCount; ++OutputIndex)
				{
					const FName ParameterName = *GetCommandAttribute(Command, MakeSignatureAttributeKey(TEXT("Output"), OutputIndex, TEXT("Name")));
					if (ParameterName.IsNone())
					{
						AddValidationError(TEXT("FunctionSignatureParameterNameMissing"), FString::Printf(TEXT("Function graph '%s' output parameter %d is missing a name."), *GraphName.ToString(), OutputIndex), Command.NodeId);
						continue;
					}

					if (!ValidateCommandSignatureTypeShape(
						Command,
						TEXT("Output"),
						OutputIndex,
						FString::Printf(TEXT("Function graph '%s' output '%s'"), *GraphName.ToString(), *ParameterName.ToString()),
						Diagnostics))
					{
						bIsValid = false;
					}
				}

				bool bPure = false;
				if (!TryParseBoolAttribute(Command, TEXT("bPure"), bPure))
				{
					AddValidationError(TEXT("FunctionPurityMissing"), FString::Printf(TEXT("Function graph '%s' is missing the bPure attribute."), *GraphName.ToString()), Command.NodeId);
				}

				const FString AccessSpecifier = GetCommandAttribute(Command, TEXT("AccessSpecifier")).TrimStartAndEnd().ToLower();
				if (!AccessSpecifier.IsEmpty()
					&& AccessSpecifier != TEXT("public")
					&& AccessSpecifier != TEXT("protected")
					&& AccessSpecifier != TEXT("private"))
				{
					AddValidationError(TEXT("FunctionAccessSpecifierInvalid"), FString::Printf(TEXT("Function graph '%s' uses unsupported access specifier '%s'."), *GraphName.ToString(), *AccessSpecifier), Command.NodeId);
				}
				break;
			}

			case EVergilCommandType::EnsureMacroGraph:
			{
				ValidateGraphBackedEnsureName(Command, TEXT("Macro graph"));
				if (!HasMacroSignatureAttributes(Command))
				{
					break;
				}

				int32 InputCount = 0;
				int32 OutputCount = 0;
				const FString GraphLabel = FString::Printf(TEXT("Macro graph '%s'"), *GraphName.ToString());
				if (!ValidateSignatureCount(Command, TEXT("InputCount"), TEXT("MacroSignatureCountInvalid"), TEXT("MacroSignatureCountNegative"), GraphLabel, TEXT("input"), InputCount)
					|| !ValidateSignatureCount(Command, TEXT("OutputCount"), TEXT("MacroSignatureCountInvalid"), TEXT("MacroSignatureCountNegative"), GraphLabel, TEXT("output"), OutputCount))
				{
					break;
				}

				auto ValidateMacroPins = [&](const TCHAR* Prefix, const int32 Count)
				{
					for (int32 PinIndex = 0; PinIndex < Count; ++PinIndex)
					{
						const FName ParameterName = *GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("Name")));
						if (ParameterName.IsNone())
						{
							AddValidationError(TEXT("MacroSignatureParameterNameMissing"), FString::Printf(TEXT("Macro graph '%s' %s pin %d is missing a name."), *GraphName.ToString(), Prefix, PinIndex), Command.NodeId);
							continue;
						}

						ValidateOptionalBoolAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("bExec")), TEXT("MacroSignatureExecFlagInvalid"));

						bool bIsExec = false;
						if (TryParseBoolAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("bExec")), bIsExec) && bIsExec)
						{
							if (!GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("PinCategory"))).TrimStartAndEnd().IsEmpty()
								|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("ObjectPath"))).TrimStartAndEnd().IsEmpty()
								|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("ContainerType"))).TrimStartAndEnd().IsEmpty()
								|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("ValuePinCategory"))).TrimStartAndEnd().IsEmpty()
								|| !GetCommandAttribute(Command, MakeSignatureAttributeKey(Prefix, PinIndex, TEXT("ValueObjectPath"))).TrimStartAndEnd().IsEmpty())
							{
								AddValidationError(TEXT("MacroExecPinTypeUnexpected"), FString::Printf(TEXT("Macro graph '%s' exec pin '%s' cannot declare data-type metadata."), *GraphName.ToString(), *ParameterName.ToString()), Command.NodeId);
							}

							continue;
						}

						if (!ValidateCommandSignatureTypeShape(
							Command,
							Prefix,
							PinIndex,
							FString::Printf(TEXT("Macro graph '%s' pin '%s'"), *GraphName.ToString(), *ParameterName.ToString()),
							Diagnostics))
						{
							bIsValid = false;
						}
					}
				};

				ValidateMacroPins(TEXT("Input"), InputCount);
				ValidateMacroPins(TEXT("Output"), OutputCount);
				break;
			}

			case EVergilCommandType::EnsureComponent:
				if (Command.SecondaryName.IsNone() || Command.StringValue.TrimStartAndEnd().IsEmpty())
				{
					AddValidationError(TEXT("InvalidEnsureComponentCommand"), TEXT("Component creation requires a target blueprint, component name, and component class path."), Command.NodeId);
				}
				break;

			case EVergilCommandType::AttachComponent:
				if (Command.Name.IsNone() || Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidAttachComponentCommand"), TEXT("Component attachment requires a target blueprint, child component name, and parent component name."), Command.NodeId);
				}
				break;

			case EVergilCommandType::SetComponentProperty:
				if (Command.Name.IsNone() || Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidSetComponentPropertyCommand"), TEXT("Component property commands require a target blueprint, component name, and property name."), Command.NodeId);
				}
				break;

			case EVergilCommandType::EnsureInterface:
				if (Command.StringValue.TrimStartAndEnd().IsEmpty())
				{
					AddValidationError(TEXT("InvalidEnsureInterfaceCommand"), TEXT("Interface commands require a target blueprint and interface class path."), Command.NodeId);
				}
				break;

			case EVergilCommandType::SetClassDefault:
				if (Command.Name.IsNone())
				{
					AddValidationError(TEXT("InvalidSetClassDefaultCommand"), TEXT("Class default commands require a target blueprint and property name."), Command.NodeId);
				}
				break;

			case EVergilCommandType::EnsureGraph:
				break;

			case EVergilCommandType::AddNode:
			{
				if (!Command.NodeId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationNodeIdMissing"), TEXT("AddNode commands require a valid node id."), Command.NodeId);
				}

				if (Command.Name.IsNone())
				{
					AddValidationError(TEXT("InvalidAddNodeCommand"), TEXT("AddNode commands require a node descriptor name."), Command.NodeId);
					break;
				}

				if (Command.Name == TEXT("Vergil.Comment")
					|| Command.Name == TEXT("Vergil.K2.Branch")
					|| Command.Name == TEXT("Vergil.K2.Sequence")
					|| Command.Name == TEXT("Vergil.K2.ForLoop")
					|| Command.Name == TEXT("Vergil.K2.ForLoopWithBreak")
					|| Command.Name == TEXT("Vergil.K2.DoOnce")
					|| Command.Name == TEXT("Vergil.K2.FlipFlop")
					|| Command.Name == TEXT("Vergil.K2.Gate")
					|| Command.Name == TEXT("Vergil.K2.WhileLoop")
					|| Command.Name == TEXT("Vergil.K2.Delay")
					|| Command.Name == TEXT("Vergil.K2.Self")
					|| Command.Name == TEXT("Vergil.K2.Reroute"))
				{
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.Event"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("InvalidEventCommand"), TEXT("Event node execution requires a parent class and event function name."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.CustomEvent"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("InvalidCustomEventCommand"), TEXT("Custom event execution requires the event name in SecondaryName."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.Call"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("MissingFunctionName"), TEXT("Call node execution requires SecondaryName to contain the function name."), Command.NodeId);
					}
					break;
				}

				if (const FVergilInterfaceInvocationCommand* const InterfaceInvocationCommand = FindInterfaceInvocationCommand(Command.Name))
				{
					UFunction* const InterfaceFunction = ResolveInterfaceFunction(Command, Diagnostics);
					if (InterfaceFunction == nullptr)
					{
						bIsValid = false;
					}
					else if (InterfaceInvocationCommand->bIsMessage && InterfaceFunction->HasAllFunctionFlags(FUNC_Static))
					{
						AddValidationError(TEXT("StaticInterfaceMessageUnsupported"), TEXT("Interface message nodes do not support static interface functions."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.CallDelegate")
					|| Command.Name == TEXT("Vergil.K2.BindDelegate")
					|| Command.Name == TEXT("Vergil.K2.RemoveDelegate")
					|| Command.Name == TEXT("Vergil.K2.ClearDelegate"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("MissingDelegatePropertyName"), TEXT("Delegate node execution requires SecondaryName to contain the delegate property name."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.CreateDelegate"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("InvalidCreateDelegateCommand"), TEXT("CreateDelegate node execution requires SecondaryName to contain the function name."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.Cast"))
				{
					if (Command.StringValue.TrimStartAndEnd().IsEmpty())
					{
						AddValidationError(TEXT("CastTargetClassMissing"), TEXT("Cast node execution requires StringValue to contain the target class path."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.ClassCast"))
				{
					UClass* TargetClass = nullptr;
					if (!ResolveNodeClassReference(
						Command.StringValue,
						TEXT("ClassCastTargetClassMissing"),
						TEXT("ClassCastTargetClassNotFound"),
						TEXT("class-cast node execution"),
						TargetClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.GetClassDefaults"))
				{
					UClass* SourceClass = nullptr;
					if (!ResolveNodeClassReference(
						Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ClassPath")) : Command.StringValue,
						TEXT("GetClassDefaultsClassMissing"),
						TEXT("GetClassDefaultsClassNotFound"),
						TEXT("GetClassDefaults node execution"),
						SourceClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
						break;
					}

					if (!ValidateGetClassDefaultsPlannedPins(Command, SourceClass, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (const FVergilLoadAssetCommand* const LoadAssetCommand = FindLoadAssetCommand(Command.Name))
				{
					if (GraphName == ConstructionScriptGraphName)
					{
						AddValidationError(
							TEXT("ConstructionScriptLoadAssetUnsupported"),
							FString::Printf(TEXT("%s is not supported on the UserConstructionScript graph because UE_5.7 async load nodes are latent."), *Command.Name.ToString()),
							Command.NodeId);
						break;
					}

					UClass* AssetClass = nullptr;
					if (!ResolveLoadAssetClass(
						Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("AssetClassPath")) : Command.StringValue,
						AssetClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
						break;
					}

					if (!ValidateLoadAssetPlannedPins(Command, *LoadAssetCommand, AssetClass, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.ConvertAsset"))
				{
					if (!ValidateConvertAssetPlannedPins(Command, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.SpawnActor"))
				{
					if (GraphName == ConstructionScriptGraphName)
					{
						AddValidationError(
							TEXT("ConstructionScriptSpawnActorUnsupported"),
							TEXT("Vergil.K2.SpawnActor is not supported on the UserConstructionScript graph."),
							Command.NodeId);
						break;
					}

					UClass* ActorClass = nullptr;
					if (!ResolveSpawnActorClass(
						Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ActorClassPath")) : Command.StringValue,
						ActorClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
						break;
					}

					if (!ValidateSpawnActorPlannedPins(Command, ActorClass, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.AddComponentByClass"))
				{
					UClass* ComponentClass = nullptr;
					if (!ResolveActorComponentClass(
						Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ComponentClassPath")) : Command.StringValue,
						TEXT("AddComponentClassMissing"),
						TEXT("AddComponentClassNotFound"),
						TEXT("AddComponentClassNotComponent"),
						TEXT("AddComponentByClass node execution"),
						ComponentClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
						break;
					}

					if (!ValidateAddComponentPlannedPins(Command, ComponentClass, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (const FVergilComponentLookupCommand* const LookupCommand = FindComponentLookupCommand(Command.Name))
				{
					UClass* ComponentClass = nullptr;
					if (!ResolveActorComponentClass(
						Command.StringValue.IsEmpty() ? GetCommandAttribute(Command, TEXT("ComponentClassPath")) : Command.StringValue,
						TEXT("ComponentLookupClassMissing"),
						TEXT("ComponentLookupClassNotFound"),
						TEXT("ComponentLookupClassNotComponent"),
						TEXT("component lookup node execution"),
						ComponentClass,
						Diagnostics,
						Command.NodeId))
					{
						bIsValid = false;
						break;
					}

					if (!ValidateComponentLookupPlannedPins(Command, *LookupCommand, ComponentClass, Diagnostics))
					{
						bIsValid = false;
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.Select"))
				{
					const FString IndexCategory = GetCommandAttribute(Command, TEXT("IndexPinCategory")).TrimStartAndEnd().ToLower();
					const bool bIndexTypeValid = ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("IndexPinCategory")),
						GetCommandAttribute(Command, TEXT("IndexObjectPath")),
						TEXT("Select node index pin"),
						TEXT("MissingSelectIndexCategory"),
						TEXT("InvalidSelectIndexType"),
						TEXT("SelectIndexObjectPathMissing"),
						Command.NodeId,
						Diagnostics);
					if (!bIndexTypeValid)
					{
						bIsValid = false;
					}
					else if (!IsSupportedSelectIndexCategory(IndexCategory))
					{
						AddValidationError(
							TEXT("UnsupportedSelectIndexTypeCombination"),
							FString::Printf(
								TEXT("Select nodes currently support IndexPinCategory values bool, int, or enum in UE 5.7; found '%s'."),
								*IndexCategory),
							Command.NodeId);
					}

					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("ValuePinCategory")),
						GetCommandAttribute(Command, TEXT("ValueObjectPath")),
						TEXT("Select node value pin"),
						TEXT("MissingSelectValueCategory"),
						TEXT("InvalidSelectValueType"),
						TEXT("SelectValueObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}

					if (IndexCategory == TEXT("bool"))
					{
						const FString RawOptions = GetCommandAttribute(Command, TEXT("NumOptions"));
						if (!RawOptions.TrimStartAndEnd().IsEmpty())
						{
							int32 RequestedOptions = 0;
							if (!TryParseInt(RawOptions, RequestedOptions) || RequestedOptions != 2)
							{
								AddValidationError(TEXT("InvalidBoolSelectOptionCount"), TEXT("Bool select nodes always require exactly 2 options."), Command.NodeId);
							}
						}
					}
					else if (IndexCategory != TEXT("enum"))
					{
						const FString RawOptions = GetCommandAttribute(Command, TEXT("NumOptions"));
						int32 RequestedOptions = 0;
						if (RawOptions.TrimStartAndEnd().IsEmpty() || !TryParseInt(RawOptions, RequestedOptions) || RequestedOptions < 2)
						{
							AddValidationError(TEXT("InvalidSelectOptionCount"), TEXT("Non-bool, non-enum select nodes require NumOptions >= 2."), Command.NodeId);
						}
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.FormatText"))
				{
					if (GetCommandAttribute(Command, TEXT("FormatPattern")).IsEmpty())
					{
						AddValidationError(TEXT("MissingFormatPattern"), TEXT("Format text node execution requires attribute FormatPattern."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.SwitchInt"))
				{
					const TArray<FName> CasePins = GetPlannedExecOutputPinNames(Command);
					if (CasePins.Num() == 0)
					{
						AddValidationError(TEXT("MissingSwitchIntCases"), TEXT("Switch int nodes require at least one planned case exec pin."), Command.NodeId);
						break;
					}

					TArray<int32> CaseValues;
					bool bCasesValid = true;
					for (const FName CasePin : CasePins)
					{
						int32 ParsedValue = 0;
						if (!TryParseInt(CasePin.ToString(), ParsedValue))
						{
							AddValidationError(TEXT("InvalidSwitchIntCaseName"), FString::Printf(TEXT("Switch int case pin '%s' is not a valid integer."), *CasePin.ToString()), Command.NodeId);
							bCasesValid = false;
							break;
						}

						CaseValues.Add(ParsedValue);
					}

					if (!bCasesValid)
					{
						break;
					}

					CaseValues.Sort();
					const int32 StartIndex = CaseValues[0];
					for (int32 CaseIndex = 1; CaseIndex < CaseValues.Num(); ++CaseIndex)
					{
						if (CaseValues[CaseIndex] != StartIndex + CaseIndex)
						{
							AddValidationError(TEXT("NonContiguousSwitchIntCases"), TEXT("Switch int cases must form a contiguous ascending integer range."), Command.NodeId);
							break;
						}
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.SwitchString"))
				{
					if (GetPlannedExecOutputPinNames(Command).Num() == 0)
					{
						AddValidationError(TEXT("MissingSwitchStringCases"), TEXT("Switch string nodes require at least one planned case exec pin."), Command.NodeId);
					}
					ValidateOptionalBoolAttribute(Command, TEXT("CaseSensitive"), TEXT("SwitchStringCaseSensitiveInvalid"));
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.SwitchEnum"))
				{
					if (GetCommandAttribute(Command, TEXT("EnumPath")).TrimStartAndEnd().IsEmpty())
					{
						AddValidationError(TEXT("SwitchEnumPathMissing"), TEXT("Switch enum nodes require attribute EnumPath."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.MakeStruct"))
				{
					if (GetCommandAttribute(Command, TEXT("StructPath")).TrimStartAndEnd().IsEmpty())
					{
						AddValidationError(TEXT("MakeStructTypePathMissing"), TEXT("Make struct nodes require attribute StructPath."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.BreakStruct"))
				{
					if (GetCommandAttribute(Command, TEXT("StructPath")).TrimStartAndEnd().IsEmpty())
					{
						AddValidationError(TEXT("BreakStructTypePathMissing"), TEXT("Break struct nodes require attribute StructPath."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.MakeArray"))
				{
					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("ValuePinCategory")),
						GetCommandAttribute(Command, TEXT("ValueObjectPath")),
						TEXT("Make array node value pin"),
						TEXT("MakeArrayValueCategoryMissing"),
						TEXT("InvalidMakeArrayValueType"),
						TEXT("MakeArrayValueObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}

					int32 NumInputs = 0;
					if (!TryGetCommandCountAttribute(Command, TEXT("NumInputs"), NumInputs, Diagnostics, TEXT("InvalidMakeArrayInputCount")))
					{
						bIsValid = false;
					}
					else if (NumInputs < 1)
					{
						AddValidationError(TEXT("InvalidMakeArrayInputCount"), TEXT("Make array nodes require NumInputs >= 1."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.MakeSet"))
				{
					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("ValuePinCategory")),
						GetCommandAttribute(Command, TEXT("ValueObjectPath")),
						TEXT("Make set node value pin"),
						TEXT("MakeSetValueCategoryMissing"),
						TEXT("InvalidMakeSetValueType"),
						TEXT("MakeSetValueObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}

					int32 NumInputs = 0;
					if (!TryGetCommandCountAttribute(Command, TEXT("NumInputs"), NumInputs, Diagnostics, TEXT("InvalidMakeSetInputCount")))
					{
						bIsValid = false;
					}
					else if (NumInputs < 1)
					{
						AddValidationError(TEXT("InvalidMakeSetInputCount"), TEXT("Make set nodes require NumInputs >= 1."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.MakeMap"))
				{
					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("KeyPinCategory")),
						GetCommandAttribute(Command, TEXT("KeyObjectPath")),
						TEXT("Make map node key pin"),
						TEXT("MakeMapKeyCategoryMissing"),
						TEXT("InvalidMakeMapKeyType"),
						TEXT("MakeMapKeyObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}

					if (!ValidateCommandTypeShape(
						GetCommandAttribute(Command, TEXT("ValuePinCategory")),
						GetCommandAttribute(Command, TEXT("ValueObjectPath")),
						TEXT("Make map node value pin"),
						TEXT("MakeMapValueCategoryMissing"),
						TEXT("InvalidMakeMapValueType"),
						TEXT("MakeMapValueObjectPathMissing"),
						Command.NodeId,
						Diagnostics))
					{
						bIsValid = false;
					}

					int32 NumPairs = 0;
					if (!TryGetCommandCountAttribute(Command, TEXT("NumPairs"), NumPairs, Diagnostics, TEXT("InvalidMakeMapPairCount")))
					{
						bIsValid = false;
					}
					else if (NumPairs < 1)
					{
						AddValidationError(TEXT("InvalidMakeMapPairCount"), TEXT("Make map nodes require NumPairs >= 1."), Command.NodeId);
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.VariableGet"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("MissingVariableName"), TEXT("Variable node execution requires SecondaryName to contain the variable name."), Command.NodeId);
					}

					if (HasPlannedExecPins(Command))
					{
						if (!ValidateImpureVariableGetPinShape(Command, Diagnostics))
						{
							bIsValid = false;
							break;
						}

						if (Blueprint != nullptr)
						{
							if (FProperty* const Property = ResolveVariableProperty(Blueprint, Command, Diagnostics))
							{
								if (!ValidateVariableGetVariantSupport(Command, Property, Diagnostics))
								{
									bIsValid = false;
								}
							}
							else
							{
								bIsValid = false;
							}
						}
					}
					break;
				}

				if (Command.Name == TEXT("Vergil.K2.VariableSet"))
				{
					if (Command.SecondaryName.IsNone())
					{
						AddValidationError(TEXT("MissingVariableName"), TEXT("Variable node execution requires SecondaryName to contain the variable name."), Command.NodeId);
					}
					break;
				}

				AddValidationError(
					TEXT("UnsupportedNodeExecution"),
					FString::Printf(TEXT("Execution is not implemented for node descriptor '%s'."), *Command.Name.ToString()),
					Command.NodeId);
				break;
			}

			case EVergilCommandType::SetNodeMetadata:
			{
				if (!Command.NodeId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationNodeIdMissing"), TEXT("SetNodeMetadata commands require a valid node id."), Command.NodeId);
				}

				if (Command.Name.IsNone())
				{
					AddValidationError(TEXT("CommandValidationMetadataKeyMissing"), TEXT("SetNodeMetadata commands require a metadata key in Name."), Command.NodeId);
				}

				const int32* const PlannedIndex = PlannedNodeIndices.Find(Command.NodeId);
				if (PlannedIndex == nullptr || *PlannedIndex >= CommandIndex)
				{
					AddValidationError(TEXT("CommandValidationMetadataTargetMissing"), FString::Printf(TEXT("No executed AddNode command exists for metadata target '%s' before this command."), *Command.NodeId.ToString()), Command.NodeId);
					break;
				}

				ValidatePlannedNodeGraph(Command, TEXT("CommandValidationMetadataGraphMismatch"), TEXT("SetNodeMetadata"));
				break;
			}

			case EVergilCommandType::ConnectPins:
			{
				if (!Command.SourcePinId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationSourcePinMissing"), TEXT("ConnectPins commands require a valid source pin id."), Command.SourceNodeId);
				}

				if (!Command.TargetPinId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationTargetPinMissing"), TEXT("ConnectPins commands require a valid target pin id."), Command.TargetNodeId);
				}

				if (Command.SourcePinId.IsValid() && Command.SourcePinId == Command.TargetPinId)
				{
					AddValidationError(TEXT("CommandValidationSelfConnection"), TEXT("ConnectPins commands cannot connect a pin to itself."), Command.SourceNodeId);
				}

				const int32* const SourcePinIndex = PlannedPinIndices.Find(Command.SourcePinId);
				if (Command.SourcePinId.IsValid() && (SourcePinIndex == nullptr || *SourcePinIndex >= CommandIndex))
				{
					AddValidationError(TEXT("CommandValidationSourcePinMissing"), FString::Printf(TEXT("Source pin '%s' is not introduced by any earlier AddNode command in this plan."), *Command.SourcePinId.ToString()), Command.SourceNodeId);
				}

				const int32* const TargetPinIndex = PlannedPinIndices.Find(Command.TargetPinId);
				if (Command.TargetPinId.IsValid() && (TargetPinIndex == nullptr || *TargetPinIndex >= CommandIndex))
				{
					AddValidationError(TEXT("CommandValidationTargetPinMissing"), FString::Printf(TEXT("Target pin '%s' is not introduced by any earlier AddNode command in this plan."), *Command.TargetPinId.ToString()), Command.TargetNodeId);
				}

				const FGuid* const PlannedSourceOwner = PlannedPinOwners.Find(Command.SourcePinId);
				if (PlannedSourceOwner != nullptr && Command.SourceNodeId.IsValid() && *PlannedSourceOwner != Command.SourceNodeId)
				{
					AddValidationError(TEXT("CommandValidationPinOwnerMismatch"), FString::Printf(TEXT("Source pin '%s' belongs to node '%s', not declared node '%s'."), *Command.SourcePinId.ToString(), *PlannedSourceOwner->ToString(), *Command.SourceNodeId.ToString()), Command.SourceNodeId);
				}

				const FGuid* const PlannedTargetOwner = PlannedPinOwners.Find(Command.TargetPinId);
				if (PlannedTargetOwner != nullptr && Command.TargetNodeId.IsValid() && *PlannedTargetOwner != Command.TargetNodeId)
				{
					AddValidationError(TEXT("CommandValidationPinOwnerMismatch"), FString::Printf(TEXT("Target pin '%s' belongs to node '%s', not declared node '%s'."), *Command.TargetPinId.ToString(), *PlannedTargetOwner->ToString(), *Command.TargetNodeId.ToString()), Command.TargetNodeId);
				}

				const FName* const SourceGraph = PlannedPinGraphs.Find(Command.SourcePinId);
				const FName* const TargetGraph = PlannedPinGraphs.Find(Command.TargetPinId);
				if (SourceGraph != nullptr && TargetGraph != nullptr && *SourceGraph != *TargetGraph)
				{
					AddValidationError(TEXT("CommandValidationPinGraphMismatch"), FString::Printf(TEXT("Connection pins '%s' and '%s' belong to different graphs ('%s' vs '%s')."), *Command.SourcePinId.ToString(), *Command.TargetPinId.ToString(), *SourceGraph->ToString(), *TargetGraph->ToString()), Command.SourceNodeId);
				}

				if (SourceGraph != nullptr && *SourceGraph != GraphName)
				{
					AddValidationError(TEXT("CommandValidationConnectionGraphMismatch"), FString::Printf(TEXT("ConnectPins command targets graph '%s', but source pin '%s' belongs to graph '%s'."), *GraphName.ToString(), *Command.SourcePinId.ToString(), *SourceGraph->ToString()), Command.SourceNodeId);
				}

				if (TargetGraph != nullptr && *TargetGraph != GraphName)
				{
					AddValidationError(TEXT("CommandValidationConnectionGraphMismatch"), FString::Printf(TEXT("ConnectPins command targets graph '%s', but target pin '%s' belongs to graph '%s'."), *GraphName.ToString(), *Command.TargetPinId.ToString(), *TargetGraph->ToString()), Command.TargetNodeId);
				}
				break;
			}

			case EVergilCommandType::RemoveNode:
				if (!Command.NodeId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationNodeIdMissing"), TEXT("RemoveNode commands require a valid node id."), Command.NodeId);
				}
				else
				{
					ValidatePlannedNodeGraph(Command, TEXT("CommandValidationRemoveNodeGraphMismatch"), TEXT("RemoveNode"));
				}
				break;

			case EVergilCommandType::RenameMember:
			{
				if (Command.Name.IsNone() || Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("InvalidRenameMemberCommand"), TEXT("Member rename commands require a target blueprint, old name, and new name."), Command.NodeId);
					break;
				}

				const FString MemberType = GetCommandAttribute(Command, TEXT("MemberType")).TrimStartAndEnd().ToLower();
				if (MemberType != TEXT("variable")
					&& MemberType != TEXT("dispatcher")
					&& MemberType != TEXT("functiongraph")
					&& MemberType != TEXT("macrograph")
					&& MemberType != TEXT("component"))
				{
					AddValidationError(TEXT("UnsupportedRenameMemberType"), FString::Printf(TEXT("Unsupported rename member type '%s'."), *MemberType), Command.NodeId);
				}
				break;
			}

			case EVergilCommandType::MoveNode:
				if (!Command.NodeId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationNodeIdMissing"), TEXT("MoveNode commands require a valid node id."), Command.NodeId);
				}
				else
				{
					ValidatePlannedNodeGraph(Command, TEXT("CommandValidationMoveNodeGraphMismatch"), TEXT("MoveNode"));
				}
				break;

			case EVergilCommandType::FinalizeNode:
			{
				if (!Command.NodeId.IsValid())
				{
					AddValidationError(TEXT("CommandValidationNodeIdMissing"), TEXT("FinalizeNode commands require a valid node id."), Command.NodeId);
					break;
				}

				const int32* const PlannedFinalizeIndex = PlannedNodeIndices.Find(Command.NodeId);
				if (PlannedFinalizeIndex == nullptr || *PlannedFinalizeIndex >= CommandIndex)
				{
					AddValidationError(TEXT("CommandValidationFinalizeTargetMissing"), FString::Printf(TEXT("FinalizeNode command references node '%s' before its AddNode command executes."), *Command.NodeId.ToString()), Command.NodeId);
					break;
				}

				ValidatePlannedNodeGraph(Command, TEXT("CommandValidationFinalizeGraphMismatch"), TEXT("FinalizeNode"));

				if (Command.Name != TEXT("Vergil.K2.CreateDelegate"))
				{
					AddValidationError(TEXT("UnsupportedFinalizeNode"), FString::Printf(TEXT("Unsupported finalize node command '%s'."), *Command.Name.ToString()), Command.NodeId);
					break;
				}

				if (Command.SecondaryName.IsNone())
				{
					AddValidationError(TEXT("FinalizeCreateDelegateMissingFunction"), TEXT("CreateDelegate finalization requires SecondaryName to contain the function name."), Command.NodeId);
				}
				break;
			}

			case EVergilCommandType::CompileBlueprint:
				break;

			default:
				AddValidationError(TEXT("UnsupportedCommandType"), TEXT("Encountered an unsupported Vergil command type."), Command.NodeId);
				break;
			}
		}

		TSet<FGuid> ConnectedTargetPins;
		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.Type == EVergilCommandType::ConnectPins && Command.TargetPinId.IsValid())
			{
				ConnectedTargetPins.Add(Command.TargetPinId);
			}
		}

		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.Type != EVergilCommandType::AddNode || Command.Name != TEXT("Vergil.K2.SpawnActor"))
			{
				continue;
			}

			const FVergilPlannedPin* const SpawnTransformPin = Command.PlannedPins.FindByPredicate([](const FVergilPlannedPin& PlannedPin)
			{
				return PlannedPin.Name == SpawnActorTransformPinName
					&& PlannedPin.bIsInput
					&& !PlannedPin.bIsExec;
			});

			if (SpawnTransformPin == nullptr || !SpawnTransformPin->PinId.IsValid())
			{
				AddValidationError(
					TEXT("SpawnActorTransformPinMissing"),
					TEXT("Vergil.K2.SpawnActor plans must include a valid planned input pin named 'SpawnTransform'."),
					Command.NodeId);
				continue;
			}

			if (!ConnectedTargetPins.Contains(SpawnTransformPin->PinId))
			{
				AddValidationError(
					TEXT("SpawnActorTransformConnectionMissing"),
					TEXT("Vergil.K2.SpawnActor plans must connect 'SpawnTransform' because UE_5.7 UK2Node_SpawnActorFromClass expands into by-reference transform calls."),
					Command.NodeId);
			}
		}

		return bIsValid;
	}
}

bool FVergilCommandExecutor::Execute(
	UBlueprint* Blueprint,
	const TArray<FVergilCompilerCommand>& Commands,
	TArray<FVergilDiagnostic>& Diagnostics,
	int32* OutExecutedCommandCount) const
{
	if (OutExecutedCommandCount != nullptr)
	{
		*OutExecutedCommandCount = 0;
	}

	TArray<FVergilCompilerCommand> OrderedCommands = Commands;
	Vergil::NormalizeCommandPlan(OrderedCommands);

	if (!ValidateCommandPlan(Blueprint, OrderedCommands, Diagnostics))
	{
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("Vergil", "ExecuteCommandPlan", "Vergil Execute Command Plan"));
	Blueprint->Modify();

	FVergilExecutionState State;
	bool bExecutedBlueprintDefinitionChange = false;
	bool bExecutedGraphStructuralChange = false;
	bool bExecutedFinalizeChange = false;
	bool bExecutedPostBlueprintCompileChange = false;
	bool bExecutedExplicitCompile = false;

	auto ExecuteSingleCommand = [&](const FVergilCompilerCommand& Command, bool& bOutChanged) -> bool
	{
		bool bCommandSucceeded = false;
		bOutChanged = false;

		switch (Command.Type)
		{
		case EVergilCommandType::EnsureDispatcher:
			bCommandSucceeded = ExecuteEnsureDispatcher(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::AddDispatcherParameter:
			bCommandSucceeded = ExecuteAddDispatcherParameter(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetBlueprintMetadata:
			bCommandSucceeded = ExecuteSetBlueprintMetadata(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureVariable:
			bCommandSucceeded = ExecuteEnsureVariable(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetVariableMetadata:
			bCommandSucceeded = ExecuteSetVariableMetadata(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetVariableDefault:
			bCommandSucceeded = ExecuteSetVariableDefault(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureFunctionGraph:
			bCommandSucceeded = ExecuteEnsureFunctionGraph(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureMacroGraph:
			bCommandSucceeded = ExecuteEnsureMacroGraph(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureGraph:
			bCommandSucceeded = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics) != nullptr;
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureComponent:
			bCommandSucceeded = ExecuteEnsureComponent(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::AttachComponent:
			bCommandSucceeded = ExecuteAttachComponent(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetComponentProperty:
			bCommandSucceeded = ExecuteSetComponentProperty(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::EnsureInterface:
			bCommandSucceeded = ExecuteEnsureInterface(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetClassDefault:
			bCommandSucceeded = ExecuteSetClassDefault(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::AddNode:
			bCommandSucceeded = ExecuteAddNode(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::SetNodeMetadata:
			bCommandSucceeded = ExecuteSetNodeMetadata(Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::ConnectPins:
			bCommandSucceeded = ExecuteConnectPins(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::RemoveNode:
			bCommandSucceeded = ExecuteRemoveNode(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::RenameMember:
			bCommandSucceeded = ExecuteRenameMember(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::MoveNode:
			bCommandSucceeded = ExecuteMoveNode(Blueprint, Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::FinalizeNode:
			bCommandSucceeded = ExecuteFinalizeNode(Command, State, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		case EVergilCommandType::CompileBlueprint:
			bCommandSucceeded = ExecuteCompileBlueprint(Blueprint, Command, Diagnostics);
			bOutChanged = bCommandSucceeded;
			break;

		default:
			Diagnostics.Add(FVergilDiagnostic::Make(
				EVergilDiagnosticSeverity::Error,
				TEXT("UnsupportedCommandType"),
				TEXT("Encountered an unsupported Vergil command type.")));
			break;
		}

		if (bCommandSucceeded && OutExecutedCommandCount != nullptr)
		{
			++(*OutExecutedCommandCount);
		}

		return bCommandSucceeded;
	};

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (!IsBlueprintDefinitionCommand(Command))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedBlueprintDefinitionChange |= bCommandChanged;
	}

	if (bExecutedBlueprintDefinitionChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (IsBlueprintDefinitionCommand(Command)
			|| IsPostBlueprintCompileCommand(Command)
			|| IsPostCompileFinalizeCommand(Command)
			|| IsExplicitCompileCommand(Command)
			|| Command.Type == EVergilCommandType::ConnectPins)
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedGraphStructuralChange |= bCommandChanged;
	}

	RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (Command.Type != EVergilCommandType::ConnectPins || IsExecConnectionCommand(Command, State))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedGraphStructuralChange |= bCommandChanged;
	}

	RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (Command.Type != EVergilCommandType::ConnectPins || !IsExecConnectionCommand(Command, State))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedGraphStructuralChange |= bCommandChanged;
	}

	if (bExecutedGraphStructuralChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);
	}

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (!IsPostCompileFinalizeCommand(Command))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedFinalizeChange |= bCommandChanged;
	}

	if (bExecutedFinalizeChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);

		bool bReappliedAnyConnection = false;
		for (const FVergilCompilerCommand& Command : OrderedCommands)
		{
			if (Command.Type != EVergilCommandType::ConnectPins || IsExecConnectionCommand(Command, State))
			{
				continue;
			}

			bool bCommandChanged = false;
			ExecuteSingleCommand(Command, bCommandChanged);
			bReappliedAnyConnection |= bCommandChanged;
		}

		RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);

		for (const FVergilCompilerCommand& Command : OrderedCommands)
		{
			if (Command.Type != EVergilCommandType::ConnectPins || !IsExecConnectionCommand(Command, State))
			{
				continue;
			}

			bool bCommandChanged = false;
			ExecuteSingleCommand(Command, bCommandChanged);
			bReappliedAnyConnection |= bCommandChanged;
		}

		if (bReappliedAnyConnection)
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (!IsExplicitCompileCommand(Command))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedExplicitCompile |= bCommandChanged;
	}

	if (bExecutedExplicitCompile)
	{
		RefreshRegisteredPins(Blueprint, OrderedCommands, State, Diagnostics);
	}

	for (const FVergilCompilerCommand& Command : OrderedCommands)
	{
		if (!IsPostBlueprintCompileCommand(Command))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedPostBlueprintCompileChange |= bCommandChanged;
	}

	if (!bExecutedBlueprintDefinitionChange
		&& !bExecutedGraphStructuralChange
		&& !bExecutedFinalizeChange
		&& !bExecutedPostBlueprintCompileChange
		&& !bExecutedExplicitCompile)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	for (const FVergilDiagnostic& Diagnostic : Diagnostics)
	{
		if (Diagnostic.Severity == EVergilDiagnosticSeverity::Error)
		{
			UE_LOG(LogVergil, Log, TEXT("Vergil command execution failed with %d diagnostics."), Diagnostics.Num());
			for (const FVergilDiagnostic& EmittedDiagnostic : Diagnostics)
			{
				const TCHAR* SeverityLabel = TEXT("Info");
				if (EmittedDiagnostic.Severity == EVergilDiagnosticSeverity::Error)
				{
					SeverityLabel = TEXT("Error");
				}
				else if (EmittedDiagnostic.Severity == EVergilDiagnosticSeverity::Warning)
				{
					SeverityLabel = TEXT("Warning");
				}

				UE_LOG(
					LogVergil,
					Log,
					TEXT("Vergil execution diagnostic [%s] %s: %s"),
					SeverityLabel,
					*EmittedDiagnostic.Code.ToString(),
					*EmittedDiagnostic.Message);
			}
			return false;
		}
	}

	return true;
}

