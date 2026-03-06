#include "VergilCommandExecutor.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FormatText.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "VergilLog.h"

namespace
{
	struct FVergilExecutionState
	{
		TMap<FName, UEdGraph*> GraphsByName;
		TMap<FGuid, UEdGraphNode*> NodesById;
		TMap<FGuid, UEdGraphPin*> PinsById;
	};

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

	UEdGraph* FindDelegateGraph(UBlueprint* Blueprint, const FName DispatcherName)
	{
		if (Blueprint == nullptr || DispatcherName.IsNone())
		{
			return nullptr;
		}

		return FBlueprintEditorUtils::GetDelegateSignatureGraphByName(Blueprint, DispatcherName);
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

	UEdGraph* ResolveOrCreateGraph(
		UBlueprint* Blueprint,
		const FVergilCompilerCommand& Command,
		FVergilExecutionState& State,
		TArray<FVergilDiagnostic>& Diagnostics)
	{
		const FName GraphName = Command.GraphName.IsNone() ? FName(TEXT("EventGraph")) : Command.GraphName;
		if (UEdGraph** ExistingGraph = State.GraphsByName.Find(GraphName))
		{
			return *ExistingGraph;
		}

		UEdGraph* Graph = FindGraphByName(Blueprint, GraphName);
		if (Graph == nullptr && Command.Type == EVergilCommandType::EnsureGraph)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			if (Graph != nullptr)
			{
				if (GraphName == TEXT("EventGraph"))
				{
					FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
				}
				else
				{
					FBlueprintEditorUtils::AddFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
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
			|| Command.Type == EVergilCommandType::AddDispatcherParameter;
	}

	bool IsPostCompileFinalizeCommand(const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::FinalizeNode;
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
		if (InValue.IsEmpty())
		{
			return false;
		}

		OutValue = FCString::Atof(*InValue);
		return true;
	}

	bool TryParseInt(const FString& InValue, int32& OutValue)
	{
		if (InValue.IsEmpty())
		{
			return false;
		}

		OutValue = FCString::Atoi(*InValue);
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

			UEdGraph* Graph = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics);
			if (Graph == nullptr)
			{
				bAllPinsResolved = false;
				continue;
			}

			UEdGraphNode* Node = nullptr;
			for (UEdGraphNode* CandidateNode : Graph->Nodes)
			{
				if (CandidateNode != nullptr && CandidateNode->NodeGuid == Command.NodeId)
				{
					Node = CandidateNode;
					break;
				}
			}

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
				return Blueprint != nullptr ? Blueprint->ParentClass : nullptr;
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

		UClass* OwnerClass = ResolveOwnerClass(Command.StringValue);
		if (OwnerClass != nullptr)
		{
			UFunction* Func = OwnerClass->FindFunctionByName(Command.SecondaryName);
			if (Func == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("FunctionNotFound"),
					FString::Printf(TEXT("Unable to resolve function '%s' on class '%s'."), *Command.SecondaryName.ToString(), *OwnerClass->GetName()),
					Command.NodeId));
				return nullptr;
			}

			return Func;
		}

		if (!Command.StringValue.IsEmpty())
		{
			if (UFunction* DirectFunction = FindObject<UFunction>(nullptr, *Command.StringValue))
			{
				return DirectFunction;
			}

			if (UFunction* LoadedFunction = LoadObject<UFunction>(nullptr, *Command.StringValue))
			{
				return LoadedFunction;
			}
		}

		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("MissingFunctionOwner"),
			FString::Printf(TEXT("Unable to resolve owner class '%s' for function '%s'."), *Command.StringValue, *Command.SecondaryName.ToString()),
			Command.NodeId));
		return nullptr;
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
		const TCHAR* CategoryKey,
		const TCHAR* ObjectPathKey,
		FEdGraphPinType& OutPinType,
		FString& OutError)
	{
		const FString Category = GetCommandAttribute(Command, CategoryKey).ToLower();
		const FString ObjectPath = GetCommandAttribute(Command, ObjectPathKey);
		OutPinType = FEdGraphPinType();

		if (Category.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Missing required attribute '%s'."), CategoryKey);
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
			CommentNode->NodeHeight = 160;
			CommentNode->NodeComment = Command.StringValue.IsEmpty() ? TEXT("Vergil Comment") : Command.StringValue;
			FinalizePlacedNode(Graph, CommentNode, Command.Position, Command.NodeId);
			NewNode = CommentNode;
		}
		else if (Command.Name == TEXT("Vergil.K2.Event"))
		{
			if (Blueprint == nullptr || Blueprint->ParentClass == nullptr || Command.SecondaryName.IsNone())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("InvalidEventCommand"),
					TEXT("Event node execution requires a parent class and event function name."),
					Command.NodeId));
				return false;
			}

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
		else if (Command.Name == TEXT("Vergil.K2.ForLoop"))
		{
			const FString MacroBlueprintPath = GetCommandAttribute(Command, TEXT("MacroBlueprintPath")).IsEmpty()
				? TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")
				: GetCommandAttribute(Command, TEXT("MacroBlueprintPath"));
			const FName MacroGraphName = GetCommandAttribute(Command, TEXT("MacroGraphName")).IsEmpty()
				? TEXT("ForLoop")
				: FName(*GetCommandAttribute(Command, TEXT("MacroGraphName")));

			UEdGraph* const MacroGraph = ResolveMacroGraphReference(MacroBlueprintPath, MacroGraphName);
			if (MacroGraph == nullptr)
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("ForLoopMacroNotFound"),
					FString::Printf(TEXT("Unable to resolve macro graph '%s' from '%s'."), *MacroGraphName.ToString(), *MacroBlueprintPath),
					Command.NodeId));
				return false;
			}

			UK2Node_MacroInstance* MacroNode = NewObject<UK2Node_MacroInstance>(Graph);
			MacroNode->SetMacroGraph(MacroGraph);
			FinalizePlacedNode(Graph, MacroNode, Command.Position, Command.NodeId);
			MacroNode->ReconstructNode();
			NewNode = MacroNode;
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
		else if (Command.Name == TEXT("Vergil.K2.Call"))
		{
			UFunction* Func = ResolveCallFunction(Blueprint, Command, Diagnostics);
			if (Func == nullptr)
			{
				return false;
			}

			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
			const bool bIsOwnCustomEvent = Func->HasAnyFunctionFlags(FUNC_BlueprintEvent)
				&& Blueprint != nullptr
				&& ((Blueprint->SkeletonGeneratedClass != nullptr && Func->GetOwnerClass() == Blueprint->SkeletonGeneratedClass)
					|| (Blueprint->GeneratedClass != nullptr && Func->GetOwnerClass() == Blueprint->GeneratedClass));

			if (bIsOwnCustomEvent)
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
			const FString IndexCategory = GetCommandAttribute(Command, TEXT("IndexPinCategory")).ToLower();
			if (IndexCategory.IsEmpty())
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("MissingSelectIndexCategory"),
					TEXT("Select node execution requires attribute IndexPinCategory."),
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

			if (HasPlannedExecPins(Command))
			{
				Diagnostics.Add(FVergilDiagnostic::Make(
					EVergilDiagnosticSeverity::Error,
					TEXT("UnsupportedVariableGetVariant"),
					TEXT("Impure variable getter execution is not implemented. Provide a pure getter shape with no exec pins."),
					Command.NodeId));
				return false;
			}

			UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
			GetNode->SetFromProperty(Property, true, Property->GetOwnerClass());
			FinalizePlacedNode(Graph, GetNode, Command.Position, Command.NodeId);
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

	if (Blueprint == nullptr)
	{
		Diagnostics.Add(FVergilDiagnostic::Make(
			EVergilDiagnosticSeverity::Error,
			TEXT("MissingBlueprint"),
			TEXT("Cannot execute a command plan without a target blueprint.")));
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("Vergil", "ExecuteCommandPlan", "Vergil Execute Command Plan"));
	Blueprint->Modify();

	FVergilExecutionState State;
	bool bExecutedBlueprintDefinitionChange = false;
	bool bExecutedGraphStructuralChange = false;
	bool bExecutedFinalizeChange = false;

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

		case EVergilCommandType::EnsureGraph:
			bCommandSucceeded = ResolveOrCreateGraph(Blueprint, Command, State, Diagnostics) != nullptr;
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

		case EVergilCommandType::FinalizeNode:
			bCommandSucceeded = ExecuteFinalizeNode(Command, State, Diagnostics);
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

	for (const FVergilCompilerCommand& Command : Commands)
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

	for (const FVergilCompilerCommand& Command : Commands)
	{
		if (IsBlueprintDefinitionCommand(Command) || IsPostCompileFinalizeCommand(Command) || Command.Type == EVergilCommandType::ConnectPins)
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedGraphStructuralChange |= bCommandChanged;
	}

	RefreshRegisteredPins(Blueprint, Commands, State, Diagnostics);

	for (const FVergilCompilerCommand& Command : Commands)
	{
		if (Command.Type != EVergilCommandType::ConnectPins || IsExecConnectionCommand(Command, State))
		{
			continue;
		}

		bool bCommandChanged = false;
		ExecuteSingleCommand(Command, bCommandChanged);
		bExecutedGraphStructuralChange |= bCommandChanged;
	}

	RefreshRegisteredPins(Blueprint, Commands, State, Diagnostics);

	for (const FVergilCompilerCommand& Command : Commands)
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
		RefreshRegisteredPins(Blueprint, Commands, State, Diagnostics);
	}

	for (const FVergilCompilerCommand& Command : Commands)
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

		RefreshRegisteredPins(Blueprint, Commands, State, Diagnostics);

		bool bReappliedAnyConnection = false;
		for (const FVergilCompilerCommand& Command : Commands)
		{
			if (Command.Type != EVergilCommandType::ConnectPins || IsExecConnectionCommand(Command, State))
			{
				continue;
			}

			bool bCommandChanged = false;
			ExecuteSingleCommand(Command, bCommandChanged);
			bReappliedAnyConnection |= bCommandChanged;
		}

		RefreshRegisteredPins(Blueprint, Commands, State, Diagnostics);

		for (const FVergilCompilerCommand& Command : Commands)
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

	if (!bExecutedBlueprintDefinitionChange && !bExecutedGraphStructuralChange && !bExecutedFinalizeChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	for (const FVergilDiagnostic& Diagnostic : Diagnostics)
	{
		if (Diagnostic.Severity == EVergilDiagnosticSeverity::Error)
		{
			UE_LOG(LogVergil, Warning, TEXT("Vergil command execution failed with %d diagnostics."), Diagnostics.Num());
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
					Warning,
					TEXT("Vergil diagnostic [%s] %s: %s"),
					SeverityLabel,
					*EmittedDiagnostic.Code.ToString(),
					*EmittedDiagnostic.Message);
			}
			return false;
		}
	}

	return true;
}

