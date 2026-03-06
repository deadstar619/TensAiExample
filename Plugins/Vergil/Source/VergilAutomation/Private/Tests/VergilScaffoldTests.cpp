#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
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
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeSet.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Select.h"
#include "K2Node_Self.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetMathLibrary.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"
#include "VergilCommandTypes.h"
#include "VergilBlueprintCompilerService.h"
#include "VergilCompilerTypes.h"
#include "VergilEditorSubsystem.h"
#include "VergilGraphDocument.h"
#include "VergilNodeRegistry.h"
#include "VergilAutomationTestInterface.h"

namespace
{
	class FTestSpecificNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Test.Special");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = TEXT("HandledBySpecificHandler");
			Command.Position = Node.Position;
			Context.AddCommand(Command);
			return true;
		}


	};

	UBlueprint* MakeTestBlueprint()
	{
		const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_VergilTransient"));
		UPackage* const Package = CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *BlueprintName.ToString()));
		Package->SetFlags(RF_Transient);

		return FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			TEXT("VergilAutomation"));
	}

	FName MakeCastResultPinName(UClass* TargetClass)
	{
		check(TargetClass != nullptr);
		return FName(*(UEdGraphSchema_K2::PN_CastedValuePrefix + TargetClass->GetDisplayNameText().ToString()));
	}

	template <typename TNode>
	TNode* FindGraphNodeByGuid(UEdGraph* Graph, const FGuid& NodeId)
	{
		if (Graph == nullptr || !NodeId.IsValid())
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node != nullptr && Node->NodeGuid == NodeId)
			{
				return Cast<TNode>(Node);
			}
		}

		return nullptr;
	}

	UEdGraph* FindBlueprintGraphByName(UBlueprint* Blueprint, const FName GraphName)
	{
		if (Blueprint == nullptr)
		{
			return nullptr;
		}

		if (GraphName == TEXT("EventGraph"))
		{
			return FBlueprintEditorUtils::FindEventGraph(Blueprint);
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

	USCS_Node* FindBlueprintComponentNode(UBlueprint* Blueprint, const FName ComponentName)
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

	const FBPVariableDescription* FindBlueprintVariableDescription(const UBlueprint* Blueprint, const FName VariableName)
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

	UK2Node_FunctionEntry* FindFunctionEntryNode(UEdGraph* Graph)
	{
		return Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilModulesLoadTest,
	"Vergil.Scaffold.ModulesLoad",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilModulesLoadTest::RunTest(const FString& Parameters)
{
	TestNotNull(TEXT("VergilCore loads"), FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("VergilCore")));
	TestNotNull(TEXT("VergilBlueprintModel loads"), FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("VergilBlueprintModel")));
	TestNotNull(TEXT("VergilBlueprintCompiler loads"), FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("VergilBlueprintCompiler")));
	TestNotNull(TEXT("VergilEditor loads"), FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("VergilEditor")));
	TestNotNull(TEXT("VergilAgent loads"), FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("VergilAgent")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilGraphDocumentValidationTest,
	"Vergil.Scaffold.GraphDocumentValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilGraphDocumentValidationTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = 1;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_Scaffold");

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Empty document is structurally valid."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Empty document has no diagnostics."), Diagnostics.Num(), 0);

	FVergilGraphDocument InvalidDocument;
	InvalidDocument.SchemaVersion = 1;
	InvalidDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidVariables");

	FVergilDispatcherDefinition Dispatcher;
	Dispatcher.Name = TEXT("SharedMember");
	InvalidDocument.Dispatchers.Add(Dispatcher);

	FVergilVariableDefinition ConflictingVariable;
	ConflictingVariable.Name = TEXT("SharedMember");
	ConflictingVariable.Type.PinCategory = TEXT("bool");

	FVergilVariableDefinition MissingTypeVariable;
	MissingTypeVariable.Name = TEXT("BrokenVar");

	FVergilVariableDefinition DuplicateVariable;
	DuplicateVariable.Name = TEXT("BrokenVar");
	DuplicateVariable.Type.PinCategory = TEXT("bool");

	FVergilVariableDefinition InvalidSpawnVariable;
	InvalidSpawnVariable.Name = TEXT("SpawnArg");
	InvalidSpawnVariable.Type.PinCategory = TEXT("bool");
	InvalidSpawnVariable.Flags.bExposeOnSpawn = true;

	InvalidDocument.Variables = { ConflictingVariable, MissingTypeVariable, DuplicateVariable, InvalidSpawnVariable };

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid variable definitions should fail structural validation."), InvalidDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Variable validation reports dispatcher conflicts."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableNameConflictsWithDispatcher");
	}));
	TestTrue(TEXT("Variable validation reports missing type categories."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableTypeCategoryMissing");
	}));
	TestTrue(TEXT("Variable validation reports duplicate variable names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableNameDuplicate");
	}));
	TestTrue(TEXT("Variable validation reports expose-on-spawn inconsistencies."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableExposeOnSpawnRequiresInstanceEditable");
	}));

	FVergilGraphDocument InvalidFunctionDocument;
	InvalidFunctionDocument.SchemaVersion = 1;
	InvalidFunctionDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidFunctions");

	FVergilVariableDefinition ExistingVariable;
	ExistingVariable.Name = TEXT("SharedMember");
	ExistingVariable.Type.PinCategory = TEXT("bool");
	InvalidFunctionDocument.Variables.Add(ExistingVariable);

	FVergilFunctionDefinition ConflictingFunction;
	ConflictingFunction.Name = TEXT("SharedMember");

	FVergilFunctionDefinition BrokenFunction;
	BrokenFunction.Name = TEXT("ComputeState");

	FVergilFunctionParameterDefinition MissingInputName;
	MissingInputName.Type.PinCategory = TEXT("bool");
	BrokenFunction.Inputs.Add(MissingInputName);

	FVergilFunctionParameterDefinition MissingOutputType;
	MissingOutputType.Name = TEXT("Result");
	BrokenFunction.Outputs.Add(MissingOutputType);

	FVergilFunctionParameterDefinition DuplicateOutputName;
	DuplicateOutputName.Name = TEXT("Result");
	DuplicateOutputName.Type.PinCategory = TEXT("int");
	BrokenFunction.Outputs.Add(DuplicateOutputName);

	FVergilFunctionDefinition DuplicateFunction;
	DuplicateFunction.Name = TEXT("ComputeState");

	InvalidFunctionDocument.Functions = { ConflictingFunction, BrokenFunction, DuplicateFunction };

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid function definitions should fail structural validation."), InvalidFunctionDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Function validation reports variable conflicts."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("FunctionNameConflictsWithVariable");
	}));
	TestTrue(TEXT("Function validation reports missing input names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("FunctionInputNameMissing");
	}));
	TestTrue(TEXT("Function validation reports missing output types."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("FunctionParameterTypeCategoryMissing");
	}));
	TestTrue(TEXT("Function validation reports duplicate signature members."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("FunctionParameterNameDuplicate");
	}));
	TestTrue(TEXT("Function validation reports duplicate function names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("FunctionNameDuplicate");
	}));

	FVergilGraphDocument InvalidMacroDocument;
	InvalidMacroDocument.SchemaVersion = 1;
	InvalidMacroDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidMacros");

	FVergilFunctionDefinition ExistingMacroFunction;
	ExistingMacroFunction.Name = TEXT("SharedGraph");
	InvalidMacroDocument.Functions.Add(ExistingMacroFunction);

	FVergilComponentDefinition ExistingMacroComponent;
	ExistingMacroComponent.Name = TEXT("SharedMacro");
	ExistingMacroComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	InvalidMacroDocument.Components.Add(ExistingMacroComponent);

	FVergilMacroDefinition ConflictingMacro;
	ConflictingMacro.Name = TEXT("SharedGraph");

	FVergilMacroDefinition ComponentConflictingMacro;
	ComponentConflictingMacro.Name = TEXT("SharedMacro");

	FVergilMacroDefinition BrokenMacro;
	BrokenMacro.Name = TEXT("ComputeMacro");

	FVergilMacroParameterDefinition MacroMissingInputName;
	MacroMissingInputName.bIsExec = true;
	BrokenMacro.Inputs.Add(MacroMissingInputName);

	FVergilMacroParameterDefinition InvalidExecOutput;
	InvalidExecOutput.Name = TEXT("Then");
	InvalidExecOutput.bIsExec = true;
	InvalidExecOutput.Type.PinCategory = TEXT("bool");
	BrokenMacro.Outputs.Add(InvalidExecOutput);

	FVergilMacroParameterDefinition MacroDuplicateOutputName;
	MacroDuplicateOutputName.Name = TEXT("Then");
	MacroDuplicateOutputName.Type.PinCategory = TEXT("int");
	BrokenMacro.Outputs.Add(MacroDuplicateOutputName);

	FVergilMacroDefinition DuplicateMacro;
	DuplicateMacro.Name = TEXT("ComputeMacro");

	InvalidMacroDocument.Macros = { ConflictingMacro, ComponentConflictingMacro, BrokenMacro, DuplicateMacro };

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid macro definitions should fail structural validation."), InvalidMacroDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Macro validation reports function conflicts."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroNameConflictsWithFunction");
	}));
	TestTrue(TEXT("Macro validation reports component conflicts."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroNameConflictsWithComponent");
	}));
	TestTrue(TEXT("Macro validation reports missing input names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroInputNameMissing");
	}));
	TestTrue(TEXT("Macro validation reports invalid exec type metadata."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroExecPinTypeUnexpected");
	}));
	TestTrue(TEXT("Macro validation reports duplicate signature members."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroParameterNameDuplicate");
	}));
	TestTrue(TEXT("Macro validation reports duplicate macro names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("MacroNameDuplicate");
	}));

	FVergilGraphDocument InvalidComponentDocument;
	InvalidComponentDocument.SchemaVersion = 1;
	InvalidComponentDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidComponents");

	FVergilFunctionDefinition ExistingFunction;
	ExistingFunction.Name = TEXT("SharedComponent");
	InvalidComponentDocument.Functions.Add(ExistingFunction);

	FVergilComponentDefinition ConflictingComponent;
	ConflictingComponent.Name = TEXT("SharedComponent");
	ConflictingComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();

	FVergilComponentDefinition MissingClassComponent;
	MissingClassComponent.Name = TEXT("MissingClass");

	FVergilComponentDefinition SelfParentComponent;
	SelfParentComponent.Name = TEXT("LoopRoot");
	SelfParentComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	SelfParentComponent.ParentComponentName = TEXT("LoopRoot");

	FVergilComponentDefinition InvalidTemplateComponent;
	InvalidTemplateComponent.Name = TEXT("Visual");
	InvalidTemplateComponent.ComponentClassPath = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	InvalidTemplateComponent.TemplateProperties.Add(NAME_None, TEXT("Broken"));

	FVergilComponentDefinition CycleA;
	CycleA.Name = TEXT("CycleA");
	CycleA.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	CycleA.ParentComponentName = TEXT("CycleB");

	FVergilComponentDefinition CycleB;
	CycleB.Name = TEXT("CycleB");
	CycleB.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	CycleB.ParentComponentName = TEXT("CycleA");

	InvalidComponentDocument.Components = { ConflictingComponent, MissingClassComponent, SelfParentComponent, InvalidTemplateComponent, CycleA, CycleB };

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid component definitions should fail structural validation."), InvalidComponentDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Component validation reports function conflicts."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ComponentNameConflictsWithFunction");
	}));
	TestTrue(TEXT("Component validation reports missing class paths."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ComponentClassPathMissing");
	}));
	TestTrue(TEXT("Component validation reports self-parent references."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ComponentParentSelfReference");
	}));
	TestTrue(TEXT("Component validation reports template properties without names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ComponentTemplatePropertyNameMissing");
	}));
	TestTrue(TEXT("Component validation reports circular parent chains."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ComponentParentCycle");
	}));

	FVergilGraphDocument InvalidInterfaceDocument;
	InvalidInterfaceDocument.SchemaVersion = 1;
	InvalidInterfaceDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidInterfaces");

	FVergilInterfaceDefinition MissingInterface;

	FVergilInterfaceDefinition DuplicateInterface;
	DuplicateInterface.InterfaceClassPath = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();

	FVergilInterfaceDefinition DuplicateInterfaceAgain = DuplicateInterface;

	InvalidInterfaceDocument.Interfaces = { MissingInterface, DuplicateInterface, DuplicateInterfaceAgain };

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid interface definitions should fail structural validation."), InvalidInterfaceDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Interface validation reports missing class paths."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("InterfaceClassPathMissing");
	}));
	TestTrue(TEXT("Interface validation reports duplicate class paths."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("InterfaceClassPathDuplicate");
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFunctionDefinitionModelTest,
	"Vergil.Scaffold.FunctionDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilFunctionDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = 1;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_FunctionModel");

	FVergilFunctionDefinition PureFunction;
	PureFunction.Name = TEXT("ComputeStatus");
	PureFunction.bPure = true;
	PureFunction.AccessSpecifier = EVergilFunctionAccessSpecifier::Protected;

	FVergilFunctionParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	PureFunction.Inputs.Add(TargetInput);

	FVergilFunctionParameterDefinition ThresholdInput;
	ThresholdInput.Name = TEXT("Threshold");
	ThresholdInput.Type.PinCategory = TEXT("float");
	PureFunction.Inputs.Add(ThresholdInput);

	FVergilFunctionParameterDefinition ResultOutput;
	ResultOutput.Name = TEXT("bIsReady");
	ResultOutput.Type.PinCategory = TEXT("bool");
	PureFunction.Outputs.Add(ResultOutput);

	FVergilFunctionParameterDefinition MessagesOutput;
	MessagesOutput.Name = TEXT("Messages");
	MessagesOutput.Type.PinCategory = TEXT("string");
	MessagesOutput.Type.ContainerType = EVergilVariableContainerType::Array;
	PureFunction.Outputs.Add(MessagesOutput);

	Document.Functions.Add(PureFunction);

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid function definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid function definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored functions."), Document.Functions.Num(), 1);
	TestEqual(TEXT("Function should retain input count."), Document.Functions[0].Inputs.Num(), 2);
	TestEqual(TEXT("Function should retain output count."), Document.Functions[0].Outputs.Num(), 2);
	TestTrue(TEXT("Function should retain purity."), Document.Functions[0].bPure);
	TestEqual(TEXT("Function should retain access."), Document.Functions[0].AccessSpecifier, EVergilFunctionAccessSpecifier::Protected);
	TestEqual(TEXT("Array outputs should retain container type."), Document.Functions[0].Outputs[1].Type.ContainerType, EVergilVariableContainerType::Array);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMacroDefinitionModelTest,
	"Vergil.Scaffold.MacroDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMacroDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = 1;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_MacroModel");

	FVergilMacroDefinition Macro;
	Macro.Name = TEXT("RouteTarget");

	FVergilMacroParameterDefinition ExecuteInput;
	ExecuteInput.Name = TEXT("Execute");
	ExecuteInput.bIsExec = true;
	Macro.Inputs.Add(ExecuteInput);

	FVergilMacroParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	Macro.Inputs.Add(TargetInput);

	FVergilMacroParameterDefinition ThenOutput;
	ThenOutput.Name = TEXT("Then");
	ThenOutput.bIsExec = true;
	Macro.Outputs.Add(ThenOutput);

	FVergilMacroParameterDefinition IndicesOutput;
	IndicesOutput.Name = TEXT("Indices");
	IndicesOutput.Type.PinCategory = TEXT("int");
	IndicesOutput.Type.ContainerType = EVergilVariableContainerType::Array;
	Macro.Outputs.Add(IndicesOutput);

	Document.Macros.Add(Macro);

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid macro definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid macro definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored macros."), Document.Macros.Num(), 1);
	TestEqual(TEXT("Macro should retain input count."), Document.Macros[0].Inputs.Num(), 2);
	TestEqual(TEXT("Macro should retain output count."), Document.Macros[0].Outputs.Num(), 2);
	TestTrue(TEXT("Exec pins should retain exec metadata."), Document.Macros[0].Inputs[0].bIsExec && Document.Macros[0].Outputs[0].bIsExec);
	TestEqual(TEXT("Array outputs should retain container type."), Document.Macros[0].Outputs[1].Type.ContainerType, EVergilVariableContainerType::Array);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilComponentDefinitionModelTest,
	"Vergil.Scaffold.ComponentDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilInterfaceDefinitionModelTest,
	"Vergil.Scaffold.InterfaceDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilComponentDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = 1;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ComponentModel");

	FVergilComponentDefinition RootComponent;
	RootComponent.Name = TEXT("Root");
	RootComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	RootComponent.RelativeTransform.bHasRelativeLocation = true;
	RootComponent.RelativeTransform.RelativeLocation = FVector(25.0f, 50.0f, 75.0f);

	FVergilComponentDefinition VisualComponent;
	VisualComponent.Name = TEXT("VisualMesh");
	VisualComponent.ComponentClassPath = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	VisualComponent.ParentComponentName = RootComponent.Name;
	VisualComponent.AttachSocketName = TEXT("WeaponSocket");
	VisualComponent.RelativeTransform.bHasRelativeRotation = true;
	VisualComponent.RelativeTransform.RelativeRotation = FRotator(0.0f, 45.0f, 0.0f);
	VisualComponent.RelativeTransform.bHasRelativeScale = true;
	VisualComponent.RelativeTransform.RelativeScale3D = FVector(1.0f, 1.5f, 1.0f);
	VisualComponent.TemplateProperties.Add(TEXT("CollisionProfileName"), TEXT("NoCollision"));

	Document.Components = { RootComponent, VisualComponent };

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid component definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid component definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored components."), Document.Components.Num(), 2);
	TestEqual(TEXT("Second component should retain its parent reference."), Document.Components[1].ParentComponentName, RootComponent.Name);
	TestEqual(TEXT("Second component should retain its attach socket."), Document.Components[1].AttachSocketName, FName(TEXT("WeaponSocket")));
	TestTrue(TEXT("Root component should retain relative location override."), Document.Components[0].RelativeTransform.bHasRelativeLocation);
	TestEqual(TEXT("Root component should retain relative location."), Document.Components[0].RelativeTransform.RelativeLocation, FVector(25.0f, 50.0f, 75.0f));
	TestTrue(TEXT("Visual component should retain relative rotation override."), Document.Components[1].RelativeTransform.bHasRelativeRotation);
	TestEqual(TEXT("Visual component should retain relative scale."), Document.Components[1].RelativeTransform.RelativeScale3D, FVector(1.0f, 1.5f, 1.0f));
	TestEqual(TEXT("Visual component should retain template properties."), Document.Components[1].TemplateProperties.FindRef(TEXT("CollisionProfileName")), FString(TEXT("NoCollision")));

	return true;
}

bool FVergilInterfaceDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = 1;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_InterfaceModel");

	FVergilInterfaceDefinition Interface;
	Interface.InterfaceClassPath = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();
	Document.Interfaces.Add(Interface);

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid interface definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid interface definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored interfaces."), Document.Interfaces.Num(), 1);
	TestEqual(TEXT("Interface should retain its class path."), Document.Interfaces[0].InterfaceClassPath, Interface.InterfaceClassPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCompilerRequiresBlueprintTest,
	"Vergil.Scaffold.CompilerRequiresBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilCompilerRequiresBlueprintTest::RunTest(const FString& Parameters)
{
	FVergilCompileRequest Request;
	Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_Scaffold");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestFalse(TEXT("Compile should fail without a target blueprint."), Result.bSucceeded);
	TestTrue(TEXT("Compile should report diagnostics."), Result.Diagnostics.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCommandPlanningTest,
	"Vergil.Scaffold.CommandPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFunctionDefinitionPlanningTest,
	"Vergil.Scaffold.FunctionDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMacroDefinitionPlanningTest,
	"Vergil.Scaffold.MacroDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilComponentDefinitionPlanningTest,
	"Vergil.Scaffold.ComponentDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilInterfaceDefinitionPlanningTest,
	"Vergil.Scaffold.InterfaceDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilResultSummaryUtilitiesTest,
	"Vergil.Scaffold.ResultSummaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilVariableAuthoringExecutionTest,
	"Vergil.Scaffold.VariableAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFunctionAuthoringExecutionTest,
	"Vergil.Scaffold.FunctionAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMacroAuthoringExecutionTest,
	"Vergil.Scaffold.MacroAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilComponentAuthoringExecutionTest,
	"Vergil.Scaffold.ComponentAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilInterfaceAuthoringExecutionTest,
	"Vergil.Scaffold.InterfaceAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilResultSummaryUtilitiesTest::RunTest(const FString& Parameters)
{
	TArray<FVergilDiagnostic> NonErrorDiagnostics;
	NonErrorDiagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Info, TEXT("InfoCode"), TEXT("Informational diagnostic")));
	NonErrorDiagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Warning, TEXT("WarnCode"), TEXT("Warning diagnostic")));

	const FVergilDiagnosticSummary DiagnosticSummary = Vergil::SummarizeDiagnostics(NonErrorDiagnostics);
	TestEqual(TEXT("Diagnostic summary counts info diagnostics."), DiagnosticSummary.InfoCount, 1);
	TestEqual(TEXT("Diagnostic summary counts warning diagnostics."), DiagnosticSummary.WarningCount, 1);
	TestEqual(TEXT("Diagnostic summary counts error diagnostics."), DiagnosticSummary.ErrorCount, 0);
	TestEqual(TEXT("Diagnostic summary exposes the total count."), DiagnosticSummary.GetTotalCount(), 2);
	TestTrue(TEXT("Diagnostic summary display text includes counts."), DiagnosticSummary.ToDisplayString().Contains(TEXT("info=1 warnings=1 errors=0 total=2")));

	FVergilCompileResult CompileResult;
	CompileResult.bSucceeded = true;
	CompileResult.Diagnostics = NonErrorDiagnostics;
	CompileResult.Commands.AddDefaulted(3);

	const FVergilExecutionSummary CompileSummary = Vergil::SummarizeCompileResult(CompileResult);
	TestEqual(TEXT("Compile summary uses the compile label."), CompileSummary.Label, FString(TEXT("Compile")));
	TestTrue(TEXT("Compile summary succeeds without errors."), CompileSummary.bSucceeded);
	TestFalse(TEXT("Compile summary does not mark the plan as applied."), CompileSummary.bApplied);
	TestEqual(TEXT("Compile summary reports planned command count."), CompileSummary.PlannedCommandCount, 3);
	TestEqual(TEXT("Compile summary reports diagnostic totals."), CompileSummary.Diagnostics.GetTotalCount(), 2);

	TArray<FVergilDiagnostic> ApplyDiagnostics = NonErrorDiagnostics;
	ApplyDiagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Error, TEXT("ApplyError"), TEXT("Apply failed")));

	FVergilCompileResult ApplyResult;
	ApplyResult.bSucceeded = false;
	ApplyResult.bApplied = true;
	ApplyResult.ExecutedCommandCount = 2;
	ApplyResult.Diagnostics = ApplyDiagnostics;
	ApplyResult.Commands.AddDefaulted(4);

	const FVergilExecutionSummary ApplySummary = Vergil::SummarizeApplyResult(ApplyResult);
	TestEqual(TEXT("Apply summary uses the apply label."), ApplySummary.Label, FString(TEXT("Apply")));
	TestTrue(TEXT("Apply summary preserves the applied flag."), ApplySummary.bApplied);
	TestFalse(TEXT("Apply summary fails when apply diagnostics contain errors."), ApplySummary.bSucceeded);
	TestEqual(TEXT("Apply summary reports executed command count."), ApplySummary.ExecutedCommandCount, 2);
	TestEqual(TEXT("Apply summary reports error counts."), ApplySummary.Diagnostics.ErrorCount, 1);
	TestTrue(TEXT("Apply summary display text includes the label."), ApplySummary.ToDisplayString().Contains(TEXT("Apply succeeded=false applied=true planned=4 executed=2")));

	const FVergilExecutionSummary TestSummary = Vergil::SummarizeTestResult(
		TEXT("Vergil.Scaffold.ResultSummaries"),
		true,
		NonErrorDiagnostics,
		5,
		1);
	TestEqual(TEXT("Test summary preserves the provided label."), TestSummary.Label, FString(TEXT("Vergil.Scaffold.ResultSummaries")));
	TestTrue(TEXT("Test summary succeeds when diagnostics contain no errors."), TestSummary.bSucceeded);
	TestTrue(TEXT("Test summary treats executed work as applied."), TestSummary.bApplied);
	TestEqual(TEXT("Test summary reports planned command count."), TestSummary.PlannedCommandCount, 5);
	TestEqual(TEXT("Test summary reports executed command count."), TestSummary.ExecutedCommandCount, 1);

	return true;
}

bool FVergilVariableAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilVariableDefinition FlagVariable;
	FlagVariable.Name = TEXT("TestFlag");
	FlagVariable.Type.PinCategory = TEXT("bool");
	FlagVariable.Flags.bInstanceEditable = true;
	FlagVariable.Flags.bSaveGame = true;
	FlagVariable.Category = TEXT("State");
	FlagVariable.Metadata.Add(TEXT("Tooltip"), TEXT("Controls the scaffold test flag."));
	FlagVariable.DefaultValue = TEXT("true");

	FVergilVariableDefinition TargetActorVariable;
	TargetActorVariable.Name = TEXT("TargetActor");
	TargetActorVariable.Type.PinCategory = TEXT("object");
	TargetActorVariable.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	TargetActorVariable.Category = TEXT("References");
	TargetActorVariable.Metadata.Add(TEXT("Tooltip"), TEXT("Stores a runtime actor reference."));

	FVergilVariableDefinition SpawnTargetVariable;
	SpawnTargetVariable.Name = TEXT("SpawnTarget");
	SpawnTargetVariable.Type.PinCategory = TEXT("object");
	SpawnTargetVariable.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	SpawnTargetVariable.Flags.bInstanceEditable = true;
	SpawnTargetVariable.Flags.bExposeOnSpawn = true;
	SpawnTargetVariable.Flags.bPrivate = true;
	SpawnTargetVariable.Flags.bTransient = true;
	SpawnTargetVariable.Flags.bAdvancedDisplay = true;
	SpawnTargetVariable.Flags.bDeprecated = true;
	SpawnTargetVariable.Flags.bExposeToCinematics = true;
	SpawnTargetVariable.Category = TEXT("Spawn");
	SpawnTargetVariable.Metadata.Add(TEXT("Tooltip"), TEXT("Spawn-only target actor."));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(260.0f, -140.0f);

	FVergilGraphPin SelfValue;
	SelfValue.Id = FGuid::NewGuid();
	SelfValue.Name = UEdGraphSchema_K2::PN_Self;
	SelfValue.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfValue);

	FVergilGraphNode TargetSetterNode;
	TargetSetterNode.Id = FGuid::NewGuid();
	TargetSetterNode.Kind = EVergilNodeKind::VariableSet;
	TargetSetterNode.Descriptor = TEXT("K2.VarSet.TargetActor");
	TargetSetterNode.Position = FVector2D(560.0f, 0.0f);

	FVergilGraphPin TargetSetterExecIn;
	TargetSetterExecIn.Id = FGuid::NewGuid();
	TargetSetterExecIn.Name = TEXT("Execute");
	TargetSetterExecIn.Direction = EVergilPinDirection::Input;
	TargetSetterExecIn.bIsExec = true;
	TargetSetterNode.Pins.Add(TargetSetterExecIn);

	FVergilGraphPin TargetSetterThen;
	TargetSetterThen.Id = FGuid::NewGuid();
	TargetSetterThen.Name = TEXT("Then");
	TargetSetterThen.Direction = EVergilPinDirection::Output;
	TargetSetterThen.bIsExec = true;
	TargetSetterNode.Pins.Add(TargetSetterThen);

	FVergilGraphPin TargetSetterValue;
	TargetSetterValue.Id = FGuid::NewGuid();
	TargetSetterValue.Name = TEXT("TargetActor");
	TargetSetterValue.Direction = EVergilPinDirection::Input;
	TargetSetterNode.Pins.Add(TargetSetterValue);

	FVergilGraphNode FlagGetterNode;
	FlagGetterNode.Id = FGuid::NewGuid();
	FlagGetterNode.Kind = EVergilNodeKind::VariableGet;
	FlagGetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");
	FlagGetterNode.Position = FVector2D(260.0f, 220.0f);

	FVergilGraphPin FlagGetterValue;
	FlagGetterValue.Id = FGuid::NewGuid();
	FlagGetterValue.Name = TEXT("TestFlag");
	FlagGetterValue.Direction = EVergilPinDirection::Output;
	FlagGetterNode.Pins.Add(FlagGetterValue);

	FVergilGraphNode NotNode;
	NotNode.Id = FGuid::NewGuid();
	NotNode.Kind = EVergilNodeKind::Call;
	NotNode.Descriptor = TEXT("K2.Call.Not_PreBool");
	NotNode.Position = FVector2D(560.0f, 220.0f);
	NotNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetMathLibrary::StaticClass()->GetClassPathName().ToString());

	FVergilGraphPin NotInput;
	NotInput.Id = FGuid::NewGuid();
	NotInput.Name = TEXT("A");
	NotInput.Direction = EVergilPinDirection::Input;
	NotNode.Pins.Add(NotInput);

	FVergilGraphPin NotReturn;
	NotReturn.Id = FGuid::NewGuid();
	NotReturn.Name = UEdGraphSchema_K2::PN_ReturnValue;
	NotReturn.Direction = EVergilPinDirection::Output;
	NotNode.Pins.Add(NotReturn);

	FVergilGraphNode FlagSetterNode;
	FlagSetterNode.Id = FGuid::NewGuid();
	FlagSetterNode.Kind = EVergilNodeKind::VariableSet;
	FlagSetterNode.Descriptor = TEXT("K2.VarSet.TestFlag");
	FlagSetterNode.Position = FVector2D(860.0f, 220.0f);

	FVergilGraphPin FlagSetterExecIn;
	FlagSetterExecIn.Id = FGuid::NewGuid();
	FlagSetterExecIn.Name = TEXT("Execute");
	FlagSetterExecIn.Direction = EVergilPinDirection::Input;
	FlagSetterExecIn.bIsExec = true;
	FlagSetterNode.Pins.Add(FlagSetterExecIn);

	FVergilGraphPin FlagSetterValue;
	FlagSetterValue.Id = FGuid::NewGuid();
	FlagSetterValue.Name = TEXT("TestFlag");
	FlagSetterValue.Direction = EVergilPinDirection::Input;
	FlagSetterNode.Pins.Add(FlagSetterValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilVariableAuthoring");
	Document.Variables = { FlagVariable, TargetActorVariable, SpawnTargetVariable };
	Document.Nodes = { BeginPlayNode, SelfNode, TargetSetterNode, FlagGetterNode, NotNode, FlagSetterNode };

	FVergilGraphEdge EventToTargetSetter;
	EventToTargetSetter.Id = FGuid::NewGuid();
	EventToTargetSetter.SourceNodeId = BeginPlayNode.Id;
	EventToTargetSetter.SourcePinId = BeginPlayThen.Id;
	EventToTargetSetter.TargetNodeId = TargetSetterNode.Id;
	EventToTargetSetter.TargetPinId = TargetSetterExecIn.Id;
	Document.Edges.Add(EventToTargetSetter);

	FVergilGraphEdge TargetSetterToFlagSetter;
	TargetSetterToFlagSetter.Id = FGuid::NewGuid();
	TargetSetterToFlagSetter.SourceNodeId = TargetSetterNode.Id;
	TargetSetterToFlagSetter.SourcePinId = TargetSetterThen.Id;
	TargetSetterToFlagSetter.TargetNodeId = FlagSetterNode.Id;
	TargetSetterToFlagSetter.TargetPinId = FlagSetterExecIn.Id;
	Document.Edges.Add(TargetSetterToFlagSetter);

	FVergilGraphEdge SelfToTargetSetter;
	SelfToTargetSetter.Id = FGuid::NewGuid();
	SelfToTargetSetter.SourceNodeId = SelfNode.Id;
	SelfToTargetSetter.SourcePinId = SelfValue.Id;
	SelfToTargetSetter.TargetNodeId = TargetSetterNode.Id;
	SelfToTargetSetter.TargetPinId = TargetSetterValue.Id;
	Document.Edges.Add(SelfToTargetSetter);

	FVergilGraphEdge FlagGetterToNot;
	FlagGetterToNot.Id = FGuid::NewGuid();
	FlagGetterToNot.SourceNodeId = FlagGetterNode.Id;
	FlagGetterToNot.SourcePinId = FlagGetterValue.Id;
	FlagGetterToNot.TargetNodeId = NotNode.Id;
	FlagGetterToNot.TargetPinId = NotInput.Id;
	Document.Edges.Add(FlagGetterToNot);

	FVergilGraphEdge NotToFlagSetter;
	NotToFlagSetter.Id = FGuid::NewGuid();
	NotToFlagSetter.SourceNodeId = NotNode.Id;
	NotToFlagSetter.SourcePinId = NotReturn.Id;
	NotToFlagSetter.TargetNodeId = FlagSetterNode.Id;
	NotToFlagSetter.TargetPinId = FlagSetterValue.Id;
	Document.Edges.Add(NotToFlagSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Variable authoring document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Variable authoring document should be applied."), Result.bApplied);
	TestTrue(TEXT("Variable authoring document should execute commands."), Result.ExecutedCommandCount > 0);

	const FBPVariableDescription* const FlagDescription = FindBlueprintVariableDescription(Blueprint, TEXT("TestFlag"));
	const FBPVariableDescription* const TargetActorDescription = FindBlueprintVariableDescription(Blueprint, TEXT("TargetActor"));
	const FBPVariableDescription* const SpawnTargetDescription = FindBlueprintVariableDescription(Blueprint, TEXT("SpawnTarget"));

	TestNotNull(TEXT("TestFlag variable should exist on the blueprint."), FlagDescription);
	TestNotNull(TEXT("TargetActor variable should exist on the blueprint."), TargetActorDescription);
	TestNotNull(TEXT("SpawnTarget variable should exist on the blueprint."), SpawnTargetDescription);
	if (FlagDescription == nullptr || TargetActorDescription == nullptr || SpawnTargetDescription == nullptr)
	{
		return false;
	}

	UObject* const BlueprintCDO = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	TestNotNull(TEXT("Generated class default object should exist."), BlueprintCDO);
	if (BlueprintCDO == nullptr)
	{
		return false;
	}

	const FBoolProperty* const TestFlagProperty = FindFProperty<FBoolProperty>(Blueprint->GeneratedClass, TEXT("TestFlag"));
	TestNotNull(TEXT("Generated class should expose the TestFlag property."), TestFlagProperty);
	if (TestFlagProperty == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("TestFlag should be a bool variable."), FlagDescription->VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	TestTrue(TEXT("TestFlag should apply its default to the generated class default object."), TestFlagProperty->GetPropertyValue_InContainer(BlueprintCDO));
	TestTrue(TEXT("TestFlag should be instance editable."), (FlagDescription->PropertyFlags & CPF_DisableEditOnInstance) == 0);
	TestTrue(TEXT("TestFlag should be marked SaveGame."), (FlagDescription->PropertyFlags & CPF_SaveGame) != 0);
	TestEqual(TEXT("TestFlag should preserve its category."), FBlueprintEditorUtils::GetBlueprintVariableCategory(Blueprint, TEXT("TestFlag"), nullptr).ToString(), FString(TEXT("State")));

	FString TooltipValue;
	TestTrue(TEXT("TestFlag tooltip metadata should exist."), FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, TEXT("TestFlag"), nullptr, TEXT("Tooltip"), TooltipValue));
	TestEqual(TEXT("TestFlag tooltip metadata should match the document."), TooltipValue, FString(TEXT("Controls the scaffold test flag.")));

	TestTrue(TEXT("TargetActor should be an object variable."), TargetActorDescription->VarType.PinCategory == UEdGraphSchema_K2::PC_Object);
	TestTrue(TEXT("TargetActor should resolve AActor as its object class."), TargetActorDescription->VarType.PinSubCategoryObject.Get() == AActor::StaticClass());
	TestEqual(TEXT("TargetActor should preserve its category."), FBlueprintEditorUtils::GetBlueprintVariableCategory(Blueprint, TEXT("TargetActor"), nullptr).ToString(), FString(TEXT("References")));

	TestTrue(TEXT("SpawnTarget should be an object variable."), SpawnTargetDescription->VarType.PinCategory == UEdGraphSchema_K2::PC_Object);
	TestTrue(TEXT("SpawnTarget should resolve AActor as its object class."), SpawnTargetDescription->VarType.PinSubCategoryObject.Get() == AActor::StaticClass());
	TestTrue(TEXT("SpawnTarget should be transient."), (SpawnTargetDescription->PropertyFlags & CPF_Transient) != 0);
	TestTrue(TEXT("SpawnTarget should be advanced display."), (SpawnTargetDescription->PropertyFlags & CPF_AdvancedDisplay) != 0);
	TestTrue(TEXT("SpawnTarget should be deprecated."), (SpawnTargetDescription->PropertyFlags & CPF_Deprecated) != 0);
	TestTrue(TEXT("SpawnTarget should be exposed to cinematics."), (SpawnTargetDescription->PropertyFlags & CPF_Interp) != 0);
	TestEqual(TEXT("SpawnTarget should preserve its category."), FBlueprintEditorUtils::GetBlueprintVariableCategory(Blueprint, TEXT("SpawnTarget"), nullptr).ToString(), FString(TEXT("Spawn")));

	FString ExposeOnSpawnValue;
	TestTrue(TEXT("SpawnTarget expose-on-spawn metadata should exist."), FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, TEXT("SpawnTarget"), nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, ExposeOnSpawnValue));
	TestEqual(TEXT("SpawnTarget expose-on-spawn metadata should be true."), ExposeOnSpawnValue, FString(TEXT("true")));

	FString PrivateValue;
	TestTrue(TEXT("SpawnTarget private metadata should exist."), FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, TEXT("SpawnTarget"), nullptr, FBlueprintMetadata::MD_Private, PrivateValue));
	TestEqual(TEXT("SpawnTarget private metadata should be true."), PrivateValue, FString(TEXT("true")));

	TooltipValue.Reset();
	TestTrue(TEXT("SpawnTarget tooltip metadata should exist."), FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, TEXT("SpawnTarget"), nullptr, TEXT("Tooltip"), TooltipValue));
	TestEqual(TEXT("SpawnTarget tooltip metadata should match the document."), TooltipValue, FString(TEXT("Spawn-only target actor.")));

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after variable authoring execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventGraphNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_Self* const SelfGraphNode = FindGraphNodeByGuid<UK2Node_Self>(EventGraph, SelfNode.Id);
	UK2Node_VariableSet* const TargetSetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, TargetSetterNode.Id);
	UK2Node_VariableGet* const FlagGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, FlagGetterNode.Id);
	UK2Node_CallFunction* const NotGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, NotNode.Id);
	UK2Node_VariableSet* const FlagSetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, FlagSetterNode.Id);

	TestNotNull(TEXT("BeginPlay event node should exist."), EventGraphNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("TargetActor setter node should exist."), TargetSetterGraphNode);
	TestNotNull(TEXT("TestFlag getter node should exist."), FlagGetterGraphNode);
	TestNotNull(TEXT("Not call node should exist."), NotGraphNode);
	TestNotNull(TEXT("TestFlag setter node should exist."), FlagSetterGraphNode);
	if (EventGraphNode == nullptr || SelfGraphNode == nullptr || TargetSetterGraphNode == nullptr || FlagGetterGraphNode == nullptr || NotGraphNode == nullptr || FlagSetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const TargetSetterExecPin = TargetSetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const TargetSetterThenPin = TargetSetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const FlagSetterExecPin = FlagSetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SelfOutputPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const TargetSetterValuePin = TargetSetterGraphNode->FindPin(TEXT("TargetActor"));
	UEdGraphPin* const FlagGetterValuePin = FlagGetterGraphNode->FindPin(TEXT("TestFlag"));
	UEdGraphPin* const NotInputPin = NotGraphNode->FindPin(TEXT("A"));
	UEdGraphPin* const NotReturnPin = NotGraphNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* const FlagSetterValuePin = FlagSetterGraphNode->FindPin(TEXT("TestFlag"));

	TestTrue(TEXT("TargetActor setter should accept the self pin connection."), SelfOutputPin != nullptr && SelfOutputPin->LinkedTo.Contains(TargetSetterValuePin));
	TestTrue(TEXT("BeginPlay should execute the TargetActor setter."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(TargetSetterExecPin));
	TestTrue(TEXT("TargetActor setter should chain into the TestFlag setter."), TargetSetterThenPin != nullptr && TargetSetterThenPin->LinkedTo.Contains(FlagSetterExecPin));
	TestTrue(TEXT("TestFlag getter should feed the Not call input."), FlagGetterValuePin != nullptr && FlagGetterValuePin->LinkedTo.Contains(NotInputPin));
	TestTrue(TEXT("Not call should feed the TestFlag setter value input."), NotReturnPin != nullptr && NotReturnPin->LinkedTo.Contains(FlagSetterValuePin));

	return true;
}

bool FVergilFunctionDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilFunctionDefinition Function;
	Function.Name = TEXT("ComputeStatus");
	Function.bPure = true;
	Function.AccessSpecifier = EVergilFunctionAccessSpecifier::Protected;

	FVergilFunctionParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	Function.Inputs.Add(TargetInput);

	FVergilFunctionParameterDefinition ThresholdInput;
	ThresholdInput.Name = TEXT("Threshold");
	ThresholdInput.Type.PinCategory = TEXT("float");
	Function.Inputs.Add(ThresholdInput);

	FVergilFunctionParameterDefinition ResultOutput;
	ResultOutput.Name = TEXT("Result");
	ResultOutput.Type.PinCategory = TEXT("bool");
	Function.Outputs.Add(ResultOutput);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_FunctionPlanning");
	Document.Functions.Add(Function);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Function definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Function definition planning should emit a function-graph command plus target graph ensure."), Result.Commands.Num(), 2);
	if (!Result.bSucceeded || Result.Commands.Num() != 2)
	{
		return false;
	}

	const FVergilCompilerCommand& EnsureFunctionGraph = Result.Commands[0];
	TestEqual(TEXT("Planner should lower function definitions into EnsureFunctionGraph."), EnsureFunctionGraph.Type, EVergilCommandType::EnsureFunctionGraph);
	TestEqual(TEXT("Function graph name should match the authored function name."), EnsureFunctionGraph.GraphName, Function.Name);
	TestEqual(TEXT("Function graph secondary name should match the authored function name."), EnsureFunctionGraph.SecondaryName, Function.Name);
	TestEqual(TEXT("Function purity should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("bPure")), FString(TEXT("true")));
	TestEqual(TEXT("Function access should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("AccessSpecifier")), FString(TEXT("Protected")));
	TestEqual(TEXT("Function input count should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("InputCount")), FString(TEXT("2")));
	TestEqual(TEXT("Function output count should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("OutputCount")), FString(TEXT("1")));
	TestEqual(TEXT("First input name should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("Input_0_Name")), FString(TEXT("TargetActor")));
	TestEqual(TEXT("Second input type should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("Input_1_PinCategory")), FString(TEXT("float")));
	TestEqual(TEXT("Output name should be encoded on the command."), EnsureFunctionGraph.Attributes.FindRef(TEXT("Output_0_Name")), FString(TEXT("Result")));

	TestEqual(TEXT("Event graph ensure should still be emitted for the compile target."), Result.Commands[1].Type, EVergilCommandType::EnsureGraph);

	return true;
}

bool FVergilMacroDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilMacroDefinition Macro;
	Macro.Name = TEXT("RouteTarget");

	FVergilMacroParameterDefinition ExecuteInput;
	ExecuteInput.Name = TEXT("Execute");
	ExecuteInput.bIsExec = true;
	Macro.Inputs.Add(ExecuteInput);

	FVergilMacroParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	Macro.Inputs.Add(TargetInput);

	FVergilMacroParameterDefinition ThenOutput;
	ThenOutput.Name = TEXT("Then");
	ThenOutput.bIsExec = true;
	Macro.Outputs.Add(ThenOutput);

	FVergilMacroParameterDefinition ThresholdOutput;
	ThresholdOutput.Name = TEXT("Threshold");
	ThresholdOutput.Type.PinCategory = TEXT("float");
	Macro.Outputs.Add(ThresholdOutput);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_MacroPlanning");
	Document.Macros.Add(Macro);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Macro definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Macro definition planning should emit a macro-graph command plus target graph ensure."), Result.Commands.Num(), 2);
	if (!Result.bSucceeded || Result.Commands.Num() != 2)
	{
		return false;
	}

	const FVergilCompilerCommand& EnsureMacroGraph = Result.Commands[0];
	TestEqual(TEXT("Planner should lower macro definitions into EnsureMacroGraph."), EnsureMacroGraph.Type, EVergilCommandType::EnsureMacroGraph);
	TestEqual(TEXT("Macro graph name should match the authored macro name."), EnsureMacroGraph.GraphName, Macro.Name);
	TestEqual(TEXT("Macro graph secondary name should match the authored macro name."), EnsureMacroGraph.SecondaryName, Macro.Name);
	TestEqual(TEXT("Macro input count should be encoded on the command."), EnsureMacroGraph.Attributes.FindRef(TEXT("InputCount")), FString(TEXT("2")));
	TestEqual(TEXT("Macro output count should be encoded on the command."), EnsureMacroGraph.Attributes.FindRef(TEXT("OutputCount")), FString(TEXT("2")));
	TestEqual(TEXT("Exec input should be encoded on the command."), EnsureMacroGraph.Attributes.FindRef(TEXT("Input_0_bExec")), FString(TEXT("true")));
	TestEqual(TEXT("Object input should retain its type metadata."), EnsureMacroGraph.Attributes.FindRef(TEXT("Input_1_PinCategory")), FString(TEXT("object")));
	TestEqual(TEXT("Exec output should be encoded on the command."), EnsureMacroGraph.Attributes.FindRef(TEXT("Output_0_bExec")), FString(TEXT("true")));
	TestEqual(TEXT("Data output should retain its type metadata."), EnsureMacroGraph.Attributes.FindRef(TEXT("Output_1_PinCategory")), FString(TEXT("float")));

	TestEqual(TEXT("Event graph ensure should still be emitted for the compile target."), Result.Commands[1].Type, EVergilCommandType::EnsureGraph);

	return true;
}

bool FVergilFunctionAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilFunctionDefinition InitialFunction;
	InitialFunction.Name = TEXT("ComputeStatus");
	InitialFunction.bPure = true;
	InitialFunction.AccessSpecifier = EVergilFunctionAccessSpecifier::Protected;

	FVergilFunctionParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	InitialFunction.Inputs.Add(TargetInput);

	FVergilFunctionParameterDefinition ThresholdInput;
	ThresholdInput.Name = TEXT("Threshold");
	ThresholdInput.Type.PinCategory = TEXT("float");
	InitialFunction.Inputs.Add(ThresholdInput);

	FVergilFunctionParameterDefinition ResultOutput;
	ResultOutput.Name = TEXT("Result");
	ResultOutput.Type.PinCategory = TEXT("bool");
	InitialFunction.Outputs.Add(ResultOutput);

	FVergilGraphDocument InitialDocument;
	InitialDocument.BlueprintPath = TEXT("/Temp/BP_VergilFunctionAuthoring");
	InitialDocument.Functions.Add(InitialFunction);

	const FVergilCompileResult InitialResult = EditorSubsystem->CompileDocument(Blueprint, InitialDocument, false, false, true);
	TestTrue(TEXT("Initial function authoring should succeed."), InitialResult.bSucceeded);
	TestTrue(TEXT("Initial function authoring should apply commands."), InitialResult.bApplied);
	if (!InitialResult.bSucceeded || !InitialResult.bApplied)
	{
		return false;
	}

	UEdGraph* FunctionGraph = FindBlueprintGraphByName(Blueprint, InitialFunction.Name);
	UK2Node_FunctionEntry* EntryNode = FindFunctionEntryNode(FunctionGraph);
	UK2Node_FunctionResult* ResultNode = FindFunctionResultNode(FunctionGraph);

	TestNotNull(TEXT("Function graph should exist after initial authoring."), FunctionGraph);
	TestNotNull(TEXT("Function entry node should exist after initial authoring."), EntryNode);
	TestNotNull(TEXT("Function result node should exist after initial authoring."), ResultNode);
	if (FunctionGraph == nullptr || EntryNode == nullptr || ResultNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const TargetActorPin = EntryNode->FindPin(TEXT("TargetActor"));
	UEdGraphPin* const ThresholdPin = EntryNode->FindPin(TEXT("Threshold"));
	UEdGraphPin* const ResultPin = ResultNode->FindPin(TEXT("Result"));

	TestNotNull(TEXT("Function entry should expose the TargetActor input pin."), TargetActorPin);
	TestNotNull(TEXT("Function entry should expose the Threshold input pin."), ThresholdPin);
	TestNotNull(TEXT("Function result node should expose the Result pin."), ResultPin);
	TestTrue(TEXT("TargetActor should be an object pin."), TargetActorPin != nullptr
		&& TargetActorPin->Direction == EGPD_Output
		&& TargetActorPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
		&& TargetActorPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass());
	TestTrue(TEXT("Threshold should be a float pin."), ThresholdPin != nullptr
		&& ThresholdPin->Direction == EGPD_Output
		&& ThresholdPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real
		&& ThresholdPin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float);
	TestTrue(TEXT("Result should be a bool pin."), ResultPin != nullptr
		&& ResultPin->Direction == EGPD_Input
		&& ResultPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	TestTrue(TEXT("Function entry should retain the protected access flag."), EntryNode->HasAllExtraFlags(FUNC_Protected));
	TestTrue(TEXT("Function entry should retain the pure flag."), EntryNode->HasAllExtraFlags(FUNC_BlueprintPure));

	FVergilFunctionDefinition UpdatedFunction;
	UpdatedFunction.Name = InitialFunction.Name;
	UpdatedFunction.bPure = false;
	UpdatedFunction.AccessSpecifier = EVergilFunctionAccessSpecifier::Private;

	FVergilFunctionParameterDefinition IterationsInput;
	IterationsInput.Name = TEXT("Iterations");
	IterationsInput.Type.PinCategory = TEXT("int");
	UpdatedFunction.Inputs.Add(IterationsInput);

	FVergilGraphDocument UpdatedDocument;
	UpdatedDocument.BlueprintPath = InitialDocument.BlueprintPath;
	UpdatedDocument.Functions.Add(UpdatedFunction);

	const FVergilCompileResult UpdatedResult = EditorSubsystem->CompileDocument(Blueprint, UpdatedDocument, false, false, true);
	TestTrue(TEXT("Function signature update should succeed."), UpdatedResult.bSucceeded);
	TestTrue(TEXT("Function signature update should apply commands."), UpdatedResult.bApplied);
	if (!UpdatedResult.bSucceeded || !UpdatedResult.bApplied)
	{
		return false;
	}

	FunctionGraph = FindBlueprintGraphByName(Blueprint, UpdatedFunction.Name);
	EntryNode = FindFunctionEntryNode(FunctionGraph);
	ResultNode = FindFunctionResultNode(FunctionGraph);

	TestNotNull(TEXT("Function graph should still exist after update."), FunctionGraph);
	TestNotNull(TEXT("Function entry node should still exist after update."), EntryNode);
	if (FunctionGraph == nullptr || EntryNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const IterationsPin = EntryNode->FindPin(TEXT("Iterations"));
	TestNotNull(TEXT("Updated function entry should expose the Iterations pin."), IterationsPin);
	TestTrue(TEXT("Old TargetActor pin should be removed during update."), EntryNode->FindPin(TEXT("TargetActor")) == nullptr);
	TestTrue(TEXT("Old Threshold pin should be removed during update."), EntryNode->FindPin(TEXT("Threshold")) == nullptr);
	TestTrue(TEXT("Iterations should be an int pin."), IterationsPin != nullptr
		&& IterationsPin->Direction == EGPD_Output
		&& IterationsPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int);
	TestTrue(TEXT("Function entry should retain the private access flag after update."), EntryNode->HasAllExtraFlags(FUNC_Private));
	TestTrue(TEXT("Protected access should be cleared during update."), !EntryNode->HasAnyExtraFlags(FUNC_Protected));
	TestTrue(TEXT("Pure flag should be cleared during update."), !EntryNode->HasAnyExtraFlags(FUNC_BlueprintPure));
	if (ResultNode != nullptr)
	{
		TestTrue(TEXT("Result pin should be removed when outputs are cleared."), ResultNode->FindPin(TEXT("Result")) == nullptr);
	}

	return true;
}

bool FVergilMacroAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilMacroDefinition InitialMacro;
	InitialMacro.Name = TEXT("RouteTarget");

	FVergilMacroParameterDefinition ExecuteInput;
	ExecuteInput.Name = TEXT("Execute");
	ExecuteInput.bIsExec = true;
	InitialMacro.Inputs.Add(ExecuteInput);

	FVergilMacroParameterDefinition TargetInput;
	TargetInput.Name = TEXT("TargetActor");
	TargetInput.Type.PinCategory = TEXT("object");
	TargetInput.Type.ObjectPath = AActor::StaticClass()->GetClassPathName().ToString();
	InitialMacro.Inputs.Add(TargetInput);

	FVergilMacroParameterDefinition ThenOutput;
	ThenOutput.Name = TEXT("Then");
	ThenOutput.bIsExec = true;
	InitialMacro.Outputs.Add(ThenOutput);

	FVergilMacroParameterDefinition CountOutput;
	CountOutput.Name = TEXT("Count");
	CountOutput.Type.PinCategory = TEXT("int");
	InitialMacro.Outputs.Add(CountOutput);

	FVergilGraphDocument InitialDocument;
	InitialDocument.BlueprintPath = TEXT("/Temp/BP_VergilMacroAuthoring");
	InitialDocument.Macros.Add(InitialMacro);

	const FVergilCompileResult InitialResult = EditorSubsystem->CompileDocument(Blueprint, InitialDocument, false, false, true);
	TestTrue(TEXT("Initial macro authoring should succeed."), InitialResult.bSucceeded);
	TestTrue(TEXT("Initial macro authoring should apply commands."), InitialResult.bApplied);
	if (!InitialResult.bSucceeded || !InitialResult.bApplied)
	{
		return false;
	}

	UEdGraph* MacroGraph = FindBlueprintGraphByName(Blueprint, InitialMacro.Name);
	UK2Node_EditablePinBase* EntryNode = FindEditableGraphEntryNode(MacroGraph);
	UK2Node_EditablePinBase* ResultNode = FindEditableGraphResultNode(MacroGraph);

	TestNotNull(TEXT("Macro graph should exist after initial authoring."), MacroGraph);
	TestNotNull(TEXT("Macro entry tunnel should exist after initial authoring."), EntryNode);
	TestNotNull(TEXT("Macro exit tunnel should exist after initial authoring."), ResultNode);
	if (MacroGraph == nullptr || EntryNode == nullptr || ResultNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const ExecutePin = EntryNode->FindPin(TEXT("Execute"));
	UEdGraphPin* const TargetActorPin = EntryNode->FindPin(TEXT("TargetActor"));
	UEdGraphPin* const ThenPin = ResultNode->FindPin(TEXT("Then"));
	UEdGraphPin* const CountPin = ResultNode->FindPin(TEXT("Count"));

	TestNotNull(TEXT("Macro entry should expose the Execute pin."), ExecutePin);
	TestNotNull(TEXT("Macro entry should expose the TargetActor pin."), TargetActorPin);
	TestNotNull(TEXT("Macro exit should expose the Then pin."), ThenPin);
	TestNotNull(TEXT("Macro exit should expose the Count pin."), CountPin);
	TestTrue(TEXT("Execute should be an output exec pin."), ExecutePin != nullptr
		&& ExecutePin->Direction == EGPD_Output
		&& ExecutePin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
	TestTrue(TEXT("TargetActor should be an object pin."), TargetActorPin != nullptr
		&& TargetActorPin->Direction == EGPD_Output
		&& TargetActorPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object
		&& TargetActorPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass());
	TestTrue(TEXT("Then should be an input exec pin on the exit tunnel."), ThenPin != nullptr
		&& ThenPin->Direction == EGPD_Input
		&& ThenPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
	TestTrue(TEXT("Count should be an int pin on the exit tunnel."), CountPin != nullptr
		&& CountPin->Direction == EGPD_Input
		&& CountPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Int);

	FVergilMacroDefinition UpdatedMacro;
	UpdatedMacro.Name = InitialMacro.Name;

	FVergilMacroParameterDefinition EnabledInput;
	EnabledInput.Name = TEXT("bEnabled");
	EnabledInput.Type.PinCategory = TEXT("bool");
	UpdatedMacro.Inputs.Add(EnabledInput);

	FVergilMacroParameterDefinition MessagesOutput;
	MessagesOutput.Name = TEXT("Messages");
	MessagesOutput.Type.PinCategory = TEXT("string");
	MessagesOutput.Type.ContainerType = EVergilVariableContainerType::Array;
	UpdatedMacro.Outputs.Add(MessagesOutput);

	FVergilGraphDocument UpdatedDocument;
	UpdatedDocument.BlueprintPath = InitialDocument.BlueprintPath;
	UpdatedDocument.Macros.Add(UpdatedMacro);

	const FVergilCompileResult UpdatedResult = EditorSubsystem->CompileDocument(Blueprint, UpdatedDocument, false, false, true);
	TestTrue(TEXT("Macro signature update should succeed."), UpdatedResult.bSucceeded);
	TestTrue(TEXT("Macro signature update should apply commands."), UpdatedResult.bApplied);
	if (!UpdatedResult.bSucceeded || !UpdatedResult.bApplied)
	{
		return false;
	}

	MacroGraph = FindBlueprintGraphByName(Blueprint, UpdatedMacro.Name);
	EntryNode = FindEditableGraphEntryNode(MacroGraph);
	ResultNode = FindEditableGraphResultNode(MacroGraph);

	TestNotNull(TEXT("Macro graph should still exist after update."), MacroGraph);
	TestNotNull(TEXT("Macro entry tunnel should still exist after update."), EntryNode);
	TestNotNull(TEXT("Macro exit tunnel should still exist after update."), ResultNode);
	if (MacroGraph == nullptr || EntryNode == nullptr || ResultNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EnabledPin = EntryNode->FindPin(TEXT("bEnabled"));
	UEdGraphPin* const MessagesPin = ResultNode->FindPin(TEXT("Messages"));

	TestNotNull(TEXT("Updated macro entry should expose the bEnabled pin."), EnabledPin);
	TestNotNull(TEXT("Updated macro exit should expose the Messages pin."), MessagesPin);
	TestTrue(TEXT("Old Execute pin should be removed during update."), EntryNode->FindPin(TEXT("Execute")) == nullptr);
	TestTrue(TEXT("Old TargetActor pin should be removed during update."), EntryNode->FindPin(TEXT("TargetActor")) == nullptr);
	TestTrue(TEXT("Old Then pin should be removed during update."), ResultNode->FindPin(TEXT("Then")) == nullptr);
	TestTrue(TEXT("Old Count pin should be removed during update."), ResultNode->FindPin(TEXT("Count")) == nullptr);
	TestTrue(TEXT("bEnabled should be a bool output pin."), EnabledPin != nullptr
		&& EnabledPin->Direction == EGPD_Output
		&& EnabledPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
	TestTrue(TEXT("Messages should be a string array input pin."), MessagesPin != nullptr
		&& MessagesPin->Direction == EGPD_Input
		&& MessagesPin->PinType.PinCategory == UEdGraphSchema_K2::PC_String
		&& MessagesPin->PinType.ContainerType == EPinContainerType::Array);

	return true;
}

bool FVergilComponentDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilComponentDefinition RootComponent;
	RootComponent.Name = TEXT("Root");
	RootComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	RootComponent.RelativeTransform.bHasRelativeLocation = true;
	RootComponent.RelativeTransform.RelativeLocation = FVector(25.0f, 50.0f, 75.0f);

	FVergilComponentDefinition PivotComponent;
	PivotComponent.Name = TEXT("Pivot");
	PivotComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	PivotComponent.ParentComponentName = RootComponent.Name;
	PivotComponent.RelativeTransform.bHasRelativeRotation = true;
	PivotComponent.RelativeTransform.RelativeRotation = FRotator(0.0f, 45.0f, 0.0f);

	FVergilComponentDefinition VisualComponent;
	VisualComponent.Name = TEXT("VisualMesh");
	VisualComponent.ComponentClassPath = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	VisualComponent.ParentComponentName = PivotComponent.Name;
	VisualComponent.AttachSocketName = TEXT("GripSocket");
	VisualComponent.RelativeTransform.bHasRelativeScale = true;
	VisualComponent.RelativeTransform.RelativeScale3D = FVector(1.0f, 1.5f, 1.0f);
	VisualComponent.TemplateProperties.Add(TEXT("CollisionProfileName"), TEXT("NoCollision"));

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ComponentPlanning");
	Document.Components = { RootComponent, PivotComponent, VisualComponent };

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Component definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Component definition planning should emit component commands plus the target graph ensure."), Result.Commands.Num(), 9);
	if (!Result.bSucceeded || Result.Commands.Num() != 9)
	{
		return false;
	}

	TestEqual(TEXT("First component ensure should target the root component."), Result.Commands[0].Type, EVergilCommandType::EnsureComponent);
	TestEqual(TEXT("Root component ensure should target the authored root name."), Result.Commands[0].SecondaryName, RootComponent.Name);
	TestEqual(TEXT("Second component ensure should target the pivot component."), Result.Commands[1].SecondaryName, PivotComponent.Name);
	TestEqual(TEXT("Third component ensure should target the visual component."), Result.Commands[2].SecondaryName, VisualComponent.Name);
	TestEqual(TEXT("Root relative location should lower into a component property command."), Result.Commands[3].Type, EVergilCommandType::SetComponentProperty);
	TestEqual(TEXT("Root relative location should target RelativeLocation."), Result.Commands[3].Name, FName(TEXT("RelativeLocation")));
	TestEqual(TEXT("Pivot should lower into an attach command."), Result.Commands[4].Type, EVergilCommandType::AttachComponent);
	TestEqual(TEXT("Pivot attach should target the authored parent."), Result.Commands[4].Name, RootComponent.Name);
	TestEqual(TEXT("Pivot relative rotation should lower into a component property command."), Result.Commands[5].Name, FName(TEXT("RelativeRotation")));
	TestEqual(TEXT("Visual should lower into an attach command."), Result.Commands[6].Type, EVergilCommandType::AttachComponent);
	TestEqual(TEXT("Visual attach should retain the authored socket."), Result.Commands[6].StringValue, FString(TEXT("GripSocket")));
	TestEqual(TEXT("Visual relative scale should lower into a component property command."), Result.Commands[7].Name, FName(TEXT("RelativeScale3D")));
	TestEqual(TEXT("Event graph ensure should still be emitted for the compile target."), Result.Commands[8].Type, EVergilCommandType::EnsureGraph);
	TestFalse(TEXT("Template properties should remain out of scope for VGR-4004 lowering."), Result.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::SetComponentProperty
			&& Command.Name == TEXT("CollisionProfileName");
	}));

	return true;
}

bool FVergilInterfaceDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilInterfaceDefinition Interface;
	Interface.InterfaceClassPath = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_InterfacePlanning");
	Document.Interfaces.Add(Interface);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Interface definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Interface definition planning should emit an interface command plus the target graph ensure."), Result.Commands.Num(), 2);
	if (!Result.bSucceeded || Result.Commands.Num() != 2)
	{
		return false;
	}

	const FVergilCompilerCommand& EnsureInterface = Result.Commands[0];
	TestEqual(TEXT("Planner should lower interface definitions into EnsureInterface."), EnsureInterface.Type, EVergilCommandType::EnsureInterface);
	TestEqual(TEXT("Interface command should preserve the authored class path."), EnsureInterface.StringValue, Interface.InterfaceClassPath);
	TestEqual(TEXT("Event graph ensure should still be emitted for the compile target."), Result.Commands[1].Type, EVergilCommandType::EnsureGraph);

	return true;
}

bool FVergilComponentAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilComponentDefinition RootComponent;
	RootComponent.Name = TEXT("Root");
	RootComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	RootComponent.RelativeTransform.bHasRelativeLocation = true;
	RootComponent.RelativeTransform.RelativeLocation = FVector(10.0f, 20.0f, 30.0f);

	FVergilComponentDefinition PivotComponent;
	PivotComponent.Name = TEXT("Pivot");
	PivotComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	PivotComponent.ParentComponentName = RootComponent.Name;
	PivotComponent.RelativeTransform.bHasRelativeRotation = true;
	PivotComponent.RelativeTransform.RelativeRotation = FRotator(0.0f, 45.0f, 0.0f);

	FVergilComponentDefinition VisualComponent;
	VisualComponent.Name = TEXT("VisualMesh");
	VisualComponent.ComponentClassPath = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	VisualComponent.ParentComponentName = RootComponent.Name;
	VisualComponent.AttachSocketName = TEXT("HolsterSocket");

	FVergilGraphDocument InitialDocument;
	InitialDocument.BlueprintPath = TEXT("/Temp/BP_VergilComponentAuthoring");
	InitialDocument.Components = { RootComponent, PivotComponent, VisualComponent };

	const FVergilCompileResult InitialResult = EditorSubsystem->CompileDocument(Blueprint, InitialDocument, false, false, true);
	TestTrue(TEXT("Initial component authoring should succeed."), InitialResult.bSucceeded);
	TestTrue(TEXT("Initial component authoring should apply commands."), InitialResult.bApplied);
	if (!InitialResult.bSucceeded || !InitialResult.bApplied)
	{
		return false;
	}

	USCS_Node* RootNode = FindBlueprintComponentNode(Blueprint, RootComponent.Name);
	USCS_Node* PivotNode = FindBlueprintComponentNode(Blueprint, PivotComponent.Name);
	USCS_Node* VisualNode = FindBlueprintComponentNode(Blueprint, VisualComponent.Name);
	TestNotNull(TEXT("Root component should exist after initial authoring."), RootNode);
	TestNotNull(TEXT("Pivot component should exist after initial authoring."), PivotNode);
	TestNotNull(TEXT("Visual component should exist after initial authoring."), VisualNode);
	if (RootNode == nullptr || PivotNode == nullptr || VisualNode == nullptr || Blueprint->SimpleConstructionScript == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Pivot should initially attach under the root component."),
		Blueprint->SimpleConstructionScript->FindParentNode(PivotNode) == RootNode);
	TestTrue(TEXT("Visual should initially attach under the root component."),
		Blueprint->SimpleConstructionScript->FindParentNode(VisualNode) == RootNode);
	TestEqual(TEXT("Visual should initially retain its attach socket."), VisualNode->AttachToName, FName(TEXT("HolsterSocket")));

	USceneComponent* RootTemplate = Cast<USceneComponent>(RootNode->ComponentTemplate);
	USceneComponent* PivotTemplate = Cast<USceneComponent>(PivotNode->ComponentTemplate);
	TestNotNull(TEXT("Root component template should be a scene component."), RootTemplate);
	TestNotNull(TEXT("Pivot component template should be a scene component."), PivotTemplate);
	TestTrue(TEXT("Root component should retain its authored relative location."), RootTemplate != nullptr
		&& RootTemplate->GetRelativeLocation().Equals(FVector(10.0f, 20.0f, 30.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Pivot component should retain its authored relative rotation."), PivotTemplate != nullptr
		&& PivotTemplate->GetRelativeRotation().Equals(FRotator(0.0f, 45.0f, 0.0f), KINDA_SMALL_NUMBER));

	FVergilComponentDefinition UpdatedRootComponent = RootComponent;
	UpdatedRootComponent.RelativeTransform.RelativeLocation = FVector(40.0f, 0.0f, 10.0f);

	FVergilComponentDefinition UpdatedPivotComponent = PivotComponent;
	UpdatedPivotComponent.RelativeTransform.RelativeRotation = FRotator(0.0f, 90.0f, 0.0f);

	FVergilComponentDefinition UpdatedVisualComponent = VisualComponent;
	UpdatedVisualComponent.ParentComponentName = PivotComponent.Name;
	UpdatedVisualComponent.AttachSocketName = TEXT("GripSocket");
	UpdatedVisualComponent.RelativeTransform.bHasRelativeLocation = true;
	UpdatedVisualComponent.RelativeTransform.RelativeLocation = FVector(15.0f, 5.0f, 0.0f);
	UpdatedVisualComponent.RelativeTransform.bHasRelativeScale = true;
	UpdatedVisualComponent.RelativeTransform.RelativeScale3D = FVector(1.0f, 1.5f, 1.0f);

	FVergilGraphDocument UpdatedDocument;
	UpdatedDocument.BlueprintPath = InitialDocument.BlueprintPath;
	UpdatedDocument.Components = { UpdatedRootComponent, UpdatedPivotComponent, UpdatedVisualComponent };

	const FVergilCompileResult UpdatedResult = EditorSubsystem->CompileDocument(Blueprint, UpdatedDocument, false, false, true);
	TestTrue(TEXT("Component hierarchy update should succeed."), UpdatedResult.bSucceeded);
	TestTrue(TEXT("Component hierarchy update should apply commands."), UpdatedResult.bApplied);
	if (!UpdatedResult.bSucceeded || !UpdatedResult.bApplied)
	{
		return false;
	}

	RootNode = FindBlueprintComponentNode(Blueprint, UpdatedRootComponent.Name);
	PivotNode = FindBlueprintComponentNode(Blueprint, UpdatedPivotComponent.Name);
	VisualNode = FindBlueprintComponentNode(Blueprint, UpdatedVisualComponent.Name);
	TestNotNull(TEXT("Root component should still exist after update."), RootNode);
	TestNotNull(TEXT("Pivot component should still exist after update."), PivotNode);
	TestNotNull(TEXT("Visual component should still exist after update."), VisualNode);
	if (RootNode == nullptr || PivotNode == nullptr || VisualNode == nullptr || Blueprint->SimpleConstructionScript == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Visual should reattach under the pivot component during update."),
		Blueprint->SimpleConstructionScript->FindParentNode(VisualNode) == PivotNode);
	TestEqual(TEXT("Visual should retain its updated attach socket."), VisualNode->AttachToName, FName(TEXT("GripSocket")));

	RootTemplate = Cast<USceneComponent>(RootNode->ComponentTemplate);
	PivotTemplate = Cast<USceneComponent>(PivotNode->ComponentTemplate);
	UStaticMeshComponent* VisualTemplate = Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate);
	TestNotNull(TEXT("Visual component template should be a static mesh component."), VisualTemplate);
	TestTrue(TEXT("Root component should retain its updated relative location."), RootTemplate != nullptr
		&& RootTemplate->GetRelativeLocation().Equals(FVector(40.0f, 0.0f, 10.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Pivot component should retain its updated relative rotation."), PivotTemplate != nullptr
		&& PivotTemplate->GetRelativeRotation().Equals(FRotator(0.0f, 90.0f, 0.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Visual component should retain its authored relative location."), VisualTemplate != nullptr
		&& VisualTemplate->GetRelativeLocation().Equals(FVector(15.0f, 5.0f, 0.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Visual component should retain its authored relative scale."), VisualTemplate != nullptr
		&& VisualTemplate->GetRelativeScale3D().Equals(FVector(1.0f, 1.5f, 1.0f), KINDA_SMALL_NUMBER));

	return true;
}

bool FVergilInterfaceAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilInterfaceDefinition Interface;
	Interface.InterfaceClassPath = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilInterfaceAuthoring");
	Document.Interfaces.Add(Interface);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
	TestTrue(TEXT("Interface authoring document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Interface authoring document should be applied."), Result.bApplied);
	TestTrue(TEXT("Interface authoring document should execute commands."), Result.ExecutedCommandCount > 0);
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	TArray<UClass*> ImplementedInterfaces;
	FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, true, ImplementedInterfaces);
	TestTrue(TEXT("Document-authored interfaces should be implemented on the blueprint."), ImplementedInterfaces.Contains(UVergilAutomationTestInterface::StaticClass()));

	return true;
}

bool FVergilCommandPlanningTest::RunTest(const FString& Parameters)
{
	FVergilGraphNode EventNode;
	EventNode.Id = FGuid::NewGuid();
	EventNode.Kind = EVergilNodeKind::Event;
	EventNode.Descriptor = TEXT("K2.Event.BeginPlay");
	EventNode.Position = FVector2D(0.0f, 0.0f);
	EventNode.Metadata.Add(TEXT("Title"), TEXT("BeginPlay"));

	FVergilGraphPin EventExecOut;
	EventExecOut.Id = FGuid::NewGuid();
	EventExecOut.Name = TEXT("Then");
	EventExecOut.Direction = EVergilPinDirection::Output;
	EventExecOut.bIsExec = true;
	EventNode.Pins.Add(EventExecOut);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.PrintString");
	CallNode.Position = FVector2D(350.0f, 0.0f);

	FVergilGraphPin CallExecIn;
	CallExecIn.Id = FGuid::NewGuid();
	CallExecIn.Name = TEXT("Execute");
	CallExecIn.Direction = EVergilPinDirection::Input;
	CallExecIn.bIsExec = true;
	CallNode.Pins.Add(CallExecIn);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_CommandPlanning");
	Document.Nodes = { EventNode, CallNode };

	FVergilGraphEdge Edge;
	Edge.Id = FGuid::NewGuid();
	Edge.SourceNodeId = EventNode.Id;
	Edge.SourcePinId = EventExecOut.Id;
	Edge.TargetNodeId = CallNode.Id;
	Edge.TargetPinId = CallExecIn.Id;
	Document.Edges.Add(Edge);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Command planning should succeed for a structurally valid document."), Result.bSucceeded);
	TestEqual(TEXT("Expected command count."), Result.Commands.Num(), 4);
	TestEqual(TEXT("First command ensures the graph."), Result.Commands[0].Type, EVergilCommandType::EnsureGraph);
	TestEqual(TEXT("Second command adds the event node."), Result.Commands[1].Type, EVergilCommandType::AddNode);
	TestEqual(TEXT("Third command adds the call node."), Result.Commands[2].Type, EVergilCommandType::AddNode);
	TestEqual(TEXT("Last command connects pins."), Result.Commands.Last().Type, EVergilCommandType::ConnectPins);
	TestEqual(TEXT("Handled planner emits explicit event command."), Result.Commands[1].Name, FName(TEXT("Vergil.K2.Event")));
	TestEqual(TEXT("Handled planner emits explicit call command."), Result.Commands[2].Name, FName(TEXT("Vergil.K2.Call")));
	TestFalse(
		TEXT("Plain event and call planning should not emit metadata commands."),
		Result.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::SetNodeMetadata;
		}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSpecificHandlerDispatchTest,
	"Vergil.Scaffold.SpecificHandlerDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSpecificHandlerDispatchTest::RunTest(const FString& Parameters)
{
	FVergilNodeRegistry::Get().Reset();
	FVergilNodeRegistry::Get().RegisterHandler(MakeShared<FTestSpecificNodeHandler, ESPMode::ThreadSafe>());

	FVergilGraphNode Node;
	Node.Id = FGuid::NewGuid();
	Node.Kind = EVergilNodeKind::Custom;
	Node.Descriptor = TEXT("Test.Special");
	Node.Position = FVector2D(128.0f, 64.0f);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_SpecificHandler");
	Document.Nodes.Add(Node);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Specific handler compile should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Specific handler produces ensure graph + add node."), Result.Commands.Num(), 2);
	TestEqual(TEXT("Specific handler overrides generic descriptor payload."), Result.Commands[1].Name, FName(TEXT("HandledBySpecificHandler")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCommentExecutionTest,
	"Vergil.Scaffold.CommentExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilCommentExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphNode CommentNode;
	CommentNode.Id = FGuid::NewGuid();
	CommentNode.Kind = EVergilNodeKind::Comment;
	CommentNode.Descriptor = TEXT("UI.Comment");
	CommentNode.Position = FVector2D(128.0f, 96.0f);
	CommentNode.Metadata.Add(TEXT("CommentText"), TEXT("Vergil applies comments"));
	CommentNode.Metadata.Add(TEXT("CommentWidth"), TEXT("520"));
	CommentNode.Metadata.Add(TEXT("CommentHeight"), TEXT("180"));
	CommentNode.Metadata.Add(TEXT("FontSize"), TEXT("20"));

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilCommentExecution");
	Document.Nodes.Add(CommentNode);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Comment document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Command plan should be applied."), Result.bApplied);
	TestTrue(TEXT("At least one command should execute."), Result.ExecutedCommandCount > 0);
	TestTrue(TEXT("Comment planning should emit commands."), Result.Commands.Num() >= 2);
	TestEqual(TEXT("Planner uses explicit Vergil comment node command."), Result.Commands[1].Name, FName(TEXT("Vergil.Comment")));

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after command execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UEdGraphNode_Comment* Comment = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (UEdGraphNode_Comment* Candidate = Cast<UEdGraphNode_Comment>(Node))
		{
			Comment = Candidate;
			break;
		}
	}

	TestNotNull(TEXT("A comment node should be created in the event graph."), Comment);
	if (Comment == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Comment text should match metadata."), Comment->NodeComment, FString(TEXT("Vergil applies comments")));
	TestEqual(TEXT("Comment width should match metadata."), Comment->NodeWidth, 520);
	TestEqual(TEXT("Comment height should match metadata."), Comment->NodeHeight, 180);
	TestEqual(TEXT("Comment font size should match metadata."), Comment->FontSize, 20);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFunctionGraphEnsureTest,
	"Vergil.Scaffold.FunctionGraphEnsure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilFunctionGraphEnsureTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilCompilerCommand EnsureFunctionGraph;
	EnsureFunctionGraph.Type = EVergilCommandType::EnsureFunctionGraph;
	EnsureFunctionGraph.GraphName = TEXT("VergilGeneratedFunction");
	EnsureFunctionGraph.SecondaryName = EnsureFunctionGraph.GraphName;

	const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { EnsureFunctionGraph });

	TestTrue(TEXT("Function graph ensure command should apply cleanly."), Result.bSucceeded);
	TestTrue(TEXT("Function graph ensure command should mutate the blueprint."), Result.bApplied);
	TestEqual(TEXT("Exactly one ensure command should execute."), Result.ExecutedCommandCount, 1);

	UEdGraph* const FunctionGraph = FindBlueprintGraphByName(Blueprint, TEXT("VergilGeneratedFunction"));
	TestNotNull(TEXT("Function graph should be created by EnsureFunctionGraph."), FunctionGraph);
	return FunctionGraph != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMacroGraphEnsureTest,
	"Vergil.Scaffold.MacroGraphEnsure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMacroGraphEnsureTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilCompilerCommand EnsureMacroGraph;
	EnsureMacroGraph.Type = EVergilCommandType::EnsureMacroGraph;
	EnsureMacroGraph.GraphName = TEXT("VergilGeneratedMacro");
	EnsureMacroGraph.SecondaryName = EnsureMacroGraph.GraphName;

	const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { EnsureMacroGraph });

	TestTrue(TEXT("Macro graph ensure command should apply cleanly."), Result.bSucceeded);
	TestTrue(TEXT("Macro graph ensure command should mutate the blueprint."), Result.bApplied);
	TestEqual(TEXT("Exactly one ensure command should execute."), Result.ExecutedCommandCount, 1);

	UEdGraph* const MacroGraph = FindBlueprintGraphByName(Blueprint, TEXT("VergilGeneratedMacro"));
	TestNotNull(TEXT("Macro graph should be created by EnsureMacroGraph."), MacroGraph);
	return MacroGraph != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilExplicitCommandSurfaceExecutionTest,
	"Vergil.Scaffold.ExplicitCommandSurfaceExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilExplicitCommandSurfaceExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FName OldFunctionName(TEXT("OldFunction"));
	const FName RenamedFunctionName(TEXT("RenamedFunction"));
	const FName OldMacroName(TEXT("OldMacro"));
	const FName RenamedMacroName(TEXT("RenamedMacro"));
	const FName RootComponentName(TEXT("Root"));
	const FName VisualComponentName(TEXT("Visual"));
	const FName RenamedVisualComponentName(TEXT("RenamedVisual"));
	const FName OldVariableName(TEXT("PendingFlag"));
	const FName RenamedVariableName(TEXT("RenamedFlag"));
	const FName AttachSocketName(TEXT("WeaponSocket"));
	const FVector DesiredRelativeLocation(25.0f, 50.0f, 75.0f);

	TArray<FVergilCompilerCommand> Commands;

	FVergilCompilerCommand EnsureFunctionGraph;
	EnsureFunctionGraph.Type = EVergilCommandType::EnsureFunctionGraph;
	EnsureFunctionGraph.GraphName = OldFunctionName;
	EnsureFunctionGraph.SecondaryName = OldFunctionName;
	Commands.Add(EnsureFunctionGraph);

	FVergilCompilerCommand EnsureMacroGraph;
	EnsureMacroGraph.Type = EVergilCommandType::EnsureMacroGraph;
	EnsureMacroGraph.GraphName = OldMacroName;
	EnsureMacroGraph.SecondaryName = OldMacroName;
	Commands.Add(EnsureMacroGraph);

	FVergilCompilerCommand EnsureRootComponent;
	EnsureRootComponent.Type = EVergilCommandType::EnsureComponent;
	EnsureRootComponent.SecondaryName = RootComponentName;
	EnsureRootComponent.StringValue = USceneComponent::StaticClass()->GetClassPathName().ToString();
	Commands.Add(EnsureRootComponent);

	FVergilCompilerCommand EnsureVisualComponent;
	EnsureVisualComponent.Type = EVergilCommandType::EnsureComponent;
	EnsureVisualComponent.SecondaryName = VisualComponentName;
	EnsureVisualComponent.StringValue = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	Commands.Add(EnsureVisualComponent);

	FVergilCompilerCommand RenameComponent;
	RenameComponent.Type = EVergilCommandType::RenameMember;
	RenameComponent.Name = VisualComponentName;
	RenameComponent.SecondaryName = RenamedVisualComponentName;
	RenameComponent.Attributes.Add(TEXT("MemberType"), TEXT("Component"));
	Commands.Add(RenameComponent);

	FVergilCompilerCommand AttachVisualComponent;
	AttachVisualComponent.Type = EVergilCommandType::AttachComponent;
	AttachVisualComponent.Name = RootComponentName;
	AttachVisualComponent.SecondaryName = RenamedVisualComponentName;
	AttachVisualComponent.StringValue = AttachSocketName.ToString();
	Commands.Add(AttachVisualComponent);

	FVergilCompilerCommand SetVisualLocation;
	SetVisualLocation.Type = EVergilCommandType::SetComponentProperty;
	SetVisualLocation.Name = TEXT("RelativeLocation");
	SetVisualLocation.SecondaryName = RenamedVisualComponentName;
	SetVisualLocation.StringValue = DesiredRelativeLocation.ToString();
	Commands.Add(SetVisualLocation);

	FVergilCompilerCommand SetVisualHiddenInGame;
	SetVisualHiddenInGame.Type = EVergilCommandType::SetComponentProperty;
	SetVisualHiddenInGame.Name = TEXT("HiddenInGame");
	SetVisualHiddenInGame.SecondaryName = RenamedVisualComponentName;
	SetVisualHiddenInGame.StringValue = TEXT("True");
	Commands.Add(SetVisualHiddenInGame);

	FVergilCompilerCommand EnsureInterface;
	EnsureInterface.Type = EVergilCommandType::EnsureInterface;
	EnsureInterface.StringValue = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();
	Commands.Add(EnsureInterface);

	FVergilCompilerCommand EnsureVariable;
	EnsureVariable.Type = EVergilCommandType::EnsureVariable;
	EnsureVariable.SecondaryName = OldVariableName;
	EnsureVariable.Attributes.Add(TEXT("PinCategory"), TEXT("bool"));
	Commands.Add(EnsureVariable);

	FVergilCompilerCommand RenameVariable;
	RenameVariable.Type = EVergilCommandType::RenameMember;
	RenameVariable.Name = OldVariableName;
	RenameVariable.SecondaryName = RenamedVariableName;
	RenameVariable.Attributes.Add(TEXT("MemberType"), TEXT("Variable"));
	Commands.Add(RenameVariable);

	FVergilCompilerCommand RenameFunctionGraph;
	RenameFunctionGraph.Type = EVergilCommandType::RenameMember;
	RenameFunctionGraph.Name = OldFunctionName;
	RenameFunctionGraph.SecondaryName = RenamedFunctionName;
	RenameFunctionGraph.Attributes.Add(TEXT("MemberType"), TEXT("FunctionGraph"));
	Commands.Add(RenameFunctionGraph);

	FVergilCompilerCommand RenameMacroGraph;
	RenameMacroGraph.Type = EVergilCommandType::RenameMember;
	RenameMacroGraph.Name = OldMacroName;
	RenameMacroGraph.SecondaryName = RenamedMacroName;
	RenameMacroGraph.Attributes.Add(TEXT("MemberType"), TEXT("MacroGraph"));
	Commands.Add(RenameMacroGraph);

	FVergilCompilerCommand CompileBlueprint;
	CompileBlueprint.Type = EVergilCommandType::CompileBlueprint;
	Commands.Add(CompileBlueprint);

	FVergilCompilerCommand SetClassDefault;
	SetClassDefault.Type = EVergilCommandType::SetClassDefault;
	SetClassDefault.Name = RenamedVariableName;
	SetClassDefault.StringValue = TEXT("True");
	Commands.Add(SetClassDefault);

	const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, Commands);

	TestTrue(TEXT("Explicit command surface plan should apply cleanly."), Result.bSucceeded);
	TestTrue(TEXT("Explicit command surface plan should mutate the blueprint."), Result.bApplied);
	TestEqual(TEXT("Every explicit command should execute exactly once."), Result.ExecutedCommandCount, Commands.Num());
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	TestNull(TEXT("Old function graph name should be gone after rename."), FindBlueprintGraphByName(Blueprint, OldFunctionName));
	TestNotNull(TEXT("Renamed function graph should exist."), FindBlueprintGraphByName(Blueprint, RenamedFunctionName));
	TestNull(TEXT("Old macro graph name should be gone after rename."), FindBlueprintGraphByName(Blueprint, OldMacroName));
	TestNotNull(TEXT("Renamed macro graph should exist."), FindBlueprintGraphByName(Blueprint, RenamedMacroName));

	USCS_Node* const RootNode = FindBlueprintComponentNode(Blueprint, RootComponentName);
	USCS_Node* const VisualNode = FindBlueprintComponentNode(Blueprint, RenamedVisualComponentName);
	TestNotNull(TEXT("Root component node should exist."), RootNode);
	TestNotNull(TEXT("Renamed visual component node should exist."), VisualNode);
	if (RootNode == nullptr || VisualNode == nullptr)
	{
		return false;
	}

	USCS_Node* const VisualParentNode = Blueprint->SimpleConstructionScript != nullptr
		? Blueprint->SimpleConstructionScript->FindParentNode(VisualNode)
		: nullptr;
	TestTrue(TEXT("Renamed visual component should remain attached under the root component."), VisualParentNode == RootNode);
	TestEqual(TEXT("Renamed visual component should retain its attach socket."), VisualNode->AttachToName, AttachSocketName);

	UStaticMeshComponent* const VisualTemplate = Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate);
	TestNotNull(TEXT("Renamed visual component should retain a static mesh component template."), VisualTemplate);
	if (VisualTemplate == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Component property command should set the relative location."), VisualTemplate->GetRelativeLocation(), DesiredRelativeLocation);

	FBoolProperty* const HiddenInGameProperty = FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("bHiddenInGame"));
	TestNotNull(TEXT("Static mesh component should expose bHiddenInGame."), HiddenInGameProperty);
	TestTrue(TEXT("Component property command should set HiddenInGame via flexible property lookup."), HiddenInGameProperty != nullptr && HiddenInGameProperty->GetPropertyValue_InContainer(VisualTemplate));

	TArray<UClass*> ImplementedInterfaces;
	FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, true, ImplementedInterfaces);
	TestTrue(TEXT("Explicit interface command should add the test interface."), ImplementedInterfaces.Contains(UVergilAutomationTestInterface::StaticClass()));

	TestNull(TEXT("Old variable name should be gone after rename."), FindBlueprintVariableDescription(Blueprint, OldVariableName));
	TestNotNull(TEXT("Renamed variable should exist after rename."), FindBlueprintVariableDescription(Blueprint, RenamedVariableName));

	UClass* const GeneratedClass = Blueprint->GeneratedClass.Get();
	TestNotNull(TEXT("Generated class should exist after explicit compile."), GeneratedClass);
	FBoolProperty* const RenamedVariableProperty = GeneratedClass != nullptr
		? FindFProperty<FBoolProperty>(GeneratedClass, RenamedVariableName)
		: nullptr;
	TestNotNull(TEXT("Generated class should expose the renamed bool property."), RenamedVariableProperty);
	TestTrue(
		TEXT("Class default command should set the renamed bool property on the CDO."),
		RenamedVariableProperty != nullptr
			&& GeneratedClass != nullptr
			&& RenamedVariableProperty->GetPropertyValue_InContainer(GeneratedClass->GetDefaultObject()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMoveAndRemoveNodeExecutionTest,
	"Vergil.Scaffold.MoveAndRemoveNodeExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMoveAndRemoveNodeExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FGuid MovedNodeId = FGuid::NewGuid();
	const FGuid RemovedNodeId = FGuid::NewGuid();

	FVergilCompilerCommand AddMovedComment;
	AddMovedComment.Type = EVergilCommandType::AddNode;
	AddMovedComment.GraphName = TEXT("EventGraph");
	AddMovedComment.NodeId = MovedNodeId;
	AddMovedComment.Name = TEXT("Vergil.Comment");
	AddMovedComment.Position = FVector2D(0.0f, 0.0f);

	FVergilCompilerCommand AddRemovedComment;
	AddRemovedComment.Type = EVergilCommandType::AddNode;
	AddRemovedComment.GraphName = TEXT("EventGraph");
	AddRemovedComment.NodeId = RemovedNodeId;
	AddRemovedComment.Name = TEXT("Vergil.Comment");
	AddRemovedComment.Position = FVector2D(120.0f, 0.0f);

	FVergilCompilerCommand MoveMovedComment;
	MoveMovedComment.Type = EVergilCommandType::MoveNode;
	MoveMovedComment.GraphName = TEXT("EventGraph");
	MoveMovedComment.NodeId = MovedNodeId;
	MoveMovedComment.Position = FVector2D(640.0f, 320.0f);

	FVergilCompilerCommand RemoveComment;
	RemoveComment.Type = EVergilCommandType::RemoveNode;
	RemoveComment.GraphName = TEXT("EventGraph");
	RemoveComment.NodeId = RemovedNodeId;

	const TArray<FVergilCompilerCommand> Commands = { AddMovedComment, AddRemovedComment, MoveMovedComment, RemoveComment };
	const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, Commands);

	TestTrue(TEXT("Move/remove node command plan should apply cleanly."), Result.bSucceeded);
	TestTrue(TEXT("Move/remove node command plan should mutate the blueprint."), Result.bApplied);
	TestEqual(TEXT("Every move/remove command should execute exactly once."), Result.ExecutedCommandCount, Commands.Num());
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UEdGraphNode_Comment* const MovedComment = FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, MovedNodeId);
	UEdGraphNode_Comment* const RemovedComment = FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, RemovedNodeId);
	TestNotNull(TEXT("Moved comment node should still exist."), MovedComment);
	TestNull(TEXT("Removed comment node should no longer exist."), RemovedComment);
	TestEqual(TEXT("Move node command should update X position."), MovedComment != nullptr ? MovedComment->NodePosX : INDEX_NONE, 640);
	TestEqual(TEXT("Move node command should update Y position."), MovedComment != nullptr ? MovedComment->NodePosY : INDEX_NONE, 320);

	return MovedComment != nullptr && RemovedComment == nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSupportedK2ChainExecutionTest,
	"Vergil.Scaffold.SupportedK2ChainExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSupportedK2ChainExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode BranchNode;
	BranchNode.Id = FGuid::NewGuid();
	BranchNode.Kind = EVergilNodeKind::ControlFlow;
	BranchNode.Descriptor = TEXT("K2.Branch");
	BranchNode.Position = FVector2D(350.0f, 0.0f);

	FVergilGraphPin BranchExecIn;
	BranchExecIn.Id = FGuid::NewGuid();
	BranchExecIn.Name = TEXT("Execute");
	BranchExecIn.Direction = EVergilPinDirection::Input;
	BranchExecIn.bIsExec = true;
	BranchNode.Pins.Add(BranchExecIn);

	FVergilGraphPin BranchThen;
	BranchThen.Id = FGuid::NewGuid();
	BranchThen.Name = TEXT("Then");
	BranchThen.Direction = EVergilPinDirection::Output;
	BranchThen.bIsExec = true;
	BranchNode.Pins.Add(BranchThen);

	FVergilGraphNode SequenceNode;
	SequenceNode.Id = FGuid::NewGuid();
	SequenceNode.Kind = EVergilNodeKind::ControlFlow;
	SequenceNode.Descriptor = TEXT("K2.Sequence");
	SequenceNode.Position = FVector2D(700.0f, 0.0f);

	FVergilGraphPin SequenceExecIn;
	SequenceExecIn.Id = FGuid::NewGuid();
	SequenceExecIn.Name = TEXT("Execute");
	SequenceExecIn.Direction = EVergilPinDirection::Input;
	SequenceExecIn.bIsExec = true;
	SequenceNode.Pins.Add(SequenceExecIn);

	FVergilGraphPin SequenceThen0;
	SequenceThen0.Id = FGuid::NewGuid();
	SequenceThen0.Name = TEXT("Then_0");
	SequenceThen0.Direction = EVergilPinDirection::Output;
	SequenceThen0.bIsExec = true;
	SequenceNode.Pins.Add(SequenceThen0);

	FVergilGraphPin SequenceThen1;
	SequenceThen1.Id = FGuid::NewGuid();
	SequenceThen1.Name = TEXT("Then_1");
	SequenceThen1.Direction = EVergilPinDirection::Output;
	SequenceThen1.bIsExec = true;
	SequenceNode.Pins.Add(SequenceThen1);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(1050.0f, 0.0f);

	FVergilGraphPin CallExecIn;
	CallExecIn.Id = FGuid::NewGuid();
	CallExecIn.Name = TEXT("Execute");
	CallExecIn.Direction = EVergilPinDirection::Input;
	CallExecIn.bIsExec = true;
	CallNode.Pins.Add(CallExecIn);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSupportedK2Chain");
	Document.Nodes = { BeginPlayNode, BranchNode, SequenceNode, CallNode };

	FVergilGraphEdge BeginPlayToBranch;
	BeginPlayToBranch.Id = FGuid::NewGuid();
	BeginPlayToBranch.SourceNodeId = BeginPlayNode.Id;
	BeginPlayToBranch.SourcePinId = BeginPlayThen.Id;
	BeginPlayToBranch.TargetNodeId = BranchNode.Id;
	BeginPlayToBranch.TargetPinId = BranchExecIn.Id;
	Document.Edges.Add(BeginPlayToBranch);

	FVergilGraphEdge BranchToSequence;
	BranchToSequence.Id = FGuid::NewGuid();
	BranchToSequence.SourceNodeId = BranchNode.Id;
	BranchToSequence.SourcePinId = BranchThen.Id;
	BranchToSequence.TargetNodeId = SequenceNode.Id;
	BranchToSequence.TargetPinId = SequenceExecIn.Id;
	Document.Edges.Add(BranchToSequence);

	FVergilGraphEdge SequenceToCall;
	SequenceToCall.Id = FGuid::NewGuid();
	SequenceToCall.SourceNodeId = SequenceNode.Id;
	SequenceToCall.SourcePinId = SequenceThen0.Id;
	SequenceToCall.TargetNodeId = CallNode.Id;
	SequenceToCall.TargetPinId = CallExecIn.Id;
	Document.Edges.Add(SequenceToCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Supported K2 chain should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Supported K2 chain should be applied."), Result.bApplied);
	TestTrue(TEXT("Supported K2 chain should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after K2 chain execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_IfThenElse* const BranchGraphNode = FindGraphNodeByGuid<UK2Node_IfThenElse>(EventGraph, BranchNode.Id);
	UK2Node_ExecutionSequence* const SequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, SequenceNode.Id);
	UK2Node_CallFunction* const CallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, CallNode.Id);

	TestNotNull(TEXT("BeginPlay event node should exist."), EventNode);
	TestNotNull(TEXT("Branch node should exist."), BranchGraphNode);
	TestNotNull(TEXT("Sequence node should exist."), SequenceGraphNode);
	TestNotNull(TEXT("Call function node should exist."), CallGraphNode);
	if (EventNode == nullptr || BranchGraphNode == nullptr || SequenceGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const BranchExecPin = BranchGraphNode->GetExecPin();
	UEdGraphPin* const BranchThenPin = BranchGraphNode->GetThenPin();
	UEdGraphPin* const SequenceExecPin = SequenceGraphNode->GetExecPin();
	UEdGraphPin* const SequenceThen0Pin = SequenceGraphNode->GetThenPinGivenIndex(0);
	UEdGraphPin* const CallExecPin = CallGraphNode->GetExecPin();

	TestTrue(TEXT("BeginPlay should link to Branch.Execute."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(BranchExecPin));
	TestTrue(TEXT("Branch.Then should link to Sequence.Execute."), BranchThenPin != nullptr && BranchThenPin->LinkedTo.Contains(SequenceExecPin));
	TestTrue(TEXT("Sequence.Then_0 should link to Call.Execute."), SequenceThen0Pin != nullptr && SequenceThen0Pin->LinkedTo.Contains(CallExecPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilPureDataFlowExecutionTest,
	"Vergil.Scaffold.PureDataFlowExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilPureDataFlowExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("Test member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestFlag"), BoolType, TEXT("true")));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");
	GetterNode.Position = FVector2D(350.0f, -180.0f);

	FVergilGraphPin GetterValue;
	GetterValue.Id = FGuid::NewGuid();
	GetterValue.Name = TEXT("TestFlag");
	GetterValue.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValue);

	FVergilGraphNode NotNode;
	NotNode.Id = FGuid::NewGuid();
	NotNode.Kind = EVergilNodeKind::Call;
	NotNode.Descriptor = TEXT("K2.Call.Not_PreBool");
	NotNode.Position = FVector2D(700.0f, -180.0f);
	NotNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetMathLibrary::StaticClass()->GetClassPathName().ToString());

	FVergilGraphPin NotInput;
	NotInput.Id = FGuid::NewGuid();
	NotInput.Name = TEXT("A");
	NotInput.Direction = EVergilPinDirection::Input;
	NotNode.Pins.Add(NotInput);

	FVergilGraphPin NotReturn;
	NotReturn.Id = FGuid::NewGuid();
	NotReturn.Name = UEdGraphSchema_K2::PN_ReturnValue;
	NotReturn.Direction = EVergilPinDirection::Output;
	NotNode.Pins.Add(NotReturn);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.TestFlag");
	SetterNode.Position = FVector2D(700.0f, 0.0f);

	FVergilGraphPin SetterExecIn;
	SetterExecIn.Id = FGuid::NewGuid();
	SetterExecIn.Name = TEXT("Execute");
	SetterExecIn.Direction = EVergilPinDirection::Input;
	SetterExecIn.bIsExec = true;
	SetterNode.Pins.Add(SetterExecIn);

	FVergilGraphPin SetterValue;
	SetterValue.Id = FGuid::NewGuid();
	SetterValue.Name = TEXT("TestFlag");
	SetterValue.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilPureDataFlow");
	Document.Nodes = { BeginPlayNode, GetterNode, NotNode, SetterNode };

	FVergilGraphEdge ExecEdge;
	ExecEdge.Id = FGuid::NewGuid();
	ExecEdge.SourceNodeId = BeginPlayNode.Id;
	ExecEdge.SourcePinId = BeginPlayThen.Id;
	ExecEdge.TargetNodeId = SetterNode.Id;
	ExecEdge.TargetPinId = SetterExecIn.Id;
	Document.Edges.Add(ExecEdge);

	FVergilGraphEdge GetToNot;
	GetToNot.Id = FGuid::NewGuid();
	GetToNot.SourceNodeId = GetterNode.Id;
	GetToNot.SourcePinId = GetterValue.Id;
	GetToNot.TargetNodeId = NotNode.Id;
	GetToNot.TargetPinId = NotInput.Id;
	Document.Edges.Add(GetToNot);

	FVergilGraphEdge NotToSet;
	NotToSet.Id = FGuid::NewGuid();
	NotToSet.SourceNodeId = NotNode.Id;
	NotToSet.SourcePinId = NotReturn.Id;
	NotToSet.TargetNodeId = SetterNode.Id;
	NotToSet.TargetPinId = SetterValue.Id;
	Document.Edges.Add(NotToSet);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Pure data-flow document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Pure data-flow document should be applied."), Result.bApplied);
	TestTrue(TEXT("Pure data-flow document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after pure data-flow execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_VariableGet* const VariableGetNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, GetterNode.Id);
	UK2Node_CallFunction* const PureCallNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, NotNode.Id);
	UK2Node_VariableSet* const VariableSetNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetterNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("Variable get node should exist."), VariableGetNode);
	TestNotNull(TEXT("Pure call node should exist."), PureCallNode);
	TestNotNull(TEXT("Variable set node should exist."), VariableSetNode);
	if (EventNode == nullptr || VariableGetNode == nullptr || PureCallNode == nullptr || VariableSetNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetExecPin = VariableSetNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const GetValuePin = VariableGetNode->FindPin(TEXT("TestFlag"));
	UEdGraphPin* const NotInputPin = PureCallNode->FindPin(TEXT("A"));
	UEdGraphPin* const NotReturnPin = PureCallNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* const SetValuePin = VariableSetNode->FindPin(TEXT("TestFlag"));

	TestTrue(TEXT("Variable getter should remain pure."), VariableGetNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("Pure call should remain pure."), PureCallNode->GetExecPin() == nullptr);
	TestTrue(TEXT("Event should link to variable set execute."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(SetExecPin));
	TestTrue(TEXT("Variable get should feed pure call input."), GetValuePin != nullptr && GetValuePin->LinkedTo.Contains(NotInputPin));
	TestTrue(TEXT("Pure call return should feed variable set input."), NotReturnPin != nullptr && NotReturnPin->LinkedTo.Contains(SetValuePin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSelectExecutionTest,
	"Vergil.Scaffold.SelectExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSelectExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("TestFlag member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestFlag"), BoolType, TEXT("true")));

	FEdGraphPinType ActorRefType;
	ActorRefType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorRefType.PinSubCategoryObject = AActor::StaticClass();
	TestTrue(TEXT("ActorRef member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ActorRef"), ActorRefType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode FlagGetterNode;
	FlagGetterNode.Id = FGuid::NewGuid();
	FlagGetterNode.Kind = EVergilNodeKind::VariableGet;
	FlagGetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");
	FlagGetterNode.Position = FVector2D(300.0f, -220.0f);

	FVergilGraphPin FlagGetterValue;
	FlagGetterValue.Id = FGuid::NewGuid();
	FlagGetterValue.Name = TEXT("TestFlag");
	FlagGetterValue.Direction = EVergilPinDirection::Output;
	FlagGetterNode.Pins.Add(FlagGetterValue);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(300.0f, -60.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode ActorGetterNode;
	ActorGetterNode.Id = FGuid::NewGuid();
	ActorGetterNode.Kind = EVergilNodeKind::VariableGet;
	ActorGetterNode.Descriptor = TEXT("K2.VarGet.ActorRef");
	ActorGetterNode.Position = FVector2D(300.0f, 120.0f);

	FVergilGraphPin ActorGetterValue;
	ActorGetterValue.Id = FGuid::NewGuid();
	ActorGetterValue.Name = TEXT("ActorRef");
	ActorGetterValue.Direction = EVergilPinDirection::Output;
	ActorGetterNode.Pins.Add(ActorGetterValue);

	FVergilGraphNode SelectNode;
	SelectNode.Id = FGuid::NewGuid();
	SelectNode.Kind = EVergilNodeKind::Custom;
	SelectNode.Descriptor = TEXT("K2.Select");
	SelectNode.Position = FVector2D(680.0f, -40.0f);
	SelectNode.Metadata.Add(TEXT("IndexPinCategory"), TEXT("bool"));
	SelectNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("object"));
	SelectNode.Metadata.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());

	FVergilGraphPin SelectIndex;
	SelectIndex.Id = FGuid::NewGuid();
	SelectIndex.Name = TEXT("Index");
	SelectIndex.Direction = EVergilPinDirection::Input;
	SelectNode.Pins.Add(SelectIndex);

	FVergilGraphPin SelectOption0;
	SelectOption0.Id = FGuid::NewGuid();
	SelectOption0.Name = TEXT("Option 0");
	SelectOption0.Direction = EVergilPinDirection::Input;
	SelectNode.Pins.Add(SelectOption0);

	FVergilGraphPin SelectOption1;
	SelectOption1.Id = FGuid::NewGuid();
	SelectOption1.Name = TEXT("Option 1");
	SelectOption1.Direction = EVergilPinDirection::Input;
	SelectNode.Pins.Add(SelectOption1);

	FVergilGraphPin SelectReturn;
	SelectReturn.Id = FGuid::NewGuid();
	SelectReturn.Name = UEdGraphSchema_K2::PN_ReturnValue;
	SelectReturn.Direction = EVergilPinDirection::Output;
	SelectNode.Pins.Add(SelectReturn);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.ActorRef");
	SetterNode.Position = FVector2D(1040.0f, 0.0f);

	FVergilGraphPin SetterExecIn;
	SetterExecIn.Id = FGuid::NewGuid();
	SetterExecIn.Name = TEXT("Execute");
	SetterExecIn.Direction = EVergilPinDirection::Input;
	SetterExecIn.bIsExec = true;
	SetterNode.Pins.Add(SetterExecIn);

	FVergilGraphPin SetterValue;
	SetterValue.Id = FGuid::NewGuid();
	SetterValue.Name = TEXT("ActorRef");
	SetterValue.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSelectExecution");
	Document.Nodes = { BeginPlayNode, FlagGetterNode, SelfNode, ActorGetterNode, SelectNode, SetterNode };

	FVergilGraphEdge ExecEdge;
	ExecEdge.Id = FGuid::NewGuid();
	ExecEdge.SourceNodeId = BeginPlayNode.Id;
	ExecEdge.SourcePinId = BeginPlayThen.Id;
	ExecEdge.TargetNodeId = SetterNode.Id;
	ExecEdge.TargetPinId = SetterExecIn.Id;
	Document.Edges.Add(ExecEdge);

	FVergilGraphEdge FlagToSelect;
	FlagToSelect.Id = FGuid::NewGuid();
	FlagToSelect.SourceNodeId = FlagGetterNode.Id;
	FlagToSelect.SourcePinId = FlagGetterValue.Id;
	FlagToSelect.TargetNodeId = SelectNode.Id;
	FlagToSelect.TargetPinId = SelectIndex.Id;
	Document.Edges.Add(FlagToSelect);

	FVergilGraphEdge SelfToSelect;
	SelfToSelect.Id = FGuid::NewGuid();
	SelfToSelect.SourceNodeId = SelfNode.Id;
	SelfToSelect.SourcePinId = SelfOutput.Id;
	SelfToSelect.TargetNodeId = SelectNode.Id;
	SelfToSelect.TargetPinId = SelectOption0.Id;
	Document.Edges.Add(SelfToSelect);

	FVergilGraphEdge ActorGetterToSelect;
	ActorGetterToSelect.Id = FGuid::NewGuid();
	ActorGetterToSelect.SourceNodeId = ActorGetterNode.Id;
	ActorGetterToSelect.SourcePinId = ActorGetterValue.Id;
	ActorGetterToSelect.TargetNodeId = SelectNode.Id;
	ActorGetterToSelect.TargetPinId = SelectOption1.Id;
	Document.Edges.Add(ActorGetterToSelect);

	FVergilGraphEdge SelectToSet;
	SelectToSet.Id = FGuid::NewGuid();
	SelectToSet.SourceNodeId = SelectNode.Id;
	SelectToSet.SourcePinId = SelectReturn.Id;
	SelectToSet.TargetNodeId = SetterNode.Id;
	SelectToSet.TargetPinId = SetterValue.Id;
	Document.Edges.Add(SelectToSet);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Select document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Select document should be applied."), Result.bApplied);
	TestTrue(TEXT("Select document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after select execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_VariableGet* FlagGetterGraphNode = nullptr;
	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_VariableGet* ActorGetterGraphNode = nullptr;
	UK2Node_Select* SelectGraphNode = nullptr;
	UK2Node_VariableSet* SetterGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (FlagGetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("TestFlag")) != nullptr)
				{
					FlagGetterGraphNode = Candidate;
					continue;
				}
			}
		}
		if (SelfGraphNode == nullptr)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}
		if (ActorGetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("ActorRef")) != nullptr)
				{
					ActorGetterGraphNode = Candidate;
					continue;
				}
			}
		}
		if (SelectGraphNode == nullptr)
		{
			SelectGraphNode = Cast<UK2Node_Select>(Node);
		}
		if (SetterGraphNode == nullptr)
		{
			SetterGraphNode = Cast<UK2Node_VariableSet>(Node);
		}
	}

	TestNotNull(TEXT("Flag getter should exist."), FlagGetterGraphNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Actor getter should exist."), ActorGetterGraphNode);
	TestNotNull(TEXT("Select node should exist."), SelectGraphNode);
	TestNotNull(TEXT("Setter node should exist."), SetterGraphNode);
	if (FlagGetterGraphNode == nullptr || SelfGraphNode == nullptr || ActorGetterGraphNode == nullptr || SelectGraphNode == nullptr || SetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const FlagPin = FlagGetterGraphNode->FindPin(TEXT("TestFlag"));
	UEdGraphPin* const SelfPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const ActorPin = ActorGetterGraphNode->FindPin(TEXT("ActorRef"));
	UEdGraphPin* const SelectIndexPin = SelectGraphNode->GetIndexPin();
	UEdGraphPin* const SelectReturnPin = SelectGraphNode->GetReturnValuePin();
	UEdGraphPin* const SetterValuePin = SetterGraphNode->FindPin(TEXT("ActorRef"));
	TArray<UEdGraphPin*> SelectOptionPins;
	SelectGraphNode->GetOptionPins(SelectOptionPins);

	TestTrue(TEXT("Select should remain pure."), SelectGraphNode->IsNodePure());
	TestEqual(TEXT("Bool select should expose two option pins."), SelectOptionPins.Num(), 2);
	TestTrue(TEXT("Flag getter should feed select index."), FlagPin != nullptr && FlagPin->LinkedTo.Contains(SelectIndexPin));
	TestTrue(TEXT("Self should feed select option 0."), SelfPin != nullptr && SelectOptionPins.Num() > 0 && SelfPin->LinkedTo.Contains(SelectOptionPins[0]));
	TestTrue(TEXT("Actor getter should feed select option 1."), ActorPin != nullptr && SelectOptionPins.Num() > 1 && ActorPin->LinkedTo.Contains(SelectOptionPins[1]));
	TestTrue(TEXT("Select return should feed setter value."), SelectReturnPin != nullptr && SelectReturnPin->LinkedTo.Contains(SetterValuePin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSwitchIntExecutionTest,
	"Vergil.Scaffold.SwitchIntExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSwitchIntExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType IntType;
	IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("Mode member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Mode"), IntType, TEXT("0")));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.Mode");
	GetterNode.Position = FVector2D(280.0f, -160.0f);

	FVergilGraphPin GetterValue;
	GetterValue.Id = FGuid::NewGuid();
	GetterValue.Name = TEXT("Mode");
	GetterValue.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValue);

	FVergilGraphNode SwitchNode;
	SwitchNode.Id = FGuid::NewGuid();
	SwitchNode.Kind = EVergilNodeKind::ControlFlow;
	SwitchNode.Descriptor = TEXT("K2.SwitchInt");
	SwitchNode.Position = FVector2D(520.0f, 0.0f);

	FVergilGraphPin SwitchExecIn;
	SwitchExecIn.Id = FGuid::NewGuid();
	SwitchExecIn.Name = UEdGraphSchema_K2::PN_Execute;
	SwitchExecIn.Direction = EVergilPinDirection::Input;
	SwitchExecIn.bIsExec = true;
	SwitchNode.Pins.Add(SwitchExecIn);

	FVergilGraphPin SwitchSelection;
	SwitchSelection.Id = FGuid::NewGuid();
	SwitchSelection.Name = TEXT("Selection");
	SwitchSelection.Direction = EVergilPinDirection::Input;
	SwitchNode.Pins.Add(SwitchSelection);

	FVergilGraphPin SwitchCase0;
	SwitchCase0.Id = FGuid::NewGuid();
	SwitchCase0.Name = TEXT("0");
	SwitchCase0.Direction = EVergilPinDirection::Output;
	SwitchCase0.bIsExec = true;
	SwitchNode.Pins.Add(SwitchCase0);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(860.0f, 0.0f);

	FVergilGraphPin CallExecIn;
	CallExecIn.Id = FGuid::NewGuid();
	CallExecIn.Name = TEXT("Execute");
	CallExecIn.Direction = EVergilPinDirection::Input;
	CallExecIn.bIsExec = true;
	CallNode.Pins.Add(CallExecIn);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSwitchIntExecution");
	Document.Nodes = { BeginPlayNode, GetterNode, SwitchNode, CallNode };

	FVergilGraphEdge EventToSwitch;
	EventToSwitch.Id = FGuid::NewGuid();
	EventToSwitch.SourceNodeId = BeginPlayNode.Id;
	EventToSwitch.SourcePinId = BeginPlayThen.Id;
	EventToSwitch.TargetNodeId = SwitchNode.Id;
	EventToSwitch.TargetPinId = SwitchExecIn.Id;
	Document.Edges.Add(EventToSwitch);

	FVergilGraphEdge GetterToSwitch;
	GetterToSwitch.Id = FGuid::NewGuid();
	GetterToSwitch.SourceNodeId = GetterNode.Id;
	GetterToSwitch.SourcePinId = GetterValue.Id;
	GetterToSwitch.TargetNodeId = SwitchNode.Id;
	GetterToSwitch.TargetPinId = SwitchSelection.Id;
	Document.Edges.Add(GetterToSwitch);

	FVergilGraphEdge SwitchToCall;
	SwitchToCall.Id = FGuid::NewGuid();
	SwitchToCall.SourceNodeId = SwitchNode.Id;
	SwitchToCall.SourcePinId = SwitchCase0.Id;
	SwitchToCall.TargetNodeId = CallNode.Id;
	SwitchToCall.TargetPinId = CallExecIn.Id;
	Document.Edges.Add(SwitchToCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Switch int document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Switch int document should be applied."), Result.bApplied);
	TestTrue(TEXT("Switch int document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after switch int execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_VariableGet* GetterGraphNode = nullptr;
	UK2Node_SwitchInteger* SwitchGraphNode = nullptr;
	UK2Node_CallFunction* CallGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (GetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("Mode")) != nullptr)
				{
					GetterGraphNode = Candidate;
				}
			}
		}
		if (SwitchGraphNode == nullptr)
		{
			SwitchGraphNode = Cast<UK2Node_SwitchInteger>(Node);
		}
		if (CallGraphNode == nullptr)
		{
			CallGraphNode = Cast<UK2Node_CallFunction>(Node);
		}
	}

	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Switch int node should exist."), SwitchGraphNode);
	TestNotNull(TEXT("Call node should exist."), CallGraphNode);
	if (GetterGraphNode == nullptr || SwitchGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const GetterPin = GetterGraphNode->FindPin(TEXT("Mode"));
	UEdGraphPin* const SwitchSelectionPin = SwitchGraphNode->GetSelectionPin();
	UEdGraphPin* const SwitchCase0Pin = SwitchGraphNode->FindPin(TEXT("0"));
	UEdGraphPin* const CallExecPin = CallGraphNode->GetExecPin();

	TestEqual(TEXT("Switch int start index should match first planned case."), SwitchGraphNode->StartIndex, 0);
	TestTrue(TEXT("Getter should feed switch selection."), GetterPin != nullptr && GetterPin->LinkedTo.Contains(SwitchSelectionPin));
	TestTrue(TEXT("Switch case 0 should link to call execute."), SwitchCase0Pin != nullptr && SwitchCase0Pin->LinkedTo.Contains(CallExecPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSwitchEnumExecutionTest,
	"Vergil.Scaffold.SwitchEnumExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSwitchEnumExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	UEnum* const MovementModeEnum = LoadObject<UEnum>(nullptr, TEXT("/Script/Engine.EMovementMode"));
	TestNotNull(TEXT("EMovementMode enum should be loadable."), MovementModeEnum);
	if (MovementModeEnum == nullptr)
	{
		return false;
	}

	FEdGraphPinType EnumType;
	EnumType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	EnumType.PinSubCategoryObject = MovementModeEnum;
	TestTrue(TEXT("MovementModeVar member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("MovementModeVar"), EnumType, TEXT("MOVE_None")));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.MovementModeVar");
	GetterNode.Position = FVector2D(280.0f, -160.0f);

	FVergilGraphPin GetterValue;
	GetterValue.Id = FGuid::NewGuid();
	GetterValue.Name = TEXT("MovementModeVar");
	GetterValue.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValue);

	FVergilGraphNode SwitchNode;
	SwitchNode.Id = FGuid::NewGuid();
	SwitchNode.Kind = EVergilNodeKind::ControlFlow;
	SwitchNode.Descriptor = TEXT("K2.SwitchEnum");
	SwitchNode.Position = FVector2D(560.0f, 0.0f);
	SwitchNode.Metadata.Add(TEXT("EnumPath"), TEXT("/Script/Engine.EMovementMode"));

	FVergilGraphPin SwitchExecIn;
	SwitchExecIn.Id = FGuid::NewGuid();
	SwitchExecIn.Name = UEdGraphSchema_K2::PN_Execute;
	SwitchExecIn.Direction = EVergilPinDirection::Input;
	SwitchExecIn.bIsExec = true;
	SwitchNode.Pins.Add(SwitchExecIn);

	FVergilGraphPin SwitchSelection;
	SwitchSelection.Id = FGuid::NewGuid();
	SwitchSelection.Name = TEXT("Selection");
	SwitchSelection.Direction = EVergilPinDirection::Input;
	SwitchNode.Pins.Add(SwitchSelection);

	FVergilGraphPin SwitchCaseNone;
	SwitchCaseNone.Id = FGuid::NewGuid();
	SwitchCaseNone.Name = TEXT("MOVE_None");
	SwitchCaseNone.Direction = EVergilPinDirection::Output;
	SwitchCaseNone.bIsExec = true;
	SwitchNode.Pins.Add(SwitchCaseNone);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(920.0f, 0.0f);

	FVergilGraphPin CallExecIn;
	CallExecIn.Id = FGuid::NewGuid();
	CallExecIn.Name = TEXT("Execute");
	CallExecIn.Direction = EVergilPinDirection::Input;
	CallExecIn.bIsExec = true;
	CallNode.Pins.Add(CallExecIn);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSwitchEnumExecution");
	Document.Nodes = { BeginPlayNode, GetterNode, SwitchNode, CallNode };

	FVergilGraphEdge EventToSwitch;
	EventToSwitch.Id = FGuid::NewGuid();
	EventToSwitch.SourceNodeId = BeginPlayNode.Id;
	EventToSwitch.SourcePinId = BeginPlayThen.Id;
	EventToSwitch.TargetNodeId = SwitchNode.Id;
	EventToSwitch.TargetPinId = SwitchExecIn.Id;
	Document.Edges.Add(EventToSwitch);

	FVergilGraphEdge GetterToSwitch;
	GetterToSwitch.Id = FGuid::NewGuid();
	GetterToSwitch.SourceNodeId = GetterNode.Id;
	GetterToSwitch.SourcePinId = GetterValue.Id;
	GetterToSwitch.TargetNodeId = SwitchNode.Id;
	GetterToSwitch.TargetPinId = SwitchSelection.Id;
	Document.Edges.Add(GetterToSwitch);

	FVergilGraphEdge SwitchToCall;
	SwitchToCall.Id = FGuid::NewGuid();
	SwitchToCall.SourceNodeId = SwitchNode.Id;
	SwitchToCall.SourcePinId = SwitchCaseNone.Id;
	SwitchToCall.TargetNodeId = CallNode.Id;
	SwitchToCall.TargetPinId = CallExecIn.Id;
	Document.Edges.Add(SwitchToCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Switch enum document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Switch enum document should be applied."), Result.bApplied);
	TestTrue(TEXT("Switch enum document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after switch enum execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_VariableGet* GetterGraphNode = nullptr;
	UK2Node_SwitchEnum* SwitchGraphNode = nullptr;
	UK2Node_CallFunction* CallGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (GetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("MovementModeVar")) != nullptr)
				{
					GetterGraphNode = Candidate;
				}
			}
		}
		if (SwitchGraphNode == nullptr)
		{
			SwitchGraphNode = Cast<UK2Node_SwitchEnum>(Node);
		}
		if (CallGraphNode == nullptr)
		{
			CallGraphNode = Cast<UK2Node_CallFunction>(Node);
		}
	}

	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Switch enum node should exist."), SwitchGraphNode);
	TestNotNull(TEXT("Call node should exist."), CallGraphNode);
	if (GetterGraphNode == nullptr || SwitchGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const GetterPin = GetterGraphNode->FindPin(TEXT("MovementModeVar"));
	UEdGraphPin* const SwitchSelectionPin = SwitchGraphNode->GetSelectionPin();
	UEdGraphPin* const SwitchCaseNonePin = SwitchGraphNode->FindPin(TEXT("MOVE_None"));
	UEdGraphPin* const CallExecPin = CallGraphNode->GetExecPin();

	TestTrue(TEXT("Switch enum should resolve the requested enum."), SwitchGraphNode->GetEnum() == MovementModeEnum);
	TestTrue(TEXT("Getter should feed switch selection."), GetterPin != nullptr && GetterPin->LinkedTo.Contains(SwitchSelectionPin));
	TestTrue(TEXT("Switch enum MOVE_None should link to call execute."), SwitchCaseNonePin != nullptr && SwitchCaseNonePin->LinkedTo.Contains(CallExecPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSelfReroutePureCastExecutionTest,
	"Vergil.Scaffold.SelfReroutePureCastExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSelfReroutePureCastExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType ActorRefType;
	ActorRefType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorRefType.PinSubCategoryObject = AActor::StaticClass();
	TestTrue(TEXT("ActorRef member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ActorRef"), ActorRefType, FString()));

	const FName CastResultPinName = MakeCastResultPinName(AActor::StaticClass());

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(300.0f, -180.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode RerouteNode;
	RerouteNode.Id = FGuid::NewGuid();
	RerouteNode.Kind = EVergilNodeKind::Custom;
	RerouteNode.Descriptor = TEXT("K2.Reroute");
	RerouteNode.Position = FVector2D(560.0f, -180.0f);

	FVergilGraphPin RerouteInput;
	RerouteInput.Id = FGuid::NewGuid();
	RerouteInput.Name = TEXT("InputPin");
	RerouteInput.Direction = EVergilPinDirection::Input;
	RerouteNode.Pins.Add(RerouteInput);

	FVergilGraphPin RerouteOutput;
	RerouteOutput.Id = FGuid::NewGuid();
	RerouteOutput.Name = TEXT("OutputPin");
	RerouteOutput.Direction = EVergilPinDirection::Output;
	RerouteNode.Pins.Add(RerouteOutput);

	FVergilGraphNode CastNode;
	CastNode.Id = FGuid::NewGuid();
	CastNode.Kind = EVergilNodeKind::Custom;
	CastNode.Descriptor = TEXT("K2.Cast");
	CastNode.Position = FVector2D(820.0f, -180.0f);
	CastNode.Metadata.Add(TEXT("TargetClassPath"), AActor::StaticClass()->GetPathName());

	FVergilGraphPin CastObjectInput;
	CastObjectInput.Id = FGuid::NewGuid();
	CastObjectInput.Name = UEdGraphSchema_K2::PN_ObjectToCast;
	CastObjectInput.Direction = EVergilPinDirection::Input;
	CastNode.Pins.Add(CastObjectInput);

	FVergilGraphPin CastResultOutput;
	CastResultOutput.Id = FGuid::NewGuid();
	CastResultOutput.Name = CastResultPinName;
	CastResultOutput.Direction = EVergilPinDirection::Output;
	CastNode.Pins.Add(CastResultOutput);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.ActorRef");
	SetterNode.Position = FVector2D(1080.0f, 0.0f);

	FVergilGraphPin SetterExecIn;
	SetterExecIn.Id = FGuid::NewGuid();
	SetterExecIn.Name = TEXT("Execute");
	SetterExecIn.Direction = EVergilPinDirection::Input;
	SetterExecIn.bIsExec = true;
	SetterNode.Pins.Add(SetterExecIn);

	FVergilGraphPin SetterValue;
	SetterValue.Id = FGuid::NewGuid();
	SetterValue.Name = TEXT("ActorRef");
	SetterValue.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSelfReroutePureCast");
	Document.Nodes = { BeginPlayNode, SelfNode, RerouteNode, CastNode, SetterNode };

	FVergilGraphEdge ExecEdge;
	ExecEdge.Id = FGuid::NewGuid();
	ExecEdge.SourceNodeId = BeginPlayNode.Id;
	ExecEdge.SourcePinId = BeginPlayThen.Id;
	ExecEdge.TargetNodeId = SetterNode.Id;
	ExecEdge.TargetPinId = SetterExecIn.Id;
	Document.Edges.Add(ExecEdge);

	FVergilGraphEdge SelfToReroute;
	SelfToReroute.Id = FGuid::NewGuid();
	SelfToReroute.SourceNodeId = SelfNode.Id;
	SelfToReroute.SourcePinId = SelfOutput.Id;
	SelfToReroute.TargetNodeId = RerouteNode.Id;
	SelfToReroute.TargetPinId = RerouteInput.Id;
	Document.Edges.Add(SelfToReroute);

	FVergilGraphEdge RerouteToCast;
	RerouteToCast.Id = FGuid::NewGuid();
	RerouteToCast.SourceNodeId = RerouteNode.Id;
	RerouteToCast.SourcePinId = RerouteOutput.Id;
	RerouteToCast.TargetNodeId = CastNode.Id;
	RerouteToCast.TargetPinId = CastObjectInput.Id;
	Document.Edges.Add(RerouteToCast);

	FVergilGraphEdge CastToSet;
	CastToSet.Id = FGuid::NewGuid();
	CastToSet.SourceNodeId = CastNode.Id;
	CastToSet.SourcePinId = CastResultOutput.Id;
	CastToSet.TargetNodeId = SetterNode.Id;
	CastToSet.TargetPinId = SetterValue.Id;
	Document.Edges.Add(CastToSet);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Self/reroute/pure cast document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Self/reroute/pure cast document should be applied."), Result.bApplied);
	TestTrue(TEXT("Self/reroute/pure cast document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after self/reroute/pure cast execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_Self* const SelfGraphNode = FindGraphNodeByGuid<UK2Node_Self>(EventGraph, SelfNode.Id);
	UK2Node_Knot* const RerouteGraphNode = FindGraphNodeByGuid<UK2Node_Knot>(EventGraph, RerouteNode.Id);
	UK2Node_DynamicCast* const CastGraphNode = FindGraphNodeByGuid<UK2Node_DynamicCast>(EventGraph, CastNode.Id);
	UK2Node_VariableSet* const VariableSetNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetterNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Reroute node should exist."), RerouteGraphNode);
	TestNotNull(TEXT("Dynamic cast node should exist."), CastGraphNode);
	TestNotNull(TEXT("Variable set node should exist."), VariableSetNode);
	if (EventNode == nullptr || SelfGraphNode == nullptr || RerouteGraphNode == nullptr || CastGraphNode == nullptr || VariableSetNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetExecPin = VariableSetNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SelfPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const RerouteInPin = RerouteGraphNode->GetInputPin();
	UEdGraphPin* const RerouteOutPin = RerouteGraphNode->GetOutputPin();
	UEdGraphPin* const CastObjectPin = CastGraphNode->GetCastSourcePin();
	UEdGraphPin* const CastResultPin = CastGraphNode->GetCastResultPin();
	UEdGraphPin* const SetValuePin = VariableSetNode->FindPin(TEXT("ActorRef"));

	TestTrue(TEXT("Self node should remain pure."), SelfGraphNode->IsNodePure());
	TestTrue(TEXT("Dynamic cast should be pure when no exec pins are planned."), CastGraphNode->IsNodePure());
	TestTrue(TEXT("Dynamic cast target type should match requested class."), CastGraphNode->TargetType.Get() == AActor::StaticClass());
	TestTrue(TEXT("Event should link to variable set execute."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(SetExecPin));
	TestTrue(TEXT("Self should feed reroute input."), SelfPin != nullptr && SelfPin->LinkedTo.Contains(RerouteInPin));
	TestTrue(TEXT("Reroute output should feed cast object input."), RerouteOutPin != nullptr && RerouteOutPin->LinkedTo.Contains(CastObjectPin));
	TestTrue(TEXT("Cast result should feed variable set input."), CastResultPin != nullptr && CastResultPin->LinkedTo.Contains(SetValuePin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilImpureCastExecutionTest,
	"Vergil.Scaffold.ImpureCastExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilImpureCastExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FName CastResultPinName = MakeCastResultPinName(AActor::StaticClass());

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(300.0f, -180.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode CastNode;
	CastNode.Id = FGuid::NewGuid();
	CastNode.Kind = EVergilNodeKind::Custom;
	CastNode.Descriptor = TEXT("K2.Cast");
	CastNode.Position = FVector2D(560.0f, 0.0f);
	CastNode.Metadata.Add(TEXT("TargetClassPath"), AActor::StaticClass()->GetPathName());

	FVergilGraphPin CastExecIn;
	CastExecIn.Id = FGuid::NewGuid();
	CastExecIn.Name = UEdGraphSchema_K2::PN_Execute;
	CastExecIn.Direction = EVergilPinDirection::Input;
	CastExecIn.bIsExec = true;
	CastNode.Pins.Add(CastExecIn);

	FVergilGraphPin CastSuccess;
	CastSuccess.Id = FGuid::NewGuid();
	CastSuccess.Name = UEdGraphSchema_K2::PN_CastSucceeded;
	CastSuccess.Direction = EVergilPinDirection::Output;
	CastSuccess.bIsExec = true;
	CastNode.Pins.Add(CastSuccess);

	FVergilGraphPin CastObjectInput;
	CastObjectInput.Id = FGuid::NewGuid();
	CastObjectInput.Name = UEdGraphSchema_K2::PN_ObjectToCast;
	CastObjectInput.Direction = EVergilPinDirection::Input;
	CastNode.Pins.Add(CastObjectInput);

	FVergilGraphPin CastResultOutput;
	CastResultOutput.Id = FGuid::NewGuid();
	CastResultOutput.Name = CastResultPinName;
	CastResultOutput.Direction = EVergilPinDirection::Output;
	CastNode.Pins.Add(CastResultOutput);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(860.0f, 0.0f);

	FVergilGraphPin CallExecIn;
	CallExecIn.Id = FGuid::NewGuid();
	CallExecIn.Name = TEXT("Execute");
	CallExecIn.Direction = EVergilPinDirection::Input;
	CallExecIn.bIsExec = true;
	CallNode.Pins.Add(CallExecIn);

	FVergilGraphPin CallSelfPin;
	CallSelfPin.Id = FGuid::NewGuid();
	CallSelfPin.Name = UEdGraphSchema_K2::PN_Self;
	CallSelfPin.Direction = EVergilPinDirection::Input;
	CallNode.Pins.Add(CallSelfPin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilImpureCast");
	Document.Nodes = { BeginPlayNode, SelfNode, CastNode, CallNode };

	FVergilGraphEdge EventToCast;
	EventToCast.Id = FGuid::NewGuid();
	EventToCast.SourceNodeId = BeginPlayNode.Id;
	EventToCast.SourcePinId = BeginPlayThen.Id;
	EventToCast.TargetNodeId = CastNode.Id;
	EventToCast.TargetPinId = CastExecIn.Id;
	Document.Edges.Add(EventToCast);

	FVergilGraphEdge SelfToCast;
	SelfToCast.Id = FGuid::NewGuid();
	SelfToCast.SourceNodeId = SelfNode.Id;
	SelfToCast.SourcePinId = SelfOutput.Id;
	SelfToCast.TargetNodeId = CastNode.Id;
	SelfToCast.TargetPinId = CastObjectInput.Id;
	Document.Edges.Add(SelfToCast);

	FVergilGraphEdge CastToCallExec;
	CastToCallExec.Id = FGuid::NewGuid();
	CastToCallExec.SourceNodeId = CastNode.Id;
	CastToCallExec.SourcePinId = CastSuccess.Id;
	CastToCallExec.TargetNodeId = CallNode.Id;
	CastToCallExec.TargetPinId = CallExecIn.Id;
	Document.Edges.Add(CastToCallExec);

	FVergilGraphEdge CastToCallSelf;
	CastToCallSelf.Id = FGuid::NewGuid();
	CastToCallSelf.SourceNodeId = CastNode.Id;
	CastToCallSelf.SourcePinId = CastResultOutput.Id;
	CastToCallSelf.TargetNodeId = CallNode.Id;
	CastToCallSelf.TargetPinId = CallSelfPin.Id;
	Document.Edges.Add(CastToCallSelf);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Impure cast document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Impure cast document should be applied."), Result.bApplied);
	TestTrue(TEXT("Impure cast document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after impure cast execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_Self* const SelfGraphNode = FindGraphNodeByGuid<UK2Node_Self>(EventGraph, SelfNode.Id);
	UK2Node_DynamicCast* const CastGraphNode = FindGraphNodeByGuid<UK2Node_DynamicCast>(EventGraph, CastNode.Id);
	UK2Node_CallFunction* const CallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, CallNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Dynamic cast node should exist."), CastGraphNode);
	TestNotNull(TEXT("Call node should exist."), CallGraphNode);
	if (EventNode == nullptr || SelfGraphNode == nullptr || CastGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SelfPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const CastExecPin = CastGraphNode->GetExecPin();
	UEdGraphPin* const CastObjectPin = CastGraphNode->GetCastSourcePin();
	UEdGraphPin* const CastSuccessPin = CastGraphNode->GetValidCastPin();
	UEdGraphPin* const CastResultPin = CastGraphNode->GetCastResultPin();
	UEdGraphPin* const CallExecPin = CallGraphNode->GetExecPin();
	UEdGraphPin* const CallSelfGraphPin = CallGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);

	TestFalse(TEXT("Dynamic cast should remain impure when exec pins are planned."), CastGraphNode->IsNodePure());
	TestTrue(TEXT("Dynamic cast target type should match requested class."), CastGraphNode->TargetType.Get() == AActor::StaticClass());
	TestTrue(TEXT("BeginPlay should link to cast execute."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(CastExecPin));
	TestTrue(TEXT("Self should feed cast object input."), SelfPin != nullptr && SelfPin->LinkedTo.Contains(CastObjectPin));
	TestTrue(TEXT("Cast success should link to call execute."), CastSuccessPin != nullptr && CastSuccessPin->LinkedTo.Contains(CallExecPin));
	TestTrue(TEXT("Cast result should link to call self pin."), CastResultPin != nullptr && CastResultPin->LinkedTo.Contains(CallSelfGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFormatTextExecutionTest,
	"Vergil.Scaffold.FormatTextExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilFormatTextExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("NameValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("NameValue"), StringType, TEXT("Vergil")));

	FEdGraphPinType TextType;
	TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
	TestTrue(TEXT("MessageText member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("MessageText"), TextType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.NameValue");
	GetterNode.Position = FVector2D(260.0f, -140.0f);

	FVergilGraphPin GetterValue;
	GetterValue.Id = FGuid::NewGuid();
	GetterValue.Name = TEXT("NameValue");
	GetterValue.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValue);

	FVergilGraphNode FormatNode;
	FormatNode.Id = FGuid::NewGuid();
	FormatNode.Kind = EVergilNodeKind::Custom;
	FormatNode.Descriptor = TEXT("K2.FormatText");
	FormatNode.Position = FVector2D(560.0f, -40.0f);
	FormatNode.Metadata.Add(TEXT("FormatPattern"), TEXT("Hello {Name}"));

	FVergilGraphPin FormatPin;
	FormatPin.Id = FGuid::NewGuid();
	FormatPin.Name = TEXT("Format");
	FormatPin.Direction = EVergilPinDirection::Input;
	FormatNode.Pins.Add(FormatPin);

	FVergilGraphPin FormatArgPin;
	FormatArgPin.Id = FGuid::NewGuid();
	FormatArgPin.Name = TEXT("Name");
	FormatArgPin.Direction = EVergilPinDirection::Input;
	FormatNode.Pins.Add(FormatArgPin);

	FVergilGraphPin FormatResultPin;
	FormatResultPin.Id = FGuid::NewGuid();
	FormatResultPin.Name = TEXT("Result");
	FormatResultPin.Direction = EVergilPinDirection::Output;
	FormatNode.Pins.Add(FormatResultPin);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.MessageText");
	SetterNode.Position = FVector2D(920.0f, 0.0f);

	FVergilGraphPin SetterExecPin;
	SetterExecPin.Id = FGuid::NewGuid();
	SetterExecPin.Name = TEXT("Execute");
	SetterExecPin.Direction = EVergilPinDirection::Input;
	SetterExecPin.bIsExec = true;
	SetterNode.Pins.Add(SetterExecPin);

	FVergilGraphPin SetterValuePin;
	SetterValuePin.Id = FGuid::NewGuid();
	SetterValuePin.Name = TEXT("MessageText");
	SetterValuePin.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilFormatTextExecution");
	Document.Nodes = { BeginPlayNode, GetterNode, FormatNode, SetterNode };

	FVergilGraphEdge EventToSetter;
	EventToSetter.Id = FGuid::NewGuid();
	EventToSetter.SourceNodeId = BeginPlayNode.Id;
	EventToSetter.SourcePinId = BeginPlayThen.Id;
	EventToSetter.TargetNodeId = SetterNode.Id;
	EventToSetter.TargetPinId = SetterExecPin.Id;
	Document.Edges.Add(EventToSetter);

	FVergilGraphEdge GetterToFormat;
	GetterToFormat.Id = FGuid::NewGuid();
	GetterToFormat.SourceNodeId = GetterNode.Id;
	GetterToFormat.SourcePinId = GetterValue.Id;
	GetterToFormat.TargetNodeId = FormatNode.Id;
	GetterToFormat.TargetPinId = FormatArgPin.Id;
	Document.Edges.Add(GetterToFormat);

	FVergilGraphEdge FormatToSetter;
	FormatToSetter.Id = FGuid::NewGuid();
	FormatToSetter.SourceNodeId = FormatNode.Id;
	FormatToSetter.SourcePinId = FormatResultPin.Id;
	FormatToSetter.TargetNodeId = SetterNode.Id;
	FormatToSetter.TargetPinId = SetterValuePin.Id;
	Document.Edges.Add(FormatToSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Format text document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Format text document should be applied."), Result.bApplied);
	TestTrue(TEXT("Format text document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after format text execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_VariableGet* GetterGraphNode = nullptr;
	UK2Node_FormatText* FormatGraphNode = nullptr;
	UK2Node_VariableSet* SetterGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (GetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("NameValue")) != nullptr)
				{
					GetterGraphNode = Candidate;
					continue;
				}
			}
		}
		if (FormatGraphNode == nullptr)
		{
			FormatGraphNode = Cast<UK2Node_FormatText>(Node);
		}
		if (SetterGraphNode == nullptr)
		{
			SetterGraphNode = Cast<UK2Node_VariableSet>(Node);
		}
	}

	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Format text node should exist."), FormatGraphNode);
	TestNotNull(TEXT("Setter node should exist."), SetterGraphNode);
	if (GetterGraphNode == nullptr || FormatGraphNode == nullptr || SetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const GetterPin = GetterGraphNode->FindPin(TEXT("NameValue"));
	UEdGraphPin* const GraphFormatPin = FormatGraphNode->GetFormatPin();
	UEdGraphPin* const GraphArgumentPin = FormatGraphNode->FindArgumentPin(TEXT("Name"));
	UEdGraphPin* const GraphResultPin = FormatGraphNode->FindPin(TEXT("Result"));
	UEdGraphPin* const SetterValueGraphPin = SetterGraphNode->FindPin(TEXT("MessageText"));

	TestTrue(TEXT("Format text should remain pure."), FormatGraphNode->IsNodePure());
	TestTrue(TEXT("Format pin should retain the configured pattern."), GraphFormatPin != nullptr && GraphFormatPin->DefaultTextValue.ToString().Equals(TEXT("Hello {Name}")));
	TestTrue(TEXT("Format text should expose the planned argument pin."), GraphArgumentPin != nullptr);
	TestTrue(TEXT("Getter should feed format argument."), GetterPin != nullptr && GetterPin->LinkedTo.Contains(GraphArgumentPin));
	TestTrue(TEXT("Format result should feed setter value."), GraphResultPin != nullptr && GraphResultPin->LinkedTo.Contains(SetterValueGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilStructFlowExecutionTest,
	"Vergil.Scaffold.StructFlowExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilStructFlowExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	UScriptStruct* const VectorStruct = TBaseStructure<FVector>::Get();
	TestNotNull(TEXT("FVector base structure should be available."), VectorStruct);
	if (VectorStruct == nullptr)
	{
		return false;
	}

	FEdGraphPinType DoubleType;
	DoubleType.PinCategory = UEdGraphSchema_K2::PC_Real;
	DoubleType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	TestTrue(TEXT("XComponent member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("XComponent"), DoubleType, TEXT("1.0")));
	TestTrue(TEXT("YComponent member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("YComponent"), DoubleType, TEXT("2.0")));
	TestTrue(TEXT("ZComponent member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ZComponent"), DoubleType, TEXT("3.0")));
	TestTrue(TEXT("CapturedX member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("CapturedX"), DoubleType, TEXT("0.0")));

	FEdGraphPinType VectorType;
	VectorType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	VectorType.PinSubCategoryObject = VectorStruct;
	TestTrue(TEXT("StoredVector member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("StoredVector"), VectorType, FString()));

	const FName VectorPinName = VectorStruct->GetFName();

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode XGetterNode;
	XGetterNode.Id = FGuid::NewGuid();
	XGetterNode.Kind = EVergilNodeKind::VariableGet;
	XGetterNode.Descriptor = TEXT("K2.VarGet.XComponent");
	XGetterNode.Position = FVector2D(260.0f, -260.0f);

	FVergilGraphPin XGetterValue;
	XGetterValue.Id = FGuid::NewGuid();
	XGetterValue.Name = TEXT("XComponent");
	XGetterValue.Direction = EVergilPinDirection::Output;
	XGetterNode.Pins.Add(XGetterValue);

	FVergilGraphNode YGetterNode;
	YGetterNode.Id = FGuid::NewGuid();
	YGetterNode.Kind = EVergilNodeKind::VariableGet;
	YGetterNode.Descriptor = TEXT("K2.VarGet.YComponent");
	YGetterNode.Position = FVector2D(260.0f, -120.0f);

	FVergilGraphPin YGetterValue;
	YGetterValue.Id = FGuid::NewGuid();
	YGetterValue.Name = TEXT("YComponent");
	YGetterValue.Direction = EVergilPinDirection::Output;
	YGetterNode.Pins.Add(YGetterValue);

	FVergilGraphNode ZGetterNode;
	ZGetterNode.Id = FGuid::NewGuid();
	ZGetterNode.Kind = EVergilNodeKind::VariableGet;
	ZGetterNode.Descriptor = TEXT("K2.VarGet.ZComponent");
	ZGetterNode.Position = FVector2D(260.0f, 20.0f);

	FVergilGraphPin ZGetterValue;
	ZGetterValue.Id = FGuid::NewGuid();
	ZGetterValue.Name = TEXT("ZComponent");
	ZGetterValue.Direction = EVergilPinDirection::Output;
	ZGetterNode.Pins.Add(ZGetterValue);

	FVergilGraphNode MakeStructNode;
	MakeStructNode.Id = FGuid::NewGuid();
	MakeStructNode.Kind = EVergilNodeKind::Custom;
	MakeStructNode.Descriptor = TEXT("K2.MakeStruct");
	MakeStructNode.Position = FVector2D(560.0f, -120.0f);
	MakeStructNode.Metadata.Add(TEXT("StructPath"), VectorStruct->GetPathName());

	FVergilGraphPin MakeStructXPin;
	MakeStructXPin.Id = FGuid::NewGuid();
	MakeStructXPin.Name = TEXT("X");
	MakeStructXPin.Direction = EVergilPinDirection::Input;
	MakeStructNode.Pins.Add(MakeStructXPin);

	FVergilGraphPin MakeStructYPin;
	MakeStructYPin.Id = FGuid::NewGuid();
	MakeStructYPin.Name = TEXT("Y");
	MakeStructYPin.Direction = EVergilPinDirection::Input;
	MakeStructNode.Pins.Add(MakeStructYPin);

	FVergilGraphPin MakeStructZPin;
	MakeStructZPin.Id = FGuid::NewGuid();
	MakeStructZPin.Name = TEXT("Z");
	MakeStructZPin.Direction = EVergilPinDirection::Input;
	MakeStructNode.Pins.Add(MakeStructZPin);

	FVergilGraphPin MakeStructResultPin;
	MakeStructResultPin.Id = FGuid::NewGuid();
	MakeStructResultPin.Name = VectorPinName;
	MakeStructResultPin.Direction = EVergilPinDirection::Output;
	MakeStructNode.Pins.Add(MakeStructResultPin);

	FVergilGraphNode SetStoredVectorNode;
	SetStoredVectorNode.Id = FGuid::NewGuid();
	SetStoredVectorNode.Kind = EVergilNodeKind::VariableSet;
	SetStoredVectorNode.Descriptor = TEXT("K2.VarSet.StoredVector");
	SetStoredVectorNode.Position = FVector2D(900.0f, -80.0f);

	FVergilGraphPin SetStoredExecPin;
	SetStoredExecPin.Id = FGuid::NewGuid();
	SetStoredExecPin.Name = TEXT("Execute");
	SetStoredExecPin.Direction = EVergilPinDirection::Input;
	SetStoredExecPin.bIsExec = true;
	SetStoredVectorNode.Pins.Add(SetStoredExecPin);

	FVergilGraphPin SetStoredThenPin;
	SetStoredThenPin.Id = FGuid::NewGuid();
	SetStoredThenPin.Name = TEXT("Then");
	SetStoredThenPin.Direction = EVergilPinDirection::Output;
	SetStoredThenPin.bIsExec = true;
	SetStoredVectorNode.Pins.Add(SetStoredThenPin);

	FVergilGraphPin SetStoredValuePin;
	SetStoredValuePin.Id = FGuid::NewGuid();
	SetStoredValuePin.Name = TEXT("StoredVector");
	SetStoredValuePin.Direction = EVergilPinDirection::Input;
	SetStoredVectorNode.Pins.Add(SetStoredValuePin);

	FVergilGraphNode StoredVectorGetterNode;
	StoredVectorGetterNode.Id = FGuid::NewGuid();
	StoredVectorGetterNode.Kind = EVergilNodeKind::VariableGet;
	StoredVectorGetterNode.Descriptor = TEXT("K2.VarGet.StoredVector");
	StoredVectorGetterNode.Position = FVector2D(900.0f, 140.0f);

	FVergilGraphPin StoredVectorGetterValue;
	StoredVectorGetterValue.Id = FGuid::NewGuid();
	StoredVectorGetterValue.Name = TEXT("StoredVector");
	StoredVectorGetterValue.Direction = EVergilPinDirection::Output;
	StoredVectorGetterNode.Pins.Add(StoredVectorGetterValue);

	FVergilGraphNode BreakStructNode;
	BreakStructNode.Id = FGuid::NewGuid();
	BreakStructNode.Kind = EVergilNodeKind::Custom;
	BreakStructNode.Descriptor = TEXT("K2.BreakStruct");
	BreakStructNode.Position = FVector2D(1220.0f, 140.0f);
	BreakStructNode.Metadata.Add(TEXT("StructPath"), VectorStruct->GetPathName());

	FVergilGraphPin BreakStructInputPin;
	BreakStructInputPin.Id = FGuid::NewGuid();
	BreakStructInputPin.Name = VectorPinName;
	BreakStructInputPin.Direction = EVergilPinDirection::Input;
	BreakStructNode.Pins.Add(BreakStructInputPin);

	FVergilGraphPin BreakStructXPin;
	BreakStructXPin.Id = FGuid::NewGuid();
	BreakStructXPin.Name = TEXT("X");
	BreakStructXPin.Direction = EVergilPinDirection::Output;
	BreakStructNode.Pins.Add(BreakStructXPin);

	FVergilGraphNode SetCapturedXNode;
	SetCapturedXNode.Id = FGuid::NewGuid();
	SetCapturedXNode.Kind = EVergilNodeKind::VariableSet;
	SetCapturedXNode.Descriptor = TEXT("K2.VarSet.CapturedX");
	SetCapturedXNode.Position = FVector2D(1560.0f, 80.0f);

	FVergilGraphPin SetCapturedExecPin;
	SetCapturedExecPin.Id = FGuid::NewGuid();
	SetCapturedExecPin.Name = TEXT("Execute");
	SetCapturedExecPin.Direction = EVergilPinDirection::Input;
	SetCapturedExecPin.bIsExec = true;
	SetCapturedXNode.Pins.Add(SetCapturedExecPin);

	FVergilGraphPin SetCapturedValuePin;
	SetCapturedValuePin.Id = FGuid::NewGuid();
	SetCapturedValuePin.Name = TEXT("CapturedX");
	SetCapturedValuePin.Direction = EVergilPinDirection::Input;
	SetCapturedXNode.Pins.Add(SetCapturedValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilStructFlowExecution");
	Document.Nodes = { BeginPlayNode, XGetterNode, YGetterNode, ZGetterNode, MakeStructNode, SetStoredVectorNode, StoredVectorGetterNode, BreakStructNode, SetCapturedXNode };

	FVergilGraphEdge EventToStoredSet;
	EventToStoredSet.Id = FGuid::NewGuid();
	EventToStoredSet.SourceNodeId = BeginPlayNode.Id;
	EventToStoredSet.SourcePinId = BeginPlayThen.Id;
	EventToStoredSet.TargetNodeId = SetStoredVectorNode.Id;
	EventToStoredSet.TargetPinId = SetStoredExecPin.Id;
	Document.Edges.Add(EventToStoredSet);

	FVergilGraphEdge StoredSetToCapturedSet;
	StoredSetToCapturedSet.Id = FGuid::NewGuid();
	StoredSetToCapturedSet.SourceNodeId = SetStoredVectorNode.Id;
	StoredSetToCapturedSet.SourcePinId = SetStoredThenPin.Id;
	StoredSetToCapturedSet.TargetNodeId = SetCapturedXNode.Id;
	StoredSetToCapturedSet.TargetPinId = SetCapturedExecPin.Id;
	Document.Edges.Add(StoredSetToCapturedSet);

	FVergilGraphEdge XToMake;
	XToMake.Id = FGuid::NewGuid();
	XToMake.SourceNodeId = XGetterNode.Id;
	XToMake.SourcePinId = XGetterValue.Id;
	XToMake.TargetNodeId = MakeStructNode.Id;
	XToMake.TargetPinId = MakeStructXPin.Id;
	Document.Edges.Add(XToMake);

	FVergilGraphEdge YToMake;
	YToMake.Id = FGuid::NewGuid();
	YToMake.SourceNodeId = YGetterNode.Id;
	YToMake.SourcePinId = YGetterValue.Id;
	YToMake.TargetNodeId = MakeStructNode.Id;
	YToMake.TargetPinId = MakeStructYPin.Id;
	Document.Edges.Add(YToMake);

	FVergilGraphEdge ZToMake;
	ZToMake.Id = FGuid::NewGuid();
	ZToMake.SourceNodeId = ZGetterNode.Id;
	ZToMake.SourcePinId = ZGetterValue.Id;
	ZToMake.TargetNodeId = MakeStructNode.Id;
	ZToMake.TargetPinId = MakeStructZPin.Id;
	Document.Edges.Add(ZToMake);

	FVergilGraphEdge MakeToStoredSet;
	MakeToStoredSet.Id = FGuid::NewGuid();
	MakeToStoredSet.SourceNodeId = MakeStructNode.Id;
	MakeToStoredSet.SourcePinId = MakeStructResultPin.Id;
	MakeToStoredSet.TargetNodeId = SetStoredVectorNode.Id;
	MakeToStoredSet.TargetPinId = SetStoredValuePin.Id;
	Document.Edges.Add(MakeToStoredSet);

	FVergilGraphEdge StoredGetterToBreak;
	StoredGetterToBreak.Id = FGuid::NewGuid();
	StoredGetterToBreak.SourceNodeId = StoredVectorGetterNode.Id;
	StoredGetterToBreak.SourcePinId = StoredVectorGetterValue.Id;
	StoredGetterToBreak.TargetNodeId = BreakStructNode.Id;
	StoredGetterToBreak.TargetPinId = BreakStructInputPin.Id;
	Document.Edges.Add(StoredGetterToBreak);

	FVergilGraphEdge BreakToCapturedSet;
	BreakToCapturedSet.Id = FGuid::NewGuid();
	BreakToCapturedSet.SourceNodeId = BreakStructNode.Id;
	BreakToCapturedSet.SourcePinId = BreakStructXPin.Id;
	BreakToCapturedSet.TargetNodeId = SetCapturedXNode.Id;
	BreakToCapturedSet.TargetPinId = SetCapturedValuePin.Id;
	Document.Edges.Add(BreakToCapturedSet);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Struct flow document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Struct flow document should be applied."), Result.bApplied);
	TestTrue(TEXT("Struct flow document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after struct flow execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UEdGraphNode* const MakeStructGraphNode = FindGraphNodeByGuid<UEdGraphNode>(EventGraph, MakeStructNode.Id);
	UEdGraphNode* const BreakStructGraphNode = FindGraphNodeByGuid<UEdGraphNode>(EventGraph, BreakStructNode.Id);
	UK2Node_VariableSet* const StoredVectorSetterNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetStoredVectorNode.Id);
	UK2Node_VariableGet* const StoredVectorGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, StoredVectorGetterNode.Id);
	UK2Node_VariableSet* const CapturedXSetterNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetCapturedXNode.Id);

	TestNotNull(TEXT("Make struct node should exist."), MakeStructGraphNode);
	TestNotNull(TEXT("Break struct node should exist."), BreakStructGraphNode);
	TestNotNull(TEXT("StoredVector setter should exist."), StoredVectorSetterNode);
	TestNotNull(TEXT("StoredVector getter should exist."), StoredVectorGetterGraphNode);
	TestNotNull(TEXT("CapturedX setter should exist."), CapturedXSetterNode);
	if (MakeStructGraphNode == nullptr || BreakStructGraphNode == nullptr || StoredVectorSetterNode == nullptr || StoredVectorGetterGraphNode == nullptr || CapturedXSetterNode == nullptr)
	{
		return false;
	}

	UK2Node_MakeStruct* const GenericMakeStructNode = Cast<UK2Node_MakeStruct>(MakeStructGraphNode);
	UK2Node_CallFunction* const NativeMakeStructNode = Cast<UK2Node_CallFunction>(MakeStructGraphNode);
	UK2Node_BreakStruct* const GenericBreakStructNode = Cast<UK2Node_BreakStruct>(BreakStructGraphNode);
	UK2Node_CallFunction* const NativeBreakStructNode = Cast<UK2Node_CallFunction>(BreakStructGraphNode);

	UEdGraphPin* const MakeXPin = MakeStructGraphNode->FindPin(TEXT("X"));
	UEdGraphPin* const MakeYPin = MakeStructGraphNode->FindPin(TEXT("Y"));
	UEdGraphPin* const MakeZPin = MakeStructGraphNode->FindPin(TEXT("Z"));
	UEdGraphPin* const MakeResultGraphPin = MakeStructGraphNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue) != nullptr
		? MakeStructGraphNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue)
		: MakeStructGraphNode->FindPin(VectorPinName);
	UEdGraphPin* const StoredVectorSetterValuePin = StoredVectorSetterNode->FindPin(TEXT("StoredVector"));
	UEdGraphPin* const StoredVectorGetterValuePin = StoredVectorGetterGraphNode->FindPin(TEXT("StoredVector"));
	UEdGraphPin* const BreakInputGraphPin = BreakStructGraphNode->FindPin(TEXT("InVec")) != nullptr
		? BreakStructGraphNode->FindPin(TEXT("InVec"))
		: BreakStructGraphNode->FindPin(VectorPinName);
	UEdGraphPin* const BreakXGraphPin = BreakStructGraphNode->FindPin(TEXT("X"));
	UEdGraphPin* const CapturedXSetterValuePin = CapturedXSetterNode->FindPin(TEXT("CapturedX"));

	TestTrue(TEXT("Make struct should resolve to a supported node."), GenericMakeStructNode != nullptr || NativeMakeStructNode != nullptr);
	TestTrue(TEXT("Break struct should resolve to a supported node."), GenericBreakStructNode != nullptr || NativeBreakStructNode != nullptr);
	TestTrue(
		TEXT("Make struct should remain pure."),
		(GenericMakeStructNode != nullptr && GenericMakeStructNode->IsNodePure())
			|| (NativeMakeStructNode != nullptr && NativeMakeStructNode->GetExecPin() == nullptr));
	TestTrue(
		TEXT("Break struct should remain pure."),
		(GenericBreakStructNode != nullptr && GenericBreakStructNode->IsNodePure())
			|| (NativeBreakStructNode != nullptr && NativeBreakStructNode->GetExecPin() == nullptr));
	if (NativeMakeStructNode != nullptr)
	{
		TestEqual(TEXT("Native make struct should resolve MakeVector."), NativeMakeStructNode->GetFunctionName(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeVector));
	}
	if (NativeBreakStructNode != nullptr)
	{
		TestEqual(TEXT("Native break struct should resolve BreakVector."), NativeBreakStructNode->GetFunctionName(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, BreakVector));
	}
	TestTrue(TEXT("Make struct should expose X pin."), MakeXPin != nullptr);
	TestTrue(TEXT("Make struct should expose Y pin."), MakeYPin != nullptr);
	TestTrue(TEXT("Make struct should expose Z pin."), MakeZPin != nullptr);
	TestTrue(TEXT("Make struct result should feed StoredVector setter."), MakeResultGraphPin != nullptr && MakeResultGraphPin->LinkedTo.Contains(StoredVectorSetterValuePin));
	TestTrue(TEXT("StoredVector getter should feed break struct input."), StoredVectorGetterValuePin != nullptr && StoredVectorGetterValuePin->LinkedTo.Contains(BreakInputGraphPin));
	TestTrue(TEXT("Break struct X output should feed CapturedX setter."), BreakXGraphPin != nullptr && BreakXGraphPin->LinkedTo.Contains(CapturedXSetterValuePin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMakeArrayExecutionTest,
	"Vergil.Scaffold.MakeArrayExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMakeArrayExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType ActorArrayType;
	ActorArrayType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorArrayType.PinSubCategoryObject = AActor::StaticClass();
	ActorArrayType.ContainerType = EPinContainerType::Array;
	TestTrue(TEXT("ActorArray member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ActorArray"), ActorArrayType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(260.0f, -80.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode MakeArrayNode;
	MakeArrayNode.Id = FGuid::NewGuid();
	MakeArrayNode.Kind = EVergilNodeKind::Custom;
	MakeArrayNode.Descriptor = TEXT("K2.MakeArray");
	MakeArrayNode.Position = FVector2D(560.0f, -40.0f);
	MakeArrayNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("object"));
	MakeArrayNode.Metadata.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());
	MakeArrayNode.Metadata.Add(TEXT("NumInputs"), TEXT("2"));

	FVergilGraphPin MakeArrayInput0Pin;
	MakeArrayInput0Pin.Id = FGuid::NewGuid();
	MakeArrayInput0Pin.Name = TEXT("[0]");
	MakeArrayInput0Pin.Direction = EVergilPinDirection::Input;
	MakeArrayNode.Pins.Add(MakeArrayInput0Pin);

	FVergilGraphPin MakeArrayInput1Pin;
	MakeArrayInput1Pin.Id = FGuid::NewGuid();
	MakeArrayInput1Pin.Name = TEXT("[1]");
	MakeArrayInput1Pin.Direction = EVergilPinDirection::Input;
	MakeArrayNode.Pins.Add(MakeArrayInput1Pin);

	FVergilGraphPin MakeArrayResultPin;
	MakeArrayResultPin.Id = FGuid::NewGuid();
	MakeArrayResultPin.Name = TEXT("Array");
	MakeArrayResultPin.Direction = EVergilPinDirection::Output;
	MakeArrayNode.Pins.Add(MakeArrayResultPin);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.ActorArray");
	SetterNode.Position = FVector2D(920.0f, 0.0f);

	FVergilGraphPin SetterExecPin;
	SetterExecPin.Id = FGuid::NewGuid();
	SetterExecPin.Name = TEXT("Execute");
	SetterExecPin.Direction = EVergilPinDirection::Input;
	SetterExecPin.bIsExec = true;
	SetterNode.Pins.Add(SetterExecPin);

	FVergilGraphPin SetterValuePin;
	SetterValuePin.Id = FGuid::NewGuid();
	SetterValuePin.Name = TEXT("ActorArray");
	SetterValuePin.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilMakeArrayExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, MakeArrayNode, SetterNode };

	FVergilGraphEdge EventToSetter;
	EventToSetter.Id = FGuid::NewGuid();
	EventToSetter.SourceNodeId = BeginPlayNode.Id;
	EventToSetter.SourcePinId = BeginPlayThen.Id;
	EventToSetter.TargetNodeId = SetterNode.Id;
	EventToSetter.TargetPinId = SetterExecPin.Id;
	Document.Edges.Add(EventToSetter);

	FVergilGraphEdge SelfToInput0;
	SelfToInput0.Id = FGuid::NewGuid();
	SelfToInput0.SourceNodeId = SelfNode.Id;
	SelfToInput0.SourcePinId = SelfOutput.Id;
	SelfToInput0.TargetNodeId = MakeArrayNode.Id;
	SelfToInput0.TargetPinId = MakeArrayInput0Pin.Id;
	Document.Edges.Add(SelfToInput0);

	FVergilGraphEdge SelfToInput1;
	SelfToInput1.Id = FGuid::NewGuid();
	SelfToInput1.SourceNodeId = SelfNode.Id;
	SelfToInput1.SourcePinId = SelfOutput.Id;
	SelfToInput1.TargetNodeId = MakeArrayNode.Id;
	SelfToInput1.TargetPinId = MakeArrayInput1Pin.Id;
	Document.Edges.Add(SelfToInput1);

	FVergilGraphEdge ArrayToSetter;
	ArrayToSetter.Id = FGuid::NewGuid();
	ArrayToSetter.SourceNodeId = MakeArrayNode.Id;
	ArrayToSetter.SourcePinId = MakeArrayResultPin.Id;
	ArrayToSetter.TargetNodeId = SetterNode.Id;
	ArrayToSetter.TargetPinId = SetterValuePin.Id;
	Document.Edges.Add(ArrayToSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Make array document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Make array document should be applied."), Result.bApplied);
	TestTrue(TEXT("Make array document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after make array execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_MakeArray* MakeArrayGraphNode = nullptr;
	UK2Node_VariableSet* SetterGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (SelfGraphNode == nullptr)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}
		if (MakeArrayGraphNode == nullptr)
		{
			MakeArrayGraphNode = Cast<UK2Node_MakeArray>(Node);
		}
		if (SetterGraphNode == nullptr)
		{
			SetterGraphNode = Cast<UK2Node_VariableSet>(Node);
		}
	}

	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Make array node should exist."), MakeArrayGraphNode);
	TestNotNull(TEXT("Setter node should exist."), SetterGraphNode);
	if (SelfGraphNode == nullptr || MakeArrayGraphNode == nullptr || SetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const ArrayInput0GraphPin = MakeArrayGraphNode->FindPin(TEXT("[0]"));
	UEdGraphPin* const ArrayInput1GraphPin = MakeArrayGraphNode->FindPin(TEXT("[1]"));
	UEdGraphPin* const ArrayOutputGraphPin = MakeArrayGraphNode->GetOutputPin();
	UEdGraphPin* const SetterValueGraphPin = SetterGraphNode->FindPin(TEXT("ActorArray"));

	TestTrue(TEXT("Make array should remain pure."), MakeArrayGraphNode->IsNodePure());
	TestTrue(TEXT("Make array output should be typed as an object array."), ArrayOutputGraphPin != nullptr && ArrayOutputGraphPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && ArrayOutputGraphPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass() && ArrayOutputGraphPin->PinType.ContainerType == EPinContainerType::Array);
	TestTrue(TEXT("Self should feed array input [0]."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(ArrayInput0GraphPin));
	TestTrue(TEXT("Self should feed array input [1]."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(ArrayInput1GraphPin));
	TestTrue(TEXT("Array output should feed setter value."), ArrayOutputGraphPin != nullptr && ArrayOutputGraphPin->LinkedTo.Contains(SetterValueGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMakeSetExecutionTest,
	"Vergil.Scaffold.MakeSetExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMakeSetExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType ActorSetType;
	ActorSetType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorSetType.PinSubCategoryObject = AActor::StaticClass();
	ActorSetType.ContainerType = EPinContainerType::Set;
	TestTrue(TEXT("ActorSet member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ActorSet"), ActorSetType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(260.0f, -80.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode MakeSetNode;
	MakeSetNode.Id = FGuid::NewGuid();
	MakeSetNode.Kind = EVergilNodeKind::Custom;
	MakeSetNode.Descriptor = TEXT("K2.MakeSet");
	MakeSetNode.Position = FVector2D(560.0f, -40.0f);
	MakeSetNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("object"));
	MakeSetNode.Metadata.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());
	MakeSetNode.Metadata.Add(TEXT("NumInputs"), TEXT("2"));

	FVergilGraphPin MakeSetInput0Pin;
	MakeSetInput0Pin.Id = FGuid::NewGuid();
	MakeSetInput0Pin.Name = TEXT("[0]");
	MakeSetInput0Pin.Direction = EVergilPinDirection::Input;
	MakeSetNode.Pins.Add(MakeSetInput0Pin);

	FVergilGraphPin MakeSetInput1Pin;
	MakeSetInput1Pin.Id = FGuid::NewGuid();
	MakeSetInput1Pin.Name = TEXT("[1]");
	MakeSetInput1Pin.Direction = EVergilPinDirection::Input;
	MakeSetNode.Pins.Add(MakeSetInput1Pin);

	FVergilGraphPin MakeSetResultPin;
	MakeSetResultPin.Id = FGuid::NewGuid();
	MakeSetResultPin.Name = TEXT("Set");
	MakeSetResultPin.Direction = EVergilPinDirection::Output;
	MakeSetNode.Pins.Add(MakeSetResultPin);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.ActorSet");
	SetterNode.Position = FVector2D(920.0f, 0.0f);

	FVergilGraphPin SetterExecPin;
	SetterExecPin.Id = FGuid::NewGuid();
	SetterExecPin.Name = TEXT("Execute");
	SetterExecPin.Direction = EVergilPinDirection::Input;
	SetterExecPin.bIsExec = true;
	SetterNode.Pins.Add(SetterExecPin);

	FVergilGraphPin SetterValuePin;
	SetterValuePin.Id = FGuid::NewGuid();
	SetterValuePin.Name = TEXT("ActorSet");
	SetterValuePin.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilMakeSetExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, MakeSetNode, SetterNode };

	FVergilGraphEdge EventToSetter;
	EventToSetter.Id = FGuid::NewGuid();
	EventToSetter.SourceNodeId = BeginPlayNode.Id;
	EventToSetter.SourcePinId = BeginPlayThen.Id;
	EventToSetter.TargetNodeId = SetterNode.Id;
	EventToSetter.TargetPinId = SetterExecPin.Id;
	Document.Edges.Add(EventToSetter);

	FVergilGraphEdge SelfToInput0;
	SelfToInput0.Id = FGuid::NewGuid();
	SelfToInput0.SourceNodeId = SelfNode.Id;
	SelfToInput0.SourcePinId = SelfOutput.Id;
	SelfToInput0.TargetNodeId = MakeSetNode.Id;
	SelfToInput0.TargetPinId = MakeSetInput0Pin.Id;
	Document.Edges.Add(SelfToInput0);

	FVergilGraphEdge SelfToInput1;
	SelfToInput1.Id = FGuid::NewGuid();
	SelfToInput1.SourceNodeId = SelfNode.Id;
	SelfToInput1.SourcePinId = SelfOutput.Id;
	SelfToInput1.TargetNodeId = MakeSetNode.Id;
	SelfToInput1.TargetPinId = MakeSetInput1Pin.Id;
	Document.Edges.Add(SelfToInput1);

	FVergilGraphEdge SetToSetter;
	SetToSetter.Id = FGuid::NewGuid();
	SetToSetter.SourceNodeId = MakeSetNode.Id;
	SetToSetter.SourcePinId = MakeSetResultPin.Id;
	SetToSetter.TargetNodeId = SetterNode.Id;
	SetToSetter.TargetPinId = SetterValuePin.Id;
	Document.Edges.Add(SetToSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Make set document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Make set document should be applied."), Result.bApplied);
	TestTrue(TEXT("Make set document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after make set execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_MakeSet* MakeSetGraphNode = nullptr;
	UK2Node_VariableSet* SetterGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (SelfGraphNode == nullptr)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}
		if (MakeSetGraphNode == nullptr)
		{
			MakeSetGraphNode = Cast<UK2Node_MakeSet>(Node);
		}
		if (SetterGraphNode == nullptr)
		{
			SetterGraphNode = Cast<UK2Node_VariableSet>(Node);
		}
	}

	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Make set node should exist."), MakeSetGraphNode);
	TestNotNull(TEXT("Setter node should exist."), SetterGraphNode);
	if (SelfGraphNode == nullptr || MakeSetGraphNode == nullptr || SetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const SetInput0GraphPin = MakeSetGraphNode->FindPin(TEXT("[0]"));
	UEdGraphPin* const SetInput1GraphPin = MakeSetGraphNode->FindPin(TEXT("[1]"));
	UEdGraphPin* const SetOutputGraphPin = MakeSetGraphNode->GetOutputPin();
	UEdGraphPin* const SetterValueGraphPin = SetterGraphNode->FindPin(TEXT("ActorSet"));

	TestTrue(TEXT("Make set should remain pure."), MakeSetGraphNode->IsNodePure());
	TestTrue(TEXT("Make set output should be typed as an object set."), SetOutputGraphPin != nullptr && SetOutputGraphPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && SetOutputGraphPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass() && SetOutputGraphPin->PinType.ContainerType == EPinContainerType::Set);
	TestTrue(TEXT("Self should feed set input [0]."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(SetInput0GraphPin));
	TestTrue(TEXT("Self should feed set input [1]."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(SetInput1GraphPin));
	TestTrue(TEXT("Set output should feed setter value."), SetOutputGraphPin != nullptr && SetOutputGraphPin->LinkedTo.Contains(SetterValueGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilMakeMapExecutionTest,
	"Vergil.Scaffold.MakeMapExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilMakeMapExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType NameValueType;
	NameValueType.PinCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("NameValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("NameValue"), NameValueType, TEXT("Vergil")));

	FEdGraphPinType ActorNameMapType;
	ActorNameMapType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorNameMapType.PinSubCategoryObject = AActor::StaticClass();
	ActorNameMapType.ContainerType = EPinContainerType::Map;
	ActorNameMapType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("ActorNameMap member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ActorNameMap"), ActorNameMapType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(260.0f, -120.0f);

	FVergilGraphPin SelfOutput;
	SelfOutput.Id = FGuid::NewGuid();
	SelfOutput.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutput.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutput);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.NameValue");
	GetterNode.Position = FVector2D(260.0f, 120.0f);

	FVergilGraphPin GetterValuePin;
	GetterValuePin.Id = FGuid::NewGuid();
	GetterValuePin.Name = TEXT("NameValue");
	GetterValuePin.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValuePin);

	FVergilGraphNode MakeMapNode;
	MakeMapNode.Id = FGuid::NewGuid();
	MakeMapNode.Kind = EVergilNodeKind::Custom;
	MakeMapNode.Descriptor = TEXT("K2.MakeMap");
	MakeMapNode.Position = FVector2D(620.0f, -20.0f);
	MakeMapNode.Metadata.Add(TEXT("KeyPinCategory"), TEXT("object"));
	MakeMapNode.Metadata.Add(TEXT("KeyObjectPath"), AActor::StaticClass()->GetPathName());
	MakeMapNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("string"));
	MakeMapNode.Metadata.Add(TEXT("NumPairs"), TEXT("2"));

	FVergilGraphPin Key0Pin;
	Key0Pin.Id = FGuid::NewGuid();
	Key0Pin.Name = TEXT("Key 0");
	Key0Pin.Direction = EVergilPinDirection::Input;
	MakeMapNode.Pins.Add(Key0Pin);

	FVergilGraphPin Value0Pin;
	Value0Pin.Id = FGuid::NewGuid();
	Value0Pin.Name = TEXT("Value 0");
	Value0Pin.Direction = EVergilPinDirection::Input;
	MakeMapNode.Pins.Add(Value0Pin);

	FVergilGraphPin Key1Pin;
	Key1Pin.Id = FGuid::NewGuid();
	Key1Pin.Name = TEXT("Key 1");
	Key1Pin.Direction = EVergilPinDirection::Input;
	MakeMapNode.Pins.Add(Key1Pin);

	FVergilGraphPin Value1Pin;
	Value1Pin.Id = FGuid::NewGuid();
	Value1Pin.Name = TEXT("Value 1");
	Value1Pin.Direction = EVergilPinDirection::Input;
	MakeMapNode.Pins.Add(Value1Pin);

	FVergilGraphPin MapOutputPin;
	MapOutputPin.Id = FGuid::NewGuid();
	MapOutputPin.Name = TEXT("Map");
	MapOutputPin.Direction = EVergilPinDirection::Output;
	MakeMapNode.Pins.Add(MapOutputPin);

	FVergilGraphNode SetterNode;
	SetterNode.Id = FGuid::NewGuid();
	SetterNode.Kind = EVergilNodeKind::VariableSet;
	SetterNode.Descriptor = TEXT("K2.VarSet.ActorNameMap");
	SetterNode.Position = FVector2D(1040.0f, 0.0f);

	FVergilGraphPin SetterExecPin;
	SetterExecPin.Id = FGuid::NewGuid();
	SetterExecPin.Name = TEXT("Execute");
	SetterExecPin.Direction = EVergilPinDirection::Input;
	SetterExecPin.bIsExec = true;
	SetterNode.Pins.Add(SetterExecPin);

	FVergilGraphPin SetterValuePin;
	SetterValuePin.Id = FGuid::NewGuid();
	SetterValuePin.Name = TEXT("ActorNameMap");
	SetterValuePin.Direction = EVergilPinDirection::Input;
	SetterNode.Pins.Add(SetterValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilMakeMapExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, GetterNode, MakeMapNode, SetterNode };

	FVergilGraphEdge EventToSetter;
	EventToSetter.Id = FGuid::NewGuid();
	EventToSetter.SourceNodeId = BeginPlayNode.Id;
	EventToSetter.SourcePinId = BeginPlayThen.Id;
	EventToSetter.TargetNodeId = SetterNode.Id;
	EventToSetter.TargetPinId = SetterExecPin.Id;
	Document.Edges.Add(EventToSetter);

	FVergilGraphEdge SelfToKey0;
	SelfToKey0.Id = FGuid::NewGuid();
	SelfToKey0.SourceNodeId = SelfNode.Id;
	SelfToKey0.SourcePinId = SelfOutput.Id;
	SelfToKey0.TargetNodeId = MakeMapNode.Id;
	SelfToKey0.TargetPinId = Key0Pin.Id;
	Document.Edges.Add(SelfToKey0);

	FVergilGraphEdge GetterToValue0;
	GetterToValue0.Id = FGuid::NewGuid();
	GetterToValue0.SourceNodeId = GetterNode.Id;
	GetterToValue0.SourcePinId = GetterValuePin.Id;
	GetterToValue0.TargetNodeId = MakeMapNode.Id;
	GetterToValue0.TargetPinId = Value0Pin.Id;
	Document.Edges.Add(GetterToValue0);

	FVergilGraphEdge SelfToKey1;
	SelfToKey1.Id = FGuid::NewGuid();
	SelfToKey1.SourceNodeId = SelfNode.Id;
	SelfToKey1.SourcePinId = SelfOutput.Id;
	SelfToKey1.TargetNodeId = MakeMapNode.Id;
	SelfToKey1.TargetPinId = Key1Pin.Id;
	Document.Edges.Add(SelfToKey1);

	FVergilGraphEdge GetterToValue1;
	GetterToValue1.Id = FGuid::NewGuid();
	GetterToValue1.SourceNodeId = GetterNode.Id;
	GetterToValue1.SourcePinId = GetterValuePin.Id;
	GetterToValue1.TargetNodeId = MakeMapNode.Id;
	GetterToValue1.TargetPinId = Value1Pin.Id;
	Document.Edges.Add(GetterToValue1);

	FVergilGraphEdge MapToSetter;
	MapToSetter.Id = FGuid::NewGuid();
	MapToSetter.SourceNodeId = MakeMapNode.Id;
	MapToSetter.SourcePinId = MapOutputPin.Id;
	MapToSetter.TargetNodeId = SetterNode.Id;
	MapToSetter.TargetPinId = SetterValuePin.Id;
	Document.Edges.Add(MapToSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Make map document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Make map document should be applied."), Result.bApplied);
	TestTrue(TEXT("Make map document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after make map execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_VariableGet* GetterGraphNode = nullptr;
	UK2Node_MakeMap* MakeMapGraphNode = nullptr;
	UK2Node_VariableSet* SetterGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (SelfGraphNode == nullptr)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}
		if (GetterGraphNode == nullptr)
		{
			if (UK2Node_VariableGet* Candidate = Cast<UK2Node_VariableGet>(Node))
			{
				if (Candidate->FindPin(TEXT("NameValue")) != nullptr)
				{
					GetterGraphNode = Candidate;
				}
			}
		}
		if (MakeMapGraphNode == nullptr)
		{
			MakeMapGraphNode = Cast<UK2Node_MakeMap>(Node);
		}
		if (SetterGraphNode == nullptr)
		{
			SetterGraphNode = Cast<UK2Node_VariableSet>(Node);
		}
	}

	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Make map node should exist."), MakeMapGraphNode);
	TestNotNull(TEXT("Setter node should exist."), SetterGraphNode);
	if (SelfGraphNode == nullptr || GetterGraphNode == nullptr || MakeMapGraphNode == nullptr || SetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const GetterGraphValuePin = GetterGraphNode->FindPin(TEXT("NameValue"));
	UEdGraphPin* const Key0GraphPin = MakeMapGraphNode->FindPin(TEXT("Key 0"));
	UEdGraphPin* const Value0GraphPin = MakeMapGraphNode->FindPin(TEXT("Value 0"));
	UEdGraphPin* const Key1GraphPin = MakeMapGraphNode->FindPin(TEXT("Key 1"));
	UEdGraphPin* const Value1GraphPin = MakeMapGraphNode->FindPin(TEXT("Value 1"));
	UEdGraphPin* const MapOutputGraphPin = MakeMapGraphNode->GetOutputPin();
	UEdGraphPin* const SetterValueGraphPin = SetterGraphNode->FindPin(TEXT("ActorNameMap"));

	TestTrue(TEXT("Variable getter should remain pure."), GetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("Make map should remain pure."), MakeMapGraphNode->IsNodePure());
	TestTrue(TEXT("Make map output should be typed as an object-to-string map."), MapOutputGraphPin != nullptr && MapOutputGraphPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && MapOutputGraphPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass() && MapOutputGraphPin->PinType.ContainerType == EPinContainerType::Map && MapOutputGraphPin->PinType.PinValueType.TerminalCategory == UEdGraphSchema_K2::PC_String);
	TestTrue(TEXT("Self should feed map key 0."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(Key0GraphPin));
	TestTrue(TEXT("Getter should feed map value 0."), GetterGraphValuePin != nullptr && GetterGraphValuePin->LinkedTo.Contains(Value0GraphPin));
	TestTrue(TEXT("Self should feed map key 1."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(Key1GraphPin));
	TestTrue(TEXT("Getter should feed map value 1."), GetterGraphValuePin != nullptr && GetterGraphValuePin->LinkedTo.Contains(Value1GraphPin));
	TestTrue(TEXT("Map output should feed setter value."), MapOutputGraphPin != nullptr && MapOutputGraphPin->LinkedTo.Contains(SetterValueGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilForLoopExecutionTest,
	"Vergil.Scaffold.ForLoopExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilForLoopExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType IntType;
	IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("FirstIndex member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("FirstIndex"), IntType, TEXT("0")));
	TestTrue(TEXT("LastIndex member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("LastIndex"), IntType, TEXT("2")));
	TestTrue(TEXT("CurrentIndex member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("CurrentIndex"), IntType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode FirstGetterNode;
	FirstGetterNode.Id = FGuid::NewGuid();
	FirstGetterNode.Kind = EVergilNodeKind::VariableGet;
	FirstGetterNode.Descriptor = TEXT("K2.VarGet.FirstIndex");
	FirstGetterNode.Position = FVector2D(220.0f, -180.0f);

	FVergilGraphPin FirstGetterValuePin;
	FirstGetterValuePin.Id = FGuid::NewGuid();
	FirstGetterValuePin.Name = TEXT("FirstIndex");
	FirstGetterValuePin.Direction = EVergilPinDirection::Output;
	FirstGetterNode.Pins.Add(FirstGetterValuePin);

	FVergilGraphNode LastGetterNode;
	LastGetterNode.Id = FGuid::NewGuid();
	LastGetterNode.Kind = EVergilNodeKind::VariableGet;
	LastGetterNode.Descriptor = TEXT("K2.VarGet.LastIndex");
	LastGetterNode.Position = FVector2D(220.0f, 140.0f);

	FVergilGraphPin LastGetterValuePin;
	LastGetterValuePin.Id = FGuid::NewGuid();
	LastGetterValuePin.Name = TEXT("LastIndex");
	LastGetterValuePin.Direction = EVergilPinDirection::Output;
	LastGetterNode.Pins.Add(LastGetterValuePin);

	FVergilGraphNode ForLoopNode;
	ForLoopNode.Id = FGuid::NewGuid();
	ForLoopNode.Kind = EVergilNodeKind::Custom;
	ForLoopNode.Descriptor = TEXT("K2.ForLoop");
	ForLoopNode.Position = FVector2D(520.0f, -20.0f);

	FVergilGraphPin LoopExecInPin;
	LoopExecInPin.Id = FGuid::NewGuid();
	LoopExecInPin.Name = UEdGraphSchema_K2::PN_Execute;
	LoopExecInPin.Direction = EVergilPinDirection::Input;
	LoopExecInPin.bIsExec = true;
	ForLoopNode.Pins.Add(LoopExecInPin);

	FVergilGraphPin LoopFirstPin;
	LoopFirstPin.Id = FGuid::NewGuid();
	LoopFirstPin.Name = TEXT("FirstIndex");
	LoopFirstPin.Direction = EVergilPinDirection::Input;
	ForLoopNode.Pins.Add(LoopFirstPin);

	FVergilGraphPin LoopLastPin;
	LoopLastPin.Id = FGuid::NewGuid();
	LoopLastPin.Name = TEXT("LastIndex");
	LoopLastPin.Direction = EVergilPinDirection::Input;
	ForLoopNode.Pins.Add(LoopLastPin);

	FVergilGraphPin LoopBodyPin;
	LoopBodyPin.Id = FGuid::NewGuid();
	LoopBodyPin.Name = TEXT("LoopBody");
	LoopBodyPin.Direction = EVergilPinDirection::Output;
	LoopBodyPin.bIsExec = true;
	ForLoopNode.Pins.Add(LoopBodyPin);

	FVergilGraphPin LoopIndexPin;
	LoopIndexPin.Id = FGuid::NewGuid();
	LoopIndexPin.Name = TEXT("Index");
	LoopIndexPin.Direction = EVergilPinDirection::Output;
	ForLoopNode.Pins.Add(LoopIndexPin);

	FVergilGraphPin LoopCompletedPin;
	LoopCompletedPin.Id = FGuid::NewGuid();
	LoopCompletedPin.Name = TEXT("Completed");
	LoopCompletedPin.Direction = EVergilPinDirection::Output;
	LoopCompletedPin.bIsExec = true;
	ForLoopNode.Pins.Add(LoopCompletedPin);

	FVergilGraphNode SetIndexNode;
	SetIndexNode.Id = FGuid::NewGuid();
	SetIndexNode.Kind = EVergilNodeKind::VariableSet;
	SetIndexNode.Descriptor = TEXT("K2.VarSet.CurrentIndex");
	SetIndexNode.Position = FVector2D(860.0f, -80.0f);

	FVergilGraphPin SetIndexExecPin;
	SetIndexExecPin.Id = FGuid::NewGuid();
	SetIndexExecPin.Name = TEXT("Execute");
	SetIndexExecPin.Direction = EVergilPinDirection::Input;
	SetIndexExecPin.bIsExec = true;
	SetIndexNode.Pins.Add(SetIndexExecPin);

	FVergilGraphPin SetIndexValuePin;
	SetIndexValuePin.Id = FGuid::NewGuid();
	SetIndexValuePin.Name = TEXT("CurrentIndex");
	SetIndexValuePin.Direction = EVergilPinDirection::Input;
	SetIndexNode.Pins.Add(SetIndexValuePin);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(860.0f, 180.0f);

	FVergilGraphPin CallExecPin;
	CallExecPin.Id = FGuid::NewGuid();
	CallExecPin.Name = TEXT("Execute");
	CallExecPin.Direction = EVergilPinDirection::Input;
	CallExecPin.bIsExec = true;
	CallNode.Pins.Add(CallExecPin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilForLoopExecution");
	Document.Nodes = { BeginPlayNode, FirstGetterNode, LastGetterNode, ForLoopNode, SetIndexNode, CallNode };

	FVergilGraphEdge EventToLoop;
	EventToLoop.Id = FGuid::NewGuid();
	EventToLoop.SourceNodeId = BeginPlayNode.Id;
	EventToLoop.SourcePinId = BeginPlayThen.Id;
	EventToLoop.TargetNodeId = ForLoopNode.Id;
	EventToLoop.TargetPinId = LoopExecInPin.Id;
	Document.Edges.Add(EventToLoop);

	FVergilGraphEdge FirstToLoop;
	FirstToLoop.Id = FGuid::NewGuid();
	FirstToLoop.SourceNodeId = FirstGetterNode.Id;
	FirstToLoop.SourcePinId = FirstGetterValuePin.Id;
	FirstToLoop.TargetNodeId = ForLoopNode.Id;
	FirstToLoop.TargetPinId = LoopFirstPin.Id;
	Document.Edges.Add(FirstToLoop);

	FVergilGraphEdge LastToLoop;
	LastToLoop.Id = FGuid::NewGuid();
	LastToLoop.SourceNodeId = LastGetterNode.Id;
	LastToLoop.SourcePinId = LastGetterValuePin.Id;
	LastToLoop.TargetNodeId = ForLoopNode.Id;
	LastToLoop.TargetPinId = LoopLastPin.Id;
	Document.Edges.Add(LastToLoop);

	FVergilGraphEdge LoopBodyToSetter;
	LoopBodyToSetter.Id = FGuid::NewGuid();
	LoopBodyToSetter.SourceNodeId = ForLoopNode.Id;
	LoopBodyToSetter.SourcePinId = LoopBodyPin.Id;
	LoopBodyToSetter.TargetNodeId = SetIndexNode.Id;
	LoopBodyToSetter.TargetPinId = SetIndexExecPin.Id;
	Document.Edges.Add(LoopBodyToSetter);

	FVergilGraphEdge LoopIndexToSetter;
	LoopIndexToSetter.Id = FGuid::NewGuid();
	LoopIndexToSetter.SourceNodeId = ForLoopNode.Id;
	LoopIndexToSetter.SourcePinId = LoopIndexPin.Id;
	LoopIndexToSetter.TargetNodeId = SetIndexNode.Id;
	LoopIndexToSetter.TargetPinId = SetIndexValuePin.Id;
	Document.Edges.Add(LoopIndexToSetter);

	FVergilGraphEdge CompletedToCall;
	CompletedToCall.Id = FGuid::NewGuid();
	CompletedToCall.SourceNodeId = ForLoopNode.Id;
	CompletedToCall.SourcePinId = LoopCompletedPin.Id;
	CompletedToCall.TargetNodeId = CallNode.Id;
	CompletedToCall.TargetPinId = CallExecPin.Id;
	Document.Edges.Add(CompletedToCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("For loop document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("For loop document should be applied."), Result.bApplied);
	TestTrue(TEXT("For loop document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after for loop execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_VariableGet* const FirstGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, FirstGetterNode.Id);
	UK2Node_VariableGet* const LastGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, LastGetterNode.Id);
	UK2Node_MacroInstance* const LoopGraphNode = FindGraphNodeByGuid<UK2Node_MacroInstance>(EventGraph, ForLoopNode.Id);
	UK2Node_VariableSet* const SetIndexGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetIndexNode.Id);
	UK2Node_CallFunction* const CallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, CallNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("First getter node should exist."), FirstGetterGraphNode);
	TestNotNull(TEXT("Last getter node should exist."), LastGetterGraphNode);
	TestNotNull(TEXT("For loop macro node should exist."), LoopGraphNode);
	TestNotNull(TEXT("CurrentIndex setter node should exist."), SetIndexGraphNode);
	TestNotNull(TEXT("DestroyActor call node should exist."), CallGraphNode);
	if (EventNode == nullptr || FirstGetterGraphNode == nullptr || LastGetterGraphNode == nullptr || LoopGraphNode == nullptr || SetIndexGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const FirstGraphPin = FirstGetterGraphNode->FindPin(TEXT("FirstIndex"));
	UEdGraphPin* const LastGraphPin = LastGetterGraphNode->FindPin(TEXT("LastIndex"));
	UEdGraphPin* const LoopExecGraphPin = LoopGraphNode->GetExecPin();
	UEdGraphPin* const LoopFirstGraphPin = LoopGraphNode->FindPin(TEXT("FirstIndex"));
	UEdGraphPin* const LoopLastGraphPin = LoopGraphNode->FindPin(TEXT("LastIndex"));
	UEdGraphPin* const LoopBodyGraphPin = LoopGraphNode->FindPin(TEXT("LoopBody"));
	UEdGraphPin* const LoopIndexGraphPin = LoopGraphNode->FindPin(TEXT("Index"));
	UEdGraphPin* const LoopCompletedGraphPin = LoopGraphNode->FindPin(TEXT("Completed"));
	UEdGraphPin* const SetIndexExecGraphPin = SetIndexGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetIndexValueGraphPin = SetIndexGraphNode->FindPin(TEXT("CurrentIndex"));
	UEdGraphPin* const CallExecGraphPin = CallGraphNode->GetExecPin();

	TestTrue(TEXT("First getter should remain pure."), FirstGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("Last getter should remain pure."), LastGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("For loop should resolve the StandardMacros ForLoop graph."), LoopGraphNode->GetMacroGraph() != nullptr && LoopGraphNode->GetMacroGraph()->GetFName() == TEXT("ForLoop"));
	TestTrue(TEXT("Event should feed loop exec."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(LoopExecGraphPin));
	TestTrue(TEXT("First getter should feed loop first index."), FirstGraphPin != nullptr && FirstGraphPin->LinkedTo.Contains(LoopFirstGraphPin));
	TestTrue(TEXT("Last getter should feed loop last index."), LastGraphPin != nullptr && LastGraphPin->LinkedTo.Contains(LoopLastGraphPin));
	TestTrue(TEXT("Loop body should feed setter exec."), LoopBodyGraphPin != nullptr && LoopBodyGraphPin->LinkedTo.Contains(SetIndexExecGraphPin));
	TestTrue(TEXT("Loop index should feed setter value."), LoopIndexGraphPin != nullptr && LoopIndexGraphPin->LinkedTo.Contains(SetIndexValueGraphPin));
	TestTrue(TEXT("Loop completed should feed destroy exec."), LoopCompletedGraphPin != nullptr && LoopCompletedGraphPin->LinkedTo.Contains(CallExecGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilDelayExecutionTest,
	"Vergil.Scaffold.DelayExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilDelayExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	TestTrue(TEXT("DelaySeconds member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DelaySeconds"), FloatType, TEXT("0.25")));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.DelaySeconds");
	GetterNode.Position = FVector2D(240.0f, -140.0f);

	FVergilGraphPin GetterValuePin;
	GetterValuePin.Id = FGuid::NewGuid();
	GetterValuePin.Name = TEXT("DelaySeconds");
	GetterValuePin.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValuePin);

	FVergilGraphNode DelayNode;
	DelayNode.Id = FGuid::NewGuid();
	DelayNode.Kind = EVergilNodeKind::Custom;
	DelayNode.Descriptor = TEXT("K2.Delay");
	DelayNode.Position = FVector2D(560.0f, -20.0f);

	FVergilGraphPin DelayExecPin;
	DelayExecPin.Id = FGuid::NewGuid();
	DelayExecPin.Name = TEXT("Execute");
	DelayExecPin.Direction = EVergilPinDirection::Input;
	DelayExecPin.bIsExec = true;
	DelayNode.Pins.Add(DelayExecPin);

	FVergilGraphPin DelayDurationPin;
	DelayDurationPin.Id = FGuid::NewGuid();
	DelayDurationPin.Name = TEXT("Duration");
	DelayDurationPin.Direction = EVergilPinDirection::Input;
	DelayNode.Pins.Add(DelayDurationPin);

	FVergilGraphPin DelayThenPin;
	DelayThenPin.Id = FGuid::NewGuid();
	DelayThenPin.Name = TEXT("Then");
	DelayThenPin.Direction = EVergilPinDirection::Output;
	DelayThenPin.bIsExec = true;
	DelayNode.Pins.Add(DelayThenPin);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Call;
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
	CallNode.Position = FVector2D(860.0f, 0.0f);

	FVergilGraphPin CallExecPin;
	CallExecPin.Id = FGuid::NewGuid();
	CallExecPin.Name = TEXT("Execute");
	CallExecPin.Direction = EVergilPinDirection::Input;
	CallExecPin.bIsExec = true;
	CallNode.Pins.Add(CallExecPin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilDelayExecution");
	Document.Nodes = { BeginPlayNode, GetterNode, DelayNode, CallNode };

	FVergilGraphEdge EventToDelay;
	EventToDelay.Id = FGuid::NewGuid();
	EventToDelay.SourceNodeId = BeginPlayNode.Id;
	EventToDelay.SourcePinId = BeginPlayThen.Id;
	EventToDelay.TargetNodeId = DelayNode.Id;
	EventToDelay.TargetPinId = DelayExecPin.Id;
	Document.Edges.Add(EventToDelay);

	FVergilGraphEdge GetterToDelay;
	GetterToDelay.Id = FGuid::NewGuid();
	GetterToDelay.SourceNodeId = GetterNode.Id;
	GetterToDelay.SourcePinId = GetterValuePin.Id;
	GetterToDelay.TargetNodeId = DelayNode.Id;
	GetterToDelay.TargetPinId = DelayDurationPin.Id;
	Document.Edges.Add(GetterToDelay);

	FVergilGraphEdge DelayToCall;
	DelayToCall.Id = FGuid::NewGuid();
	DelayToCall.SourceNodeId = DelayNode.Id;
	DelayToCall.SourcePinId = DelayThenPin.Id;
	DelayToCall.TargetNodeId = CallNode.Id;
	DelayToCall.TargetPinId = CallExecPin.Id;
	Document.Edges.Add(DelayToCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Delay document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Delay document should be applied."), Result.bApplied);
	TestTrue(TEXT("Delay document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after delay execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_VariableGet* const GetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, GetterNode.Id);
	UK2Node_CallFunction* const DelayGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, DelayNode.Id);
	UK2Node_CallFunction* const CallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, CallNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Delay call node should exist."), DelayGraphNode);
	TestNotNull(TEXT("DestroyActor call node should exist."), CallGraphNode);
	if (EventNode == nullptr || GetterGraphNode == nullptr || DelayGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const GetterGraphValuePin = GetterGraphNode->FindPin(TEXT("DelaySeconds"));
	UEdGraphPin* const DelayExecGraphPin = DelayGraphNode->GetExecPin();
	UEdGraphPin* const DelayDurationGraphPin = DelayGraphNode->FindPin(TEXT("Duration"));
	UEdGraphPin* const DelayThenGraphPin = DelayGraphNode->GetThenPin();
	UEdGraphPin* const CallExecGraphPin = CallGraphNode->GetExecPin();

	TestTrue(TEXT("Getter should remain pure."), GetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("Delay node should resolve UKismetSystemLibrary::Delay."), DelayGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay)));
	TestTrue(TEXT("Event should feed delay exec."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(DelayExecGraphPin));
	TestTrue(TEXT("Getter should feed delay duration."), GetterGraphValuePin != nullptr && GetterGraphValuePin->LinkedTo.Contains(DelayDurationGraphPin));
	TestTrue(TEXT("Delay then should feed destroy exec."), DelayThenGraphPin != nullptr && DelayThenGraphPin->LinkedTo.Contains(CallExecGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilBindDelegateExecutionTest,
	"Vergil.Scaffold.BindDelegateExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilBindDelegateExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(260.0f, 120.0f);

	FVergilGraphPin SelfOutputPin;
	SelfOutputPin.Id = FGuid::NewGuid();
	SelfOutputPin.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutputPin.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutputPin);

	FVergilGraphNode CustomEventNode;
	CustomEventNode.Id = FGuid::NewGuid();
	CustomEventNode.Kind = EVergilNodeKind::Custom;
	CustomEventNode.Descriptor = TEXT("K2.CustomEvent.HandleDestroyed");
	CustomEventNode.Position = FVector2D(0.0f, 260.0f);
	CustomEventNode.Metadata.Add(TEXT("DelegateOwnerClassPath"), AActor::StaticClass()->GetPathName());
	CustomEventNode.Metadata.Add(TEXT("DelegatePropertyName"), TEXT("OnDestroyed"));

	FVergilGraphPin CustomEventDelegatePin;
	CustomEventDelegatePin.Id = FGuid::NewGuid();
	CustomEventDelegatePin.Name = UK2Node_Event::DelegateOutputName;
	CustomEventDelegatePin.Direction = EVergilPinDirection::Output;
	CustomEventNode.Pins.Add(CustomEventDelegatePin);

	FVergilGraphPin DestroyedActorPin;
	DestroyedActorPin.Id = FGuid::NewGuid();
	DestroyedActorPin.Name = TEXT("DestroyedActor");
	DestroyedActorPin.Direction = EVergilPinDirection::Output;
	CustomEventNode.Pins.Add(DestroyedActorPin);

	FVergilGraphNode BindNode;
	BindNode.Id = FGuid::NewGuid();
	BindNode.Kind = EVergilNodeKind::Custom;
	BindNode.Descriptor = TEXT("K2.BindDelegate.OnDestroyed");
	BindNode.Position = FVector2D(620.0f, 0.0f);
	BindNode.Metadata.Add(TEXT("OwnerClassPath"), AActor::StaticClass()->GetPathName());

	FVergilGraphPin BindExecPin;
	BindExecPin.Id = FGuid::NewGuid();
	BindExecPin.Name = TEXT("Execute");
	BindExecPin.Direction = EVergilPinDirection::Input;
	BindExecPin.bIsExec = true;
	BindNode.Pins.Add(BindExecPin);

	FVergilGraphPin BindSelfPin;
	BindSelfPin.Id = FGuid::NewGuid();
	BindSelfPin.Name = UEdGraphSchema_K2::PN_Self;
	BindSelfPin.Direction = EVergilPinDirection::Input;
	BindNode.Pins.Add(BindSelfPin);

	FVergilGraphPin BindDelegatePin;
	BindDelegatePin.Id = FGuid::NewGuid();
	BindDelegatePin.Name = TEXT("Delegate");
	BindDelegatePin.Direction = EVergilPinDirection::Input;
	BindNode.Pins.Add(BindDelegatePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilBindDelegateExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, CustomEventNode, BindNode };

	FVergilGraphEdge EventToBind;
	EventToBind.Id = FGuid::NewGuid();
	EventToBind.SourceNodeId = BeginPlayNode.Id;
	EventToBind.SourcePinId = BeginPlayThen.Id;
	EventToBind.TargetNodeId = BindNode.Id;
	EventToBind.TargetPinId = BindExecPin.Id;
	Document.Edges.Add(EventToBind);

	FVergilGraphEdge SelfToBind;
	SelfToBind.Id = FGuid::NewGuid();
	SelfToBind.SourceNodeId = SelfNode.Id;
	SelfToBind.SourcePinId = SelfOutputPin.Id;
	SelfToBind.TargetNodeId = BindNode.Id;
	SelfToBind.TargetPinId = BindSelfPin.Id;
	Document.Edges.Add(SelfToBind);

	FVergilGraphEdge DelegateToBind;
	DelegateToBind.Id = FGuid::NewGuid();
	DelegateToBind.SourceNodeId = CustomEventNode.Id;
	DelegateToBind.SourcePinId = CustomEventDelegatePin.Id;
	DelegateToBind.TargetNodeId = BindNode.Id;
	DelegateToBind.TargetPinId = BindDelegatePin.Id;
	Document.Edges.Add(DelegateToBind);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Bind delegate document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Bind delegate document should be applied."), Result.bApplied);
	TestTrue(TEXT("Bind delegate document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after bind delegate execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* EventNode = nullptr;
	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_CustomEvent* CustomEventGraphNode = nullptr;
	UK2Node_AddDelegate* BindGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (EventNode == nullptr && Node != nullptr && Node->NodeGuid == BeginPlayNode.Id)
		{
			EventNode = Cast<UK2Node_Event>(Node);
		}
		if (SelfGraphNode == nullptr && Node != nullptr && Node->NodeGuid == SelfNode.Id)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}
		if (CustomEventGraphNode == nullptr && Node != nullptr && Node->NodeGuid == CustomEventNode.Id)
		{
			CustomEventGraphNode = Cast<UK2Node_CustomEvent>(Node);
		}
		if (BindGraphNode == nullptr && Node != nullptr && Node->NodeGuid == BindNode.Id)
		{
			BindGraphNode = Cast<UK2Node_AddDelegate>(Node);
		}
	}

	TestNotNull(TEXT("BeginPlay event should exist."), EventNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Custom event should exist."), CustomEventGraphNode);
	TestNotNull(TEXT("Bind delegate node should exist."), BindGraphNode);
	if (EventNode == nullptr || SelfGraphNode == nullptr || CustomEventGraphNode == nullptr || BindGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const CustomDelegateGraphPin = CustomEventGraphNode->FindPin(UK2Node_Event::DelegateOutputName);
	UEdGraphPin* const CustomDestroyedActorGraphPin = CustomEventGraphNode->FindPin(TEXT("DestroyedActor"));
	UEdGraphPin* const BindExecGraphPin = BindGraphNode->GetExecPin();
	UEdGraphPin* const BindSelfGraphPin = BindGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const BindDelegateGraphPin = BindGraphNode->GetDelegatePin();

	TestEqual(TEXT("Bind delegate property should be OnDestroyed."), BindGraphNode->GetPropertyName(), FName(TEXT("OnDestroyed")));
	TestTrue(TEXT("Custom event should expose OutputDelegate."), CustomDelegateGraphPin != nullptr);
	TestTrue(TEXT("Custom event should expose DestroyedActor output from the delegate signature."), CustomDestroyedActorGraphPin != nullptr);
	TestTrue(TEXT("BeginPlay should feed bind execute."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(BindExecGraphPin));
	TestTrue(TEXT("Self should feed bind target."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(BindSelfGraphPin));
	TestTrue(TEXT("Custom event delegate should feed bind delegate pin."), CustomDelegateGraphPin != nullptr && CustomDelegateGraphPin->LinkedTo.Contains(BindDelegateGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilTimerDelegateExecutionTest,
	"Vergil.Scaffold.TimerDelegateExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilTimerDelegateExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType FloatType;
	FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
	FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	TestTrue(TEXT("TimerSeconds member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TimerSeconds"), FloatType, TEXT("0.25")));

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("Looping member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Looping"), BoolType, TEXT("false")));

	FEdGraphPinType TimerHandleType;
	TimerHandleType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	TimerHandleType.PinSubCategoryObject = TBaseStructure<FTimerHandle>::Get();
	TestTrue(TEXT("TimerHandleVar member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TimerHandleVar"), TimerHandleType, FString()));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode TimerEventNode;
	TimerEventNode.Id = FGuid::NewGuid();
	TimerEventNode.Kind = EVergilNodeKind::Custom;
	TimerEventNode.Descriptor = TEXT("K2.CustomEvent.HandleTimer");
	TimerEventNode.Position = FVector2D(0.0f, 260.0f);

	FVergilGraphPin TimerEventDelegatePin;
	TimerEventDelegatePin.Id = FGuid::NewGuid();
	TimerEventDelegatePin.Name = UK2Node_Event::DelegateOutputName;
	TimerEventDelegatePin.Direction = EVergilPinDirection::Output;
	TimerEventNode.Pins.Add(TimerEventDelegatePin);

	FVergilGraphNode SecondsGetterNode;
	SecondsGetterNode.Id = FGuid::NewGuid();
	SecondsGetterNode.Kind = EVergilNodeKind::VariableGet;
	SecondsGetterNode.Descriptor = TEXT("K2.VarGet.TimerSeconds");
	SecondsGetterNode.Position = FVector2D(220.0f, -180.0f);

	FVergilGraphPin SecondsGetterValuePin;
	SecondsGetterValuePin.Id = FGuid::NewGuid();
	SecondsGetterValuePin.Name = TEXT("TimerSeconds");
	SecondsGetterValuePin.Direction = EVergilPinDirection::Output;
	SecondsGetterNode.Pins.Add(SecondsGetterValuePin);

	FVergilGraphNode LoopingGetterNode;
	LoopingGetterNode.Id = FGuid::NewGuid();
	LoopingGetterNode.Kind = EVergilNodeKind::VariableGet;
	LoopingGetterNode.Descriptor = TEXT("K2.VarGet.Looping");
	LoopingGetterNode.Position = FVector2D(220.0f, 60.0f);

	FVergilGraphPin LoopingGetterValuePin;
	LoopingGetterValuePin.Id = FGuid::NewGuid();
	LoopingGetterValuePin.Name = TEXT("Looping");
	LoopingGetterValuePin.Direction = EVergilPinDirection::Output;
	LoopingGetterNode.Pins.Add(LoopingGetterValuePin);

	FVergilGraphNode SetTimerNode;
	SetTimerNode.Id = FGuid::NewGuid();
	SetTimerNode.Kind = EVergilNodeKind::Call;
	SetTimerNode.Descriptor = TEXT("K2.Call.K2_SetTimerDelegate");
	SetTimerNode.Position = FVector2D(620.0f, -20.0f);
	SetTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin SetTimerExecPin;
	SetTimerExecPin.Id = FGuid::NewGuid();
	SetTimerExecPin.Name = TEXT("Execute");
	SetTimerExecPin.Direction = EVergilPinDirection::Input;
	SetTimerExecPin.bIsExec = true;
	SetTimerNode.Pins.Add(SetTimerExecPin);

	FVergilGraphPin SetTimerThenPin;
	SetTimerThenPin.Id = FGuid::NewGuid();
	SetTimerThenPin.Name = TEXT("Then");
	SetTimerThenPin.Direction = EVergilPinDirection::Output;
	SetTimerThenPin.bIsExec = true;
	SetTimerNode.Pins.Add(SetTimerThenPin);

	FVergilGraphPin SetTimerDelegatePin;
	SetTimerDelegatePin.Id = FGuid::NewGuid();
	SetTimerDelegatePin.Name = TEXT("Delegate");
	SetTimerDelegatePin.Direction = EVergilPinDirection::Input;
	SetTimerNode.Pins.Add(SetTimerDelegatePin);

	FVergilGraphPin SetTimerTimePin;
	SetTimerTimePin.Id = FGuid::NewGuid();
	SetTimerTimePin.Name = TEXT("Time");
	SetTimerTimePin.Direction = EVergilPinDirection::Input;
	SetTimerNode.Pins.Add(SetTimerTimePin);

	FVergilGraphPin SetTimerLoopingPin;
	SetTimerLoopingPin.Id = FGuid::NewGuid();
	SetTimerLoopingPin.Name = TEXT("bLooping");
	SetTimerLoopingPin.Direction = EVergilPinDirection::Input;
	SetTimerNode.Pins.Add(SetTimerLoopingPin);

	FVergilGraphPin SetTimerReturnPin;
	SetTimerReturnPin.Id = FGuid::NewGuid();
	SetTimerReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	SetTimerReturnPin.Direction = EVergilPinDirection::Output;
	SetTimerNode.Pins.Add(SetTimerReturnPin);

	FVergilGraphNode SetHandleNode;
	SetHandleNode.Id = FGuid::NewGuid();
	SetHandleNode.Kind = EVergilNodeKind::VariableSet;
	SetHandleNode.Descriptor = TEXT("K2.VarSet.TimerHandleVar");
	SetHandleNode.Position = FVector2D(1040.0f, -20.0f);

	FVergilGraphPin SetHandleExecPin;
	SetHandleExecPin.Id = FGuid::NewGuid();
	SetHandleExecPin.Name = TEXT("Execute");
	SetHandleExecPin.Direction = EVergilPinDirection::Input;
	SetHandleExecPin.bIsExec = true;
	SetHandleNode.Pins.Add(SetHandleExecPin);

	FVergilGraphPin SetHandleThenPin;
	SetHandleThenPin.Id = FGuid::NewGuid();
	SetHandleThenPin.Name = TEXT("Then");
	SetHandleThenPin.Direction = EVergilPinDirection::Output;
	SetHandleThenPin.bIsExec = true;
	SetHandleNode.Pins.Add(SetHandleThenPin);

	FVergilGraphPin SetHandleValuePin;
	SetHandleValuePin.Id = FGuid::NewGuid();
	SetHandleValuePin.Name = TEXT("TimerHandleVar");
	SetHandleValuePin.Direction = EVergilPinDirection::Input;
	SetHandleNode.Pins.Add(SetHandleValuePin);

	FVergilGraphNode GetHandleNode;
	GetHandleNode.Id = FGuid::NewGuid();
	GetHandleNode.Kind = EVergilNodeKind::VariableGet;
	GetHandleNode.Descriptor = TEXT("K2.VarGet.TimerHandleVar");
	GetHandleNode.Position = FVector2D(1040.0f, 220.0f);

	FVergilGraphPin GetHandleValuePin;
	GetHandleValuePin.Id = FGuid::NewGuid();
	GetHandleValuePin.Name = TEXT("TimerHandleVar");
	GetHandleValuePin.Direction = EVergilPinDirection::Output;
	GetHandleNode.Pins.Add(GetHandleValuePin);

	FVergilGraphNode ClearTimerNode;
	ClearTimerNode.Id = FGuid::NewGuid();
	ClearTimerNode.Kind = EVergilNodeKind::Call;
	ClearTimerNode.Descriptor = TEXT("K2.Call.K2_ClearAndInvalidateTimerHandle");
	ClearTimerNode.Position = FVector2D(1420.0f, 20.0f);
	ClearTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin ClearTimerExecPin;
	ClearTimerExecPin.Id = FGuid::NewGuid();
	ClearTimerExecPin.Name = TEXT("Execute");
	ClearTimerExecPin.Direction = EVergilPinDirection::Input;
	ClearTimerExecPin.bIsExec = true;
	ClearTimerNode.Pins.Add(ClearTimerExecPin);

	FVergilGraphPin ClearTimerHandlePin;
	ClearTimerHandlePin.Id = FGuid::NewGuid();
	ClearTimerHandlePin.Name = TEXT("Handle");
	ClearTimerHandlePin.Direction = EVergilPinDirection::Input;
	ClearTimerNode.Pins.Add(ClearTimerHandlePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilTimerDelegateExecution");
	Document.Nodes = { BeginPlayNode, TimerEventNode, SecondsGetterNode, LoopingGetterNode, SetTimerNode, SetHandleNode, GetHandleNode, ClearTimerNode };

	FVergilGraphEdge EventToSetTimer;
	EventToSetTimer.Id = FGuid::NewGuid();
	EventToSetTimer.SourceNodeId = BeginPlayNode.Id;
	EventToSetTimer.SourcePinId = BeginPlayThen.Id;
	EventToSetTimer.TargetNodeId = SetTimerNode.Id;
	EventToSetTimer.TargetPinId = SetTimerExecPin.Id;
	Document.Edges.Add(EventToSetTimer);

	FVergilGraphEdge DelegateToSetTimer;
	DelegateToSetTimer.Id = FGuid::NewGuid();
	DelegateToSetTimer.SourceNodeId = TimerEventNode.Id;
	DelegateToSetTimer.SourcePinId = TimerEventDelegatePin.Id;
	DelegateToSetTimer.TargetNodeId = SetTimerNode.Id;
	DelegateToSetTimer.TargetPinId = SetTimerDelegatePin.Id;
	Document.Edges.Add(DelegateToSetTimer);

	FVergilGraphEdge SecondsToSetTimer;
	SecondsToSetTimer.Id = FGuid::NewGuid();
	SecondsToSetTimer.SourceNodeId = SecondsGetterNode.Id;
	SecondsToSetTimer.SourcePinId = SecondsGetterValuePin.Id;
	SecondsToSetTimer.TargetNodeId = SetTimerNode.Id;
	SecondsToSetTimer.TargetPinId = SetTimerTimePin.Id;
	Document.Edges.Add(SecondsToSetTimer);

	FVergilGraphEdge LoopingToSetTimer;
	LoopingToSetTimer.Id = FGuid::NewGuid();
	LoopingToSetTimer.SourceNodeId = LoopingGetterNode.Id;
	LoopingToSetTimer.SourcePinId = LoopingGetterValuePin.Id;
	LoopingToSetTimer.TargetNodeId = SetTimerNode.Id;
	LoopingToSetTimer.TargetPinId = SetTimerLoopingPin.Id;
	Document.Edges.Add(LoopingToSetTimer);

	FVergilGraphEdge SetTimerToSetHandle;
	SetTimerToSetHandle.Id = FGuid::NewGuid();
	SetTimerToSetHandle.SourceNodeId = SetTimerNode.Id;
	SetTimerToSetHandle.SourcePinId = SetTimerThenPin.Id;
	SetTimerToSetHandle.TargetNodeId = SetHandleNode.Id;
	SetTimerToSetHandle.TargetPinId = SetHandleExecPin.Id;
	Document.Edges.Add(SetTimerToSetHandle);

	FVergilGraphEdge TimerReturnToSetHandle;
	TimerReturnToSetHandle.Id = FGuid::NewGuid();
	TimerReturnToSetHandle.SourceNodeId = SetTimerNode.Id;
	TimerReturnToSetHandle.SourcePinId = SetTimerReturnPin.Id;
	TimerReturnToSetHandle.TargetNodeId = SetHandleNode.Id;
	TimerReturnToSetHandle.TargetPinId = SetHandleValuePin.Id;
	Document.Edges.Add(TimerReturnToSetHandle);

	FVergilGraphEdge SetHandleToClearTimer;
	SetHandleToClearTimer.Id = FGuid::NewGuid();
	SetHandleToClearTimer.SourceNodeId = SetHandleNode.Id;
	SetHandleToClearTimer.SourcePinId = SetHandleThenPin.Id;
	SetHandleToClearTimer.TargetNodeId = ClearTimerNode.Id;
	SetHandleToClearTimer.TargetPinId = ClearTimerExecPin.Id;
	Document.Edges.Add(SetHandleToClearTimer);

	FVergilGraphEdge GetHandleToClearTimer;
	GetHandleToClearTimer.Id = FGuid::NewGuid();
	GetHandleToClearTimer.SourceNodeId = GetHandleNode.Id;
	GetHandleToClearTimer.SourcePinId = GetHandleValuePin.Id;
	GetHandleToClearTimer.TargetNodeId = ClearTimerNode.Id;
	GetHandleToClearTimer.TargetPinId = ClearTimerHandlePin.Id;
	Document.Edges.Add(GetHandleToClearTimer);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Timer delegate document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Timer delegate document should be applied."), Result.bApplied);
	TestTrue(TEXT("Timer delegate document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after timer delegate execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_CustomEvent* const TimerEventGraphNode = FindGraphNodeByGuid<UK2Node_CustomEvent>(EventGraph, TimerEventNode.Id);
	UK2Node_VariableGet* const SecondsGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, SecondsGetterNode.Id);
	UK2Node_VariableGet* const LoopingGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, LoopingGetterNode.Id);
	UK2Node_CallFunction* const SetTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, SetTimerNode.Id);
	UK2Node_VariableSet* const SetHandleGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetHandleNode.Id);
	UK2Node_VariableGet* const GetHandleGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, GetHandleNode.Id);
	UK2Node_CallFunction* const ClearTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ClearTimerNode.Id);

	TestNotNull(TEXT("BeginPlay event should exist."), EventNode);
	TestNotNull(TEXT("Timer custom event should exist."), TimerEventGraphNode);
	TestNotNull(TEXT("TimerSeconds getter should exist."), SecondsGetterGraphNode);
	TestNotNull(TEXT("Looping getter should exist."), LoopingGetterGraphNode);
	TestNotNull(TEXT("SetTimerDelegate call should exist."), SetTimerGraphNode);
	TestNotNull(TEXT("TimerHandle setter should exist."), SetHandleGraphNode);
	TestNotNull(TEXT("TimerHandle getter should exist."), GetHandleGraphNode);
	TestNotNull(TEXT("ClearAndInvalidateTimerHandle call should exist."), ClearTimerGraphNode);
	if (EventNode == nullptr || TimerEventGraphNode == nullptr || SecondsGetterGraphNode == nullptr || LoopingGetterGraphNode == nullptr || SetTimerGraphNode == nullptr || SetHandleGraphNode == nullptr || GetHandleGraphNode == nullptr || ClearTimerGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const TimerDelegateGraphPin = TimerEventGraphNode->FindPin(UK2Node_Event::DelegateOutputName);
	UEdGraphPin* const SecondsGraphPin = SecondsGetterGraphNode->FindPin(TEXT("TimerSeconds"));
	UEdGraphPin* const LoopingGraphPin = LoopingGetterGraphNode->FindPin(TEXT("Looping"));
	UEdGraphPin* const SetTimerExecGraphPin = SetTimerGraphNode->GetExecPin();
	UEdGraphPin* const SetTimerDelegateGraphInput = SetTimerGraphNode->FindPin(TEXT("Delegate"));
	UEdGraphPin* const SetTimerTimeGraphInput = SetTimerGraphNode->FindPin(TEXT("Time"));
	UEdGraphPin* const SetTimerLoopingGraphInput = SetTimerGraphNode->FindPin(TEXT("bLooping"));
	UEdGraphPin* const SetTimerThenGraphPin = SetTimerGraphNode->GetThenPin();
	UEdGraphPin* const SetTimerReturnGraphPin = SetTimerGraphNode->GetReturnValuePin();
	UEdGraphPin* const SetHandleExecGraphPin = SetHandleGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetHandleThenGraphPin = SetHandleGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetHandleValueGraphPin = SetHandleGraphNode->FindPin(TEXT("TimerHandleVar"));
	UEdGraphPin* const GetHandleGraphValuePin = GetHandleGraphNode->FindPin(TEXT("TimerHandleVar"));
	UEdGraphPin* const ClearTimerExecGraphPin = ClearTimerGraphNode->GetExecPin();
	UEdGraphPin* const ClearTimerHandleGraphPin = ClearTimerGraphNode->FindPin(TEXT("Handle"));

	TestTrue(TEXT("Timer event should expose OutputDelegate."), TimerDelegateGraphPin != nullptr);
	TestTrue(TEXT("BeginPlay should feed SetTimerDelegate execute."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(SetTimerExecGraphPin));
	TestTrue(TEXT("Custom event delegate should feed SetTimerDelegate delegate input."), TimerDelegateGraphPin != nullptr && TimerDelegateGraphPin->LinkedTo.Contains(SetTimerDelegateGraphInput));
	TestTrue(TEXT("TimerSeconds getter should feed SetTimerDelegate Time."), SecondsGraphPin != nullptr && SecondsGraphPin->LinkedTo.Contains(SetTimerTimeGraphInput));
	TestTrue(TEXT("Looping getter should feed SetTimerDelegate bLooping."), LoopingGraphPin != nullptr && LoopingGraphPin->LinkedTo.Contains(SetTimerLoopingGraphInput));
	TestTrue(TEXT("SetTimerDelegate return value should feed TimerHandle setter."), SetTimerReturnGraphPin != nullptr && SetTimerReturnGraphPin->LinkedTo.Contains(SetHandleValueGraphPin));
	TestTrue(TEXT("SetTimerDelegate Then should feed TimerHandle setter execute."), SetTimerThenGraphPin != nullptr && SetTimerThenGraphPin->LinkedTo.Contains(SetHandleExecGraphPin));
	TestTrue(TEXT("TimerHandle setter Then should feed ClearAndInvalidateTimerHandle execute."), SetHandleThenGraphPin != nullptr && SetHandleThenGraphPin->LinkedTo.Contains(ClearTimerExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed ClearAndInvalidateTimerHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(ClearTimerHandleGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilDispatcherAuthoringExecutionTest,
	"Vergil.Scaffold.DispatcherAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilDispatcherAuthoringExecutionTest::RunTest(const FString& Parameters)
{
	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilDispatcherAuthoringExecution");

	FVergilDispatcherDefinition Dispatcher;
	Dispatcher.Name = TEXT("OnVergilMessage");

	FVergilDispatcherParameter MessageParameter;
	MessageParameter.Name = TEXT("Message");
	MessageParameter.PinCategory = TEXT("string");
	Dispatcher.Parameters.Add(MessageParameter);

	Document.Dispatchers.Add(Dispatcher);

	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem should be available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
	TestTrue(TEXT("Dispatcher authoring should succeed."), Result.bSucceeded);
	TestTrue(TEXT("Dispatcher authoring should apply commands."), Result.bApplied);
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	bool bFoundDispatcherVariable = false;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Variable.VarName == Dispatcher.Name && Variable.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			bFoundDispatcherVariable = true;
			break;
		}
	}

	UEdGraph* const DispatcherGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(Blueprint, Dispatcher.Name);
	UK2Node_FunctionEntry* EntryNode = nullptr;
	if (DispatcherGraph != nullptr)
	{
		for (UEdGraphNode* Node : DispatcherGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node))
			{
				EntryNode = Candidate;
				break;
			}
		}
	}

	TestTrue(TEXT("Dispatcher variable should exist on the blueprint."), bFoundDispatcherVariable);
	TestNotNull(TEXT("Dispatcher signature graph should exist."), DispatcherGraph);
	TestNotNull(TEXT("Dispatcher entry node should exist."), EntryNode);
	if (DispatcherGraph == nullptr || EntryNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const MessagePin = EntryNode->FindPin(TEXT("Message"));
	TestNotNull(TEXT("Dispatcher should expose the Message parameter."), MessagePin);
	TestTrue(TEXT("Dispatcher Message pin should be an output string pin."), MessagePin != nullptr
		&& MessagePin->Direction == EGPD_Output
		&& MessagePin->PinType.PinCategory == UEdGraphSchema_K2::PC_String);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCallDelegateExecutionTest,
	"Vergil.Scaffold.CallDelegateExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilCallDelegateExecutionTest::RunTest(const FString& Parameters)
{
	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThenPin;
	BeginPlayThenPin.Id = FGuid::NewGuid();
	BeginPlayThenPin.Name = TEXT("Then");
	BeginPlayThenPin.Direction = EVergilPinDirection::Output;
	BeginPlayThenPin.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThenPin);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(0.0f, 180.0f);

	FVergilGraphPin SelfOutputPin;
	SelfOutputPin.Id = FGuid::NewGuid();
	SelfOutputPin.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutputPin.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutputPin);

	FVergilGraphNode HandlerEventNode;
	HandlerEventNode.Id = FGuid::NewGuid();
	HandlerEventNode.Kind = EVergilNodeKind::Custom;
	HandlerEventNode.Descriptor = TEXT("K2.CustomEvent.HandleSignal");
	HandlerEventNode.Position = FVector2D(0.0f, 360.0f);
	HandlerEventNode.Metadata.Add(TEXT("DelegatePropertyName"), TEXT("OnSignal"));

	FVergilGraphNode CreateDelegateNode;
	CreateDelegateNode.Id = FGuid::NewGuid();
	CreateDelegateNode.Kind = EVergilNodeKind::Custom;
	CreateDelegateNode.Descriptor = TEXT("K2.CreateDelegate.HandleSignal");
	CreateDelegateNode.Position = FVector2D(320.0f, 360.0f);

	FVergilGraphPin CreateDelegateOutputPin;
	CreateDelegateOutputPin.Id = FGuid::NewGuid();
	CreateDelegateOutputPin.Name = TEXT("OutputDelegate");
	CreateDelegateOutputPin.Direction = EVergilPinDirection::Output;
	CreateDelegateNode.Pins.Add(CreateDelegateOutputPin);

	FVergilGraphNode BindNode;
	BindNode.Id = FGuid::NewGuid();
	BindNode.Kind = EVergilNodeKind::Custom;
	BindNode.Descriptor = TEXT("K2.BindDelegate.OnSignal");
	BindNode.Position = FVector2D(320.0f, 0.0f);

	FVergilGraphPin BindExecPin;
	BindExecPin.Id = FGuid::NewGuid();
	BindExecPin.Name = TEXT("Execute");
	BindExecPin.Direction = EVergilPinDirection::Input;
	BindExecPin.bIsExec = true;
	BindNode.Pins.Add(BindExecPin);

	FVergilGraphPin BindThenPin;
	BindThenPin.Id = FGuid::NewGuid();
	BindThenPin.Name = TEXT("Then");
	BindThenPin.Direction = EVergilPinDirection::Output;
	BindThenPin.bIsExec = true;
	BindNode.Pins.Add(BindThenPin);

	FVergilGraphPin BindSelfPin;
	BindSelfPin.Id = FGuid::NewGuid();
	BindSelfPin.Name = UEdGraphSchema_K2::PN_Self;
	BindSelfPin.Direction = EVergilPinDirection::Input;
	BindNode.Pins.Add(BindSelfPin);

	FVergilGraphPin BindDelegatePin;
	BindDelegatePin.Id = FGuid::NewGuid();
	BindDelegatePin.Name = TEXT("Delegate");
	BindDelegatePin.Direction = EVergilPinDirection::Input;
	BindNode.Pins.Add(BindDelegatePin);

	FVergilGraphNode CallNode;
	CallNode.Id = FGuid::NewGuid();
	CallNode.Kind = EVergilNodeKind::Custom;
	CallNode.Descriptor = TEXT("K2.CallDelegate.OnSignal");
	CallNode.Position = FVector2D(640.0f, 0.0f);

	FVergilGraphPin CallExecPin;
	CallExecPin.Id = FGuid::NewGuid();
	CallExecPin.Name = TEXT("Execute");
	CallExecPin.Direction = EVergilPinDirection::Input;
	CallExecPin.bIsExec = true;
	CallNode.Pins.Add(CallExecPin);

	FVergilGraphPin CallThenPin;
	CallThenPin.Id = FGuid::NewGuid();
	CallThenPin.Name = TEXT("Then");
	CallThenPin.Direction = EVergilPinDirection::Output;
	CallThenPin.bIsExec = true;
	CallNode.Pins.Add(CallThenPin);

	FVergilGraphPin CallSelfPin;
	CallSelfPin.Id = FGuid::NewGuid();
	CallSelfPin.Name = UEdGraphSchema_K2::PN_Self;
	CallSelfPin.Direction = EVergilPinDirection::Input;
	CallNode.Pins.Add(CallSelfPin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilCallDelegateExecution");

	FVergilDispatcherDefinition Dispatcher;
	Dispatcher.Name = TEXT("OnSignal");
	Document.Dispatchers.Add(Dispatcher);

	Document.Nodes = { BeginPlayNode, SelfNode, HandlerEventNode, CreateDelegateNode, BindNode, CallNode };

	FVergilGraphEdge BeginPlayToBind;
	BeginPlayToBind.Id = FGuid::NewGuid();
	BeginPlayToBind.SourceNodeId = BeginPlayNode.Id;
	BeginPlayToBind.SourcePinId = BeginPlayThenPin.Id;
	BeginPlayToBind.TargetNodeId = BindNode.Id;
	BeginPlayToBind.TargetPinId = BindExecPin.Id;
	Document.Edges.Add(BeginPlayToBind);

	FVergilGraphEdge CreateDelegateToBind;
	CreateDelegateToBind.Id = FGuid::NewGuid();
	CreateDelegateToBind.SourceNodeId = CreateDelegateNode.Id;
	CreateDelegateToBind.SourcePinId = CreateDelegateOutputPin.Id;
	CreateDelegateToBind.TargetNodeId = BindNode.Id;
	CreateDelegateToBind.TargetPinId = BindDelegatePin.Id;
	Document.Edges.Add(CreateDelegateToBind);

	FVergilGraphEdge SelfToBind;
	SelfToBind.Id = FGuid::NewGuid();
	SelfToBind.SourceNodeId = SelfNode.Id;
	SelfToBind.SourcePinId = SelfOutputPin.Id;
	SelfToBind.TargetNodeId = BindNode.Id;
	SelfToBind.TargetPinId = BindSelfPin.Id;
	Document.Edges.Add(SelfToBind);

	FVergilGraphEdge BindToCall;
	BindToCall.Id = FGuid::NewGuid();
	BindToCall.SourceNodeId = BindNode.Id;
	BindToCall.SourcePinId = BindThenPin.Id;
	BindToCall.TargetNodeId = CallNode.Id;
	BindToCall.TargetPinId = CallExecPin.Id;
	Document.Edges.Add(BindToCall);

	FVergilGraphEdge SelfToCall;
	SelfToCall.Id = FGuid::NewGuid();
	SelfToCall.SourceNodeId = SelfNode.Id;
	SelfToCall.SourcePinId = SelfOutputPin.Id;
	SelfToCall.TargetNodeId = CallNode.Id;
	SelfToCall.TargetPinId = CallSelfPin.Id;
	Document.Edges.Add(SelfToCall);

	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem should be available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
	TestTrue(TEXT("Dispatcher call flow should succeed."), Result.bSucceeded);
	TestTrue(TEXT("Dispatcher call flow should apply commands."), Result.bApplied);
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* EventGraphNode = nullptr;
	UK2Node_Self* SelfGraphNode = nullptr;
	UK2Node_CustomEvent* HandlerGraphNode = nullptr;
	UK2Node_CreateDelegate* CreateDelegateGraphNode = nullptr;
	UK2Node_AddDelegate* BindGraphNode = nullptr;
	UK2Node_CallDelegate* CallGraphNode = nullptr;

	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (EventGraphNode == nullptr && Node != nullptr && Node->NodeGuid == BeginPlayNode.Id)
		{
			EventGraphNode = Cast<UK2Node_Event>(Node);
		}

		if (SelfGraphNode == nullptr && Node != nullptr && Node->NodeGuid == SelfNode.Id)
		{
			SelfGraphNode = Cast<UK2Node_Self>(Node);
		}

		if (HandlerGraphNode == nullptr && Node != nullptr && Node->NodeGuid == HandlerEventNode.Id)
		{
			HandlerGraphNode = Cast<UK2Node_CustomEvent>(Node);
		}

		if (CreateDelegateGraphNode == nullptr && Node != nullptr && Node->NodeGuid == CreateDelegateNode.Id)
		{
			CreateDelegateGraphNode = Cast<UK2Node_CreateDelegate>(Node);
		}

		if (BindGraphNode == nullptr && Node != nullptr && Node->NodeGuid == BindNode.Id)
		{
			BindGraphNode = Cast<UK2Node_AddDelegate>(Node);
		}

		if (CallGraphNode == nullptr && Node != nullptr && Node->NodeGuid == CallNode.Id)
		{
			CallGraphNode = Cast<UK2Node_CallDelegate>(Node);
		}
	}

	TestNotNull(TEXT("BeginPlay node should exist."), EventGraphNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Handler custom event should exist."), HandlerGraphNode);
	TestNotNull(TEXT("CreateDelegate node should exist."), CreateDelegateGraphNode);
	TestNotNull(TEXT("Bind delegate node should exist."), BindGraphNode);
	TestNotNull(TEXT("Call delegate node should exist."), CallGraphNode);
	if (EventGraphNode == nullptr || SelfGraphNode == nullptr || HandlerGraphNode == nullptr || CreateDelegateGraphNode == nullptr || BindGraphNode == nullptr || CallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const CreateDelegateOutGraphPin = CreateDelegateGraphNode->GetDelegateOutPin();
	UEdGraphPin* const BindExecGraphPin = BindGraphNode->GetExecPin();
	UEdGraphPin* const BindThenGraphPin = BindGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const BindSelfGraphPin = BindGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const BindDelegateGraphPin = BindGraphNode->GetDelegatePin();
	UEdGraphPin* const CallExecGraphPin = CallGraphNode->GetExecPin();
	UEdGraphPin* const CallSelfGraphPin = CallGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);

	TestEqual(TEXT("CreateDelegate should finalize to the handler function."), CreateDelegateGraphNode->GetFunctionName(), FName(TEXT("HandleSignal")));
	TestTrue(TEXT("BeginPlay should feed bind delegate execute."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(BindExecGraphPin));
	TestTrue(TEXT("CreateDelegate output should feed bind delegate input."), CreateDelegateOutGraphPin != nullptr && CreateDelegateOutGraphPin->LinkedTo.Contains(BindDelegateGraphPin));
	TestTrue(TEXT("Self should feed bind delegate self pin."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(BindSelfGraphPin));
	TestTrue(TEXT("Bind delegate Then should feed call delegate Execute."), BindThenGraphPin != nullptr && BindThenGraphPin->LinkedTo.Contains(CallExecGraphPin));
	TestTrue(TEXT("Self should feed call delegate self pin."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(CallSelfGraphPin));

	return true;
}

