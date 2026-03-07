#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "HAL/FileManager.h"
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
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackageTools.h"
#include "GameFramework/Pawn.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "VergilAgentSubsystem.h"
#include "VergilCommandTypes.h"
#include "VergilBlueprintCompilerService.h"
#include "VergilCompilerTypes.h"
#include "VergilEditorSubsystem.h"
#include "VergilGraphDocument.h"
#include "VergilNodeRegistry.h"
#include "VergilAutomationTestInterface.h"
#include "VergilVersion.h"

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

	class FTestFailingNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Test.FailLowering");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			Context.AddDiagnostic(
				EVergilDiagnosticSeverity::Error,
				TEXT("IntentionalNodeLoweringFailure"),
				TEXT("Intentional node-lowering failure for automation coverage."),
				Node.Id);
			return false;
		}
	};

	class FTestPinDroppingNodeHandler final : public IVergilNodeHandler
	{
	public:
		virtual FName GetDescriptor() const override
		{
			return TEXT("Test.DropPins");
		}

		virtual bool BuildCommands(const FVergilGraphNode& Node, FVergilCompilerContext& Context) const override
		{
			FVergilCompilerCommand Command;
			Command.Type = EVergilCommandType::AddNode;
			Command.GraphName = Context.GetGraphName();
			Command.NodeId = Node.Id;
			Command.Name = TEXT("HandledWithoutPins");
			Command.Position = Node.Position;
			Context.AddCommand(Command);
			return true;
		}
	};

	UBlueprint* MakeTestBlueprint(UClass* ParentClass)
	{
		if (ParentClass == nullptr)
		{
			ParentClass = AActor::StaticClass();
		}

		const FName BlueprintName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BP_VergilTransient"));
		UPackage* const Package = CreatePackage(*FString::Printf(TEXT("/Temp/%s"), *BlueprintName.ToString()));
		Package->SetFlags(RF_Transient);

		return FKismetEditorUtilities::CreateBlueprint(
			ParentClass,
			Package,
			BlueprintName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			TEXT("VergilAutomation"));
	}

	UBlueprint* MakeTestBlueprint()
	{
		return MakeTestBlueprint(AActor::StaticClass());
	}

	bool ContainsNameValue(const TArray<FName>& Values, const FName ExpectedValue)
	{
		return Values.ContainsByPredicate([ExpectedValue](const FName Value)
		{
			return Value == ExpectedValue;
		});
	}

	bool ContainsStringValue(const TArray<FString>& Values, const FString& ExpectedValue)
	{
		return Values.ContainsByPredicate([&ExpectedValue](const FString& Value)
		{
			return Value == ExpectedValue;
		});
	}

	const FVergilSupportedDescriptorContract* FindSupportedDescriptorContract(
		const TArray<FVergilSupportedDescriptorContract>& Contracts,
		const FString& DescriptorContract)
	{
		return Contracts.FindByPredicate([&DescriptorContract](const FVergilSupportedDescriptorContract& Contract)
		{
			return Contract.DescriptorContract == DescriptorContract;
		});
	}

	struct FScopedPersistentTestBlueprint final
	{
		explicit FScopedPersistentTestBlueprint(const FString& BaseAssetName)
		{
			const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
			AssetName = FString::Printf(TEXT("%s_%s"), *BaseAssetName, *UniqueSuffix);
			PackagePath = FString::Printf(TEXT("/Game/Tests/%s"), *AssetName);
			ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
			verify(FPackageName::TryConvertLongPackageNameToFilename(
				PackagePath,
				PackageFilename,
				FPackageName::GetAssetPackageExtension()));
		}

		~FScopedPersistentTestBlueprint()
		{
			Cleanup();
		}

		UBlueprint* CreateBlueprintAsset() const
		{
			UPackage* const Package = CreatePackage(*PackagePath);
			if (Package == nullptr)
			{
				return nullptr;
			}

			return FKismetEditorUtilities::CreateBlueprint(
				AActor::StaticClass(),
				Package,
				FName(*AssetName),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass(),
				TEXT("VergilAutomation"));
		}

		bool Save(UBlueprint* Blueprint, FString* OutErrorMessage = nullptr) const
		{
			if (Blueprint == nullptr)
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = TEXT("Blueprint was null.");
				}
				return false;
			}

			UPackage* const Package = Blueprint->GetOutermost();
			if (Package == nullptr)
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = TEXT("Blueprint package was null.");
				}
				return false;
			}

			Package->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (!UPackage::SavePackage(Package, Blueprint, *PackageFilename, SaveArgs))
			{
				if (OutErrorMessage != nullptr)
				{
					*OutErrorMessage = FString::Printf(TEXT("Failed to save package '%s'."), *PackageFilename);
				}
				return false;
			}

			return true;
		}

		UBlueprint* Reload(FString* OutErrorMessage = nullptr) const
		{
			if (UPackage* const ExistingPackage = FindPackage(nullptr, *PackagePath))
			{
				const TArray<UPackage*> PackagesToReload = { ExistingPackage };
				FText ReloadError;
				if (!UPackageTools::ReloadPackages(
					PackagesToReload,
					ReloadError,
					UPackageTools::EReloadPackagesInteractionMode::AssumePositive))
				{
					if (OutErrorMessage != nullptr)
					{
						*OutErrorMessage = ReloadError.ToString();
					}
					return nullptr;
				}
			}

			UBlueprint* const ReloadedBlueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
			if (ReloadedBlueprint == nullptr && OutErrorMessage != nullptr)
			{
				*OutErrorMessage = FString::Printf(TEXT("Failed to load blueprint '%s'."), *ObjectPath);
			}

			return ReloadedBlueprint;
		}

		void Cleanup() const
		{
			if (UPackage* const ExistingPackage = FindPackage(nullptr, *PackagePath))
			{
				UPackageTools::FUnloadPackageParams UnloadParams({ ExistingPackage });
				UnloadParams.bUnloadDirtyPackages = true;
				UPackageTools::UnloadPackages(UnloadParams);
			}

			CollectGarbage(RF_NoFlags);

			IFileManager& FileManager = IFileManager::Get();
			FileManager.Delete(*PackageFilename, false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(PackageFilename, TEXT("uexp")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(PackageFilename, TEXT("ubulk")), false, true, true);
			FileManager.Delete(*FPaths::ChangeExtension(PackageFilename, TEXT("uptnl")), false, true, true);
		}

		FString AssetName;
		FString PackagePath;
		FString ObjectPath;
		FString PackageFilename;
	};

	struct FScopedAuditTrailFileBackup final
	{
		explicit FScopedAuditTrailFileBackup(const FString& InFilePath)
			: FilePath(InFilePath)
		{
			IFileManager& FileManager = IFileManager::Get();
			bHadOriginalFile = FileManager.FileExists(*FilePath);
			if (bHadOriginalFile)
			{
				bBackupReady = FFileHelper::LoadFileToString(OriginalFileContents, *FilePath);
			}
			else
			{
				bBackupReady = true;
			}
		}

		~FScopedAuditTrailFileBackup()
		{
			IFileManager& FileManager = IFileManager::Get();
			if (bHadOriginalFile && bBackupReady)
			{
				FFileHelper::SaveStringToFile(OriginalFileContents, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				return;
			}

			if (!bHadOriginalFile)
			{
				FileManager.Delete(*FilePath, false, true, true);
			}
		}

		bool IsReady() const
		{
			return bBackupReady;
		}

		FString FilePath;
		FString OriginalFileContents;
		bool bHadOriginalFile = false;
		bool bBackupReady = false;
	};

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
		else if (GraphName == UEdGraphSchema_K2::FN_UserConstructionScript)
		{
			return FBlueprintEditorUtils::FindUserConstructionScript(Blueprint);
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
	Document.SchemaVersion = Vergil::SchemaVersion;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_Scaffold");

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Empty document is structurally valid."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Empty document has no diagnostics."), Diagnostics.Num(), 0);

	FVergilGraphDocument InvalidDispatcherDocument;
	InvalidDispatcherDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidDispatcherDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidDispatchers");

	FVergilDispatcherDefinition InvalidDispatcher;
	InvalidDispatcher.Name = TEXT("OnBrokenState");

	FVergilDispatcherParameter UnsupportedDispatcherParameter;
	UnsupportedDispatcherParameter.Name = TEXT("Mode");
	UnsupportedDispatcherParameter.PinCategory = TEXT("unsupported");
	InvalidDispatcher.Parameters.Add(UnsupportedDispatcherParameter);

	FVergilDispatcherParameter MissingObjectPathDispatcherParameter;
	MissingObjectPathDispatcherParameter.Name = TEXT("TargetActor");
	MissingObjectPathDispatcherParameter.PinCategory = TEXT("object");
	MissingObjectPathDispatcherParameter.ObjectPath = TEXT("   ");
	InvalidDispatcher.Parameters.Add(MissingObjectPathDispatcherParameter);

	InvalidDispatcherDocument.Dispatchers.Add(InvalidDispatcher);

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid dispatcher definitions should fail structural validation."), InvalidDispatcherDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Dispatcher validation reports unsupported parameter categories."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("DispatcherParameterCategoryUnsupported");
	}));
	TestTrue(TEXT("Dispatcher validation reports missing object paths for typed parameters."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("DispatcherParameterObjectPathMissing");
	}));

	FVergilGraphDocument InvalidBlueprintMetadataDocument;
	InvalidBlueprintMetadataDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidBlueprintMetadataDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidBlueprintMetadata");
	InvalidBlueprintMetadataDocument.Metadata.Add(NAME_None, TEXT("Broken"));
	InvalidBlueprintMetadataDocument.Metadata.Add(TEXT("UnsupportedKey"), TEXT("Value"));

	Diagnostics.Reset();
	TestFalse(TEXT("Unsupported Blueprint metadata definitions should fail structural validation."), InvalidBlueprintMetadataDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Blueprint metadata validation reports empty metadata keys."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("BlueprintMetadataKeyMissing");
	}));
	TestTrue(TEXT("Blueprint metadata validation reports unsupported metadata keys."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("BlueprintMetadataKeyUnsupported");
	}));

	FVergilGraphDocument InvalidDocument;
	InvalidDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidVariables");

	FVergilDispatcherDefinition Dispatcher;
	Dispatcher.Name = TEXT("SharedMember");
	InvalidDocument.Dispatchers.Add(Dispatcher);

	FVergilVariableDefinition ConflictingVariable;
	ConflictingVariable.Name = TEXT("SharedMember");
	ConflictingVariable.Type.PinCategory = TEXT("bool");

	FVergilVariableDefinition MissingTypeVariable;
	MissingTypeVariable.Name = TEXT("BrokenVar");
	MissingTypeVariable.Metadata.Add(NAME_None, TEXT("Broken"));

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
	TestTrue(TEXT("Variable validation reports metadata keys without names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableMetadataKeyMissing");
	}));
	TestTrue(TEXT("Variable validation reports expose-on-spawn inconsistencies."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("VariableExposeOnSpawnRequiresInstanceEditable");
	}));

	FVergilGraphDocument InvalidFunctionDocument;
	InvalidFunctionDocument.SchemaVersion = Vergil::SchemaVersion;
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
	InvalidMacroDocument.SchemaVersion = Vergil::SchemaVersion;
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
	InvalidComponentDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidComponentDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidComponents");

	FVergilFunctionDefinition ExistingFunction;
	ExistingFunction.Name = TEXT("SharedComponent");
	InvalidComponentDocument.Functions.Add(ExistingFunction);

	FVergilComponentDefinition ConflictingComponent;
	ConflictingComponent.Name = TEXT("SharedComponent");
	ConflictingComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();

	FVergilComponentDefinition MissingClassComponent;
	MissingClassComponent.Name = TEXT("MissingClass");
	MissingClassComponent.ComponentClassPath = TEXT("   ");

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
	InvalidInterfaceDocument.SchemaVersion = Vergil::SchemaVersion;
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

	FVergilGraphDocument InvalidClassDefaultDocument;
	InvalidClassDefaultDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidClassDefaultDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidClassDefaults");
	InvalidClassDefaultDocument.ClassDefaults.Add(NAME_None, TEXT("True"));

	Diagnostics.Reset();
	TestFalse(TEXT("Invalid class default definitions should fail structural validation."), InvalidClassDefaultDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Class default validation reports missing property names."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("ClassDefaultPropertyNameMissing");
	}));

	FVergilGraphDocument InvalidConstructionScriptDocument;
	InvalidConstructionScriptDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidConstructionScriptDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidConstructionScript");

	FVergilGraphNode SharedPrimaryNode;
	SharedPrimaryNode.Id = FGuid::NewGuid();
	SharedPrimaryNode.Kind = EVergilNodeKind::Comment;
	SharedPrimaryNode.Descriptor = TEXT("UI.Comment");
	InvalidConstructionScriptDocument.Nodes.Add(SharedPrimaryNode);

	FVergilGraphNode SharedConstructionNode;
	SharedConstructionNode.Id = SharedPrimaryNode.Id;
	SharedConstructionNode.Kind = EVergilNodeKind::Comment;
	SharedConstructionNode.Descriptor = TEXT("UI.Comment");
	InvalidConstructionScriptDocument.ConstructionScriptNodes.Add(SharedConstructionNode);

	Diagnostics.Reset();
	TestFalse(
		TEXT("Construction script definitions should share graph id validation with the primary graph."),
		InvalidConstructionScriptDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Construction script validation reports duplicate node ids across graph surfaces."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("NodeIdDuplicate");
	}));

	FVergilGraphDocument InvalidEdgeOwnershipDocument;
	InvalidEdgeOwnershipDocument.SchemaVersion = Vergil::SchemaVersion;
	InvalidEdgeOwnershipDocument.BlueprintPath = TEXT("/Game/Tests/BP_InvalidEdgeOwnership");

	FVergilGraphNode SourceNode;
	SourceNode.Id = FGuid::NewGuid();
	SourceNode.Kind = EVergilNodeKind::Event;
	SourceNode.Descriptor = TEXT("K2.Event.BeginPlay");

	FVergilGraphPin SourceExecPin;
	SourceExecPin.Id = FGuid::NewGuid();
	SourceExecPin.Name = TEXT("Then");
	SourceExecPin.Direction = EVergilPinDirection::Output;
	SourceExecPin.bIsExec = true;
	SourceNode.Pins.Add(SourceExecPin);

	FVergilGraphNode IntermediateNode;
	IntermediateNode.Id = FGuid::NewGuid();
	IntermediateNode.Kind = EVergilNodeKind::Comment;
	IntermediateNode.Descriptor = TEXT("UI.Comment");

	FVergilGraphPin IntermediatePin;
	IntermediatePin.Id = FGuid::NewGuid();
	IntermediatePin.Name = TEXT("CommentPin");
	IntermediatePin.Direction = EVergilPinDirection::Output;
	IntermediateNode.Pins.Add(IntermediatePin);

	FVergilGraphNode TargetNode;
	TargetNode.Id = FGuid::NewGuid();
	TargetNode.Kind = EVergilNodeKind::Call;
	TargetNode.Descriptor = TEXT("K2.Call.PrintString");

	FVergilGraphPin TargetExecPin;
	TargetExecPin.Id = FGuid::NewGuid();
	TargetExecPin.Name = TEXT("Execute");
	TargetExecPin.Direction = EVergilPinDirection::Input;
	TargetExecPin.bIsExec = true;
	TargetNode.Pins.Add(TargetExecPin);

	FVergilGraphEdge InvalidOwnershipEdge;
	InvalidOwnershipEdge.Id = FGuid::NewGuid();
	InvalidOwnershipEdge.SourceNodeId = SourceNode.Id;
	InvalidOwnershipEdge.SourcePinId = IntermediatePin.Id;
	InvalidOwnershipEdge.TargetNodeId = TargetNode.Id;
	InvalidOwnershipEdge.TargetPinId = TargetExecPin.Id;

	InvalidEdgeOwnershipDocument.Nodes = { SourceNode, IntermediateNode, TargetNode };
	InvalidEdgeOwnershipDocument.Edges.Add(InvalidOwnershipEdge);

	Diagnostics.Reset();
	TestFalse(TEXT("Graph edges should fail validation when their pins do not belong to their declared nodes."), InvalidEdgeOwnershipDocument.IsStructurallyValid(&Diagnostics));
	TestTrue(TEXT("Graph validation reports pin ownership mismatches for edges."), Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("EdgePinNodeMismatch");
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilBlueprintMetadataModelTest,
	"Vergil.Scaffold.BlueprintMetadataModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilFunctionDefinitionModelTest,
	"Vergil.Scaffold.FunctionDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilBlueprintMetadataModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_BlueprintMetadataModel");
	Document.Metadata.Add(TEXT("BlueprintDisplayName"), TEXT("Vergil Asset"));
	Document.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Blueprint-level metadata test."));
	Document.Metadata.Add(TEXT("BlueprintCategory"), TEXT("Vergil|Scaffold"));
	Document.Metadata.Add(TEXT("HideCategories"), TEXT("Rendering, Actor"));

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid Blueprint metadata definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid Blueprint metadata definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored Blueprint metadata entries."), Document.Metadata.Num(), 4);
	TestEqual(TEXT("Blueprint display name metadata should retain its authored value."), Document.Metadata.FindRef(TEXT("BlueprintDisplayName")), FString(TEXT("Vergil Asset")));
	TestEqual(TEXT("Blueprint description metadata should retain its authored value."), Document.Metadata.FindRef(TEXT("BlueprintDescription")), FString(TEXT("Blueprint-level metadata test.")));
	TestEqual(TEXT("Blueprint category metadata should retain its authored value."), Document.Metadata.FindRef(TEXT("BlueprintCategory")), FString(TEXT("Vergil|Scaffold")));
	TestEqual(TEXT("Hide categories metadata should retain its authored value."), Document.Metadata.FindRef(TEXT("HideCategories")), FString(TEXT("Rendering, Actor")));

	return true;
}

bool FVergilFunctionDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
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
	Document.SchemaVersion = Vergil::SchemaVersion;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilClassDefaultDefinitionModelTest,
	"Vergil.Scaffold.ClassDefaultDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilConstructionScriptDefinitionModelTest,
	"Vergil.Scaffold.ConstructionScriptDefinitionModel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSchemaMigrationHelpersTest,
	"Vergil.Scaffold.SchemaMigrationHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilComponentDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
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
	Document.SchemaVersion = Vergil::SchemaVersion;
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

bool FVergilClassDefaultDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ClassDefaultModel");
	Document.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));
	Document.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("2.5"));

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid class default definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid class default definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Document should retain authored class defaults."), Document.ClassDefaults.Num(), 2);
	TestEqual(TEXT("Replicates should retain its authored value."), Document.ClassDefaults.FindRef(TEXT("Replicates")), FString(TEXT("True")));
	TestEqual(TEXT("InitialLifeSpan should retain its authored value."), Document.ClassDefaults.FindRef(TEXT("InitialLifeSpan")), FString(TEXT("2.5")));

	return true;
}

bool FVergilConstructionScriptDefinitionModelTest::RunTest(const FString& Parameters)
{
	FVergilGraphNode ConstructionEventNode;
	ConstructionEventNode.Id = FGuid::NewGuid();
	ConstructionEventNode.Kind = EVergilNodeKind::Event;
	ConstructionEventNode.Descriptor = TEXT("K2.Event.UserConstructionScript");
	ConstructionEventNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin ConstructionThenPin;
	ConstructionThenPin.Id = FGuid::NewGuid();
	ConstructionThenPin.Name = TEXT("Then");
	ConstructionThenPin.Direction = EVergilPinDirection::Output;
	ConstructionThenPin.bIsExec = true;
	ConstructionEventNode.Pins.Add(ConstructionThenPin);

	FVergilGraphNode PrintNode;
	PrintNode.Id = FGuid::NewGuid();
	PrintNode.Kind = EVergilNodeKind::Call;
	PrintNode.Descriptor = TEXT("K2.Call.PrintString");
	PrintNode.Position = FVector2D(350.0f, 0.0f);
	PrintNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin PrintExecPin;
	PrintExecPin.Id = FGuid::NewGuid();
	PrintExecPin.Name = TEXT("Execute");
	PrintExecPin.Direction = EVergilPinDirection::Input;
	PrintExecPin.bIsExec = true;
	PrintNode.Pins.Add(PrintExecPin);

	FVergilGraphEdge ConstructionEdge;
	ConstructionEdge.Id = FGuid::NewGuid();
	ConstructionEdge.SourceNodeId = ConstructionEventNode.Id;
	ConstructionEdge.SourcePinId = ConstructionThenPin.Id;
	ConstructionEdge.TargetNodeId = PrintNode.Id;
	ConstructionEdge.TargetPinId = PrintExecPin.Id;

	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ConstructionScriptModel");
	Document.ConstructionScriptNodes = { ConstructionEventNode, PrintNode };
	Document.ConstructionScriptEdges.Add(ConstructionEdge);

	TArray<FVergilDiagnostic> Diagnostics;
	TestTrue(TEXT("Valid construction script definitions should pass structural validation."), Document.IsStructurallyValid(&Diagnostics));
	TestEqual(TEXT("Valid construction script definitions should not emit diagnostics."), Diagnostics.Num(), 0);
	TestEqual(TEXT("Construction script should retain authored nodes."), Document.ConstructionScriptNodes.Num(), 2);
	TestEqual(TEXT("Construction script should retain authored edges."), Document.ConstructionScriptEdges.Num(), 1);
	TestEqual(
		TEXT("Construction script should retain the authored entry descriptor."),
		Document.ConstructionScriptNodes[0].Descriptor,
		FName(TEXT("K2.Event.UserConstructionScript")));

	return true;
}

bool FVergilSchemaMigrationHelpersTest::RunTest(const FString& Parameters)
{
	const int32 LegacySchemaVersion = Vergil::SchemaVersion - 1;
	TestTrue(TEXT("The scaffold should retain at least one older schema for migration coverage."), LegacySchemaVersion > 0);
	if (LegacySchemaVersion <= 0)
	{
		return false;
	}

	FVergilGraphDocument LegacyDocument;
	LegacyDocument.SchemaVersion = LegacySchemaVersion;
	LegacyDocument.BlueprintPath = TEXT("/Game/Tests/BP_LegacySchemaDocument");
	LegacyDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Migrated metadata"));

	FVergilVariableDefinition LegacyVariable;
	LegacyVariable.Name = TEXT("LegacyFlag");
	LegacyVariable.Type.PinCategory = TEXT("bool");
	LegacyVariable.DefaultValue = TEXT("true");
	LegacyDocument.Variables.Add(LegacyVariable);
	LegacyDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));

	TestTrue(TEXT("Forward migration path should exist from the previous schema to the current schema."), Vergil::CanMigrateSchemaVersion(LegacySchemaVersion));
	TestFalse(TEXT("Downgrades should not report a supported schema migration path."), Vergil::CanMigrateSchemaVersion(Vergil::SchemaVersion, LegacySchemaVersion));

	FVergilGraphDocument MigratedDocument;
	TArray<FVergilDiagnostic> MigrationDiagnostics;
	TestTrue(TEXT("Legacy document should migrate to the current schema."), Vergil::MigrateDocumentToCurrentSchema(LegacyDocument, MigratedDocument, &MigrationDiagnostics));
	TestEqual(TEXT("Migrated document should report the current schema version."), MigratedDocument.SchemaVersion, Vergil::SchemaVersion);
	TestEqual(TEXT("Migration should preserve authored variables."), MigratedDocument.Variables.Num(), 1);
	TestEqual(TEXT("Migration should preserve authored class defaults."), MigratedDocument.ClassDefaults.FindRef(TEXT("Replicates")), FString(TEXT("True")));
	TestEqual(TEXT("Migration should preserve authored Blueprint metadata."), MigratedDocument.Metadata.FindRef(TEXT("BlueprintDescription")), FString(TEXT("Migrated metadata")));
	TestTrue(TEXT("Migration should emit an informational applied diagnostic."), MigrationDiagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationApplied") && Diagnostic.Severity == EVergilDiagnosticSeverity::Info;
	}));

	FVergilGraphDocument CurrentDocument = MigratedDocument;
	CurrentDocument.SchemaVersion = Vergil::SchemaVersion;

	FVergilGraphDocument NoOpDocument;
	TArray<FVergilDiagnostic> NoOpDiagnostics;
	TestTrue(TEXT("Migrating the current schema should succeed as a no-op."), Vergil::MigrateDocumentToCurrentSchema(CurrentDocument, NoOpDocument, &NoOpDiagnostics));
	TestEqual(TEXT("No-op migration should preserve the current schema version."), NoOpDocument.SchemaVersion, Vergil::SchemaVersion);
	TestEqual(TEXT("No-op migration should not emit diagnostics."), NoOpDiagnostics.Num(), 0);

	FVergilGraphDocument DowngradedDocument;
	TArray<FVergilDiagnostic> DowngradeDiagnostics;
	TestFalse(TEXT("Schema migration should reject downgrades."), Vergil::MigrateDocumentSchema(CurrentDocument, DowngradedDocument, &DowngradeDiagnostics, LegacySchemaVersion));
	TestTrue(TEXT("Downgrade rejection should report an explicit diagnostic."), DowngradeDiagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationDowngradeUnsupported") && Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	}));

	FVergilGraphDocument MissingPathDocument;
	TArray<FVergilDiagnostic> MissingPathDiagnostics;
	TestFalse(TEXT("Schema migration should reject missing forward paths."), Vergil::MigrateDocumentSchema(LegacyDocument, MissingPathDocument, &MissingPathDiagnostics, Vergil::SchemaVersion + 1));
	TestTrue(TEXT("Missing forward paths should report an explicit diagnostic."), MissingPathDiagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationPathMissing") && Diagnostic.Severity == EVergilDiagnosticSeverity::Error;
	}));

	TArray<FVergilDiagnostic> StructuralDiagnostics;
	TestTrue(TEXT("Migrated document should remain structurally valid."), MigratedDocument.IsStructurallyValid(&StructuralDiagnostics));
	TestEqual(TEXT("Migrated document should not emit structural diagnostics."), StructuralDiagnostics.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCompilerRequiresBlueprintTest,
	"Vergil.Scaffold.CompilerRequiresBlueprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCompilerSchemaMigrationPassTest,
	"Vergil.Scaffold.CompilerSchemaMigrationPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilLegacySchemaExecutionCoverageTest,
	"Vergil.Scaffold.LegacySchemaExecutionCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSemanticValidationPassTest,
	"Vergil.Scaffold.SemanticValidationPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSymbolResolutionPassTest,
	"Vergil.Scaffold.SymbolResolutionPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilTypeResolutionPassTest,
	"Vergil.Scaffold.TypeResolutionPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilNodeLoweringPassTest,
	"Vergil.Scaffold.NodeLoweringPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilConnectionLegalityPassTest,
	"Vergil.Scaffold.ConnectionLegalityPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilPostCompileFinalizePassTest,
	"Vergil.Scaffold.PostCompileFinalizePass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilLayoutCommentPostPassesTest,
	"Vergil.Scaffold.LayoutCommentPostPasses",
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

bool FVergilCompilerSchemaMigrationPassTest::RunTest(const FString& Parameters)
{
	const int32 LegacySchemaVersion = Vergil::SchemaVersion - 1;
	TestTrue(TEXT("The scaffold should retain at least one older schema for compiler migration coverage."), LegacySchemaVersion > 0);
	if (LegacySchemaVersion <= 0)
	{
		return false;
	}

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Compiler migration coverage requires a transient Blueprint."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FVergilGraphDocument LegacyDocument;
	LegacyDocument.SchemaVersion = LegacySchemaVersion;
	LegacyDocument.BlueprintPath = TEXT("/Game/Tests/BP_CompilerSchemaMigration");
	LegacyDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Compiler migration pass coverage."));

	FVergilVariableDefinition LegacyVariable;
	LegacyVariable.Name = TEXT("MigratedFlag");
	LegacyVariable.Type.PinCategory = TEXT("bool");
	LegacyVariable.DefaultValue = TEXT("true");
	LegacyDocument.Variables.Add(LegacyVariable);
	LegacyDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));

	FVergilCompileRequest LegacyRequest;
	LegacyRequest.TargetBlueprint = Blueprint;
	LegacyRequest.Document = LegacyDocument;

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult LegacyResult = CompilerService.Compile(LegacyRequest);

	TestTrue(TEXT("Compile should succeed for a legacy document when a forward migration path exists."), LegacyResult.bSucceeded);
	TestTrue(TEXT("Compile should emit a migration-applied diagnostic for legacy documents."), LegacyResult.Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationApplied") && Diagnostic.Severity == EVergilDiagnosticSeverity::Info;
	}));
	TestFalse(TEXT("Compile should not emit future-schema warnings for migrated legacy documents."), LegacyResult.Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaVersionFuture");
	}));
	TestTrue(TEXT("Compile should still plan the authored legacy variable after migration."), LegacyResult.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::EnsureVariable && Command.SecondaryName == TEXT("MigratedFlag");
	}));
	TestTrue(TEXT("Compile should still plan the authored legacy class default after migration."), LegacyResult.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::SetClassDefault && Command.Name == TEXT("Replicates") && Command.StringValue == TEXT("True");
	}));

	FVergilCompileRequest CurrentRequest = LegacyRequest;
	CurrentRequest.Document.SchemaVersion = Vergil::SchemaVersion;

	const FVergilCompileResult CurrentResult = CompilerService.Compile(CurrentRequest);
	TestTrue(TEXT("Compile should succeed for a current-schema document."), CurrentResult.bSucceeded);
	TestFalse(TEXT("Compile should not emit migration diagnostics for a current-schema document."), CurrentResult.Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationApplied");
	}));

	return true;
}

bool FVergilLegacySchemaExecutionCoverageTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem should be available for legacy schema execution coverage."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	TArray<int32> SupportedLegacySchemaVersions;
	for (int32 Version = 1; Version < Vergil::SchemaVersion; ++Version)
	{
		if (Vergil::CanMigrateSchemaVersion(Version))
		{
			SupportedLegacySchemaVersions.Add(Version);
		}
	}

	TestTrue(TEXT("Release-hardening migration coverage should keep at least one supported legacy schema."), SupportedLegacySchemaVersions.Num() > 0);
	if (SupportedLegacySchemaVersions.Num() == 0)
	{
		return false;
	}

	for (const int32 LegacySchemaVersion : SupportedLegacySchemaVersions)
	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(*FString::Printf(TEXT("Schema %d execution coverage requires a transient Blueprint."), LegacySchemaVersion), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		const FString ExpectedDescription = FString::Printf(
			TEXT("Legacy schema %d migrated through the current pipeline."),
			LegacySchemaVersion);
		const FName VariableName(*FString::Printf(TEXT("LegacyFlagV%d"), LegacySchemaVersion));
		const FName FunctionName(*FString::Printf(TEXT("ComputeLegacyStatusV%d"), LegacySchemaVersion));
		const FName MacroName(*FString::Printf(TEXT("LegacyRouteV%d"), LegacySchemaVersion));

		FVergilFunctionDefinition LegacyFunction;
		LegacyFunction.Name = FunctionName;
		LegacyFunction.bPure = true;

		FVergilFunctionParameterDefinition ThresholdInput;
		ThresholdInput.Name = TEXT("Threshold");
		ThresholdInput.Type.PinCategory = TEXT("float");
		LegacyFunction.Inputs.Add(ThresholdInput);

		FVergilFunctionParameterDefinition ResultOutput;
		ResultOutput.Name = TEXT("Result");
		ResultOutput.Type.PinCategory = TEXT("bool");
		LegacyFunction.Outputs.Add(ResultOutput);

		FVergilMacroDefinition LegacyMacro;
		LegacyMacro.Name = MacroName;

		FVergilMacroParameterDefinition ExecuteInput;
		ExecuteInput.Name = TEXT("Execute");
		ExecuteInput.bIsExec = true;
		LegacyMacro.Inputs.Add(ExecuteInput);

		FVergilMacroParameterDefinition ThenOutput;
		ThenOutput.Name = TEXT("Then");
		ThenOutput.bIsExec = true;
		LegacyMacro.Outputs.Add(ThenOutput);

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

		FVergilGraphNode SequenceNode;
		SequenceNode.Id = FGuid::NewGuid();
		SequenceNode.Kind = EVergilNodeKind::Custom;
		SequenceNode.Descriptor = TEXT("K2.Sequence");
		SequenceNode.Position = FVector2D(320.0f, 0.0f);

		FVergilGraphPin SequenceExecPin;
		SequenceExecPin.Id = FGuid::NewGuid();
		SequenceExecPin.Name = TEXT("Execute");
		SequenceExecPin.Direction = EVergilPinDirection::Input;
		SequenceExecPin.bIsExec = true;
		SequenceNode.Pins.Add(SequenceExecPin);

		FVergilGraphEdge EventToSequenceEdge;
		EventToSequenceEdge.Id = FGuid::NewGuid();
		EventToSequenceEdge.SourceNodeId = BeginPlayNode.Id;
		EventToSequenceEdge.SourcePinId = BeginPlayThenPin.Id;
		EventToSequenceEdge.TargetNodeId = SequenceNode.Id;
		EventToSequenceEdge.TargetPinId = SequenceExecPin.Id;

		FVergilVariableDefinition LegacyVariable;
		LegacyVariable.Name = VariableName;
		LegacyVariable.Type.PinCategory = TEXT("bool");
		LegacyVariable.DefaultValue = TEXT("true");

		FVergilGraphDocument LegacyDocument;
		LegacyDocument.SchemaVersion = LegacySchemaVersion;
		LegacyDocument.BlueprintPath = FString::Printf(TEXT("/Game/Tests/BP_LegacySchemaExecution_%d"), LegacySchemaVersion);
		LegacyDocument.Metadata.Add(TEXT("BlueprintDescription"), ExpectedDescription);
		LegacyDocument.Variables.Add(LegacyVariable);
		LegacyDocument.Functions.Add(LegacyFunction);
		LegacyDocument.Macros.Add(LegacyMacro);
		LegacyDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));
		LegacyDocument.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("3.5"));
		LegacyDocument.Nodes = { BeginPlayNode, SequenceNode };
		LegacyDocument.Edges.Add(EventToSequenceEdge);

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, LegacyDocument, false, false, true);

		TestTrue(
			*FString::Printf(TEXT("Schema %d legacy documents should compile successfully through migration."), LegacySchemaVersion),
			Result.bSucceeded);
		TestTrue(
			*FString::Printf(TEXT("Schema %d legacy documents should apply successfully through migration."), LegacySchemaVersion),
			Result.bApplied);
		TestTrue(
			*FString::Printf(TEXT("Schema %d legacy documents should execute at least one command."), LegacySchemaVersion),
			Result.ExecutedCommandCount > 0);
		if (!Result.bSucceeded || !Result.bApplied)
		{
			return false;
		}

		TestEqual(
			*FString::Printf(TEXT("Schema %d execution should preserve the requested schema version."), LegacySchemaVersion),
			Result.Statistics.RequestedSchemaVersion,
			LegacySchemaVersion);
		TestEqual(
			*FString::Printf(TEXT("Schema %d execution should report the current effective schema version."), LegacySchemaVersion),
			Result.Statistics.EffectiveSchemaVersion,
			Vergil::SchemaVersion);
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should record that apply was requested."), LegacySchemaVersion),
			Result.Statistics.bApplyRequested);
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should record that execution was attempted."), LegacySchemaVersion),
			Result.Statistics.bExecutionAttempted);
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should use the returned normalized command plan."), LegacySchemaVersion),
			Result.Statistics.bExecutionUsedReturnedCommandPlan);
		TestEqual(
			*FString::Printf(TEXT("Schema %d execution should plan exactly once."), LegacySchemaVersion),
			Result.Statistics.PlanningInvocationCount,
			1);
		TestEqual(
			*FString::Printf(TEXT("Schema %d execution should apply exactly once."), LegacySchemaVersion),
			Result.Statistics.ApplyInvocationCount,
			1);
		TestFalse(
			*FString::Printf(TEXT("Schema %d execution should retain a command-plan fingerprint."), LegacySchemaVersion),
			Result.Statistics.CommandPlanFingerprint.IsEmpty());
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should emit SchemaMigrationApplied."), LegacySchemaVersion),
			ContainsDiagnostic(Result.Diagnostics, TEXT("SchemaMigrationApplied")));
		TestFalse(
			*FString::Printf(TEXT("Schema %d execution should not emit future-schema warnings."), LegacySchemaVersion),
			ContainsDiagnostic(Result.Diagnostics, TEXT("SchemaVersionFuture")));

		if (Result.PassRecords.Num() > 0)
		{
			TestEqual(
				*FString::Printf(TEXT("Schema %d execution should still run the schema-migration compiler pass first."), LegacySchemaVersion),
				Result.PassRecords[0].PassName,
				FName(TEXT("SchemaMigration")));
		}

		TestEqual(
			*FString::Printf(TEXT("Schema %d execution should apply Blueprint metadata after migration."), LegacySchemaVersion),
			Blueprint->BlueprintDescription,
			ExpectedDescription);

		const FBPVariableDescription* const VariableDescription = FindBlueprintVariableDescription(Blueprint, VariableName);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should retain the authored variable."), LegacySchemaVersion),
			VariableDescription);
		if (VariableDescription == nullptr)
		{
			return false;
		}

		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated bool variable type."), LegacySchemaVersion),
			VariableDescription->VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean);

		AActor* const BlueprintCDO = Blueprint->GeneratedClass != nullptr ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should produce a generated class default object."), LegacySchemaVersion),
			BlueprintCDO);
		if (BlueprintCDO == nullptr || Blueprint->GeneratedClass == nullptr)
		{
			return false;
		}

		const FBoolProperty* const LegacyFlagProperty = FindFProperty<FBoolProperty>(Blueprint->GeneratedClass, VariableName);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should generate the migrated bool property."), LegacySchemaVersion),
			LegacyFlagProperty);
		if (LegacyFlagProperty == nullptr)
		{
			return false;
		}

		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated variable default value."), LegacySchemaVersion),
			LegacyFlagProperty->GetPropertyValue_InContainer(BlueprintCDO));
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated Replicates class default."), LegacySchemaVersion),
			BlueprintCDO->GetIsReplicated());
		TestTrue(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated InitialLifeSpan class default."), LegacySchemaVersion),
			FMath::IsNearlyEqual(BlueprintCDO->InitialLifeSpan, 3.5f));

		UEdGraph* const FunctionGraph = FindBlueprintGraphByName(Blueprint, FunctionName);
		UK2Node_FunctionEntry* const FunctionEntry = FindFunctionEntryNode(FunctionGraph);
		UK2Node_FunctionResult* const FunctionResult = FindFunctionResultNode(FunctionGraph);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated function graph."), LegacySchemaVersion),
			FunctionGraph);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated function entry node."), LegacySchemaVersion),
			FunctionEntry);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated function result node."), LegacySchemaVersion),
			FunctionResult);
		if (FunctionEntry == nullptr || FunctionResult == nullptr)
		{
			return false;
		}

		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated function input pin."), LegacySchemaVersion),
			FunctionEntry->FindPin(TEXT("Threshold")));
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated function output pin."), LegacySchemaVersion),
			FunctionResult->FindPin(TEXT("Result")));

		UEdGraph* const MacroGraph = FindBlueprintGraphByName(Blueprint, MacroName);
		UK2Node_EditablePinBase* const MacroEntry = FindEditableGraphEntryNode(MacroGraph);
		UK2Node_EditablePinBase* const MacroExit = FindEditableGraphResultNode(MacroGraph);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated macro graph."), LegacySchemaVersion),
			MacroGraph);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated macro entry tunnel."), LegacySchemaVersion),
			MacroEntry);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated macro exit tunnel."), LegacySchemaVersion),
			MacroExit);
		if (MacroEntry == nullptr || MacroExit == nullptr)
		{
			return false;
		}

		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated macro exec input pin."), LegacySchemaVersion),
			MacroEntry->FindPin(TEXT("Execute")));
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should preserve the migrated macro exec output pin."), LegacySchemaVersion),
			MacroExit->FindPin(TEXT("Then")));

		UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
		UK2Node_ExecutionSequence* const SequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, SequenceNode.Id);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should retain the migrated event graph."), LegacySchemaVersion),
			EventGraph);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated BeginPlay event node."), LegacySchemaVersion),
			EventNode);
		TestNotNull(
			*FString::Printf(TEXT("Schema %d execution should create the migrated sequence node."), LegacySchemaVersion),
			SequenceGraphNode);
	}

	return true;
}

bool FVergilSemanticValidationPassTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	{
		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.TargetGraphName = TEXT("UnsupportedGraph");
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_UnsupportedGraph");

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Unsupported compile target graphs should fail semantic validation."), Result.bSucceeded);
		TestEqual(TEXT("Unsupported compile target graphs should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Unsupported compile target graphs should report an explicit diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedCompileTargetGraph")));
	}

	{
		FVergilGraphNode MismatchedCallNode;
		MismatchedCallNode.Id = FGuid::NewGuid();
		MismatchedCallNode.Kind = EVergilNodeKind::Custom;
		MismatchedCallNode.Descriptor = TEXT("K2.Call.PrintString");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_CallKindMismatch");
		Request.Document.Nodes.Add(MismatchedCallNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Descriptor/kind mismatches should fail semantic validation."), Result.bSucceeded);
		TestEqual(TEXT("Descriptor/kind mismatches should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Descriptor/kind mismatches should report the call-kind diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("CallNodeKindInvalid")));
	}

	{
		FVergilGraphNode CastNode;
		CastNode.Id = FGuid::NewGuid();
		CastNode.Kind = EVergilNodeKind::Custom;
		CastNode.Descriptor = TEXT("K2.Cast");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_CastMetadata");
		Request.Document.Nodes.Add(CastNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Missing required node metadata should fail semantic validation."), Result.bSucceeded);
		TestEqual(TEXT("Missing required node metadata should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing required node metadata should report the existing cast diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("MissingTargetClassPath")));
	}

	{
		FVergilGraphNode SpawnActorNode;
		SpawnActorNode.Id = FGuid::NewGuid();
		SpawnActorNode.Kind = EVergilNodeKind::Custom;
		SpawnActorNode.Descriptor = TEXT("K2.SpawnActor");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_SpawnActorMetadata");
		Request.Document.Nodes.Add(SpawnActorNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Spawn actor nodes without ActorClassPath should fail semantic validation."), Result.bSucceeded);
		TestEqual(TEXT("Missing spawn actor metadata should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing spawn actor metadata should report MissingSpawnActorClassPath."), ContainsDiagnostic(Result.Diagnostics, TEXT("MissingSpawnActorClassPath")));
	}

	{
		FVergilGraphNode UnsupportedSelectNode;
		UnsupportedSelectNode.Id = FGuid::NewGuid();
		UnsupportedSelectNode.Kind = EVergilNodeKind::Custom;
		UnsupportedSelectNode.Descriptor = TEXT("K2.Select");
		UnsupportedSelectNode.Metadata.Add(TEXT("IndexPinCategory"), TEXT("string"));
		UnsupportedSelectNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("object"));
		UnsupportedSelectNode.Metadata.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());
		UnsupportedSelectNode.Metadata.Add(TEXT("NumOptions"), TEXT("2"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_SelectIndexType");
		Request.Document.Nodes.Add(UnsupportedSelectNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Unsupported select index categories should fail semantic validation."), Result.bSucceeded);
		TestEqual(TEXT("Unsupported select index categories should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Unsupported select index categories should report an explicit diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSelectIndexTypeCombination")));
	}

	{
		FVergilGraphNode InvalidConstructionEvent;
		InvalidConstructionEvent.Id = FGuid::NewGuid();
		InvalidConstructionEvent.Kind = EVergilNodeKind::Event;
		InvalidConstructionEvent.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.TargetGraphName = TEXT("UserConstructionScript");
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_ConstructionScriptEvent");
		Request.Document.ConstructionScriptNodes.Add(InvalidConstructionEvent);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Construction-script graphs should reject non-construction event descriptors."), Result.bSucceeded);
		TestEqual(TEXT("Construction-script semantic failures should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Construction-script semantic failures should report an explicit event diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConstructionScriptEventInvalid")));
	}

	{
		FVergilGraphNode SpawnActorNode;
		SpawnActorNode.Id = FGuid::NewGuid();
		SpawnActorNode.Kind = EVergilNodeKind::Custom;
		SpawnActorNode.Descriptor = TEXT("K2.SpawnActor");
		SpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), AActor::StaticClass()->GetPathName());

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.TargetGraphName = TEXT("UserConstructionScript");
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SemanticValidation_ConstructionSpawnActor");
		Request.Document.ConstructionScriptNodes.Add(SpawnActorNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Construction-script graphs should reject spawn actor nodes."), Result.bSucceeded);
		TestEqual(TEXT("Construction-script spawn actor failures should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Construction-script spawn actor failures should report the explicit diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConstructionScriptSpawnActorUnsupported")));
	}

	return true;
}

bool FVergilSymbolResolutionPassTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddNode && Command.NodeId == NodeId;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	{
		FVergilGraphNode DestroyActorNode;
		DestroyActorNode.Id = FGuid::NewGuid();
		DestroyActorNode.Kind = EVergilNodeKind::Call;
		DestroyActorNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_NativeCall");
		Request.Document.Nodes.Add(DestroyActorNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("Inherited native call symbols should resolve during compilation."), Result.bSucceeded);

		const FVergilCompilerCommand* const PlannedCallCommand = FindNodeCommand(Result.Commands, DestroyActorNode.Id);
		TestNotNull(TEXT("Resolved native calls should still lower into an AddNode command."), PlannedCallCommand);
		if (PlannedCallCommand != nullptr)
		{
			TestEqual(TEXT("Native call resolution should normalize the owner class path into the planned command."), PlannedCallCommand->StringValue, AActor::StaticClass()->GetPathName());
		}
	}

	{
		FVergilVariableDefinition FlagVariable;
		FlagVariable.Name = TEXT("TestFlag");
		FlagVariable.Type.PinCategory = TEXT("bool");

		FVergilDispatcherDefinition Dispatcher;
		Dispatcher.Name = TEXT("OnSignal");

		FVergilGraphNode GetterNode;
		GetterNode.Id = FGuid::NewGuid();
		GetterNode.Kind = EVergilNodeKind::VariableGet;
		GetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");

		FVergilGraphNode CustomEventNode;
		CustomEventNode.Id = FGuid::NewGuid();
		CustomEventNode.Kind = EVergilNodeKind::Custom;
		CustomEventNode.Descriptor = TEXT("K2.CustomEvent.HandleSignal");
		CustomEventNode.Metadata.Add(TEXT("DelegatePropertyName"), TEXT("OnSignal"));

		FVergilGraphNode CallDelegateNode;
		CallDelegateNode.Id = FGuid::NewGuid();
		CallDelegateNode.Kind = EVergilNodeKind::Custom;
		CallDelegateNode.Descriptor = TEXT("K2.CallDelegate.OnSignal");

		FVergilGraphNode CreateDelegateNode;
		CreateDelegateNode.Id = FGuid::NewGuid();
		CreateDelegateNode.Kind = EVergilNodeKind::Custom;
		CreateDelegateNode.Descriptor = TEXT("K2.CreateDelegate.HandleSignal");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_DocumentSymbols");
		Request.Document.Variables.Add(FlagVariable);
		Request.Document.Dispatchers.Add(Dispatcher);
		Request.Document.Nodes = { GetterNode, CustomEventNode, CallDelegateNode, CreateDelegateNode };

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("Document-authored variables, dispatchers, and custom events should resolve without explicit owner metadata."), Result.bSucceeded);

		const FVergilCompilerCommand* const PlannedGetterCommand = FindNodeCommand(Result.Commands, GetterNode.Id);
		TestNotNull(TEXT("Resolved variable getters should still lower into AddNode commands."), PlannedGetterCommand);
		if (PlannedGetterCommand != nullptr)
		{
			TestTrue(TEXT("Document-authored self variables should not require an explicit owner path."), PlannedGetterCommand->StringValue.IsEmpty());
		}

		const FVergilCompilerCommand* const PlannedDelegateCommand = FindNodeCommand(Result.Commands, CallDelegateNode.Id);
		TestNotNull(TEXT("Resolved delegate helpers should still lower into AddNode commands."), PlannedDelegateCommand);
		if (PlannedDelegateCommand != nullptr)
		{
			TestTrue(TEXT("Document-authored self dispatchers should not require an explicit owner path."), PlannedDelegateCommand->StringValue.IsEmpty());
		}
	}

	{
		FVergilVariableDefinition FloatVariable;
		FloatVariable.Name = TEXT("FloatValue");
		FloatVariable.Type.PinCategory = TEXT("float");

		FVergilGraphNode GetterNode;
		GetterNode.Id = FGuid::NewGuid();
		GetterNode.Kind = EVergilNodeKind::VariableGet;
		GetterNode.Descriptor = TEXT("K2.VarGet.FloatValue");

		FVergilGraphPin GetterExec;
		GetterExec.Id = FGuid::NewGuid();
		GetterExec.Name = TEXT("Execute");
		GetterExec.Direction = EVergilPinDirection::Input;
		GetterExec.bIsExec = true;
		GetterNode.Pins.Add(GetterExec);

		FVergilGraphPin GetterThen;
		GetterThen.Id = FGuid::NewGuid();
		GetterThen.Name = TEXT("Then");
		GetterThen.Direction = EVergilPinDirection::Output;
		GetterThen.bIsExec = true;
		GetterNode.Pins.Add(GetterThen);

		FVergilGraphPin GetterElse;
		GetterElse.Id = FGuid::NewGuid();
		GetterElse.Name = TEXT("Else");
		GetterElse.Direction = EVergilPinDirection::Output;
		GetterElse.bIsExec = true;
		GetterNode.Pins.Add(GetterElse);

		FVergilGraphPin GetterValue;
		GetterValue.Id = FGuid::NewGuid();
		GetterValue.Name = TEXT("FloatValue");
		GetterValue.Direction = EVergilPinDirection::Output;
		GetterNode.Pins.Add(GetterValue);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_InvalidVariableGetterVariant");
		Request.Document.Variables.Add(FloatVariable);
		Request.Document.Nodes.Add(GetterNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Unsupported impure variable getter types should fail compilation."), Result.bSucceeded);
		TestEqual(TEXT("Unsupported impure variable getter types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Unsupported impure variable getter types should report the dedicated diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedVariableGetVariant")));
	}

	{
		FVergilFunctionDefinition PureFunction;
		PureFunction.Name = TEXT("ComputeLocalResult");
		PureFunction.bPure = true;

		FVergilFunctionParameterDefinition PureInput;
		PureInput.Name = TEXT("Value");
		PureInput.Type.PinCategory = TEXT("bool");
		PureFunction.Inputs.Add(PureInput);

		FVergilFunctionParameterDefinition PureOutput;
		PureOutput.Name = TEXT("Result");
		PureOutput.Type.PinCategory = TEXT("bool");
		PureFunction.Outputs.Add(PureOutput);

		FVergilFunctionDefinition ImpureFunction;
		ImpureFunction.Name = TEXT("ApplyLocalResult");

		FVergilFunctionParameterDefinition ImpureInput;
		ImpureInput.Name = TEXT("Value");
		ImpureInput.Type.PinCategory = TEXT("bool");
		ImpureFunction.Inputs.Add(ImpureInput);

		FVergilGraphNode PureCallNode;
		PureCallNode.Id = FGuid::NewGuid();
		PureCallNode.Kind = EVergilNodeKind::Call;
		PureCallNode.Descriptor = TEXT("K2.Call.ComputeLocalResult");

		FVergilGraphNode ImpureCallNode;
		ImpureCallNode.Id = FGuid::NewGuid();
		ImpureCallNode.Kind = EVergilNodeKind::Call;
		ImpureCallNode.Descriptor = TEXT("K2.Call.ApplyLocalResult");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_SelfCalls");
		Request.Document.Functions = { PureFunction, ImpureFunction };
		Request.Document.Nodes = { PureCallNode, ImpureCallNode };

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("Document-authored pure and impure self calls should resolve during compilation."), Result.bSucceeded);

		const FVergilCompilerCommand* const PlannedPureCallCommand = FindNodeCommand(Result.Commands, PureCallNode.Id);
		TestNotNull(TEXT("Resolved pure self calls should still lower into AddNode commands."), PlannedPureCallCommand);
		if (PlannedPureCallCommand != nullptr)
		{
			TestTrue(TEXT("Document-authored pure self calls should not require an explicit owner path."), PlannedPureCallCommand->StringValue.IsEmpty());
		}

		const FVergilCompilerCommand* const PlannedImpureCallCommand = FindNodeCommand(Result.Commands, ImpureCallNode.Id);
		TestNotNull(TEXT("Resolved impure self calls should still lower into AddNode commands."), PlannedImpureCallCommand);
		if (PlannedImpureCallCommand != nullptr)
		{
			TestTrue(TEXT("Document-authored impure self calls should not require an explicit owner path."), PlannedImpureCallCommand->StringValue.IsEmpty());
		}
	}

	{
		FVergilGraphNode MissingCallNode;
		MissingCallNode.Id = FGuid::NewGuid();
		MissingCallNode.Kind = EVergilNodeKind::Call;
		MissingCallNode.Descriptor = TEXT("K2.Call.DoesNotExist");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_MissingCall");
		Request.Document.Nodes.Add(MissingCallNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Missing callable symbols should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Missing callable symbols should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing callable symbols should report FunctionNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("FunctionNotFound")));
	}

	{
		FVergilGraphNode MissingVariableNode;
		MissingVariableNode.Id = FGuid::NewGuid();
		MissingVariableNode.Kind = EVergilNodeKind::VariableGet;
		MissingVariableNode.Descriptor = TEXT("K2.VarGet.DoesNotExist");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_MissingVariable");
		Request.Document.Nodes.Add(MissingVariableNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Missing variable symbols should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Missing variable symbols should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing variable symbols should report VariablePropertyNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("VariablePropertyNotFound")));
	}

	{
		FVergilGraphNode InvalidCustomEventNode;
		InvalidCustomEventNode.Id = FGuid::NewGuid();
		InvalidCustomEventNode.Kind = EVergilNodeKind::Custom;
		InvalidCustomEventNode.Descriptor = TEXT("K2.CustomEvent.HandleSignal");
		InvalidCustomEventNode.Metadata.Add(TEXT("DelegatePropertyName"), TEXT("MissingSignal"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_MissingDelegate");
		Request.Document.Nodes.Add(InvalidCustomEventNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Missing delegate signature symbols should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Missing delegate signature symbols should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing delegate signature symbols should report DelegatePropertyNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("DelegatePropertyNotFound")));
	}

	{
		FVergilGraphNode MissingCreateDelegateNode;
		MissingCreateDelegateNode.Id = FGuid::NewGuid();
		MissingCreateDelegateNode.Kind = EVergilNodeKind::Custom;
		MissingCreateDelegateNode.Descriptor = TEXT("K2.CreateDelegate.DoesNotExist");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_MissingCreateDelegate");
		Request.Document.Nodes.Add(MissingCreateDelegateNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("CreateDelegate should fail when its target symbol cannot be resolved."), Result.bSucceeded);
		TestEqual(TEXT("Missing CreateDelegate targets should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing CreateDelegate targets should report CreateDelegateFunctionNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("CreateDelegateFunctionNotFound")));
	}

	{
		FVergilGraphNode InvalidForLoopNode;
		InvalidForLoopNode.Id = FGuid::NewGuid();
		InvalidForLoopNode.Kind = EVergilNodeKind::Custom;
		InvalidForLoopNode.Descriptor = TEXT("K2.ForLoop");
		InvalidForLoopNode.Metadata.Add(TEXT("MacroBlueprintPath"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
		InvalidForLoopNode.Metadata.Add(TEXT("MacroGraphName"), TEXT("DoesNotExist"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_InvalidForLoop");
		Request.Document.Nodes.Add(InvalidForLoopNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid macro references should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid macro references should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid macro references should report ForLoopMacroNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("ForLoopMacroNotFound")));
	}

	{
		FVergilGraphNode InvalidDoOnceNode;
		InvalidDoOnceNode.Id = FGuid::NewGuid();
		InvalidDoOnceNode.Kind = EVergilNodeKind::Custom;
		InvalidDoOnceNode.Descriptor = TEXT("K2.DoOnce");
		InvalidDoOnceNode.Metadata.Add(TEXT("MacroBlueprintPath"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
		InvalidDoOnceNode.Metadata.Add(TEXT("MacroGraphName"), TEXT("DoesNotExist"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_InvalidDoOnce");
		Request.Document.Nodes.Add(InvalidDoOnceNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid DoOnce macro references should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid DoOnce macro references should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid DoOnce macro references should report DoOnceMacroNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("DoOnceMacroNotFound")));
	}

	{
		FVergilGraphNode InvalidFlipFlopNode;
		InvalidFlipFlopNode.Id = FGuid::NewGuid();
		InvalidFlipFlopNode.Kind = EVergilNodeKind::Custom;
		InvalidFlipFlopNode.Descriptor = TEXT("K2.FlipFlop");
		InvalidFlipFlopNode.Metadata.Add(TEXT("MacroBlueprintPath"), TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
		InvalidFlipFlopNode.Metadata.Add(TEXT("MacroGraphName"), TEXT("DoesNotExist"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_SymbolResolution_InvalidFlipFlop");
		Request.Document.Nodes.Add(InvalidFlipFlopNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid FlipFlop macro references should fail symbol resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid FlipFlop macro references should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid FlipFlop macro references should report FlipFlopMacroNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("FlipFlopMacroNotFound")));
	}

	return true;
}

bool FVergilTypeResolutionPassTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddNode && Command.NodeId == NodeId;
		});
	};

	auto FindDefinitionCommand = [](
		const TArray<FVergilCompilerCommand>& Commands,
		const EVergilCommandType CommandType,
		const FName SecondaryName) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([CommandType, SecondaryName](const FVergilCompilerCommand& Command)
		{
			return Command.Type == CommandType && Command.SecondaryName == SecondaryName;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;
	UEnum* const MovementModeEnum = LoadObject<UEnum>(nullptr, TEXT("/Script/Engine.EMovementMode"));
	TestNotNull(TEXT("Type resolution coverage requires EMovementMode."), MovementModeEnum);
	if (MovementModeEnum == nullptr)
	{
		return false;
	}

	UScriptStruct* const VectorStruct = TBaseStructure<FVector>::Get();
	TestNotNull(TEXT("Type resolution coverage requires FVector."), VectorStruct);
	UScriptStruct* const TransformStruct = TBaseStructure<FTransform>::Get();
	TestNotNull(TEXT("Type resolution coverage requires FTransform."), TransformStruct);
	if (VectorStruct == nullptr || TransformStruct == nullptr)
	{
		return false;
	}

	{
		FVergilVariableDefinition Variable;
		Variable.Name = TEXT("ActorModeMap");
		Variable.Type.PinCategory = TEXT("OBJECT");
		Variable.Type.ObjectPath = TEXT("   /Script/Engine.Actor   ");
		Variable.Type.ContainerType = EVergilVariableContainerType::Map;
		Variable.Type.ValuePinCategory = TEXT("ENUM");
		Variable.Type.ValueObjectPath = TEXT("   /Script/Engine.EMovementMode   ");

		FVergilFunctionDefinition Function;
		Function.Name = TEXT("ResolveTarget");
		FVergilFunctionParameterDefinition FunctionInput;
		FunctionInput.Name = TEXT("Target");
		FunctionInput.Type.PinCategory = TEXT("OBJECT");
		FunctionInput.Type.ObjectPath = TEXT("   /Script/Engine.Actor   ");
		Function.Inputs.Add(FunctionInput);
		FVergilFunctionParameterDefinition FunctionOutput;
		FunctionOutput.Name = TEXT("TargetClass");
		FunctionOutput.Type.PinCategory = TEXT("CLASS");
		FunctionOutput.Type.ObjectPath = TEXT("   /Script/Engine.Actor   ");
		Function.Outputs.Add(FunctionOutput);

		FVergilDispatcherDefinition Dispatcher;
		Dispatcher.Name = TEXT("OnResolved");
		FVergilDispatcherParameter DispatcherParameter;
		DispatcherParameter.Name = TEXT("ResolvedStruct");
		DispatcherParameter.PinCategory = TEXT("STRUCT");
		DispatcherParameter.ObjectPath = TEXT("   /Script/CoreUObject.Vector   ");
		Dispatcher.Parameters.Add(DispatcherParameter);

		FVergilMacroDefinition Macro;
		Macro.Name = TEXT("ResolveMacro");
		FVergilMacroParameterDefinition MacroInput;
		MacroInput.Name = TEXT("ItemType");
		MacroInput.Type.PinCategory = TEXT("CLASS");
		MacroInput.Type.ObjectPath = TEXT("   /Script/Engine.Actor   ");
		Macro.Inputs.Add(MacroInput);

		FVergilComponentDefinition Component;
		Component.Name = TEXT("ActorComponentA");
		Component.ComponentClassPath = TEXT("   /Script/Engine.ActorComponent   ");

		FVergilInterfaceDefinition Interface;
		Interface.InterfaceClassPath = TEXT("   /Script/CoreUObject.Interface   ");

		FVergilGraphNode CastNode;
		CastNode.Id = FGuid::NewGuid();
		CastNode.Kind = EVergilNodeKind::Custom;
		CastNode.Descriptor = TEXT("K2.Cast");
		CastNode.Metadata.Add(TEXT("TargetClassPath"), TEXT("   /Script/Engine.Actor   "));

		FVergilGraphNode SelectNode;
		SelectNode.Id = FGuid::NewGuid();
		SelectNode.Kind = EVergilNodeKind::Custom;
		SelectNode.Descriptor = TEXT("K2.Select");
		SelectNode.Metadata.Add(TEXT("IndexPinCategory"), TEXT(" ENUM "));
		SelectNode.Metadata.Add(TEXT("IndexObjectPath"), TEXT("   /Script/Engine.EMovementMode   "));
		SelectNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT(" OBJECT "));
		SelectNode.Metadata.Add(TEXT("ValueObjectPath"), TEXT("   /Script/Engine.Actor   "));

		FVergilGraphNode SwitchNode;
		SwitchNode.Id = FGuid::NewGuid();
		SwitchNode.Kind = EVergilNodeKind::ControlFlow;
		SwitchNode.Descriptor = TEXT("K2.SwitchEnum");
		SwitchNode.Metadata.Add(TEXT("EnumPath"), TEXT("   /Script/Engine.EMovementMode   "));

		FVergilGraphNode MakeStructNode;
		MakeStructNode.Id = FGuid::NewGuid();
		MakeStructNode.Kind = EVergilNodeKind::Custom;
		MakeStructNode.Descriptor = TEXT("K2.MakeStruct");
		MakeStructNode.Metadata.Add(TEXT("StructPath"), TEXT("   /Script/CoreUObject.Vector   "));

		FVergilGraphNode BreakStructNode;
		BreakStructNode.Id = FGuid::NewGuid();
		BreakStructNode.Kind = EVergilNodeKind::Custom;
		BreakStructNode.Descriptor = TEXT("K2.BreakStruct");
		BreakStructNode.Metadata.Add(TEXT("StructPath"), TEXT("   /Script/CoreUObject.Vector   "));

		FVergilGraphNode MakeArrayNode;
		MakeArrayNode.Id = FGuid::NewGuid();
		MakeArrayNode.Kind = EVergilNodeKind::Custom;
		MakeArrayNode.Descriptor = TEXT("K2.MakeArray");
		MakeArrayNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT(" CLASS "));
		MakeArrayNode.Metadata.Add(TEXT("ValueObjectPath"), TEXT("   /Script/Engine.Actor   "));

		FVergilGraphNode MakeSetNode;
		MakeSetNode.Id = FGuid::NewGuid();
		MakeSetNode.Kind = EVergilNodeKind::Custom;
		MakeSetNode.Descriptor = TEXT("K2.MakeSet");
		MakeSetNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT(" OBJECT "));
		MakeSetNode.Metadata.Add(TEXT("ValueObjectPath"), TEXT("   /Script/Engine.Actor   "));

		FVergilGraphNode MakeMapNode;
		MakeMapNode.Id = FGuid::NewGuid();
		MakeMapNode.Kind = EVergilNodeKind::Custom;
		MakeMapNode.Descriptor = TEXT("K2.MakeMap");
		MakeMapNode.Metadata.Add(TEXT("KeyPinCategory"), TEXT(" OBJECT "));
		MakeMapNode.Metadata.Add(TEXT("KeyObjectPath"), TEXT("   /Script/Engine.Actor   "));
		MakeMapNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT(" STRUCT "));
		MakeMapNode.Metadata.Add(TEXT("ValueObjectPath"), TEXT("   /Script/CoreUObject.Vector   "));

		FVergilGraphNode MakeTransformNode;
		MakeTransformNode.Id = FGuid::NewGuid();
		MakeTransformNode.Kind = EVergilNodeKind::Custom;
		MakeTransformNode.Descriptor = TEXT("K2.MakeStruct");
		MakeTransformNode.Metadata.Add(TEXT("StructPath"), TEXT("   /Script/CoreUObject.Transform   "));

		FVergilGraphPin MakeTransformResultPin;
		MakeTransformResultPin.Id = FGuid::NewGuid();
		MakeTransformResultPin.Name = TransformStruct->GetFName();
		MakeTransformResultPin.Direction = EVergilPinDirection::Output;
		MakeTransformNode.Pins.Add(MakeTransformResultPin);

		FVergilGraphNode SpawnActorNode;
		SpawnActorNode.Id = FGuid::NewGuid();
		SpawnActorNode.Kind = EVergilNodeKind::Custom;
		SpawnActorNode.Descriptor = TEXT("K2.SpawnActor");
		SpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), TEXT("   /Script/Engine.Actor   "));

		FVergilGraphPin SpawnTransformPin;
		SpawnTransformPin.Id = FGuid::NewGuid();
		SpawnTransformPin.Name = TEXT("SpawnTransform");
		SpawnTransformPin.Direction = EVergilPinDirection::Input;
		SpawnActorNode.Pins.Add(SpawnTransformPin);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_Normalized");
		Request.Document.Variables.Add(Variable);
		Request.Document.Functions.Add(Function);
		Request.Document.Dispatchers.Add(Dispatcher);
		Request.Document.Macros.Add(Macro);
		Request.Document.Components.Add(Component);
		Request.Document.Interfaces.Add(Interface);
		Request.Document.Nodes = { CastNode, SelectNode, SwitchNode, MakeStructNode, BreakStructNode, MakeArrayNode, MakeSetNode, MakeMapNode, MakeTransformNode, SpawnActorNode };

		FVergilGraphEdge TransformToSpawn;
		TransformToSpawn.Id = FGuid::NewGuid();
		TransformToSpawn.SourceNodeId = MakeTransformNode.Id;
		TransformToSpawn.SourcePinId = MakeTransformResultPin.Id;
		TransformToSpawn.TargetNodeId = SpawnActorNode.Id;
		TransformToSpawn.TargetPinId = SpawnTransformPin.Id;
		Request.Document.Edges.Add(TransformToSpawn);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("Supported typed definitions and wildcard node metadata should resolve during compilation."), Result.bSucceeded);

		const FVergilCompilerCommand* const VariableCommand = FindDefinitionCommand(Result.Commands, EVergilCommandType::EnsureVariable, Variable.Name);
		TestNotNull(TEXT("Resolved variables should still lower into EnsureVariable commands."), VariableCommand);
		if (VariableCommand != nullptr)
		{
			TestEqual(TEXT("Variable key type category should normalize to lowercase."), VariableCommand->Attributes.FindRef(TEXT("PinCategory")), FString(TEXT("object")));
			TestEqual(TEXT("Variable key object path should normalize to the resolved class path."), VariableCommand->Attributes.FindRef(TEXT("ObjectPath")), AActor::StaticClass()->GetPathName());
			TestEqual(TEXT("Variable map value type category should normalize to lowercase."), VariableCommand->Attributes.FindRef(TEXT("ValuePinCategory")), FString(TEXT("enum")));
			TestEqual(TEXT("Variable map value object path should normalize to the resolved enum path."), VariableCommand->Attributes.FindRef(TEXT("ValueObjectPath")), MovementModeEnum->GetPathName());
		}

		const FVergilCompilerCommand* const FunctionCommand = FindDefinitionCommand(Result.Commands, EVergilCommandType::EnsureFunctionGraph, Function.Name);
		TestNotNull(TEXT("Resolved functions should still lower into EnsureFunctionGraph commands."), FunctionCommand);
		if (FunctionCommand != nullptr)
		{
			TestEqual(TEXT("Function input type category should normalize to lowercase."), FunctionCommand->Attributes.FindRef(TEXT("Input_0_PinCategory")), FString(TEXT("object")));
			TestEqual(TEXT("Function input type object path should normalize to the resolved class path."), FunctionCommand->Attributes.FindRef(TEXT("Input_0_ObjectPath")), AActor::StaticClass()->GetPathName());
			TestEqual(TEXT("Function output type category should normalize to lowercase."), FunctionCommand->Attributes.FindRef(TEXT("Output_0_PinCategory")), FString(TEXT("class")));
			TestEqual(TEXT("Function output type object path should normalize to the resolved class path."), FunctionCommand->Attributes.FindRef(TEXT("Output_0_ObjectPath")), AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const DispatcherCommand = Result.Commands.FindByPredicate([&Dispatcher](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddDispatcherParameter
				&& Command.SecondaryName == Dispatcher.Name
				&& Command.Name == TEXT("ResolvedStruct");
		});
		TestNotNull(TEXT("Resolved dispatchers should still lower into AddDispatcherParameter commands."), DispatcherCommand);
		if (DispatcherCommand != nullptr)
		{
			TestEqual(TEXT("Dispatcher parameter type category should normalize to lowercase."), DispatcherCommand->Attributes.FindRef(TEXT("PinCategory")), FString(TEXT("struct")));
			TestEqual(TEXT("Dispatcher parameter object path should normalize to the resolved struct path."), DispatcherCommand->Attributes.FindRef(TEXT("ObjectPath")), VectorStruct->GetPathName());
		}

		const FVergilCompilerCommand* const MacroCommand = FindDefinitionCommand(Result.Commands, EVergilCommandType::EnsureMacroGraph, Macro.Name);
		TestNotNull(TEXT("Resolved macros should still lower into EnsureMacroGraph commands."), MacroCommand);
		if (MacroCommand != nullptr)
		{
			TestEqual(TEXT("Macro parameter type category should normalize to lowercase."), MacroCommand->Attributes.FindRef(TEXT("Input_0_PinCategory")), FString(TEXT("class")));
			TestEqual(TEXT("Macro parameter object path should normalize to the resolved class path."), MacroCommand->Attributes.FindRef(TEXT("Input_0_ObjectPath")), AActor::StaticClass()->GetPathName());
		}

		TestTrue(TEXT("Resolved component class paths should normalize in planned commands."), Result.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::EnsureComponent
				&& Command.SecondaryName == TEXT("ActorComponentA")
				&& Command.StringValue == UActorComponent::StaticClass()->GetPathName();
		}));
		TestTrue(TEXT("Resolved interface class paths should normalize in planned commands."), Result.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::EnsureInterface
				&& Command.StringValue == UInterface::StaticClass()->GetPathName();
		}));

		const FVergilCompilerCommand* const PlannedCastCommand = FindNodeCommand(Result.Commands, CastNode.Id);
		TestNotNull(TEXT("Resolved cast nodes should still lower into AddNode commands."), PlannedCastCommand);
		if (PlannedCastCommand != nullptr)
		{
			TestEqual(TEXT("Cast target class paths should normalize before planning."), PlannedCastCommand->StringValue, AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedSelectCommand = FindNodeCommand(Result.Commands, SelectNode.Id);
		TestNotNull(TEXT("Resolved select nodes should still lower into AddNode commands."), PlannedSelectCommand);
		if (PlannedSelectCommand != nullptr)
		{
			TestEqual(TEXT("Select index categories should normalize to lowercase."), PlannedSelectCommand->Attributes.FindRef(TEXT("IndexPinCategory")), FString(TEXT("enum")));
			TestEqual(TEXT("Select index enum paths should normalize to the resolved enum path."), PlannedSelectCommand->Attributes.FindRef(TEXT("IndexObjectPath")), MovementModeEnum->GetPathName());
			TestEqual(TEXT("Select value categories should normalize to lowercase."), PlannedSelectCommand->Attributes.FindRef(TEXT("ValuePinCategory")), FString(TEXT("object")));
			TestEqual(TEXT("Select value object paths should normalize to the resolved class path."), PlannedSelectCommand->Attributes.FindRef(TEXT("ValueObjectPath")), AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedSwitchCommand = FindNodeCommand(Result.Commands, SwitchNode.Id);
		TestNotNull(TEXT("Resolved switch enum nodes should still lower into AddNode commands."), PlannedSwitchCommand);
		if (PlannedSwitchCommand != nullptr)
		{
			TestEqual(TEXT("Switch enum paths should normalize to the resolved enum path."), PlannedSwitchCommand->Attributes.FindRef(TEXT("EnumPath")), MovementModeEnum->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedMakeStructCommand = FindNodeCommand(Result.Commands, MakeStructNode.Id);
		TestNotNull(TEXT("Resolved make struct nodes should still lower into AddNode commands."), PlannedMakeStructCommand);
		if (PlannedMakeStructCommand != nullptr)
		{
			TestEqual(TEXT("Make struct paths should normalize to the resolved struct path."), PlannedMakeStructCommand->Attributes.FindRef(TEXT("StructPath")), VectorStruct->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedMakeArrayCommand = FindNodeCommand(Result.Commands, MakeArrayNode.Id);
		TestNotNull(TEXT("Resolved make array nodes should still lower into AddNode commands."), PlannedMakeArrayCommand);
		if (PlannedMakeArrayCommand != nullptr)
		{
			TestEqual(TEXT("Make array value categories should normalize to lowercase."), PlannedMakeArrayCommand->Attributes.FindRef(TEXT("ValuePinCategory")), FString(TEXT("class")));
			TestEqual(TEXT("Make array value object paths should normalize to the resolved class path."), PlannedMakeArrayCommand->Attributes.FindRef(TEXT("ValueObjectPath")), AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedBreakStructCommand = FindNodeCommand(Result.Commands, BreakStructNode.Id);
		TestNotNull(TEXT("Resolved break struct nodes should still lower into AddNode commands."), PlannedBreakStructCommand);
		if (PlannedBreakStructCommand != nullptr)
		{
			TestEqual(TEXT("Break struct paths should normalize to the resolved struct path."), PlannedBreakStructCommand->Attributes.FindRef(TEXT("StructPath")), VectorStruct->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedMakeSetCommand = FindNodeCommand(Result.Commands, MakeSetNode.Id);
		TestNotNull(TEXT("Resolved make set nodes should still lower into AddNode commands."), PlannedMakeSetCommand);
		if (PlannedMakeSetCommand != nullptr)
		{
			TestEqual(TEXT("Make set value categories should normalize to lowercase."), PlannedMakeSetCommand->Attributes.FindRef(TEXT("ValuePinCategory")), FString(TEXT("object")));
			TestEqual(TEXT("Make set value object paths should normalize to the resolved class path."), PlannedMakeSetCommand->Attributes.FindRef(TEXT("ValueObjectPath")), AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedMakeMapCommand = FindNodeCommand(Result.Commands, MakeMapNode.Id);
		TestNotNull(TEXT("Resolved make map nodes should still lower into AddNode commands."), PlannedMakeMapCommand);
		if (PlannedMakeMapCommand != nullptr)
		{
			TestEqual(TEXT("Make map key categories should normalize to lowercase."), PlannedMakeMapCommand->Attributes.FindRef(TEXT("KeyPinCategory")), FString(TEXT("object")));
			TestEqual(TEXT("Make map key object paths should normalize to the resolved class path."), PlannedMakeMapCommand->Attributes.FindRef(TEXT("KeyObjectPath")), AActor::StaticClass()->GetPathName());
			TestEqual(TEXT("Make map value categories should normalize to lowercase."), PlannedMakeMapCommand->Attributes.FindRef(TEXT("ValuePinCategory")), FString(TEXT("struct")));
			TestEqual(TEXT("Make map value object paths should normalize to the resolved struct path."), PlannedMakeMapCommand->Attributes.FindRef(TEXT("ValueObjectPath")), VectorStruct->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedSpawnActorCommand = FindNodeCommand(Result.Commands, SpawnActorNode.Id);
		TestNotNull(TEXT("Resolved spawn actor nodes should still lower into AddNode commands."), PlannedSpawnActorCommand);
		if (PlannedSpawnActorCommand != nullptr)
		{
			TestEqual(TEXT("Spawn actor class paths should normalize before planning."), PlannedSpawnActorCommand->StringValue, AActor::StaticClass()->GetPathName());
			TestEqual(TEXT("Spawn actor metadata should retain the normalized class path."), PlannedSpawnActorCommand->Attributes.FindRef(TEXT("ActorClassPath")), AActor::StaticClass()->GetPathName());
		}

		const FVergilCompilerCommand* const PlannedMakeTransformCommand = FindNodeCommand(Result.Commands, MakeTransformNode.Id);
		TestNotNull(TEXT("Resolved transform make-struct nodes should still lower into AddNode commands."), PlannedMakeTransformCommand);
		if (PlannedMakeTransformCommand != nullptr)
		{
			TestEqual(TEXT("Transform make-struct paths should normalize to the resolved struct path."), PlannedMakeTransformCommand->Attributes.FindRef(TEXT("StructPath")), TransformStruct->GetPathName());
		}
	}

	{
		FVergilVariableDefinition InvalidVariable;
		InvalidVariable.Name = TEXT("MissingType");
		InvalidVariable.Type.PinCategory = TEXT("object");
		InvalidVariable.Type.ObjectPath = TEXT("/Script/Engine.DoesNotExist");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidVariable");
		Request.Document.Variables.Add(InvalidVariable);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid variable types should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid variable types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid variable types should report VariableTypeInvalid."), ContainsDiagnostic(Result.Diagnostics, TEXT("VariableTypeInvalid")));
	}

	{
		FVergilFunctionDefinition InvalidFunction;
		InvalidFunction.Name = TEXT("InvalidFunction");
		FVergilFunctionParameterDefinition InvalidOutput;
		InvalidOutput.Name = TEXT("Result");
		InvalidOutput.Type.PinCategory = TEXT("struct");
		InvalidOutput.Type.ObjectPath = TEXT("/Script/CoreUObject.DoesNotExist");
		InvalidFunction.Outputs.Add(InvalidOutput);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidFunction");
		Request.Document.Functions.Add(InvalidFunction);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid function signature types should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid function signature types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid function signature types should report FunctionSignatureParameterTypeInvalid."), ContainsDiagnostic(Result.Diagnostics, TEXT("FunctionSignatureParameterTypeInvalid")));
	}

	{
		FVergilDispatcherDefinition InvalidDispatcher;
		InvalidDispatcher.Name = TEXT("InvalidDispatcher");
		FVergilDispatcherParameter InvalidParameter;
		InvalidParameter.Name = TEXT("Target");
		InvalidParameter.PinCategory = TEXT("class");
		InvalidParameter.ObjectPath = TEXT("/Script/Engine.DoesNotExist");
		InvalidDispatcher.Parameters.Add(InvalidParameter);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidDispatcher");
		Request.Document.Dispatchers.Add(InvalidDispatcher);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid dispatcher parameter types should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid dispatcher parameter types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid dispatcher parameter types should report DispatcherParameterTypeInvalid."), ContainsDiagnostic(Result.Diagnostics, TEXT("DispatcherParameterTypeInvalid")));
	}

	{
		FVergilMacroDefinition InvalidMacro;
		InvalidMacro.Name = TEXT("InvalidMacro");
		FVergilMacroParameterDefinition InvalidParameter;
		InvalidParameter.Name = TEXT("Payload");
		InvalidParameter.Type.PinCategory = TEXT("enum");
		InvalidParameter.Type.ObjectPath = TEXT("/Script/Engine.DoesNotExist");
		InvalidMacro.Outputs.Add(InvalidParameter);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidMacro");
		Request.Document.Macros.Add(InvalidMacro);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid macro parameter types should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid macro parameter types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid macro parameter types should report MacroSignatureParameterTypeInvalid."), ContainsDiagnostic(Result.Diagnostics, TEXT("MacroSignatureParameterTypeInvalid")));
	}

	{
		FVergilComponentDefinition InvalidComponent;
		InvalidComponent.Name = TEXT("InvalidComponent");
		InvalidComponent.ComponentClassPath = AActor::StaticClass()->GetPathName();

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidComponent");
		Request.Document.Components.Add(InvalidComponent);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid component classes should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid component classes should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid component classes should report InvalidComponentClass."), ContainsDiagnostic(Result.Diagnostics, TEXT("InvalidComponentClass")));
	}

	{
		FVergilInterfaceDefinition InvalidInterface;
		InvalidInterface.InterfaceClassPath = AActor::StaticClass()->GetPathName();

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidInterface");
		Request.Document.Interfaces.Add(InvalidInterface);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid interface classes should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid interface classes should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid interface classes should report InvalidInterfaceClass."), ContainsDiagnostic(Result.Diagnostics, TEXT("InvalidInterfaceClass")));
	}

	{
		FVergilGraphNode InvalidCastNode;
		InvalidCastNode.Id = FGuid::NewGuid();
		InvalidCastNode.Kind = EVergilNodeKind::Custom;
		InvalidCastNode.Descriptor = TEXT("K2.Cast");
		InvalidCastNode.Metadata.Add(TEXT("TargetClassPath"), TEXT("/Script/Engine.DoesNotExist"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidCast");
		Request.Document.Nodes.Add(InvalidCastNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid cast target classes should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid cast target classes should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid cast target classes should report CastTargetClassNotFound."), ContainsDiagnostic(Result.Diagnostics, TEXT("CastTargetClassNotFound")));
	}

	{
		FVergilGraphNode InvalidSelectNode;
		InvalidSelectNode.Id = FGuid::NewGuid();
		InvalidSelectNode.Kind = EVergilNodeKind::Custom;
		InvalidSelectNode.Descriptor = TEXT("K2.Select");
		InvalidSelectNode.Metadata.Add(TEXT("IndexPinCategory"), TEXT("bool"));
		InvalidSelectNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("unsupported"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidSelect");
		Request.Document.Nodes.Add(InvalidSelectNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Invalid wildcard node categories should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid wildcard node categories should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid wildcard node categories should report InvalidSelectValueType."), ContainsDiagnostic(Result.Diagnostics, TEXT("InvalidSelectValueType")));
	}

	{
		FVergilGraphNode InvalidSpawnActorNode;
		InvalidSpawnActorNode.Id = FGuid::NewGuid();
		InvalidSpawnActorNode.Kind = EVergilNodeKind::Custom;
		InvalidSpawnActorNode.Descriptor = TEXT("K2.SpawnActor");
		InvalidSpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), UActorComponent::StaticClass()->GetPathName());

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_TypeResolution_InvalidSpawnActor");
		Request.Document.Nodes.Add(InvalidSpawnActorNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Non-actor spawn classes should fail type resolution."), Result.bSucceeded);
		TestEqual(TEXT("Invalid spawn actor class types should plan zero commands."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Invalid spawn actor class types should report SpawnActorClassNotActor."), ContainsDiagnostic(Result.Diagnostics, TEXT("SpawnActorClassNotActor")));
	}

	return true;
}

bool FVergilNodeLoweringPassTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code, const FGuid& SourceId = FGuid())
	{
		return Diagnostics.ContainsByPredicate([Code, SourceId](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code
				&& (!SourceId.IsValid() || Diagnostic.SourceId == SourceId);
		});
	};

	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const EVergilCommandType Type, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([Type, NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == Type && Command.NodeId == NodeId;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	{
		FVergilGraphNode HandlerNode;
		HandlerNode.Id = FGuid::NewGuid();
		HandlerNode.Kind = EVergilNodeKind::Custom;
		HandlerNode.Descriptor = TEXT("K2.CustomEvent.HandleSignal");
		HandlerNode.Position = FVector2D(0.0f, 0.0f);

		FVergilGraphNode CreateDelegateNode;
		CreateDelegateNode.Id = FGuid::NewGuid();
		CreateDelegateNode.Kind = EVergilNodeKind::Custom;
		CreateDelegateNode.Descriptor = TEXT("K2.CreateDelegate.HandleSignal");
		CreateDelegateNode.Position = FVector2D(320.0f, 0.0f);
		CreateDelegateNode.Metadata.Add(TEXT("Title"), TEXT("Create Handler Delegate"));

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_NodeLowering_CreateDelegate");
		Request.Document.Nodes = { HandlerNode, CreateDelegateNode };

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("CreateDelegate lowering should succeed after prior semantic and symbol passes."), Result.bSucceeded);

		const FVergilCompilerCommand* const LoweredCreateDelegateNode = FindNodeCommand(Result.Commands, EVergilCommandType::AddNode, CreateDelegateNode.Id);
		TestNotNull(TEXT("Node lowering should still emit an AddNode command for CreateDelegate."), LoweredCreateDelegateNode);
		if (LoweredCreateDelegateNode != nullptr)
		{
			TestEqual(TEXT("CreateDelegate should lower through the dedicated handler descriptor."), LoweredCreateDelegateNode->Name, FName(TEXT("Vergil.K2.CreateDelegate")));
			TestEqual(TEXT("CreateDelegate should preserve the resolved function name."), LoweredCreateDelegateNode->SecondaryName, FName(TEXT("HandleSignal")));
		}
	}

	{
		FVergilNodeRegistry::Get().Reset();
		FVergilNodeRegistry::Get().RegisterHandler(MakeShared<FTestFailingNodeHandler, ESPMode::ThreadSafe>());

		FVergilGraphNode FailingNode;
		FailingNode.Id = FGuid::NewGuid();
		FailingNode.Kind = EVergilNodeKind::Custom;
		FailingNode.Descriptor = TEXT("Test.FailLowering");

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_NodeLowering_Failure");
		Request.Document.Nodes.Add(FailingNode);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Node lowering failures should stop compilation."), Result.bSucceeded);
		TestEqual(TEXT("Node lowering failures should prevent command planning from producing a partial plan."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Handler-reported lowering failures should preserve the specific node-level cause."), ContainsDiagnostic(Result.Diagnostics, TEXT("IntentionalNodeLoweringFailure"), FailingNode.Id));
		TestTrue(TEXT("Node lowering failures should also report the dedicated pass-level diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("NodeLoweringFailed"), FailingNode.Id));

		FVergilNodeRegistry::Get().Reset();
	}

	return true;
}

bool FVergilPostCompileFinalizePassTest::RunTest(const FString& Parameters)
{
	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const EVergilCommandType Type, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([Type, NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == Type && Command.NodeId == NodeId;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	FVergilGraphNode HandlerNode;
	HandlerNode.Id = FGuid::NewGuid();
	HandlerNode.Kind = EVergilNodeKind::Custom;
	HandlerNode.Descriptor = TEXT("K2.CustomEvent.HandleSignal");
	HandlerNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphNode CreateDelegateNode;
	CreateDelegateNode.Id = FGuid::NewGuid();
	CreateDelegateNode.Kind = EVergilNodeKind::Custom;
	CreateDelegateNode.Descriptor = TEXT("K2.CreateDelegate.HandleSignal");
	CreateDelegateNode.Position = FVector2D(320.0f, 0.0f);
	CreateDelegateNode.Metadata.Add(TEXT("Title"), TEXT("Create Handler Delegate"));

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_PostCompileFinalize_CreateDelegate");
	Request.Document.Nodes = { HandlerNode, CreateDelegateNode };

	const FVergilCompileResult Result = CompilerService.Compile(Request);
	TestTrue(TEXT("Post-compile finalize coverage should compile a valid CreateDelegate node."), Result.bSucceeded);

	const FVergilCompilerCommand* const FinalizeCreateDelegateNode = FindNodeCommand(Result.Commands, EVergilCommandType::FinalizeNode, CreateDelegateNode.Id);
	TestNotNull(TEXT("The dedicated post-compile finalize pass should emit a FinalizeNode command for CreateDelegate."), FinalizeCreateDelegateNode);
	if (FinalizeCreateDelegateNode == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("CreateDelegate finalization should target the dedicated finalize payload."), FinalizeCreateDelegateNode->Name, FName(TEXT("Vergil.K2.CreateDelegate")));
	TestEqual(TEXT("CreateDelegate finalization should preserve the selected function name."), FinalizeCreateDelegateNode->SecondaryName, FName(TEXT("HandleSignal")));
	TestEqual(TEXT("CreateDelegate finalization should preserve normalized metadata for later execution."), FinalizeCreateDelegateNode->Attributes.FindRef(TEXT("Title")), FString(TEXT("Create Handler Delegate")));

	const int32 FinalizeCommandIndex = Result.Commands.IndexOfByPredicate([CreateDelegateNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::FinalizeNode && Command.NodeId == CreateDelegateNode.Id;
	});
	const int32 AddNodeCommandIndex = Result.Commands.IndexOfByPredicate([CreateDelegateNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::AddNode && Command.NodeId == CreateDelegateNode.Id;
	});

	TestTrue(TEXT("Post-compile finalize commands should appear after graph-structure AddNode commands in the final normalized plan."), AddNodeCommandIndex >= 0 && FinalizeCommandIndex > AddNodeCommandIndex);
	return AddNodeCommandIndex >= 0 && FinalizeCommandIndex > AddNodeCommandIndex;
}

bool FVergilLayoutCommentPostPassesTest::RunTest(const FString& Parameters)
{
	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const EVergilCommandType Type, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([Type, NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == Type && Command.NodeId == NodeId;
		});
	};
	auto FindMoveCommand = [&FindNodeCommand](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return FindNodeCommand(Commands, EVergilCommandType::MoveNode, NodeId);
	};
	auto FindMetadataCommand = [](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId, const FName MetadataKey) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([NodeId, MetadataKey](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::SetNodeMetadata
				&& Command.NodeId == NodeId
				&& Command.Name == MetadataKey;
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	FVergilGraphNode PrimaryNode;
	PrimaryNode.Id = FGuid::NewGuid();
	PrimaryNode.Kind = EVergilNodeKind::Custom;
	PrimaryNode.Descriptor = TEXT("Test.LayoutAnchor");
	PrimaryNode.Position = FVector2D(160.0f, 48.0f);

	FVergilGraphNode CommentNode;
	CommentNode.Id = FGuid::NewGuid();
	CommentNode.Kind = EVergilNodeKind::Comment;
	CommentNode.Descriptor = TEXT("UI.Comment");
	CommentNode.Position = FVector2D(96.0f, -64.0f);
	CommentNode.Metadata.Add(TEXT("CommentText"), TEXT("Generated by comment post-pass"));
	CommentNode.Metadata.Add(TEXT("CommentWidth"), TEXT("420"));

	FVergilCompileRequest CommentEnabledRequest;
	CommentEnabledRequest.TargetBlueprint = MakeTestBlueprint();
	CommentEnabledRequest.Document.BlueprintPath = TEXT("/Game/Tests/BP_LayoutCommentPostPasses_Enabled");
	CommentEnabledRequest.Document.Nodes = { PrimaryNode, CommentNode };
	CommentEnabledRequest.bGenerateComments = true;
	CommentEnabledRequest.bAutoLayout = true;
	CommentEnabledRequest.AutoLayout.HorizontalSpacing = 256.0f;
	CommentEnabledRequest.AutoLayout.VerticalSpacing = 192.0f;
	CommentEnabledRequest.AutoLayout.CommentPadding = 80.0f;
	CommentEnabledRequest.CommentGeneration.DefaultWidth = 512.0f;
	CommentEnabledRequest.CommentGeneration.DefaultHeight = 144.0f;
	CommentEnabledRequest.CommentGeneration.DefaultFontSize = 24;
	CommentEnabledRequest.CommentGeneration.DefaultColor = FLinearColor(FColor(0x22, 0x88, 0xCC, 0xFF));
	CommentEnabledRequest.CommentGeneration.bShowBubbleWhenZoomed = false;
	CommentEnabledRequest.CommentGeneration.bColorBubble = true;
	CommentEnabledRequest.CommentGeneration.MoveMode = EVergilCommentMoveMode::NoGroupMovement;

	const FVergilCompileResult CommentEnabledResult = CompilerService.Compile(CommentEnabledRequest);
	TestTrue(TEXT("Comment/layout post-pass coverage should compile successfully when comments are enabled."), CommentEnabledResult.bSucceeded);

	const FVergilCompilerCommand* const PrimaryAddNode = FindNodeCommand(CommentEnabledResult.Commands, EVergilCommandType::AddNode, PrimaryNode.Id);
	const FVergilCompilerCommand* const CommentAddNode = FindNodeCommand(CommentEnabledResult.Commands, EVergilCommandType::AddNode, CommentNode.Id);
	const FVergilCompilerCommand* const PrimaryMoveNode = FindMoveCommand(CommentEnabledResult.Commands, PrimaryNode.Id);
	const FVergilCompilerCommand* const CommentMoveNode = FindMoveCommand(CommentEnabledResult.Commands, CommentNode.Id);
	const FVergilCompilerCommand* const CommentWidthMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("CommentWidth"));
	const FVergilCompilerCommand* const CommentHeightMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("CommentHeight"));
	const FVergilCompilerCommand* const CommentFontSizeMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("FontSize"));
	const FVergilCompilerCommand* const CommentColorMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("CommentColor"));
	const FVergilCompilerCommand* const CommentBubbleMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("ShowBubbleWhenZoomed"));
	const FVergilCompilerCommand* const CommentColorBubbleMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("ColorBubble"));
	const FVergilCompilerCommand* const CommentMoveModeMetadata = FindMetadataCommand(CommentEnabledResult.Commands, CommentNode.Id, TEXT("MoveMode"));
	TestNotNull(TEXT("Core node lowering should still emit the primary AddNode command."), PrimaryAddNode);
	TestNotNull(TEXT("The explicit comment post-pass should emit the comment AddNode command when comments are enabled."), CommentAddNode);
	TestNotNull(TEXT("Auto-layout should emit a MoveNode command for the primary node when the deterministic layout differs from the authored position."), PrimaryMoveNode);
	TestNotNull(TEXT("Auto-layout should emit a MoveNode command for comment nodes when comments are enabled."), CommentMoveNode);
	TestNotNull(TEXT("Explicit comment width metadata should still be preserved."), CommentWidthMetadata);
	TestNotNull(TEXT("Missing comment height metadata should be synthesized from the explicit comment-generation settings."), CommentHeightMetadata);
	TestNotNull(TEXT("Missing comment font size metadata should be synthesized from the explicit comment-generation settings."), CommentFontSizeMetadata);
	TestNotNull(TEXT("Missing comment color metadata should be synthesized from the explicit comment-generation settings."), CommentColorMetadata);
	TestNotNull(TEXT("Missing comment bubble metadata should be synthesized from the explicit comment-generation settings."), CommentBubbleMetadata);
	TestNotNull(TEXT("Missing comment bubble-color metadata should be synthesized from the explicit comment-generation settings."), CommentColorBubbleMetadata);
	TestNotNull(TEXT("Missing comment move-mode metadata should be synthesized from the explicit comment-generation settings."), CommentMoveModeMetadata);
	if (PrimaryAddNode == nullptr
		|| CommentAddNode == nullptr
		|| PrimaryMoveNode == nullptr
		|| CommentMoveNode == nullptr
		|| CommentWidthMetadata == nullptr
		|| CommentHeightMetadata == nullptr
		|| CommentFontSizeMetadata == nullptr
		|| CommentColorMetadata == nullptr
		|| CommentBubbleMetadata == nullptr
		|| CommentColorBubbleMetadata == nullptr
		|| CommentMoveModeMetadata == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Primary auto-layout should anchor the first primary node at the requested origin."), PrimaryMoveNode->Position, FVector2D::ZeroVector);
	TestEqual(TEXT("Comment auto-layout should place comments to the left of the primary layout band using the requested padding."), CommentMoveNode->Position, FVector2D(-500.0f, 0.0f));
	TestEqual(TEXT("Explicit authored comment width metadata should win over request defaults."), CommentWidthMetadata->StringValue, FString(TEXT("420")));

	float ParsedCommentHeight = 0.0f;
	TestTrue(TEXT("Synthesized comment height metadata should remain parseable."), LexTryParseString(ParsedCommentHeight, *CommentHeightMetadata->StringValue));
	TestEqual(TEXT("Synthesized comment height metadata should use the request default."), ParsedCommentHeight, 144.0f);
	TestEqual(TEXT("Synthesized comment font size metadata should use the request default."), CommentFontSizeMetadata->StringValue, FString(TEXT("24")));
	TestEqual(TEXT("Synthesized comment color metadata should use the request default."), CommentColorMetadata->StringValue, FString(TEXT("2288CCFF")));
	TestEqual(TEXT("Synthesized comment bubble metadata should use the request default."), CommentBubbleMetadata->StringValue, FString(TEXT("false")));
	TestEqual(TEXT("Synthesized comment bubble-color metadata should use the request default."), CommentColorBubbleMetadata->StringValue, FString(TEXT("true")));
	TestEqual(TEXT("Synthesized comment move-mode metadata should use the request default."), CommentMoveModeMetadata->StringValue, FString(TEXT("NoGroupMovement")));

	const int32 PrimaryAddIndex = CommentEnabledResult.Commands.IndexOfByPredicate([PrimaryNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::AddNode && Command.NodeId == PrimaryNode.Id;
	});
	const int32 CommentAddIndex = CommentEnabledResult.Commands.IndexOfByPredicate([CommentNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::AddNode && Command.NodeId == CommentNode.Id;
	});
	const int32 PrimaryMoveIndex = CommentEnabledResult.Commands.IndexOfByPredicate([PrimaryNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::MoveNode && Command.NodeId == PrimaryNode.Id;
	});
	const int32 CommentMoveIndex = CommentEnabledResult.Commands.IndexOfByPredicate([CommentNode](const FVergilCompilerCommand& Command)
	{
		return Command.Type == EVergilCommandType::MoveNode && Command.NodeId == CommentNode.Id;
	});
	TestTrue(TEXT("Comment post-pass AddNode work should land after core node-lowering AddNode work in the normalized plan."), PrimaryAddIndex >= 0 && CommentAddIndex > PrimaryAddIndex);
	TestTrue(TEXT("Primary layout work should run after core node-lowering AddNode work in the normalized plan."), PrimaryAddIndex >= 0 && PrimaryMoveIndex > PrimaryAddIndex);
	TestTrue(TEXT("Comment layout work should run after comment AddNode work in the normalized plan."), CommentAddIndex >= 0 && CommentMoveIndex > CommentAddIndex);

	FVergilCompileRequest CommentDisabledRequest = CommentEnabledRequest;
	CommentDisabledRequest.Document.BlueprintPath = TEXT("/Game/Tests/BP_LayoutCommentPostPasses_Disabled");
	CommentDisabledRequest.bGenerateComments = false;

	const FVergilCompileResult CommentDisabledResult = CompilerService.Compile(CommentDisabledRequest);
	TestTrue(TEXT("Disabling comment post-pass work should still compile successfully."), CommentDisabledResult.bSucceeded);
	TestNull(TEXT("Comment AddNode work should be omitted when bGenerateComments is false."), FindNodeCommand(CommentDisabledResult.Commands, EVergilCommandType::AddNode, CommentNode.Id));
	TestNull(TEXT("Comment MoveNode work should be omitted when comment generation is disabled because the comment node is never authored."), FindMoveCommand(CommentDisabledResult.Commands, CommentNode.Id));
	TestNotNull(TEXT("Disabling comment post-pass work should not affect the primary lowered node."), FindNodeCommand(CommentDisabledResult.Commands, EVergilCommandType::AddNode, PrimaryNode.Id));
	TestNotNull(TEXT("Primary layout work should still run when comments are disabled."), FindMoveCommand(CommentDisabledResult.Commands, PrimaryNode.Id));

	FVergilCompileRequest LayoutDisabledRequest = CommentEnabledRequest;
	LayoutDisabledRequest.Document.BlueprintPath = TEXT("/Game/Tests/BP_LayoutCommentPostPasses_NoLayout");
	LayoutDisabledRequest.bAutoLayout = false;

	const FVergilCompileResult LayoutDisabledResult = CompilerService.Compile(LayoutDisabledRequest);
	TestTrue(TEXT("Disabling the layout post-pass should still compile successfully."), LayoutDisabledResult.bSucceeded);
	TestNull(TEXT("Disabling auto-layout should omit primary MoveNode work."), FindMoveCommand(LayoutDisabledResult.Commands, PrimaryNode.Id));
	TestNull(TEXT("Disabling auto-layout should omit comment MoveNode work."), FindMoveCommand(LayoutDisabledResult.Commands, CommentNode.Id));
	TestTrue(
		TEXT("Enabling the deterministic layout pass should now change the normalized command plan relative to a layout-disabled compile."),
		Vergil::SerializeCommandPlan(CommentEnabledResult.Commands, false) != Vergil::SerializeCommandPlan(LayoutDisabledResult.Commands, false));

	return true;
}

bool FVergilConnectionLegalityPassTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code, const FGuid& SourceId = FGuid())
	{
		return Diagnostics.ContainsByPredicate([Code, SourceId](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code
				&& (!SourceId.IsValid() || Diagnostic.SourceId == SourceId);
		});
	};

	const FVergilBlueprintCompilerService CompilerService;

	{
		FVergilGraphNode SourceNode;
		SourceNode.Id = FGuid::NewGuid();
		SourceNode.Kind = EVergilNodeKind::Custom;
		SourceNode.Descriptor = TEXT("Test.ConnectionSource");

		FVergilGraphPin SourcePin;
		SourcePin.Id = FGuid::NewGuid();
		SourcePin.Name = TEXT("Value");
		SourcePin.Direction = EVergilPinDirection::Output;
		SourceNode.Pins.Add(SourcePin);

		FVergilGraphNode TargetNode;
		TargetNode.Id = FGuid::NewGuid();
		TargetNode.Kind = EVergilNodeKind::Custom;
		TargetNode.Descriptor = TEXT("Test.ConnectionTarget");

		FVergilGraphPin TargetPin;
		TargetPin.Id = FGuid::NewGuid();
		TargetPin.Name = TEXT("Input");
		TargetPin.Direction = EVergilPinDirection::Input;
		TargetNode.Pins.Add(TargetPin);

		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNode.Id;
		Edge.SourcePinId = SourcePin.Id;
		Edge.TargetNodeId = TargetNode.Id;
		Edge.TargetPinId = TargetPin.Id;

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_Valid");
		Request.Document.Nodes = { SourceNode, TargetNode };
		Request.Document.Edges.Add(Edge);
		Request.bAutoLayout = false;

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestTrue(TEXT("Legal connections should survive the connection-legality pass."), Result.bSucceeded);
		TestEqual(TEXT("Legal generic connections should still lower into ensure, two AddNode commands, and one ConnectPins command."), Result.Commands.Num(), 4);
		if (Result.Commands.Num() == 4)
		{
			TestEqual(TEXT("Legal connections should still plan the ConnectPins command last after normalization."), Result.Commands.Last().Type, EVergilCommandType::ConnectPins);
		}
	}

	{
		FVergilGraphNode SourceNode;
		SourceNode.Id = FGuid::NewGuid();
		SourceNode.Kind = EVergilNodeKind::Custom;
		SourceNode.Descriptor = TEXT("Test.BadSourceDirection");

		FVergilGraphPin SourcePin;
		SourcePin.Id = FGuid::NewGuid();
		SourcePin.Name = TEXT("InputPin");
		SourcePin.Direction = EVergilPinDirection::Input;
		SourceNode.Pins.Add(SourcePin);

		FVergilGraphNode TargetNode;
		TargetNode.Id = FGuid::NewGuid();
		TargetNode.Kind = EVergilNodeKind::Custom;
		TargetNode.Descriptor = TEXT("Test.TargetDirection");

		FVergilGraphPin TargetPin;
		TargetPin.Id = FGuid::NewGuid();
		TargetPin.Name = TEXT("Input");
		TargetPin.Direction = EVergilPinDirection::Input;
		TargetNode.Pins.Add(TargetPin);

		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNode.Id;
		Edge.SourcePinId = SourcePin.Id;
		Edge.TargetNodeId = TargetNode.Id;
		Edge.TargetPinId = TargetPin.Id;

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_SourceDirection");
		Request.Document.Nodes = { SourceNode, TargetNode };
		Request.Document.Edges.Add(Edge);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Input pins should not be allowed to drive outgoing connections."), Result.bSucceeded);
		TestEqual(TEXT("Connection-legality failures should stop command planning before any commands are returned."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Wrong source pin direction should report ConnectionSourcePinDirectionInvalid."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConnectionSourcePinDirectionInvalid"), SourceNode.Id));
	}

	{
		FVergilGraphNode SourceNode;
		SourceNode.Id = FGuid::NewGuid();
		SourceNode.Kind = EVergilNodeKind::Custom;
		SourceNode.Descriptor = TEXT("Test.ExecMismatchSource");

		FVergilGraphPin SourcePin;
		SourcePin.Id = FGuid::NewGuid();
		SourcePin.Name = TEXT("Value");
		SourcePin.Direction = EVergilPinDirection::Output;
		SourceNode.Pins.Add(SourcePin);

		FVergilGraphNode TargetNode;
		TargetNode.Id = FGuid::NewGuid();
		TargetNode.Kind = EVergilNodeKind::Custom;
		TargetNode.Descriptor = TEXT("Test.ExecMismatchTarget");

		FVergilGraphPin TargetPin;
		TargetPin.Id = FGuid::NewGuid();
		TargetPin.Name = TEXT("Execute");
		TargetPin.Direction = EVergilPinDirection::Input;
		TargetPin.bIsExec = true;
		TargetNode.Pins.Add(TargetPin);

		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNode.Id;
		Edge.SourcePinId = SourcePin.Id;
		Edge.TargetNodeId = TargetNode.Id;
		Edge.TargetPinId = TargetPin.Id;

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_ExecMismatch");
		Request.Document.Nodes = { SourceNode, TargetNode };
		Request.Document.Edges.Add(Edge);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Exec pins should not connect to data pins."), Result.bSucceeded);
		TestEqual(TEXT("Exec/data mismatches should stop command planning before any commands are returned."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Exec/data mismatches should report ConnectionExecMismatch."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConnectionExecMismatch"), SourceNode.Id));
	}

	{
		FVergilGraphNode SourceNodeA;
		SourceNodeA.Id = FGuid::NewGuid();
		SourceNodeA.Kind = EVergilNodeKind::Custom;
		SourceNodeA.Descriptor = TEXT("Test.MultipleDriverA");

		FVergilGraphPin SourcePinA;
		SourcePinA.Id = FGuid::NewGuid();
		SourcePinA.Name = TEXT("ValueA");
		SourcePinA.Direction = EVergilPinDirection::Output;
		SourceNodeA.Pins.Add(SourcePinA);

		FVergilGraphNode SourceNodeB;
		SourceNodeB.Id = FGuid::NewGuid();
		SourceNodeB.Kind = EVergilNodeKind::Custom;
		SourceNodeB.Descriptor = TEXT("Test.MultipleDriverB");

		FVergilGraphPin SourcePinB;
		SourcePinB.Id = FGuid::NewGuid();
		SourcePinB.Name = TEXT("ValueB");
		SourcePinB.Direction = EVergilPinDirection::Output;
		SourceNodeB.Pins.Add(SourcePinB);

		FVergilGraphNode TargetNode;
		TargetNode.Id = FGuid::NewGuid();
		TargetNode.Kind = EVergilNodeKind::Custom;
		TargetNode.Descriptor = TEXT("Test.MultipleDriverTarget");

		FVergilGraphPin TargetPin;
		TargetPin.Id = FGuid::NewGuid();
		TargetPin.Name = TEXT("Input");
		TargetPin.Direction = EVergilPinDirection::Input;
		TargetNode.Pins.Add(TargetPin);

		FVergilGraphEdge FirstEdge;
		FirstEdge.Id = FGuid::NewGuid();
		FirstEdge.SourceNodeId = SourceNodeA.Id;
		FirstEdge.SourcePinId = SourcePinA.Id;
		FirstEdge.TargetNodeId = TargetNode.Id;
		FirstEdge.TargetPinId = TargetPin.Id;

		FVergilGraphEdge SecondEdge;
		SecondEdge.Id = FGuid::NewGuid();
		SecondEdge.SourceNodeId = SourceNodeB.Id;
		SecondEdge.SourcePinId = SourcePinB.Id;
		SecondEdge.TargetNodeId = TargetNode.Id;
		SecondEdge.TargetPinId = TargetPin.Id;

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_MultipleDrivers");
		Request.Document.Nodes = { SourceNodeA, SourceNodeB, TargetNode };
		Request.Document.Edges = { FirstEdge, SecondEdge };

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Input pins should reject multiple incoming connections during compile-time legality validation."), Result.bSucceeded);
		TestEqual(TEXT("Multiple drivers should stop command planning before any commands are returned."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Multiple drivers should report ConnectionTargetPinMultiplyDriven."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConnectionTargetPinMultiplyDriven"), TargetNode.Id));
	}

	{
		FVergilGraphNode SpawnActorNode;
		SpawnActorNode.Id = FGuid::NewGuid();
		SpawnActorNode.Kind = EVergilNodeKind::Custom;
		SpawnActorNode.Descriptor = TEXT("K2.SpawnActor");
		SpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), AActor::StaticClass()->GetPathName());

		FVergilGraphPin SpawnTransformPin;
		SpawnTransformPin.Id = FGuid::NewGuid();
		SpawnTransformPin.Name = TEXT("SpawnTransform");
		SpawnTransformPin.Direction = EVergilPinDirection::Input;
		SpawnActorNode.Pins.Add(SpawnTransformPin);

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_SpawnActorMissingTransformConnection");
		Request.Document.Nodes = { SpawnActorNode };

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("SpawnActor should fail compile-time legality validation when SpawnTransform is not driven."), Result.bSucceeded);
		TestEqual(TEXT("Missing SpawnTransform inputs should stop command planning before any commands are returned."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing SpawnTransform inputs should report SpawnActorTransformConnectionMissing."), ContainsDiagnostic(Result.Diagnostics, TEXT("SpawnActorTransformConnectionMissing"), SpawnActorNode.Id));
	}

	{
		FVergilNodeRegistry::Get().Reset();
		FVergilNodeRegistry::Get().RegisterHandler(MakeShared<FTestPinDroppingNodeHandler, ESPMode::ThreadSafe>());

		FVergilGraphNode SourceNode;
		SourceNode.Id = FGuid::NewGuid();
		SourceNode.Kind = EVergilNodeKind::Custom;
		SourceNode.Descriptor = TEXT("Test.DropPins");

		FVergilGraphPin SourcePin;
		SourcePin.Id = FGuid::NewGuid();
		SourcePin.Name = TEXT("Value");
		SourcePin.Direction = EVergilPinDirection::Output;
		SourceNode.Pins.Add(SourcePin);

		FVergilGraphNode TargetNode;
		TargetNode.Id = FGuid::NewGuid();
		TargetNode.Kind = EVergilNodeKind::Custom;
		TargetNode.Descriptor = TEXT("Test.DropPinsTarget");

		FVergilGraphPin TargetPin;
		TargetPin.Id = FGuid::NewGuid();
		TargetPin.Name = TEXT("Input");
		TargetPin.Direction = EVergilPinDirection::Input;
		TargetNode.Pins.Add(TargetPin);

		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNode.Id;
		Edge.SourcePinId = SourcePin.Id;
		Edge.TargetNodeId = TargetNode.Id;
		Edge.TargetPinId = TargetPin.Id;

		FVergilCompileRequest Request;
		Request.TargetBlueprint = MakeTestBlueprint();
		Request.Document.BlueprintPath = TEXT("/Game/Tests/BP_ConnectionLegality_DroppedPins");
		Request.Document.Nodes = { SourceNode, TargetNode };
		Request.Document.Edges.Add(Edge);

		const FVergilCompileResult Result = CompilerService.Compile(Request);
		TestFalse(TEXT("Connection legality should validate the lowered pin surface, not just authored pin ids."), Result.bSucceeded);
		TestEqual(TEXT("Dropped lowered pins should stop command planning before any commands are returned."), Result.Commands.Num(), 0);
		TestTrue(TEXT("Missing lowered source pins should report ConnectionSourcePinNotLowered."), ContainsDiagnostic(Result.Diagnostics, TEXT("ConnectionSourcePinNotLowered"), SourceNode.Id));

		FVergilNodeRegistry::Get().Reset();
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCommandPlanningTest,
	"Vergil.Scaffold.CommandPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilBlueprintMetadataPlanningTest,
	"Vergil.Scaffold.BlueprintMetadataPlanning",
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
	FVergilClassDefaultDefinitionPlanningTest,
	"Vergil.Scaffold.ClassDefaultDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilConstructionScriptDefinitionPlanningTest,
	"Vergil.Scaffold.ConstructionScriptDefinitionPlanning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilResultSummaryUtilitiesTest,
	"Vergil.Scaffold.ResultSummaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCompileResultMetadataTest,
	"Vergil.Scaffold.CompileResultMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilDryRunApplyPlanningParityTest,
	"Vergil.Scaffold.DryRunApplyPlanningParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilCommandSerializationUtilitiesTest,
	"Vergil.Scaffold.CommandSerializationUtilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilVariableAuthoringExecutionTest,
	"Vergil.Scaffold.VariableAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilBlueprintMetadataAuthoringExecutionTest,
	"Vergil.Scaffold.BlueprintMetadataAuthoringExecution",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilClassDefaultAuthoringExecutionTest,
	"Vergil.Scaffold.ClassDefaultAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilConstructionScriptAuthoringExecutionTest,
	"Vergil.Scaffold.ConstructionScriptAuthoringExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSaveReloadCompileRoundtripTest,
	"Vergil.Scaffold.SaveReloadCompileRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilVersioningAndMigrationContractsTest,
	"Vergil.Scaffold.VersioningAndMigrationContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSupportedContractInspectionTest,
	"Vergil.Scaffold.SupportedContractInspection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilInspectorToolingTest,
	"Vergil.Scaffold.InspectorTooling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilAgentRequestResponseContractsTest,
	"Vergil.Scaffold.AgentRequestResponseContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilAgentAuditPersistenceTest,
	"Vergil.Scaffold.AgentAuditPersistence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilAgentPlanApplySeparationTest,
	"Vergil.Scaffold.AgentPlanApplySeparation",
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

bool FVergilVersioningAndMigrationContractsTest::RunTest(const FString& Parameters)
{
	const FString SemanticVersion = Vergil::GetSemanticVersionString();
	TestEqual(TEXT("Semantic-version helpers should report the current plugin semantic version."), SemanticVersion, FString(TEXT("0.1.0")));
	TestEqual(TEXT("Plugin descriptor version should remain 1."), Vergil::PluginDescriptorVersion, 1);

	const TArray<FString> SupportedMigrationPaths = Vergil::GetSupportedSchemaMigrationPaths();
	TestEqual(TEXT("The current scaffold should advertise two explicit schema migration steps."), SupportedMigrationPaths.Num(), 2);
	TestTrue(TEXT("Supported schema migration paths should include 1->2."), ContainsStringValue(SupportedMigrationPaths, TEXT("1->2")));
	TestTrue(TEXT("Supported schema migration paths should include 2->3."), ContainsStringValue(SupportedMigrationPaths, TEXT("2->3")));

	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for versioning inspection."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	const FVergilSupportedContractManifest Manifest = AgentSubsystem->InspectSupportedContracts();
	TestEqual(TEXT("Supported-contract inspection should report the current plugin semantic version."), Manifest.PluginSemanticVersion, SemanticVersion);
	TestEqual(TEXT("Supported-contract inspection should report the current plugin descriptor version."), Manifest.PluginDescriptorVersion, Vergil::PluginDescriptorVersion);
	TestEqual(TEXT("Supported-contract inspection should mirror the supported migration-path count."), Manifest.SupportedSchemaMigrationPaths.Num(), SupportedMigrationPaths.Num());
	TestTrue(TEXT("Supported-contract inspection should include 1->2."), ContainsStringValue(Manifest.SupportedSchemaMigrationPaths, TEXT("1->2")));
	TestTrue(TEXT("Supported-contract inspection should include 2->3."), ContainsStringValue(Manifest.SupportedSchemaMigrationPaths, TEXT("2->3")));

	const FString ContractDescription = AgentSubsystem->DescribeSupportedContracts();
	TestTrue(TEXT("Supported-contract description should include the plugin semantic version."), ContractDescription.Contains(TEXT("pluginSemanticVersion: 0.1.0")));
	TestTrue(TEXT("Supported-contract description should include the schema migration path summary."), ContractDescription.Contains(TEXT("schemaMigrationPaths: 1->2, 2->3")));

	const FString ContractJson = AgentSubsystem->InspectSupportedContractsAsJson(false);
	TestTrue(TEXT("Supported-contract JSON should include the plugin descriptor version."), ContractJson.Contains(TEXT("\"pluginDescriptorVersion\":1")));
	TestTrue(TEXT("Supported-contract JSON should include the plugin semantic version."), ContractJson.Contains(TEXT("\"pluginSemanticVersion\":\"0.1.0\"")));
	TestTrue(TEXT("Supported-contract JSON should include the supported migration paths."), ContractJson.Contains(TEXT("\"supportedSchemaMigrationPaths\":[\"1->2\",\"2->3\"]")));

	const FString PluginDescriptorPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins/Vergil/Vergil.uplugin"));
	FString PluginDescriptorText;
	TestTrue(TEXT("The Vergil plugin descriptor should load for version-alignment coverage."), FFileHelper::LoadFileToString(PluginDescriptorText, *PluginDescriptorPath));
	if (!PluginDescriptorText.IsEmpty())
	{
		TestTrue(TEXT("The plugin descriptor should advertise Version 1."), PluginDescriptorText.Contains(TEXT("\"Version\": 1")));
		TestTrue(TEXT("The plugin descriptor should advertise VersionName 0.1.0."), PluginDescriptorText.Contains(TEXT("\"VersionName\": \"0.1.0\"")));
	}

	return true;
}

bool FVergilSupportedContractInspectionTest::RunTest(const FString& Parameters)
{
	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for contract inspection."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	const FVergilSupportedContractManifest Manifest = AgentSubsystem->InspectSupportedContracts();
	TestEqual(TEXT("Supported-contract inspection should report the current schema version."), Manifest.SchemaVersion, Vergil::SchemaVersion);
	TestEqual(TEXT("Supported-contract inspection should report manifest version 1."), Manifest.ManifestVersion, 1);
	TestEqual(TEXT("Supported-contract inspection should report the current plugin descriptor version."), Manifest.PluginDescriptorVersion, Vergil::PluginDescriptorVersion);
	TestEqual(TEXT("Supported-contract inspection should report the current plugin semantic version."), Manifest.PluginSemanticVersion, Vergil::GetSemanticVersionString());
	TestEqual(TEXT("Supported-contract inspection should report the current command-plan format name."), Manifest.CommandPlanFormat, Vergil::GetCommandPlanFormatName());
	TestEqual(TEXT("Supported-contract inspection should report the current command-plan format version."), Manifest.CommandPlanFormatVersion, Vergil::GetCommandPlanFormatVersion());
	TestTrue(TEXT("Supported-contract inspection should include 1->2 in the schema migration manifest."), ContainsStringValue(Manifest.SupportedSchemaMigrationPaths, TEXT("1->2")));
	TestTrue(TEXT("Supported-contract inspection should include 2->3 in the schema migration manifest."), ContainsStringValue(Manifest.SupportedSchemaMigrationPaths, TEXT("2->3")));
	TestTrue(TEXT("Supported-contract inspection should include Metadata in the document field manifest."), ContainsNameValue(Manifest.SupportedDocumentFields, TEXT("Metadata")));
	TestTrue(TEXT("Supported-contract inspection should include ConstructionScriptNodes in the document field manifest."), ContainsNameValue(Manifest.SupportedDocumentFields, TEXT("ConstructionScriptNodes")));
	TestTrue(TEXT("Supported-contract inspection should include EventGraph as a supported target graph."), ContainsNameValue(Manifest.SupportedTargetGraphs, TEXT("EventGraph")));
	TestTrue(TEXT("Supported-contract inspection should include UserConstructionScript as a supported target graph."), ContainsNameValue(Manifest.SupportedTargetGraphs, TEXT("UserConstructionScript")));
	TestTrue(TEXT("Supported-contract inspection should include BlueprintDisplayName as a supported Blueprint metadata key."), ContainsNameValue(Manifest.SupportedBlueprintMetadataKeys, TEXT("BlueprintDisplayName")));
	TestTrue(TEXT("Supported-contract inspection should include HideCategories as a supported Blueprint metadata key."), ContainsNameValue(Manifest.SupportedBlueprintMetadataKeys, TEXT("HideCategories")));
	TestTrue(TEXT("Supported-contract inspection should include struct as a supported type category."), ContainsStringValue(Manifest.SupportedTypeCategories, TEXT("struct")));
	TestTrue(TEXT("Supported-contract inspection should include Map as a supported container type."), ContainsStringValue(Manifest.SupportedContainerTypes, TEXT("Map")));
	TestTrue(TEXT("Supported-contract inspection should include SetBlueprintMetadata as a supported command type."), ContainsNameValue(Manifest.SupportedCommandTypes, TEXT("SetBlueprintMetadata")));
	TestTrue(TEXT("Supported-contract inspection should include FinalizeNode as a supported command type."), ContainsNameValue(Manifest.SupportedCommandTypes, TEXT("FinalizeNode")));
	TestTrue(TEXT("Supported-contract inspection should expose a non-empty descriptor catalog."), Manifest.SupportedDescriptors.Num() > 0);

	const FVergilSupportedDescriptorContract* const SelectContract = FindSupportedDescriptorContract(Manifest.SupportedDescriptors, TEXT("K2.Select"));
	TestNotNull(TEXT("Supported-contract inspection should include K2.Select."), SelectContract);
	if (SelectContract != nullptr)
	{
		TestEqual(TEXT("K2.Select should report exact-match descriptor inspection."), SelectContract->MatchKind, EVergilDescriptorMatchKind::Exact);
		TestTrue(TEXT("K2.Select should require IndexPinCategory metadata."), ContainsNameValue(SelectContract->RequiredMetadataKeys, TEXT("IndexPinCategory")));
		TestTrue(TEXT("K2.Select should require ValuePinCategory metadata."), ContainsNameValue(SelectContract->RequiredMetadataKeys, TEXT("ValuePinCategory")));
	}

	const FVergilSupportedDescriptorContract* const SpawnActorContract = FindSupportedDescriptorContract(Manifest.SupportedDescriptors, TEXT("K2.SpawnActor"));
	TestNotNull(TEXT("Supported-contract inspection should include K2.SpawnActor."), SpawnActorContract);
	if (SpawnActorContract != nullptr)
	{
		TestEqual(TEXT("K2.SpawnActor should report exact-match descriptor inspection."), SpawnActorContract->MatchKind, EVergilDescriptorMatchKind::Exact);
		TestTrue(TEXT("K2.SpawnActor should require ActorClassPath metadata."), ContainsNameValue(SpawnActorContract->RequiredMetadataKeys, TEXT("ActorClassPath")));
		TestTrue(TEXT("K2.SpawnActor notes should mention expose-on-spawn pins."), SpawnActorContract->Notes.Contains(TEXT("ExposeOnSpawn")));
	}

	const FVergilSupportedDescriptorContract* const CallContract = FindSupportedDescriptorContract(Manifest.SupportedDescriptors, TEXT("K2.Call.<FunctionName>"));
	TestNotNull(TEXT("Supported-contract inspection should include K2.Call.<FunctionName>."), CallContract);
	if (CallContract != nullptr)
	{
		TestEqual(TEXT("K2.Call.<FunctionName> should report a prefix descriptor match."), CallContract->MatchKind, EVergilDescriptorMatchKind::Prefix);
		TestEqual(TEXT("K2.Call.<FunctionName> should report Call as its expected node kind."), CallContract->ExpectedNodeKind, FString(TEXT("Call")));
		TestTrue(TEXT("K2.Call.<FunctionName> should mention OwnerClassPath normalization in its notes."), CallContract->Notes.Contains(TEXT("OwnerClassPath")));
	}

	const FVergilSupportedDescriptorContract* const CommentContract = FindSupportedDescriptorContract(Manifest.SupportedDescriptors, TEXT("any non-empty descriptor"));
	TestNotNull(TEXT("Supported-contract inspection should include the comment-node contract."), CommentContract);
	if (CommentContract != nullptr)
	{
		TestEqual(TEXT("Comment-node contract should report NodeKind matching."), CommentContract->MatchKind, EVergilDescriptorMatchKind::NodeKind);
		TestEqual(TEXT("Comment-node contract should report Comment as its expected node kind."), CommentContract->ExpectedNodeKind, FString(TEXT("Comment")));
	}

	const TArray<FVergilSupportedDescriptorContract> DescriptorContracts = AgentSubsystem->InspectSupportedDescriptorContracts();
	TestEqual(TEXT("Descriptor-only inspection should mirror the manifest descriptor count."), DescriptorContracts.Num(), Manifest.SupportedDescriptors.Num());

	const FString ContractDescription = AgentSubsystem->DescribeSupportedContracts();
	TestTrue(TEXT("Supported-contract description should include the manifest format label."), ContractDescription.Contains(TEXT("Vergil.ContractManifest")));
	TestTrue(TEXT("Supported-contract description should include K2.CreateDelegate.<FunctionName>."), ContractDescription.Contains(TEXT("K2.CreateDelegate.<FunctionName>")));

	const FString ContractJson = AgentSubsystem->InspectSupportedContractsAsJson(false);
	TestTrue(TEXT("Supported-contract JSON should include the manifest format field."), ContractJson.Contains(TEXT("\"format\":\"Vergil.ContractManifest\"")));
	TestTrue(TEXT("Supported-contract JSON should include the command-plan format field."), ContractJson.Contains(TEXT("\"commandPlanFormat\":\"Vergil.CommandPlan\"")));
	TestTrue(TEXT("Supported-contract JSON should include UserConstructionScript."), ContractJson.Contains(TEXT("UserConstructionScript")));
	TestTrue(TEXT("Supported-contract JSON should include K2.MakeMap."), ContractJson.Contains(TEXT("K2.MakeMap")));

	return true;
}

bool FVergilInspectorToolingTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem should be available for inspection tooling."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for inspection tooling."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	FVergilGraphNode InspectNode;
	InspectNode.Id = FGuid::NewGuid();
	InspectNode.Kind = EVergilNodeKind::Custom;
	InspectNode.Descriptor = TEXT("Test.Special");
	InspectNode.Position = FVector2D(128.0f, 96.0f);
	InspectNode.Metadata.Add(TEXT("InspectorKey"), TEXT("InspectorValue"));

	FVergilGraphPin InspectOutputPin;
	InspectOutputPin.Id = FGuid::NewGuid();
	InspectOutputPin.Name = TEXT("Result");
	InspectOutputPin.Direction = EVergilPinDirection::Output;
	InspectOutputPin.TypeName = TEXT("bool");
	InspectNode.Pins.Add(InspectOutputPin);

	FVergilVariableDefinition InspectVariable;
	InspectVariable.Name = TEXT("Counter");
	InspectVariable.Type.PinCategory = TEXT("int");
	InspectVariable.Flags.bInstanceEditable = true;
	InspectVariable.Category = TEXT("State");
	InspectVariable.Metadata.Add(TEXT("Tooltip"), TEXT("Inspector variable"));
	InspectVariable.DefaultValue = TEXT("7");

	FVergilGraphDocument InspectDocument;
	InspectDocument.SchemaVersion = 1;
	InspectDocument.BlueprintPath = TEXT("/Game/Tests/BP_InspectorTooling");
	InspectDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Inspector path"));
	InspectDocument.Variables.Add(InspectVariable);
	InspectDocument.Nodes.Add(InspectNode);
	InspectDocument.Tags.Add(TEXT("Smoke"));

	const FString NamespaceDocumentDescription = Vergil::DescribeGraphDocument(InspectDocument);
	const FString EditorDocumentDescription = EditorSubsystem->DescribeDocument(InspectDocument);
	const FString AgentDocumentDescription = AgentSubsystem->DescribeDocument(InspectDocument);
	TestEqual(TEXT("Editor document inspection should mirror the namespace helper."), EditorDocumentDescription, NamespaceDocumentDescription);
	TestEqual(TEXT("Agent document inspection should mirror the namespace helper."), AgentDocumentDescription, NamespaceDocumentDescription);
	TestTrue(TEXT("Document inspection should advertise the document format label."), EditorDocumentDescription.Contains(TEXT("Vergil.GraphDocument version=1")));
	TestTrue(TEXT("Document inspection should include Blueprint metadata values."), EditorDocumentDescription.Contains(TEXT("BlueprintDescription=\"Inspector path\"")));
	TestTrue(TEXT("Document inspection should include authored variable names."), EditorDocumentDescription.Contains(TEXT("Counter")));
	TestTrue(TEXT("Document inspection should include authored node descriptors."), EditorDocumentDescription.Contains(TEXT("Test.Special")));

	const FString NamespaceDocumentJson = Vergil::SerializeGraphDocument(InspectDocument, false);
	const FString EditorDocumentJson = EditorSubsystem->SerializeDocument(InspectDocument, false);
	const FString AgentDocumentJson = AgentSubsystem->InspectDocumentAsJson(InspectDocument, false);
	TestEqual(TEXT("Editor document JSON should mirror the namespace helper."), EditorDocumentJson, NamespaceDocumentJson);
	TestEqual(TEXT("Agent document JSON should mirror the namespace helper."), AgentDocumentJson, NamespaceDocumentJson);
	TestTrue(TEXT("Serialized documents should advertise the document format marker."), NamespaceDocumentJson.Contains(TEXT("\"format\":\"Vergil.GraphDocument\"")));
	TestTrue(TEXT("Serialized documents should include Blueprint metadata."), NamespaceDocumentJson.Contains(TEXT("\"BlueprintDescription\":\"Inspector path\"")));
	TestTrue(TEXT("Serialized documents should include authored node metadata."), NamespaceDocumentJson.Contains(TEXT("\"InspectorKey\":\"InspectorValue\"")));

	FVergilCompilerCommand InspectMetadataCommand;
	InspectMetadataCommand.Type = EVergilCommandType::SetBlueprintMetadata;
	InspectMetadataCommand.Name = TEXT("BlueprintDescription");
	InspectMetadataCommand.StringValue = TEXT("Inspector command plan");

	FVergilCompilerCommand InspectCompileCommand;
	InspectCompileCommand.Type = EVergilCommandType::CompileBlueprint;

	const TArray<FVergilCompilerCommand> InspectCommands = { InspectMetadataCommand, InspectCompileCommand };
	const FString NamespaceCommandPlanDescription = Vergil::DescribeCommandPlan(InspectCommands);
	TestTrue(TEXT("Command-plan inspection should include indexed output."), NamespaceCommandPlanDescription.Contains(TEXT("0: SetBlueprintMetadata")));
	TestEqual(TEXT("Editor command-plan inspection should mirror the namespace helper."), EditorSubsystem->DescribeCommandPlan(InspectCommands), NamespaceCommandPlanDescription);
	TestEqual(TEXT("Agent command-plan inspection should mirror the namespace helper."), AgentSubsystem->DescribeCommandPlan(InspectCommands), NamespaceCommandPlanDescription);

	const FString NamespaceCommandPlanJson = Vergil::SerializeCommandPlan(InspectCommands, false);
	TestEqual(TEXT("Editor command-plan JSON should mirror the namespace helper."), EditorSubsystem->SerializeCommandPlan(InspectCommands, false), NamespaceCommandPlanJson);
	TestEqual(TEXT("Agent command-plan JSON should mirror the namespace helper."), AgentSubsystem->InspectCommandPlanAsJson(InspectCommands, false), NamespaceCommandPlanJson);
	TestTrue(TEXT("Serialized command plans should advertise the command-plan format marker."), NamespaceCommandPlanJson.Contains(TEXT("\"format\":\"Vergil.CommandPlan\"")));

	TArray<FVergilDiagnostic> InspectDiagnostics;
	const FGuid WarningSourceId = FGuid::NewGuid();
	InspectDiagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Info, TEXT("InspectorInfo"), TEXT("Informational inspector diagnostic")));
	InspectDiagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Warning, TEXT("InspectorWarning"), TEXT("Warning inspector diagnostic"), WarningSourceId));

	const FString NamespaceDiagnosticsDescription = Vergil::DescribeDiagnostics(InspectDiagnostics);
	const FString EditorDiagnosticsDescription = EditorSubsystem->DescribeDiagnostics(InspectDiagnostics);
	const FString AgentDiagnosticsDescription = AgentSubsystem->DescribeDiagnostics(InspectDiagnostics);
	TestEqual(TEXT("Editor diagnostic inspection should mirror the namespace helper."), EditorDiagnosticsDescription, NamespaceDiagnosticsDescription);
	TestEqual(TEXT("Agent diagnostic inspection should mirror the namespace helper."), AgentDiagnosticsDescription, NamespaceDiagnosticsDescription);
	TestTrue(TEXT("Diagnostic inspection should include the warning code."), NamespaceDiagnosticsDescription.Contains(TEXT("InspectorWarning")));
	TestTrue(TEXT("Diagnostic inspection should include the warning source id."), NamespaceDiagnosticsDescription.Contains(WarningSourceId.ToString(EGuidFormats::DigitsWithHyphensLower)));

	const FString NamespaceDiagnosticsJson = Vergil::SerializeDiagnostics(InspectDiagnostics, false);
	const FString EditorDiagnosticsJson = EditorSubsystem->SerializeDiagnostics(InspectDiagnostics, false);
	const FString AgentDiagnosticsJson = AgentSubsystem->InspectDiagnosticsAsJson(InspectDiagnostics, false);
	TestEqual(TEXT("Editor diagnostic JSON should mirror the namespace helper."), EditorDiagnosticsJson, NamespaceDiagnosticsJson);
	TestEqual(TEXT("Agent diagnostic JSON should mirror the namespace helper."), AgentDiagnosticsJson, NamespaceDiagnosticsJson);
	TestTrue(TEXT("Serialized diagnostics should advertise the diagnostics format marker."), NamespaceDiagnosticsJson.Contains(TEXT("\"format\":\"Vergil.Diagnostics\"")));
	TestTrue(TEXT("Serialized diagnostics should include the warning source id."), NamespaceDiagnosticsJson.Contains(WarningSourceId.ToString(EGuidFormats::DigitsWithHyphensLower)));

	FVergilNodeRegistry::Get().Reset();
	FVergilNodeRegistry::Get().RegisterHandler(MakeShared<FTestSpecificNodeHandler, ESPMode::ThreadSafe>());

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created for compile-result inspection."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FVergilCompileResult CompileResult = EditorSubsystem->CompileDocument(Blueprint, InspectDocument, false, false, false);
	TestTrue(TEXT("Inspector-tooling compile should succeed."), CompileResult.bSucceeded);
	TestFalse(TEXT("Inspector-tooling compile should remain a dry run."), CompileResult.bApplied);
	TestTrue(TEXT("Inspector-tooling compile should plan commands."), CompileResult.Commands.Num() > 0);
	TestTrue(TEXT("Inspector-tooling compile should record pass data."), CompileResult.PassRecords.Num() > 0);
	TestTrue(TEXT("Inspector-tooling compile should include schema migration diagnostics."), CompileResult.Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
	{
		return Diagnostic.Code == TEXT("SchemaMigrationApplied");
	}));

	const FString NamespaceCompileResultDescription = Vergil::DescribeCompileResult(CompileResult);
	const FString EditorCompileResultDescription = EditorSubsystem->DescribeCompileResult(CompileResult);
	const FString AgentCompileResultDescription = AgentSubsystem->DescribeCompileResult(CompileResult);
	TestEqual(TEXT("Editor compile-result inspection should mirror the namespace helper."), EditorCompileResultDescription, NamespaceCompileResultDescription);
	TestEqual(TEXT("Agent compile-result inspection should mirror the namespace helper."), AgentCompileResultDescription, NamespaceCompileResultDescription);
	TestTrue(TEXT("Compile-result inspection should advertise the compile-result format marker."), NamespaceCompileResultDescription.Contains(TEXT("Vergil.CompileResult version=1")));
	TestTrue(TEXT("Compile-result inspection should include compile summary text."), NamespaceCompileResultDescription.Contains(TEXT("compileSummary: Compile")));
	TestTrue(TEXT("Compile-result inspection should include the schema-migration diagnostic."), NamespaceCompileResultDescription.Contains(TEXT("SchemaMigrationApplied")));
	TestTrue(TEXT("Compile-result inspection should include planned command text."), NamespaceCompileResultDescription.Contains(TEXT("AddNode")));

	const FString NamespaceCompileResultJson = Vergil::SerializeCompileResult(CompileResult, false);
	const FString EditorCompileResultJson = EditorSubsystem->SerializeCompileResult(CompileResult, false);
	const FString AgentCompileResultJson = AgentSubsystem->InspectCompileResultAsJson(CompileResult, false);
	TestEqual(TEXT("Editor compile-result JSON should mirror the namespace helper."), EditorCompileResultJson, NamespaceCompileResultJson);
	TestEqual(TEXT("Agent compile-result JSON should mirror the namespace helper."), AgentCompileResultJson, NamespaceCompileResultJson);
	TestTrue(TEXT("Serialized compile results should advertise the compile-result format marker."), NamespaceCompileResultJson.Contains(TEXT("\"format\":\"Vergil.CompileResult\"")));
	TestTrue(TEXT("Serialized compile results should embed diagnostics in the structured inspector payload."), NamespaceCompileResultJson.Contains(TEXT("\"diagnostics\":{\"format\":\"Vergil.Diagnostics\"")));
	TestTrue(TEXT("Serialized compile results should embed the command-plan payload."), NamespaceCompileResultJson.Contains(TEXT("\"commandPlan\":{\"format\":\"Vergil.CommandPlan\"")));
	TestTrue(TEXT("Serialized compile results should include pass records."), NamespaceCompileResultJson.Contains(TEXT("\"passRecords\"")));
	TestTrue(TEXT("Serialized compile results should include the schema-migration diagnostic code."), NamespaceCompileResultJson.Contains(TEXT("SchemaMigrationApplied")));

	FVergilNodeRegistry::Get().Reset();
	return true;
}

bool FVergilAgentRequestResponseContractsTest::RunTest(const FString& Parameters)
{
	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for agent contract inspection."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	AgentSubsystem->ClearAuditTrail();

	FVergilGraphDocument PlanDocument;
	PlanDocument.SchemaVersion = Vergil::SchemaVersion;
	PlanDocument.BlueprintPath = TEXT("/Game/Tests/BP_AgentRequestContract");
	PlanDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Agent request contract"));

	FVergilAgentRequest PlanRequest;
	PlanRequest.Context.RequestId = FGuid::NewGuid();
	PlanRequest.Context.Summary = TEXT("Plan the authored document.");
	PlanRequest.Context.InputText = TEXT("Generate a dry-run command plan.");
	PlanRequest.Context.Tags = { TEXT("Agent"), TEXT("Plan") };
	PlanRequest.Operation = EVergilAgentOperation::PlanDocument;
	PlanRequest.Plan.TargetBlueprintPath = TEXT("/Game/Tests/BP_AgentRequestContract");
	PlanRequest.Plan.Document = PlanDocument;
	PlanRequest.Plan.TargetGraphName = TEXT("EventGraph");
	PlanRequest.Plan.bAutoLayout = false;
	PlanRequest.Plan.bGenerateComments = false;

	TestFalse(TEXT("PlanDocument requests should remain read-only."), PlanRequest.IsWriteRequest());
	TestEqual(TEXT("Agent request format name should be versioned."), Vergil::GetAgentRequestFormatName(), FString(TEXT("Vergil.AgentRequest")));
	TestEqual(TEXT("Agent request format version should remain 1."), Vergil::GetAgentRequestFormatVersion(), 1);

	const FString NamespacePlanRequestDescription = Vergil::DescribeAgentRequest(PlanRequest);
	const FString AgentPlanRequestDescription = AgentSubsystem->DescribeAgentRequest(PlanRequest);
	TestEqual(TEXT("Agent request description should mirror the namespace helper."), AgentPlanRequestDescription, NamespacePlanRequestDescription);
	TestTrue(TEXT("Plan request description should advertise the request format."), NamespacePlanRequestDescription.Contains(TEXT("Vergil.AgentRequest version=1")));
	TestTrue(TEXT("Plan request description should include the plan summary."), NamespacePlanRequestDescription.Contains(TEXT("Plan the authored document.")));
	TestTrue(TEXT("Plan request description should include the graph-document format."), NamespacePlanRequestDescription.Contains(TEXT("Vergil.GraphDocument version=1")));

	const FString NamespacePlanRequestJson = Vergil::SerializeAgentRequest(PlanRequest, false);
	const FString AgentPlanRequestJson = AgentSubsystem->InspectAgentRequestAsJson(PlanRequest, false);
	TestEqual(TEXT("Agent request JSON should mirror the namespace helper."), AgentPlanRequestJson, NamespacePlanRequestJson);
	TestTrue(TEXT("Plan request JSON should advertise the request format."), NamespacePlanRequestJson.Contains(TEXT("\"format\":\"Vergil.AgentRequest\"")));
	TestTrue(TEXT("Plan request JSON should include the plan operation."), NamespacePlanRequestJson.Contains(TEXT("\"operation\":\"PlanDocument\"")));
	TestTrue(TEXT("Plan request JSON should embed the graph-document payload."), NamespacePlanRequestJson.Contains(TEXT("\"document\":{\"format\":\"Vergil.GraphDocument\"")));
	TestTrue(TEXT("Plan request JSON should include the read-only request classification."), NamespacePlanRequestJson.Contains(TEXT("\"writeRequest\":false")));

	FVergilCompilerCommand ApplyMetadataCommand;
	ApplyMetadataCommand.Type = EVergilCommandType::SetBlueprintMetadata;
	ApplyMetadataCommand.Name = TEXT("BlueprintDescription");
	ApplyMetadataCommand.StringValue = TEXT("Agent apply contract");

	FVergilCompileResult ResponseResult;
	ResponseResult.bSucceeded = true;
	ResponseResult.Commands = { ApplyMetadataCommand };
	ResponseResult.Diagnostics.Add(FVergilDiagnostic::Make(EVergilDiagnosticSeverity::Info, TEXT("AgentContract"), TEXT("Agent contract result")));
	ResponseResult.Statistics.TargetGraphName = TEXT("EventGraph");
	ResponseResult.Statistics.RequestedSchemaVersion = Vergil::SchemaVersion;
	ResponseResult.Statistics.EffectiveSchemaVersion = Vergil::SchemaVersion;
	ResponseResult.Statistics.RebuildCommandStatistics(ResponseResult.Commands);

	FVergilAgentRequest ApplyRequest;
	ApplyRequest.Context.RequestId = FGuid::NewGuid();
	ApplyRequest.Context.Summary = TEXT("Apply the normalized command plan.");
	ApplyRequest.Context.InputText = TEXT("Replay the approved plan.");
	ApplyRequest.Context.Tags = { TEXT("Agent"), TEXT("Apply") };
	ApplyRequest.Operation = EVergilAgentOperation::ApplyCommandPlan;
	ApplyRequest.Apply.TargetBlueprintPath = TEXT("/Game/Tests/BP_AgentRequestContract");
	ApplyRequest.Apply.Commands = ResponseResult.Commands;
	ApplyRequest.Apply.ExpectedCommandPlanFingerprint = ResponseResult.Statistics.CommandPlanFingerprint;

	TestTrue(TEXT("ApplyCommandPlan requests should be treated as write requests."), ApplyRequest.IsWriteRequest());
	TestTrue(TEXT("Apply payload should preserve the expected fingerprint."), ApplyRequest.Apply.ExpectedCommandPlanFingerprint == ResponseResult.Statistics.CommandPlanFingerprint);

	const FString NamespaceApplyRequestJson = Vergil::SerializeAgentRequest(ApplyRequest, false);
	TestTrue(TEXT("Apply request JSON should include the apply operation."), NamespaceApplyRequestJson.Contains(TEXT("\"operation\":\"ApplyCommandPlan\"")));
	TestTrue(TEXT("Apply request JSON should include the expected fingerprint."), NamespaceApplyRequestJson.Contains(*FString::Printf(TEXT("\"expectedCommandPlanFingerprint\":\"%s\""), *ResponseResult.Statistics.CommandPlanFingerprint)));
	TestTrue(TEXT("Apply request JSON should embed the command-plan payload."), NamespaceApplyRequestJson.Contains(TEXT("\"commandPlan\":{\"format\":\"Vergil.CommandPlan\"")));
	TestTrue(TEXT("Apply request JSON should include the write-request classification."), NamespaceApplyRequestJson.Contains(TEXT("\"writeRequest\":true")));

	FVergilAgentResponse Response;
	Response.RequestId = PlanRequest.Context.RequestId;
	Response.Operation = EVergilAgentOperation::PlanDocument;
	Response.State = EVergilAgentExecutionState::Completed;
	Response.Message = TEXT("Dry-run planning completed.");
	Response.Result = ResponseResult;

	TestEqual(TEXT("Agent response format name should be versioned."), Vergil::GetAgentResponseFormatName(), FString(TEXT("Vergil.AgentResponse")));
	TestEqual(TEXT("Agent response format version should remain 1."), Vergil::GetAgentResponseFormatVersion(), 1);

	const FString NamespaceResponseDescription = Vergil::DescribeAgentResponse(Response);
	const FString AgentResponseDescription = AgentSubsystem->DescribeAgentResponse(Response);
	TestEqual(TEXT("Agent response description should mirror the namespace helper."), AgentResponseDescription, NamespaceResponseDescription);
	TestTrue(TEXT("Agent response description should advertise the response format."), NamespaceResponseDescription.Contains(TEXT("Vergil.AgentResponse version=1")));
	TestTrue(TEXT("Agent response description should include the response message."), NamespaceResponseDescription.Contains(TEXT("Dry-run planning completed.")));
	TestTrue(TEXT("Agent response description should include the nested compile-result format."), NamespaceResponseDescription.Contains(TEXT("Vergil.CompileResult version=1")));

	const FString NamespaceResponseJson = Vergil::SerializeAgentResponse(Response, false);
	const FString AgentResponseJson = AgentSubsystem->InspectAgentResponseAsJson(Response, false);
	TestEqual(TEXT("Agent response JSON should mirror the namespace helper."), AgentResponseJson, NamespaceResponseJson);
	TestTrue(TEXT("Agent response JSON should advertise the response format."), NamespaceResponseJson.Contains(TEXT("\"format\":\"Vergil.AgentResponse\"")));
	TestTrue(TEXT("Agent response JSON should include the completed state."), NamespaceResponseJson.Contains(TEXT("\"state\":\"Completed\"")));
	TestTrue(TEXT("Agent response JSON should embed the compile-result payload."), NamespaceResponseJson.Contains(TEXT("\"result\":{\"format\":\"Vergil.CompileResult\"")));

	FVergilAgentAuditEntry AuditEntry;
	AuditEntry.Request = ApplyRequest;
	AuditEntry.Response.Message = TEXT("Apply request queued.");
	AuditEntry.Response.State = EVergilAgentExecutionState::Pending;

	AgentSubsystem->RecordAuditEntry(AuditEntry);
	const TArray<FVergilAgentAuditEntry> AuditEntries = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Recording one audit entry should store one normalized record."), AuditEntries.Num(), 1);
	if (AuditEntries.Num() != 1)
	{
		return false;
	}

	const FVergilAgentAuditEntry& NormalizedAuditEntry = AuditEntries[0];
	TestTrue(TEXT("Recorded audit entries should synthesize a timestamp when omitted."), !NormalizedAuditEntry.TimestampUtc.IsEmpty());
	TestEqual(TEXT("Recorded audit entries should preserve the request id on the response."), NormalizedAuditEntry.Response.RequestId, ApplyRequest.Context.RequestId);
	TestEqual(TEXT("Recorded audit entries should synthesize the response operation from the request."), NormalizedAuditEntry.Response.Operation, ApplyRequest.Operation);

	TestEqual(TEXT("Agent audit-entry format name should be versioned."), Vergil::GetAgentAuditEntryFormatName(), FString(TEXT("Vergil.AgentAuditEntry")));
	TestEqual(TEXT("Agent audit-entry format version should remain 1."), Vergil::GetAgentAuditEntryFormatVersion(), 1);

	const FString NamespaceAuditDescription = Vergil::DescribeAgentAuditEntry(NormalizedAuditEntry);
	const FString AgentAuditDescription = AgentSubsystem->DescribeAgentAuditEntry(NormalizedAuditEntry);
	TestEqual(TEXT("Agent audit-entry description should mirror the namespace helper."), AgentAuditDescription, NamespaceAuditDescription);
	TestTrue(TEXT("Agent audit-entry description should advertise the audit-entry format."), NamespaceAuditDescription.Contains(TEXT("Vergil.AgentAuditEntry version=1")));
	TestTrue(TEXT("Agent audit-entry description should include the nested apply request operation."), NamespaceAuditDescription.Contains(TEXT("ApplyCommandPlan")));
	TestTrue(TEXT("Agent audit-entry description should include the nested pending response state."), NamespaceAuditDescription.Contains(TEXT("state=Pending")));

	const FString NamespaceAuditJson = Vergil::SerializeAgentAuditEntry(NormalizedAuditEntry, false);
	const FString AgentAuditJson = AgentSubsystem->InspectAgentAuditEntryAsJson(NormalizedAuditEntry, false);
	TestEqual(TEXT("Agent audit-entry JSON should mirror the namespace helper."), AgentAuditJson, NamespaceAuditJson);
	TestTrue(TEXT("Agent audit-entry JSON should advertise the audit-entry format."), NamespaceAuditJson.Contains(TEXT("\"format\":\"Vergil.AgentAuditEntry\"")));
	TestTrue(TEXT("Agent audit-entry JSON should embed the nested request payload."), NamespaceAuditJson.Contains(TEXT("\"request\":{\"format\":\"Vergil.AgentRequest\"")));
	TestTrue(TEXT("Agent audit-entry JSON should embed the nested response payload."), NamespaceAuditJson.Contains(TEXT("\"response\":{\"format\":\"Vergil.AgentResponse\"")));

	AgentSubsystem->ClearAuditTrail();
	return true;
}

bool FVergilAgentAuditPersistenceTest::RunTest(const FString& Parameters)
{
	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for persisted audit coverage."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	const FString PersistencePath = AgentSubsystem->GetAuditTrailPersistencePath();
	FScopedAuditTrailFileBackup AuditTrailBackup(PersistencePath);
	TestTrue(TEXT("Persisted audit tests should be able to preserve any existing audit-log file."), AuditTrailBackup.IsReady());
	if (!AuditTrailBackup.IsReady())
	{
		return false;
	}

	AgentSubsystem->ClearAuditTrail();
	TestEqual(TEXT("Clearing the audit trail should clear in-memory entries."), AgentSubsystem->GetRecentAuditEntries().Num(), 0);
	TestFalse(TEXT("Clearing the audit trail should remove the persisted audit-log file."), IFileManager::Get().FileExists(*PersistencePath));

	FVergilGraphDocument PersistedDocument;
	PersistedDocument.SchemaVersion = Vergil::SchemaVersion;
	PersistedDocument.BlueprintPath = TEXT("/Game/Tests/BP_AgentAuditPersistence");
	PersistedDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Persisted audit coverage"));

	FVergilAgentAuditEntry PersistedEntry;
	PersistedEntry.Request.Context.RequestId = FGuid::NewGuid();
	PersistedEntry.Request.Context.Summary = TEXT("Persist the audit entry.");
	PersistedEntry.Request.Context.InputText = TEXT("Store the request and response to disk.");
	PersistedEntry.Request.Context.Tags = { TEXT("Agent"), TEXT("Persistence") };
	PersistedEntry.Request.Operation = EVergilAgentOperation::PlanDocument;
	PersistedEntry.Request.Plan.TargetBlueprintPath = PersistedDocument.BlueprintPath;
	PersistedEntry.Request.Plan.Document = PersistedDocument;
	PersistedEntry.Request.Plan.TargetGraphName = TEXT("EventGraph");
	PersistedEntry.Request.Plan.bAutoLayout = false;
	PersistedEntry.Request.Plan.bGenerateComments = false;
	PersistedEntry.Response.RequestId = PersistedEntry.Request.Context.RequestId;
	PersistedEntry.Response.Operation = EVergilAgentOperation::PlanDocument;
	PersistedEntry.Response.State = EVergilAgentExecutionState::Completed;
	PersistedEntry.Response.Message = TEXT("Planning completed and persisted.");
	PersistedEntry.Response.Result.bSucceeded = true;
	PersistedEntry.Response.Result.Statistics.TargetGraphName = TEXT("EventGraph");
	PersistedEntry.Response.Result.Statistics.RequestedSchemaVersion = Vergil::SchemaVersion;
	PersistedEntry.Response.Result.Statistics.EffectiveSchemaVersion = Vergil::SchemaVersion;

	AgentSubsystem->RecordAuditEntry(PersistedEntry);

	const TArray<FVergilAgentAuditEntry> InMemoryEntries = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Recording an audit entry should keep one in-memory record."), InMemoryEntries.Num(), 1);
	if (InMemoryEntries.Num() != 1)
	{
		return false;
	}

	TestTrue(TEXT("Recording an audit entry should write the persisted audit-log file."), IFileManager::Get().FileExists(*PersistencePath));
	TestTrue(TEXT("Explicit audit-log flush should succeed."), AgentSubsystem->FlushAuditTrailToDisk());

	FString PersistedAuditJson;
	TestTrue(TEXT("The persisted audit-log file should be readable."), FFileHelper::LoadFileToString(PersistedAuditJson, *PersistencePath));
	TestTrue(TEXT("The persisted audit-log file should advertise the persisted audit-log format."), PersistedAuditJson.Contains(TEXT("\"format\": \"Vergil.AgentAuditLog\"")));
	TestTrue(TEXT("The persisted audit-log file should include the request summary."), PersistedAuditJson.Contains(TEXT("Persist the audit entry.")));
	TestTrue(TEXT("The persisted audit-log file should include the response message."), PersistedAuditJson.Contains(TEXT("Planning completed and persisted.")));

	TestTrue(TEXT("Reloading from a valid persisted audit-log file should succeed."), AgentSubsystem->ReloadAuditTrailFromDisk());

	const TArray<FVergilAgentAuditEntry> ReloadedEntries = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Reloading from disk should restore the one persisted audit entry."), ReloadedEntries.Num(), 1);
	if (ReloadedEntries.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Reloaded audit entries should preserve the request id."), ReloadedEntries[0].Request.Context.RequestId, PersistedEntry.Request.Context.RequestId);
	TestEqual(TEXT("Reloaded audit entries should preserve the response state."), ReloadedEntries[0].Response.State, EVergilAgentExecutionState::Completed);
	TestEqual(TEXT("Reloaded audit entries should preserve the Blueprint path."), ReloadedEntries[0].Request.Plan.TargetBlueprintPath, PersistedDocument.BlueprintPath);

	TestTrue(
		TEXT("The persisted audit-log file should be replaceable with invalid JSON for reload-failure coverage."),
		FFileHelper::SaveStringToFile(TEXT("{invalid json"), *PersistencePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	TestFalse(TEXT("Reloading from invalid audit-log JSON should fail."), AgentSubsystem->ReloadAuditTrailFromDisk());

	const TArray<FVergilAgentAuditEntry> EntriesAfterFailedReload = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Failed reloads should preserve the last in-memory audit trail."), EntriesAfterFailedReload.Num(), 1);
	if (EntriesAfterFailedReload.Num() == 1)
	{
		TestEqual(TEXT("Failed reloads should preserve the last in-memory request id."), EntriesAfterFailedReload[0].Request.Context.RequestId, PersistedEntry.Request.Context.RequestId);
	}

	AgentSubsystem->ClearAuditTrail();
	TestFalse(TEXT("Clearing the audit trail should delete the persisted audit-log file."), IFileManager::Get().FileExists(*PersistencePath));
	return true;
}

bool FVergilAgentPlanApplySeparationTest::RunTest(const FString& Parameters)
{
	UVergilAgentSubsystem* const AgentSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilAgentSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil agent subsystem should be available for plan/apply execution coverage."), AgentSubsystem);
	if (AgentSubsystem == nullptr)
	{
		return false;
	}

	AgentSubsystem->ClearAuditTrail();

	FScopedPersistentTestBlueprint PersistentBlueprint(TEXT("BP_AgentPlanApplySeparation"));
	UBlueprint* const Blueprint = PersistentBlueprint.CreateBlueprintAsset();
	TestNotNull(TEXT("Plan/apply separation coverage should create a persistent Blueprint asset."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("The test Blueprint should start without the authored description."), Blueprint->BlueprintDescription.IsEmpty());

	FVergilGraphDocument Document;
	Document.SchemaVersion = Vergil::SchemaVersion;
	Document.BlueprintPath = PersistentBlueprint.PackagePath;
	Document.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Agent plan/apply separation"));

	FVergilAgentRequest PlanRequest;
	PlanRequest.Context.RequestId = FGuid::NewGuid();
	PlanRequest.Context.Summary = TEXT("Plan the requested document.");
	PlanRequest.Context.InputText = TEXT("Produce a dry-run command plan only.");
	PlanRequest.Context.Tags = { TEXT("Agent"), TEXT("Plan"), TEXT("Separation") };
	PlanRequest.Operation = EVergilAgentOperation::PlanDocument;
	PlanRequest.Plan.Document = Document;
	PlanRequest.Plan.bAutoLayout = false;
	PlanRequest.Plan.bGenerateComments = false;

	const FVergilAgentResponse PlanResponse = AgentSubsystem->ExecuteRequest(PlanRequest);
	TestEqual(TEXT("Plan execution should report the plan operation."), PlanResponse.Operation, EVergilAgentOperation::PlanDocument);
	TestEqual(TEXT("Plan execution should complete successfully for a valid request."), PlanResponse.State, EVergilAgentExecutionState::Completed);
	TestTrue(TEXT("Plan execution should return a successful dry-run compile result."), PlanResponse.Result.bSucceeded);
	TestFalse(TEXT("Plan execution should remain read-only."), PlanResponse.Result.bApplied);
	TestFalse(TEXT("Plan execution should not mark apply as requested."), PlanResponse.Result.Statistics.bApplyRequested);
	TestFalse(TEXT("Plan execution should not attempt editor execution."), PlanResponse.Result.Statistics.bExecutionAttempted);
	TestTrue(TEXT("Plan execution should return a normalized command plan."), PlanResponse.Result.Statistics.bCommandPlanNormalized);
	TestTrue(TEXT("Plan execution should return at least one planned command."), PlanResponse.Result.Commands.Num() > 0);
	TestTrue(TEXT("Plan execution should produce a command-plan fingerprint."), !PlanResponse.Result.Statistics.CommandPlanFingerprint.IsEmpty());
	TestTrue(TEXT("Plan execution message should advertise planning."), PlanResponse.Message.Contains(TEXT("Planned")));
	TestTrue(TEXT("Planning should not mutate Blueprint metadata before apply."), Blueprint->BlueprintDescription.IsEmpty());

	TArray<FVergilAgentAuditEntry> AuditEntries = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Planning should record one normalized audit entry."), AuditEntries.Num(), 1);
	if (AuditEntries.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Recorded plan audit entries should keep the plan operation."), AuditEntries[0].Request.Operation, EVergilAgentOperation::PlanDocument);
	TestEqual(TEXT("Recorded plan audit entries should normalize the target Blueprint path from the document."), AuditEntries[0].Request.Plan.TargetBlueprintPath, PersistentBlueprint.PackagePath);
	TestEqual(TEXT("Recorded plan audit entries should keep the response state."), AuditEntries[0].Response.State, EVergilAgentExecutionState::Completed);

	FVergilAgentRequestContext RejectedApplyContext;
	RejectedApplyContext.RequestId = FGuid::NewGuid();
	RejectedApplyContext.Summary = TEXT("Reject a mismatched apply request.");
	RejectedApplyContext.InputText = TEXT("Do not replay a plan whose fingerprint changed.");
	RejectedApplyContext.Tags = { TEXT("Agent"), TEXT("Apply"), TEXT("Rejected") };

	FVergilAgentRequest RejectedApplyRequest = AgentSubsystem->MakeApplyRequestFromPlan(RejectedApplyContext, PlanRequest, PlanResponse.Result);
	TestEqual(TEXT("The helper should always produce an apply request operation."), RejectedApplyRequest.Operation, EVergilAgentOperation::ApplyCommandPlan);
	TestEqual(TEXT("The helper should carry the reviewed Blueprint path into the apply request."), RejectedApplyRequest.Apply.TargetBlueprintPath, PersistentBlueprint.PackagePath);
	TestEqual(TEXT("The helper should carry the reviewed command count into the apply request."), RejectedApplyRequest.Apply.Commands.Num(), PlanResponse.Result.Commands.Num());
	TestEqual(TEXT("The helper should carry the reviewed fingerprint into the apply request."), RejectedApplyRequest.Apply.ExpectedCommandPlanFingerprint, PlanResponse.Result.Statistics.CommandPlanFingerprint);

	RejectedApplyRequest.Apply.ExpectedCommandPlanFingerprint = TEXT("deadbeef");
	const FVergilAgentResponse RejectedApplyResponse = AgentSubsystem->ExecuteRequest(RejectedApplyRequest);
	TestEqual(TEXT("Mismatched apply requests should be rejected before mutation."), RejectedApplyResponse.State, EVergilAgentExecutionState::Rejected);
	TestFalse(TEXT("Rejected apply requests should not report a successful apply."), RejectedApplyResponse.Result.bApplied);
	TestFalse(TEXT("Rejected apply requests should not attempt editor execution."), RejectedApplyResponse.Result.Statistics.bExecutionAttempted);
	TestTrue(
		TEXT("Rejected apply requests should emit an explicit fingerprint-mismatch diagnostic."),
		RejectedApplyResponse.Result.Diagnostics.ContainsByPredicate([](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == TEXT("CommandPlanFingerprintMismatch");
		}));
	TestTrue(TEXT("Rejected apply requests should keep Blueprint metadata unchanged."), Blueprint->BlueprintDescription.IsEmpty());

	FVergilAgentRequestContext ApplyContext;
	ApplyContext.RequestId = FGuid::NewGuid();
	ApplyContext.Summary = TEXT("Apply the reviewed command plan.");
	ApplyContext.InputText = TEXT("Replay the approved command plan against the Blueprint.");
	ApplyContext.Tags = { TEXT("Agent"), TEXT("Apply"), TEXT("Approved") };

	const FVergilAgentRequest ApplyRequest = AgentSubsystem->MakeApplyRequestFromPlan(ApplyContext, PlanRequest, PlanResponse.Result);
	const FVergilAgentResponse ApplyResponse = AgentSubsystem->ExecuteRequest(ApplyRequest);
	TestEqual(TEXT("Apply execution should report the apply operation."), ApplyResponse.Operation, EVergilAgentOperation::ApplyCommandPlan);
	TestEqual(TEXT("Apply execution should complete successfully for the reviewed plan."), ApplyResponse.State, EVergilAgentExecutionState::Completed);
	TestTrue(TEXT("Apply execution should apply the returned command plan."), ApplyResponse.Result.bApplied);
	TestTrue(TEXT("Apply execution should succeed."), ApplyResponse.Result.bSucceeded);
	TestTrue(TEXT("Apply execution should mark apply as requested."), ApplyResponse.Result.Statistics.bApplyRequested);
	TestTrue(TEXT("Apply execution should attempt editor execution."), ApplyResponse.Result.Statistics.bExecutionAttempted);
	TestTrue(TEXT("Apply execution should report that it used the returned command plan."), ApplyResponse.Result.Statistics.bExecutionUsedReturnedCommandPlan);
	TestTrue(TEXT("Apply execution should execute at least one command."), ApplyResponse.Result.ExecutedCommandCount > 0);
	TestTrue(TEXT("Apply execution message should advertise apply."), ApplyResponse.Message.Contains(TEXT("Applied")));
	TestEqual(TEXT("The explicit second-phase apply should mutate Blueprint metadata."), Blueprint->BlueprintDescription, FString(TEXT("Agent plan/apply separation")));

	AuditEntries = AgentSubsystem->GetRecentAuditEntries();
	TestEqual(TEXT("Plan, rejected apply, and successful apply should each record an audit entry."), AuditEntries.Num(), 3);
	if (AuditEntries.Num() == 3)
	{
		TestEqual(TEXT("The second audit entry should record the rejected apply."), AuditEntries[1].Response.State, EVergilAgentExecutionState::Rejected);
		TestEqual(TEXT("The third audit entry should record the successful apply."), AuditEntries[2].Response.State, EVergilAgentExecutionState::Completed);
		TestEqual(TEXT("The third audit entry should preserve the approved fingerprint."), AuditEntries[2].Request.Apply.ExpectedCommandPlanFingerprint, PlanResponse.Result.Statistics.CommandPlanFingerprint);
	}

	AgentSubsystem->ClearAuditTrail();
	return true;
}

bool FVergilCompileResultMetadataTest::RunTest(const FString& Parameters)
{
	FVergilNodeRegistry::Get().Reset();
	FVergilNodeRegistry::Get().RegisterHandler(MakeShared<FTestSpecificNodeHandler, ESPMode::ThreadSafe>());

	FVergilGraphNode SpecificNode;
	SpecificNode.Id = FGuid::NewGuid();
	SpecificNode.Kind = EVergilNodeKind::Custom;
	SpecificNode.Descriptor = TEXT("Test.Special");
	SpecificNode.Position = FVector2D(128.0f, 64.0f);

	FVergilGraphDocument CompileDocument;
	CompileDocument.SchemaVersion = 1;
	CompileDocument.BlueprintPath = TEXT("/Game/Tests/BP_CompileResultMetadata");
	CompileDocument.Nodes.Add(SpecificNode);

	FVergilCompileRequest CompileRequest;
	CompileRequest.TargetBlueprint = MakeTestBlueprint();
	CompileRequest.Document = CompileDocument;
	CompileRequest.TargetGraphName = TEXT("EventGraph");
	CompileRequest.bAutoLayout = false;
	CompileRequest.bGenerateComments = false;

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult CompileResult = CompilerService.Compile(CompileRequest);

	TestTrue(TEXT("Compile result metadata test should compile successfully."), CompileResult.bSucceeded);
	TestEqual(TEXT("Compile metadata should preserve the target graph."), CompileResult.Statistics.TargetGraphName, FName(TEXT("EventGraph")));
	TestEqual(TEXT("Compile metadata should preserve the requested schema version."), CompileResult.Statistics.RequestedSchemaVersion, 1);
	TestEqual(TEXT("Compile metadata should record the effective migrated schema version."), CompileResult.Statistics.EffectiveSchemaVersion, Vergil::SchemaVersion);
	TestFalse(TEXT("Compile metadata should preserve the requested auto-layout flag."), CompileResult.Statistics.bAutoLayoutRequested);
	TestFalse(TEXT("Compile metadata should preserve the requested comment-generation flag."), CompileResult.Statistics.bGenerateCommentsRequested);
	TestFalse(TEXT("Dry-run compile should not report apply requested."), CompileResult.Statistics.bApplyRequested);
	TestFalse(TEXT("Dry-run compile should not report execution attempted."), CompileResult.Statistics.bExecutionAttempted);
	TestTrue(TEXT("Compiler output should always record normalized command plans."), CompileResult.Statistics.bCommandPlanNormalized);
	TestFalse(TEXT("Dry-run compile should not report execution using the returned plan."), CompileResult.Statistics.bExecutionUsedReturnedCommandPlan);
	TestEqual(TEXT("Dry-run compile should record one planning invocation."), CompileResult.Statistics.PlanningInvocationCount, 1);
	TestEqual(TEXT("Dry-run compile should record zero apply invocations."), CompileResult.Statistics.ApplyInvocationCount, 0);
	TestEqual(TEXT("Compile metadata should count target-graph nodes."), CompileResult.Statistics.SourceNodeCount, 1);
	TestEqual(TEXT("Compile metadata should count target-graph edges."), CompileResult.Statistics.SourceEdgeCount, 0);
	TestEqual(TEXT("Compile metadata should report the planned command count."), CompileResult.Statistics.PlannedCommandCount, 2);
	TestEqual(TEXT("Compile metadata should classify graph-structure commands."), CompileResult.Statistics.GraphStructureCommandCount, 2);
	TestEqual(TEXT("Compile metadata should account for every planned command exactly once."), CompileResult.Statistics.GetTotalAccountedCommandCount(), 2);
	TestFalse(TEXT("Compile metadata should include a stable command-plan fingerprint."), CompileResult.Statistics.CommandPlanFingerprint.IsEmpty());
	TestEqual(TEXT("Compile metadata should record every successful pass."), CompileResult.Statistics.GetCompletedPassCount(), 11);
	TestEqual(TEXT("Compile metadata should record the final completed pass."), CompileResult.Statistics.LastCompletedPassName, FName(TEXT("CommandPlanning")));
	TestEqual(TEXT("Successful compiles should not record a failed pass."), CompileResult.Statistics.FailedPassName, NAME_None);
	TestEqual(TEXT("Compile metadata should retain one pass record per attempted pass."), CompileResult.PassRecords.Num(), 11);
	TestTrue(TEXT("Compile statistics display text should include the target graph."), CompileResult.Statistics.ToDisplayString().Contains(TEXT("graph=EventGraph")));
	if (CompileResult.PassRecords.Num() == 11)
	{
		TestEqual(TEXT("The first recorded pass should be schema migration."), CompileResult.PassRecords[0].PassName, FName(TEXT("SchemaMigration")));
		TestTrue(TEXT("The first recorded pass should succeed."), CompileResult.PassRecords[0].bSucceeded);
		TestEqual(TEXT("The last recorded pass should be command planning."), CompileResult.PassRecords.Last().PassName, FName(TEXT("CommandPlanning")));
		TestEqual(TEXT("The last recorded pass should retain the planned command count."), CompileResult.PassRecords.Last().PlannedCommandCount, 2);
		TestTrue(TEXT("Pass-record display text should include the pass name."), CompileResult.PassRecords[0].ToDisplayString().Contains(TEXT("SchemaMigration")));
	}

	FVergilCompileRequest InvalidRequest = CompileRequest;
	InvalidRequest.TargetGraphName = TEXT("UnsupportedGraph");

	const FVergilCompileResult InvalidResult = CompilerService.Compile(InvalidRequest);
	TestFalse(TEXT("Unsupported compile targets should fail."), InvalidResult.bSucceeded);
	TestEqual(TEXT("Failed compile metadata should identify the failing pass."), InvalidResult.Statistics.FailedPassName, FName(TEXT("SemanticValidation")));
	TestEqual(TEXT("Failed compile metadata should preserve the last completed pass."), InvalidResult.Statistics.LastCompletedPassName, FName(TEXT("StructuralValidation")));
	TestEqual(TEXT("Failed compile metadata should only count passes completed before failure."), InvalidResult.Statistics.GetCompletedPassCount(), 2);
	TestEqual(TEXT("Failed compile metadata should record only the attempted passes."), InvalidResult.PassRecords.Num(), 3);
	TestEqual(TEXT("Failed compile metadata should still preserve the requested schema version."), InvalidResult.Statistics.RequestedSchemaVersion, 1);
	TestEqual(TEXT("Failed compile metadata should still preserve the effective migrated schema version."), InvalidResult.Statistics.EffectiveSchemaVersion, Vergil::SchemaVersion);
	TestTrue(TEXT("Failed compile metadata should still mark the empty plan as normalized."), InvalidResult.Statistics.bCommandPlanNormalized);
	TestEqual(TEXT("Failed compile metadata should still record one planning invocation."), InvalidResult.Statistics.PlanningInvocationCount, 1);
	TestEqual(TEXT("Failed compile metadata should record zero apply invocations."), InvalidResult.Statistics.ApplyInvocationCount, 0);
	TestEqual(TEXT("Failed compile metadata should plan zero commands."), InvalidResult.Statistics.PlannedCommandCount, 0);
	TestFalse(TEXT("Failed compile metadata should still include a stable empty-plan fingerprint."), InvalidResult.Statistics.CommandPlanFingerprint.IsEmpty());
	if (InvalidResult.PassRecords.Num() == 3)
	{
		TestEqual(TEXT("The failed pass record should identify semantic validation."), InvalidResult.PassRecords.Last().PassName, FName(TEXT("SemanticValidation")));
		TestFalse(TEXT("The failed pass record should be marked unsuccessful."), InvalidResult.PassRecords.Last().bSucceeded);
		TestEqual(TEXT("The failed pass record should retain a zero command count."), InvalidResult.PassRecords.Last().PlannedCommandCount, 0);
	}

	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		FVergilNodeRegistry::Get().Reset();
		return false;
	}

	UBlueprint* const ApplyBlueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created for apply metadata coverage."), ApplyBlueprint);
	if (ApplyBlueprint == nullptr)
	{
		FVergilNodeRegistry::Get().Reset();
		return false;
	}

	FVergilGraphNode CommentNode;
	CommentNode.Id = FGuid::NewGuid();
	CommentNode.Kind = EVergilNodeKind::Comment;
	CommentNode.Descriptor = TEXT("UI.Comment");
	CommentNode.Position = FVector2D(64.0f, 96.0f);
	CommentNode.Metadata.Add(TEXT("CommentText"), TEXT("Compile metadata apply coverage"));

	FVergilGraphDocument ApplyDocument;
	ApplyDocument.BlueprintPath = TEXT("/Game/Tests/BP_CompileResultMetadataApply");
	ApplyDocument.Nodes.Add(CommentNode);

	const FVergilCompileResult ApplyResult = EditorSubsystem->CompileDocument(ApplyBlueprint, ApplyDocument, false, true, true);
	TestTrue(TEXT("Compile+apply metadata coverage should succeed."), ApplyResult.bSucceeded);
	TestTrue(TEXT("Compile+apply metadata coverage should apply commands."), ApplyResult.bApplied);
	TestTrue(TEXT("Compile+apply metadata should record that apply was requested."), ApplyResult.Statistics.bApplyRequested);
	TestTrue(TEXT("Compile+apply metadata should record that execution was attempted."), ApplyResult.Statistics.bExecutionAttempted);
	TestTrue(TEXT("Compile+apply metadata should report execution using the returned plan."), ApplyResult.Statistics.bExecutionUsedReturnedCommandPlan);
	TestTrue(TEXT("Compile+apply metadata should retain normalized command-plan state."), ApplyResult.Statistics.bCommandPlanNormalized);
	TestEqual(TEXT("Compile+apply metadata should record one planning invocation."), ApplyResult.Statistics.PlanningInvocationCount, 1);
	TestEqual(TEXT("Compile+apply metadata should record one apply invocation."), ApplyResult.Statistics.ApplyInvocationCount, 1);
	TestEqual(TEXT("Compile+apply metadata should preserve the default target graph."), ApplyResult.Statistics.TargetGraphName, FName(TEXT("EventGraph")));
	TestEqual(TEXT("Compile+apply metadata should count the authored comment node."), ApplyResult.Statistics.SourceNodeCount, 1);
	TestEqual(TEXT("Compile+apply metadata should keep planned command counts in sync with returned commands."), ApplyResult.Statistics.PlannedCommandCount, ApplyResult.Commands.Num());
	TestEqual(TEXT("Compile+apply metadata should classify comment authoring as graph-structure work."), ApplyResult.Statistics.GraphStructureCommandCount, ApplyResult.Commands.Num());
	TestEqual(TEXT("Compile+apply metadata should execute every planned command once."), ApplyResult.ExecutedCommandCount, ApplyResult.Commands.Num());
	TestFalse(TEXT("Compile+apply metadata should retain a stable command-plan fingerprint."), ApplyResult.Statistics.CommandPlanFingerprint.IsEmpty());

	FVergilNodeRegistry::Get().Reset();
	return true;
}

bool FVergilDryRunApplyPlanningParityTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	UBlueprint* const DryRunBlueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Dry-run test blueprint should be created."), DryRunBlueprint);
	if (DryRunBlueprint == nullptr)
	{
		return false;
	}

	UBlueprint* const ApplyBlueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Apply test blueprint should be created."), ApplyBlueprint);
	if (ApplyBlueprint == nullptr)
	{
		return false;
	}

	FVergilGraphNode EventNode;
	EventNode.Id = FGuid::NewGuid();
	EventNode.Kind = EVergilNodeKind::Event;
	EventNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	EventNode.Position = FVector2D(0.0f, 0.0f);
	EventNode.Metadata.Add(TEXT("Title"), TEXT("BeginPlay"));

	FVergilGraphPin EventExecOut;
	EventExecOut.Id = FGuid::NewGuid();
	EventExecOut.Name = TEXT("Then");
	EventExecOut.Direction = EVergilPinDirection::Output;
	EventExecOut.bIsExec = true;
	EventNode.Pins.Add(EventExecOut);

	FVergilGraphNode PrintNode;
	PrintNode.Id = FGuid::NewGuid();
	PrintNode.Kind = EVergilNodeKind::Call;
	PrintNode.Descriptor = TEXT("K2.Call.PrintString");
	PrintNode.Position = FVector2D(320.0f, 0.0f);
	PrintNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin PrintExecIn;
	PrintExecIn.Id = FGuid::NewGuid();
	PrintExecIn.Name = TEXT("execute");
	PrintExecIn.Direction = EVergilPinDirection::Input;
	PrintExecIn.bIsExec = true;
	PrintNode.Pins.Add(PrintExecIn);

	FVergilGraphPin PrintExecOut;
	PrintExecOut.Id = FGuid::NewGuid();
	PrintExecOut.Name = TEXT("then");
	PrintExecOut.Direction = EVergilPinDirection::Output;
	PrintExecOut.bIsExec = true;
	PrintNode.Pins.Add(PrintExecOut);

	FVergilGraphPin PrintMessagePin;
	PrintMessagePin.Id = FGuid::NewGuid();
	PrintMessagePin.Name = TEXT("InString");
	PrintMessagePin.Direction = EVergilPinDirection::Input;
	PrintNode.Pins.Add(PrintMessagePin);
	PrintNode.Metadata.Add(TEXT("Input.InString"), TEXT("Dry-run/apply parity"));

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_DryRunApplyPlanningParity");
	Document.Nodes = { EventNode, PrintNode };

	FVergilGraphEdge ExecEdge;
	ExecEdge.Id = FGuid::NewGuid();
	ExecEdge.SourceNodeId = EventNode.Id;
	ExecEdge.SourcePinId = EventExecOut.Id;
	ExecEdge.TargetNodeId = PrintNode.Id;
	ExecEdge.TargetPinId = PrintExecIn.Id;
	Document.Edges.Add(ExecEdge);

	const FVergilCompileResult DryRunResult = EditorSubsystem->CompileDocument(DryRunBlueprint, Document, false, false, false);
	const FVergilCompileResult ApplyResult = EditorSubsystem->CompileDocument(ApplyBlueprint, Document, false, false, true);

	TestTrue(TEXT("Dry-run compile should succeed."), DryRunResult.bSucceeded);
	TestTrue(TEXT("Compile+apply should succeed."), ApplyResult.bSucceeded);
	TestTrue(TEXT("Compile+apply should apply commands."), ApplyResult.bApplied);

	const FString DryRunSerializedPlan = Vergil::SerializeCommandPlan(DryRunResult.Commands, false);
	const FString ApplySerializedPlan = Vergil::SerializeCommandPlan(ApplyResult.Commands, false);
	TestEqual(TEXT("Dry-run and compile+apply should return identical normalized command plans."), DryRunSerializedPlan, ApplySerializedPlan);
	TestEqual(TEXT("Dry-run and compile+apply should preserve command count parity."), DryRunResult.Commands.Num(), ApplyResult.Commands.Num());
	TestEqual(TEXT("Dry-run and compile+apply should preserve plan fingerprints."), DryRunResult.Statistics.CommandPlanFingerprint, ApplyResult.Statistics.CommandPlanFingerprint);
	TestEqual(TEXT("Dry-run and compile+apply should preserve completed pass count."), DryRunResult.Statistics.GetCompletedPassCount(), ApplyResult.Statistics.GetCompletedPassCount());
	TestEqual(TEXT("Dry-run and compile+apply should preserve pass-record count."), DryRunResult.PassRecords.Num(), ApplyResult.PassRecords.Num());
	TestEqual(TEXT("Dry-run compile should record exactly one planning invocation."), DryRunResult.Statistics.PlanningInvocationCount, 1);
	TestEqual(TEXT("Compile+apply should record exactly one planning invocation."), ApplyResult.Statistics.PlanningInvocationCount, 1);
	TestEqual(TEXT("Dry-run compile should not record any apply invocations."), DryRunResult.Statistics.ApplyInvocationCount, 0);
	TestEqual(TEXT("Compile+apply should record exactly one apply invocation."), ApplyResult.Statistics.ApplyInvocationCount, 1);
	TestFalse(TEXT("Dry-run compile should not report execution using the returned plan."), DryRunResult.Statistics.bExecutionUsedReturnedCommandPlan);
	TestTrue(TEXT("Compile+apply should report execution using the returned plan."), ApplyResult.Statistics.bExecutionUsedReturnedCommandPlan);
	TestFalse(TEXT("Dry-run compile should not attempt execution."), DryRunResult.Statistics.bExecutionAttempted);
	TestTrue(TEXT("Compile+apply should attempt execution."), ApplyResult.Statistics.bExecutionAttempted);
	TestEqual(TEXT("Compile+apply should execute every planned command exactly once."), ApplyResult.ExecutedCommandCount, ApplyResult.Commands.Num());

	return true;
}

bool FVergilCommandSerializationUtilitiesTest::RunTest(const FString& Parameters)
{
	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	FVergilCompilerCommand SerializableCommand;
	SerializableCommand.Type = EVergilCommandType::AddNode;
	SerializableCommand.GraphName = TEXT("EventGraph");
	SerializableCommand.NodeId = FGuid::NewGuid();
	SerializableCommand.Name = TEXT("Vergil.Comment");
	SerializableCommand.StringValue = TEXT("Serialized\nComment");
	SerializableCommand.Attributes.Add(TEXT("CommentWidth"), TEXT("420"));
	SerializableCommand.Attributes.Add(TEXT("Color"), TEXT("Red"));
	SerializableCommand.Position = FVector2D(128.5f, 64.25f);

	FVergilPlannedPin PlannedExecPin;
	PlannedExecPin.PinId = FGuid::NewGuid();
	PlannedExecPin.Name = TEXT("Execute");
	PlannedExecPin.bIsInput = true;
	PlannedExecPin.bIsExec = true;
	SerializableCommand.PlannedPins.Add(PlannedExecPin);

	const TArray<FVergilCompilerCommand> SerializableCommands = { SerializableCommand };

	TestTrue(TEXT("Planned-pin debug strings should include their authored names."), PlannedExecPin.ToDisplayString().Contains(TEXT("Execute")));
	TestTrue(TEXT("Command debug strings should include the command type."), SerializableCommand.ToDisplayString().Contains(TEXT("AddNode")));
	TestTrue(TEXT("Command debug strings should include authored attributes."), SerializableCommand.ToDisplayString().Contains(TEXT("CommentWidth=420")));
	TestTrue(TEXT("Command-plan descriptions should include indexed command output."), Vergil::DescribeCommandPlan(SerializableCommands).Contains(TEXT("0: AddNode")));

	const FString SerializedPlanA = Vergil::SerializeCommandPlan(SerializableCommands, false);
	const FString SerializedPlanB = Vergil::SerializeCommandPlan(SerializableCommands, false);
	TestEqual(TEXT("Command serialization should be deterministic for the same input plan."), SerializedPlanA, SerializedPlanB);
	TestTrue(TEXT("Serialized command plans should advertise their format marker."), SerializedPlanA.Contains(TEXT("\"format\":\"Vergil.CommandPlan\"")));

	TArray<FVergilCompilerCommand> RoundTrippedCommands;
	TArray<FVergilDiagnostic> RoundTripDiagnostics;
	TestTrue(TEXT("Serialized command plans should deserialize successfully."), Vergil::DeserializeCommandPlan(SerializedPlanA, RoundTrippedCommands, &RoundTripDiagnostics));
	TestEqual(TEXT("Round-tripped command plans should preserve command count."), RoundTrippedCommands.Num(), 1);
	TestEqual(TEXT("Successful command-plan deserialization should not emit diagnostics."), RoundTripDiagnostics.Num(), 0);
	if (RoundTrippedCommands.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Round-tripped command should preserve its type."), RoundTrippedCommands[0].Type, SerializableCommand.Type);
	TestEqual(TEXT("Round-tripped command should preserve its graph name."), RoundTrippedCommands[0].GraphName, SerializableCommand.GraphName);
	TestEqual(TEXT("Round-tripped command should preserve its node id."), RoundTrippedCommands[0].NodeId, SerializableCommand.NodeId);
	TestEqual(TEXT("Round-tripped command should preserve its descriptor name."), RoundTrippedCommands[0].Name, SerializableCommand.Name);
	TestEqual(TEXT("Round-tripped command should preserve its string value."), RoundTrippedCommands[0].StringValue, SerializableCommand.StringValue);
	TestEqual(TEXT("Round-tripped command should preserve attribute count."), RoundTrippedCommands[0].Attributes.Num(), SerializableCommand.Attributes.Num());
	TestEqual(TEXT("Round-tripped command should preserve the CommentWidth attribute."), RoundTrippedCommands[0].Attributes.FindRef(TEXT("CommentWidth")), FString(TEXT("420")));
	TestEqual(TEXT("Round-tripped command should preserve the Color attribute."), RoundTrippedCommands[0].Attributes.FindRef(TEXT("Color")), FString(TEXT("Red")));
	TestEqual(TEXT("Round-tripped command should preserve X position."), RoundTrippedCommands[0].Position.X, SerializableCommand.Position.X);
	TestEqual(TEXT("Round-tripped command should preserve Y position."), RoundTrippedCommands[0].Position.Y, SerializableCommand.Position.Y);
	TestEqual(TEXT("Round-tripped command should preserve planned pin count."), RoundTrippedCommands[0].PlannedPins.Num(), 1);
	if (RoundTrippedCommands[0].PlannedPins.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Round-tripped planned pin should preserve pin id."), RoundTrippedCommands[0].PlannedPins[0].PinId, PlannedExecPin.PinId);
	TestEqual(TEXT("Round-tripped planned pin should preserve pin name."), RoundTrippedCommands[0].PlannedPins[0].Name, PlannedExecPin.Name);
	TestEqual(TEXT("Round-tripped planned pin should preserve input direction."), RoundTrippedCommands[0].PlannedPins[0].bIsInput, PlannedExecPin.bIsInput);
	TestEqual(TEXT("Round-tripped planned pin should preserve exec state."), RoundTrippedCommands[0].PlannedPins[0].bIsExec, PlannedExecPin.bIsExec);

	TArray<FVergilCompilerCommand> InvalidCommands;
	TArray<FVergilDiagnostic> InvalidDiagnostics;
	TestFalse(
		TEXT("Invalid serialized command types should fail deserialization."),
		Vergil::DeserializeCommandPlan(
			TEXT("{\"format\":\"Vergil.CommandPlan\",\"version\":1,\"commands\":[{\"type\":\"UnsupportedCommand\"}]}"),
			InvalidCommands,
			&InvalidDiagnostics));
	TestTrue(TEXT("Invalid serialized command types should emit a typed diagnostic."), ContainsDiagnostic(InvalidDiagnostics, TEXT("SerializedCommandTypeInvalid")));

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

	const FName VariableName(TEXT("SerializedFlag"));

	FVergilCompilerCommand SetClassDefault;
	SetClassDefault.Type = EVergilCommandType::SetClassDefault;
	SetClassDefault.Name = VariableName;
	SetClassDefault.StringValue = TEXT("True");

	FVergilCompilerCommand SetBlueprintDescription;
	SetBlueprintDescription.Type = EVergilCommandType::SetBlueprintMetadata;
	SetBlueprintDescription.Name = TEXT("BlueprintDescription");
	SetBlueprintDescription.StringValue = TEXT("Serialized replay path.");

	FVergilCompilerCommand CompileBlueprint;
	CompileBlueprint.Type = EVergilCommandType::CompileBlueprint;

	FVergilCompilerCommand EnsureVariable;
	EnsureVariable.Type = EVergilCommandType::EnsureVariable;
	EnsureVariable.SecondaryName = VariableName;
	EnsureVariable.Attributes.Add(TEXT("PinCategory"), TEXT("bool"));

	const TArray<FVergilCompilerCommand> ReplayCommands = { SetClassDefault, SetBlueprintDescription, CompileBlueprint, EnsureVariable };
	const FString SerializedReplayCommands = EditorSubsystem->SerializeCommandPlan(ReplayCommands, false);
	const FVergilCompileResult ReplayResult = EditorSubsystem->ExecuteSerializedCommandPlan(Blueprint, SerializedReplayCommands);

	TestTrue(TEXT("Serialized command-plan replay should succeed."), ReplayResult.bSucceeded);
	TestTrue(TEXT("Serialized command-plan replay should apply commands."), ReplayResult.bApplied);
	TestEqual(TEXT("Serialized command-plan replay should execute every normalized command once."), ReplayResult.ExecutedCommandCount, ReplayCommands.Num());
	TestEqual(TEXT("Serialized command-plan replay should preserve command count."), ReplayResult.Commands.Num(), ReplayCommands.Num());
	if (!ReplayResult.bSucceeded || !ReplayResult.bApplied || ReplayResult.Commands.Num() != ReplayCommands.Num())
	{
		return false;
	}

	TestEqual(TEXT("Serialized replay should normalize blueprint metadata commands into the first phase position."), ReplayResult.Commands[0].Type, EVergilCommandType::SetBlueprintMetadata);
	TestEqual(TEXT("Serialized replay should normalize variable creation into the same definition phase after metadata."), ReplayResult.Commands[1].Type, EVergilCommandType::EnsureVariable);
	TestEqual(TEXT("Serialized replay should normalize explicit compile commands before post-compile defaults."), ReplayResult.Commands[2].Type, EVergilCommandType::CompileBlueprint);
	TestEqual(TEXT("Serialized replay should normalize class-default commands last."), ReplayResult.Commands[3].Type, EVergilCommandType::SetClassDefault);
	TestEqual(TEXT("Serialized replay should update the Blueprint description."), Blueprint->BlueprintDescription, FString(TEXT("Serialized replay path.")));

	UClass* const GeneratedClass = Blueprint->GeneratedClass.Get();
	TestNotNull(TEXT("Serialized replay should leave a generated class available."), GeneratedClass);
	FBoolProperty* const VariableProperty = GeneratedClass != nullptr
		? FindFProperty<FBoolProperty>(GeneratedClass, VariableName)
		: nullptr;
	TestNotNull(TEXT("Serialized replay should create the authored bool variable."), VariableProperty);
	TestTrue(
		TEXT("Serialized replay should apply the authored class default after compile."),
		VariableProperty != nullptr
			&& GeneratedClass != nullptr
			&& VariableProperty->GetPropertyValue_InContainer(GeneratedClass->GetDefaultObject()));

	return VariableProperty != nullptr && GeneratedClass != nullptr;
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

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, true, true);

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

bool FVergilBlueprintMetadataPlanningTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_BlueprintMetadataPlanning");
	Document.Metadata.Add(TEXT("HideCategories"), TEXT("Rendering, Actor"));
	Document.Metadata.Add(TEXT("BlueprintDisplayName"), TEXT("Vergil Metadata Blueprint"));
	Document.Metadata.Add(TEXT("BlueprintCategory"), TEXT("Vergil|Planning"));
	Document.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Blueprint metadata planning test."));

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Blueprint metadata planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Blueprint metadata planning should emit metadata commands plus the target graph ensure."), Result.Commands.Num(), 5);
	if (!Result.bSucceeded || Result.Commands.Num() != 5)
	{
		return false;
	}

	TestEqual(TEXT("First metadata command should target BlueprintCategory after deterministic key sorting."), Result.Commands[0].Type, EVergilCommandType::SetBlueprintMetadata);
	TestEqual(TEXT("First metadata command should target BlueprintCategory."), Result.Commands[0].Name, FName(TEXT("BlueprintCategory")));
	TestEqual(TEXT("First metadata command should preserve the authored category value."), Result.Commands[0].StringValue, FString(TEXT("Vergil|Planning")));
	TestEqual(TEXT("Second metadata command should target BlueprintDescription."), Result.Commands[1].Name, FName(TEXT("BlueprintDescription")));
	TestEqual(TEXT("Second metadata command should preserve the authored description."), Result.Commands[1].StringValue, FString(TEXT("Blueprint metadata planning test.")));
	TestEqual(TEXT("Third metadata command should target BlueprintDisplayName."), Result.Commands[2].Name, FName(TEXT("BlueprintDisplayName")));
	TestEqual(TEXT("Third metadata command should preserve the authored display name."), Result.Commands[2].StringValue, FString(TEXT("Vergil Metadata Blueprint")));
	TestEqual(TEXT("Fourth metadata command should target HideCategories."), Result.Commands[3].Name, FName(TEXT("HideCategories")));
	TestEqual(TEXT("Fourth metadata command should preserve the authored hide-categories value."), Result.Commands[3].StringValue, FString(TEXT("Rendering, Actor")));
	TestEqual(TEXT("Event graph ensure should still be emitted after blueprint metadata commands."), Result.Commands[4].Type, EVergilCommandType::EnsureGraph);

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

bool FVergilBlueprintMetadataAuthoringExecutionTest::RunTest(const FString& Parameters)
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

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilBlueprintMetadataAuthoring");
	Document.Metadata.Add(TEXT("BlueprintDisplayName"), TEXT("Vergil Metadata Asset"));
	Document.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Blueprint metadata authoring test."));
	Document.Metadata.Add(TEXT("BlueprintCategory"), TEXT("Vergil|Automation"));
	Document.Metadata.Add(TEXT("HideCategories"), TEXT("Rendering, Actor;Actor\nInput"));

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, true, true);

	TestTrue(TEXT("Blueprint metadata authoring should succeed."), Result.bSucceeded);
	TestTrue(TEXT("Blueprint metadata authoring should apply commands."), Result.bApplied);
	TestEqual(TEXT("Blueprint metadata authoring should execute metadata commands plus the target graph ensure."), Result.ExecutedCommandCount, 5);
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	TestEqual(TEXT("Blueprint display name should match the authored metadata."), Blueprint->BlueprintDisplayName, FString(TEXT("Vergil Metadata Asset")));
	TestEqual(TEXT("Blueprint description should match the authored metadata."), Blueprint->BlueprintDescription, FString(TEXT("Blueprint metadata authoring test.")));
	TestEqual(TEXT("Blueprint category should match the authored metadata."), Blueprint->BlueprintCategory, FString(TEXT("Vergil|Automation")));
	TestEqual(TEXT("HideCategories should dedupe and sort the authored categories."), Blueprint->HideCategories.Num(), 3);
	if (Blueprint->HideCategories.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("First hide category should sort lexically."), Blueprint->HideCategories[0], FString(TEXT("Actor")));
	TestEqual(TEXT("Second hide category should sort lexically."), Blueprint->HideCategories[1], FString(TEXT("Input")));
	TestEqual(TEXT("Third hide category should sort lexically."), Blueprint->HideCategories[2], FString(TEXT("Rendering")));

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
	VisualComponent.TemplateProperties.Add(TEXT("HiddenInGame"), TEXT("True"));
	VisualComponent.TemplateProperties.Add(TEXT("CastShadow"), TEXT("False"));

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
	TestEqual(TEXT("Component definition planning should emit component commands plus the target graph ensure."), Result.Commands.Num(), 11);
	if (!Result.bSucceeded || Result.Commands.Num() != 11)
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
	TestEqual(TEXT("Template properties should lower into component property commands."), Result.Commands[7].Type, EVergilCommandType::SetComponentProperty);
	TestEqual(TEXT("Template property lowering should sort keys deterministically."), Result.Commands[7].Name, FName(TEXT("CastShadow")));
	TestEqual(TEXT("CastShadow command should preserve the authored value."), Result.Commands[7].StringValue, FString(TEXT("False")));
	TestEqual(TEXT("Second template property command should target HiddenInGame."), Result.Commands[8].Name, FName(TEXT("HiddenInGame")));
	TestEqual(TEXT("Second template property command should preserve the authored value."), Result.Commands[8].StringValue, FString(TEXT("True")));
	TestEqual(TEXT("Visual relative scale should lower into a component property command after template properties."), Result.Commands[9].Name, FName(TEXT("RelativeScale3D")));
	TestEqual(TEXT("Event graph ensure should still be emitted for the compile target."), Result.Commands[10].Type, EVergilCommandType::EnsureGraph);

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

bool FVergilClassDefaultDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ClassDefaultPlanning");
	Document.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));
	Document.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("2.5"));

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("EventGraph");

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Class default definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Class default definition planning should emit class-default commands plus the target graph ensure."), Result.Commands.Num(), 3);
	if (!Result.bSucceeded || Result.Commands.Num() != 3)
	{
		return false;
	}

	TestEqual(TEXT("Deterministic command ordering should emit the target graph ensure before post-compile defaults."), Result.Commands[0].Type, EVergilCommandType::EnsureGraph);
	TestEqual(TEXT("Planner should lower class defaults into SetClassDefault."), Result.Commands[1].Type, EVergilCommandType::SetClassDefault);
	TestEqual(TEXT("Class default lowering should sort keys deterministically."), Result.Commands[1].Name, FName(TEXT("InitialLifeSpan")));
	TestEqual(TEXT("InitialLifeSpan should preserve the authored value."), Result.Commands[1].StringValue, FString(TEXT("2.5")));
	TestEqual(TEXT("Second class default command should target Replicates."), Result.Commands[2].Name, FName(TEXT("Replicates")));
	TestEqual(TEXT("Second class default command should preserve the authored value."), Result.Commands[2].StringValue, FString(TEXT("True")));

	return true;
}

bool FVergilConstructionScriptDefinitionPlanningTest::RunTest(const FString& Parameters)
{
	FVergilGraphNode ConstructionEventNode;
	ConstructionEventNode.Id = FGuid::NewGuid();
	ConstructionEventNode.Kind = EVergilNodeKind::Event;
	ConstructionEventNode.Descriptor = TEXT("K2.Event.UserConstructionScript");
	ConstructionEventNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin ConstructionThenPin;
	ConstructionThenPin.Id = FGuid::NewGuid();
	ConstructionThenPin.Name = TEXT("Then");
	ConstructionThenPin.Direction = EVergilPinDirection::Output;
	ConstructionThenPin.bIsExec = true;
	ConstructionEventNode.Pins.Add(ConstructionThenPin);

	FVergilGraphNode PrintNode;
	PrintNode.Id = FGuid::NewGuid();
	PrintNode.Kind = EVergilNodeKind::Call;
	PrintNode.Descriptor = TEXT("K2.Call.PrintString");
	PrintNode.Position = FVector2D(350.0f, 0.0f);
	PrintNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin PrintExecPin;
	PrintExecPin.Id = FGuid::NewGuid();
	PrintExecPin.Name = TEXT("Execute");
	PrintExecPin.Direction = EVergilPinDirection::Input;
	PrintExecPin.bIsExec = true;
	PrintNode.Pins.Add(PrintExecPin);

	FVergilGraphEdge ConstructionEdge;
	ConstructionEdge.Id = FGuid::NewGuid();
	ConstructionEdge.SourceNodeId = ConstructionEventNode.Id;
	ConstructionEdge.SourcePinId = ConstructionThenPin.Id;
	ConstructionEdge.TargetNodeId = PrintNode.Id;
	ConstructionEdge.TargetPinId = PrintExecPin.Id;

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Game/Tests/BP_ConstructionScriptPlanning");
	Document.ConstructionScriptNodes = { ConstructionEventNode, PrintNode };
	Document.ConstructionScriptEdges.Add(ConstructionEdge);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = MakeTestBlueprint();
	Request.Document = Document;
	Request.TargetGraphName = TEXT("UserConstructionScript");
	Request.bAutoLayout = false;

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult Result = CompilerService.Compile(Request);

	TestTrue(TEXT("Construction script definition planning should succeed."), Result.bSucceeded);
	TestEqual(TEXT("Construction script definition planning should emit ensure, node, and connect commands."), Result.Commands.Num(), 4);
	if (!Result.bSucceeded || Result.Commands.Num() != 4)
	{
		return false;
	}

	TestEqual(TEXT("First command should ensure the construction script graph."), Result.Commands[0].Type, EVergilCommandType::EnsureGraph);
	TestEqual(TEXT("Ensure graph should target UserConstructionScript."), Result.Commands[0].GraphName, FName(TEXT("UserConstructionScript")));
	TestEqual(TEXT("Ensure graph should preserve the compile target name."), Result.Commands[0].Name, FName(TEXT("UserConstructionScript")));
	TestEqual(TEXT("Construction event should lower through the event handler."), Result.Commands[1].Name, FName(TEXT("Vergil.K2.Event")));
	TestEqual(TEXT("Construction event should preserve the construction-script graph name."), Result.Commands[1].GraphName, FName(TEXT("UserConstructionScript")));
	TestEqual(TEXT("Construction event should preserve the authored suffix."), Result.Commands[1].SecondaryName, FName(TEXT("UserConstructionScript")));
	TestEqual(TEXT("Print node should target the construction-script graph."), Result.Commands[2].GraphName, FName(TEXT("UserConstructionScript")));
	TestEqual(TEXT("Connection planning should target the construction-script graph."), Result.Commands[3].GraphName, FName(TEXT("UserConstructionScript")));

	return true;
}

bool FVergilConstructionScriptAuthoringExecutionTest::RunTest(const FString& Parameters)
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

	FVergilGraphNode ConstructionEventNode;
	ConstructionEventNode.Id = FGuid::NewGuid();
	ConstructionEventNode.Kind = EVergilNodeKind::Event;
	ConstructionEventNode.Descriptor = TEXT("K2.Event.UserConstructionScript");
	ConstructionEventNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin ConstructionThenPin;
	ConstructionThenPin.Id = FGuid::NewGuid();
	ConstructionThenPin.Name = TEXT("Then");
	ConstructionThenPin.Direction = EVergilPinDirection::Output;
	ConstructionThenPin.bIsExec = true;
	ConstructionEventNode.Pins.Add(ConstructionThenPin);

	FVergilGraphNode SequenceNode;
	SequenceNode.Id = FGuid::NewGuid();
	SequenceNode.Kind = EVergilNodeKind::Custom;
	SequenceNode.Descriptor = TEXT("K2.Sequence");
	SequenceNode.Position = FVector2D(320.0f, 0.0f);

	FVergilGraphPin SequenceExecPin;
	SequenceExecPin.Id = FGuid::NewGuid();
	SequenceExecPin.Name = TEXT("Execute");
	SequenceExecPin.Direction = EVergilPinDirection::Input;
	SequenceExecPin.bIsExec = true;
	SequenceNode.Pins.Add(SequenceExecPin);

	FVergilGraphEdge ConstructionEdge;
	ConstructionEdge.Id = FGuid::NewGuid();
	ConstructionEdge.SourceNodeId = ConstructionEventNode.Id;
	ConstructionEdge.SourcePinId = ConstructionThenPin.Id;
	ConstructionEdge.TargetNodeId = SequenceNode.Id;
	ConstructionEdge.TargetPinId = SequenceExecPin.Id;

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilConstructionScriptAuthoring");
	Document.ConstructionScriptNodes = { ConstructionEventNode, SequenceNode };
	Document.ConstructionScriptEdges.Add(ConstructionEdge);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocumentToGraph(
		Blueprint,
		Document,
		UEdGraphSchema_K2::FN_UserConstructionScript,
		false,
		false,
		true);

	TestTrue(TEXT("Construction script authoring should succeed."), Result.bSucceeded);
	TestTrue(TEXT("Construction script authoring should apply commands."), Result.bApplied);
	TestTrue(TEXT("Construction script authoring should execute commands."), Result.ExecutedCommandCount > 0);
	if (!Result.bSucceeded || !Result.bApplied)
	{
		return false;
	}

	UEdGraph* const ConstructionScriptGraph = FindBlueprintGraphByName(Blueprint, UEdGraphSchema_K2::FN_UserConstructionScript);
	TestNotNull(TEXT("Construction script graph should exist after authoring."), ConstructionScriptGraph);
	if (ConstructionScriptGraph == nullptr)
	{
		return false;
	}

	UK2Node_FunctionEntry* const ConstructionEntryNode = FindGraphNodeByGuid<UK2Node_FunctionEntry>(ConstructionScriptGraph, ConstructionEventNode.Id);
	UK2Node_ExecutionSequence* const SequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(ConstructionScriptGraph, SequenceNode.Id);

	TestNotNull(TEXT("Construction script should reuse the graph entry node."), ConstructionEntryNode);
	TestNotNull(TEXT("Construction script should create the Sequence node."), SequenceGraphNode);
	if (ConstructionEntryNode == nullptr || SequenceGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const ConstructionThenGraphPin = ConstructionEntryNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SequenceExecGraphPin = SequenceGraphNode->GetExecPin();
	TestTrue(TEXT("Construction entry should execute the Sequence node."), ConstructionThenGraphPin != nullptr && ConstructionThenGraphPin->LinkedTo.Contains(SequenceExecGraphPin));

	return true;
}

bool FVergilSaveReloadCompileRoundtripTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	FScopedPersistentTestBlueprint PersistentBlueprint(TEXT("BP_VergilRoundtrip"));
	UBlueprint* const Blueprint = PersistentBlueprint.CreateBlueprintAsset();
	TestNotNull(TEXT("Persistent test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FName RoundtripVariableName(TEXT("RoundtripFlag"));
	const FName FunctionName(TEXT("ComputeStatus"));
	const FName MacroName(TEXT("RouteTarget"));
	const FName RootComponentName(TEXT("Root"));
	const FName VisualComponentName(TEXT("VisualMesh"));

	FVergilVariableDefinition RoundtripVariable;
	RoundtripVariable.Name = RoundtripVariableName;
	RoundtripVariable.Type.PinCategory = TEXT("bool");
	RoundtripVariable.DefaultValue = TEXT("true");
	RoundtripVariable.Category = TEXT("State");
	RoundtripVariable.Metadata.Add(TEXT("Tooltip"), TEXT("Persists across save and reload."));

	FVergilFunctionDefinition Function;
	Function.Name = FunctionName;
	Function.bPure = true;
	Function.AccessSpecifier = EVergilFunctionAccessSpecifier::Protected;

	FVergilFunctionParameterDefinition ThresholdInput;
	ThresholdInput.Name = TEXT("Threshold");
	ThresholdInput.Type.PinCategory = TEXT("float");
	Function.Inputs.Add(ThresholdInput);

	FVergilFunctionParameterDefinition ResultOutput;
	ResultOutput.Name = TEXT("Result");
	ResultOutput.Type.PinCategory = TEXT("bool");
	Function.Outputs.Add(ResultOutput);

	FVergilMacroDefinition Macro;
	Macro.Name = MacroName;

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

	FVergilMacroParameterDefinition CountOutput;
	CountOutput.Name = TEXT("Count");
	CountOutput.Type.PinCategory = TEXT("int");
	Macro.Outputs.Add(CountOutput);

	FVergilComponentDefinition RootComponent;
	RootComponent.Name = RootComponentName;
	RootComponent.ComponentClassPath = USceneComponent::StaticClass()->GetClassPathName().ToString();
	RootComponent.RelativeTransform.bHasRelativeLocation = true;
	RootComponent.RelativeTransform.RelativeLocation = FVector(25.0f, 10.0f, 5.0f);

	FVergilComponentDefinition VisualComponent;
	VisualComponent.Name = VisualComponentName;
	VisualComponent.ComponentClassPath = UStaticMeshComponent::StaticClass()->GetClassPathName().ToString();
	VisualComponent.ParentComponentName = RootComponentName;
	VisualComponent.AttachSocketName = TEXT("GripSocket");
	VisualComponent.TemplateProperties.Add(TEXT("HiddenInGame"), TEXT("True"));
	VisualComponent.TemplateProperties.Add(TEXT("CastShadow"), TEXT("False"));

	FVergilInterfaceDefinition Interface;
	Interface.InterfaceClassPath = UVergilAutomationTestInterface::StaticClass()->GetClassPathName().ToString();

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

	FVergilGraphNode EventSequenceNode;
	EventSequenceNode.Id = FGuid::NewGuid();
	EventSequenceNode.Kind = EVergilNodeKind::Custom;
	EventSequenceNode.Descriptor = TEXT("K2.Sequence");
	EventSequenceNode.Position = FVector2D(320.0f, 0.0f);

	FVergilGraphPin EventSequenceExecPin;
	EventSequenceExecPin.Id = FGuid::NewGuid();
	EventSequenceExecPin.Name = TEXT("Execute");
	EventSequenceExecPin.Direction = EVergilPinDirection::Input;
	EventSequenceExecPin.bIsExec = true;
	EventSequenceNode.Pins.Add(EventSequenceExecPin);

	FVergilGraphEdge EventGraphEdge;
	EventGraphEdge.Id = FGuid::NewGuid();
	EventGraphEdge.SourceNodeId = BeginPlayNode.Id;
	EventGraphEdge.SourcePinId = BeginPlayThenPin.Id;
	EventGraphEdge.TargetNodeId = EventSequenceNode.Id;
	EventGraphEdge.TargetPinId = EventSequenceExecPin.Id;

	FVergilGraphDocument EventDocument;
	EventDocument.BlueprintPath = PersistentBlueprint.PackagePath;
	EventDocument.Metadata.Add(TEXT("BlueprintDisplayName"), TEXT("Vergil Roundtrip Asset"));
	EventDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Persisted save/reload/compile coverage."));
	EventDocument.Metadata.Add(TEXT("BlueprintCategory"), TEXT("Vergil|Roundtrip"));
	EventDocument.Metadata.Add(TEXT("HideCategories"), TEXT("Rendering, Actor"));
	EventDocument.Variables.Add(RoundtripVariable);
	EventDocument.Functions.Add(Function);
	EventDocument.Macros.Add(Macro);
	EventDocument.Components = { RootComponent, VisualComponent };
	EventDocument.Interfaces.Add(Interface);
	EventDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));
	EventDocument.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("4.5"));
	EventDocument.Nodes = { BeginPlayNode, EventSequenceNode };
	EventDocument.Edges.Add(EventGraphEdge);

	FVergilGraphNode ConstructionEventNode;
	ConstructionEventNode.Id = FGuid::NewGuid();
	ConstructionEventNode.Kind = EVergilNodeKind::Event;
	ConstructionEventNode.Descriptor = TEXT("K2.Event.UserConstructionScript");
	ConstructionEventNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin ConstructionThenPin;
	ConstructionThenPin.Id = FGuid::NewGuid();
	ConstructionThenPin.Name = TEXT("Then");
	ConstructionThenPin.Direction = EVergilPinDirection::Output;
	ConstructionThenPin.bIsExec = true;
	ConstructionEventNode.Pins.Add(ConstructionThenPin);

	FVergilGraphNode ConstructionSequenceNode;
	ConstructionSequenceNode.Id = FGuid::NewGuid();
	ConstructionSequenceNode.Kind = EVergilNodeKind::Custom;
	ConstructionSequenceNode.Descriptor = TEXT("K2.Sequence");
	ConstructionSequenceNode.Position = FVector2D(320.0f, 0.0f);

	FVergilGraphPin ConstructionSequenceExecPin;
	ConstructionSequenceExecPin.Id = FGuid::NewGuid();
	ConstructionSequenceExecPin.Name = TEXT("Execute");
	ConstructionSequenceExecPin.Direction = EVergilPinDirection::Input;
	ConstructionSequenceExecPin.bIsExec = true;
	ConstructionSequenceNode.Pins.Add(ConstructionSequenceExecPin);

	FVergilGraphEdge ConstructionEdge;
	ConstructionEdge.Id = FGuid::NewGuid();
	ConstructionEdge.SourceNodeId = ConstructionEventNode.Id;
	ConstructionEdge.SourcePinId = ConstructionThenPin.Id;
	ConstructionEdge.TargetNodeId = ConstructionSequenceNode.Id;
	ConstructionEdge.TargetPinId = ConstructionSequenceExecPin.Id;

	FVergilGraphDocument ConstructionDocument;
	ConstructionDocument.BlueprintPath = PersistentBlueprint.PackagePath;
	ConstructionDocument.ConstructionScriptNodes = { ConstructionEventNode, ConstructionSequenceNode };
	ConstructionDocument.ConstructionScriptEdges.Add(ConstructionEdge);

	auto MakeMessage = [](const TCHAR* Phase, const TCHAR* Message) -> FString
	{
		return FString::Printf(TEXT("%s: %s"), Phase, Message);
	};

	auto VerifyBlueprintState = [&](UBlueprint* CandidateBlueprint, const TCHAR* Phase) -> bool
	{
		TestNotNull(*MakeMessage(Phase, TEXT("Blueprint should exist.")), CandidateBlueprint);
		if (CandidateBlueprint == nullptr)
		{
			return false;
		}

		TestEqual(*MakeMessage(Phase, TEXT("Blueprint display name should persist.")),
			CandidateBlueprint->BlueprintDisplayName,
			FString(TEXT("Vergil Roundtrip Asset")));
		TestEqual(*MakeMessage(Phase, TEXT("Blueprint description should persist.")),
			CandidateBlueprint->BlueprintDescription,
			FString(TEXT("Persisted save/reload/compile coverage.")));
		TestEqual(*MakeMessage(Phase, TEXT("Blueprint category should persist.")),
			CandidateBlueprint->BlueprintCategory,
			FString(TEXT("Vergil|Roundtrip")));
		TestEqual(*MakeMessage(Phase, TEXT("HideCategories count should persist.")),
			CandidateBlueprint->HideCategories.Num(),
			2);
		if (CandidateBlueprint->HideCategories.Num() == 2)
		{
			TestEqual(*MakeMessage(Phase, TEXT("First hide category should remain sorted.")),
				CandidateBlueprint->HideCategories[0],
				FString(TEXT("Actor")));
			TestEqual(*MakeMessage(Phase, TEXT("Second hide category should remain sorted.")),
				CandidateBlueprint->HideCategories[1],
				FString(TEXT("Rendering")));
		}

		const FBPVariableDescription* const VariableDescription = FindBlueprintVariableDescription(CandidateBlueprint, RoundtripVariableName);
		TestNotNull(*MakeMessage(Phase, TEXT("Roundtrip variable should exist.")), VariableDescription);
		if (VariableDescription == nullptr)
		{
			return false;
		}

		TestTrue(*MakeMessage(Phase, TEXT("Roundtrip variable should remain a bool.")),
			VariableDescription->VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean);
		TestEqual(*MakeMessage(Phase, TEXT("Roundtrip variable category should persist.")),
			FBlueprintEditorUtils::GetBlueprintVariableCategory(CandidateBlueprint, RoundtripVariableName, nullptr).ToString(),
			FString(TEXT("State")));

		FString VariableTooltip;
		TestTrue(*MakeMessage(Phase, TEXT("Roundtrip variable tooltip should exist.")),
			FBlueprintEditorUtils::GetBlueprintVariableMetaData(CandidateBlueprint, RoundtripVariableName, nullptr, TEXT("Tooltip"), VariableTooltip));
		TestEqual(*MakeMessage(Phase, TEXT("Roundtrip variable tooltip should persist.")),
			VariableTooltip,
			FString(TEXT("Persists across save and reload.")));

		UEdGraph* const FunctionGraph = FindBlueprintGraphByName(CandidateBlueprint, FunctionName);
		UK2Node_FunctionEntry* const FunctionEntry = FindFunctionEntryNode(FunctionGraph);
		UK2Node_FunctionResult* const FunctionResult = FindFunctionResultNode(FunctionGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("Function graph should persist.")), FunctionGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("Function entry should persist.")), FunctionEntry);
		TestNotNull(*MakeMessage(Phase, TEXT("Function result should persist.")), FunctionResult);
		if (FunctionGraph == nullptr || FunctionEntry == nullptr || FunctionResult == nullptr)
		{
			return false;
		}

		UEdGraphPin* const ThresholdPin = FunctionEntry->FindPin(TEXT("Threshold"));
		UEdGraphPin* const ResultPin = FunctionResult->FindPin(TEXT("Result"));
		TestNotNull(*MakeMessage(Phase, TEXT("Function Threshold input should persist.")), ThresholdPin);
		TestNotNull(*MakeMessage(Phase, TEXT("Function Result output should persist.")), ResultPin);
		TestTrue(*MakeMessage(Phase, TEXT("Function purity should persist.")), FunctionEntry->HasAllExtraFlags(FUNC_BlueprintPure));
		TestTrue(*MakeMessage(Phase, TEXT("Function protected access should persist.")), FunctionEntry->HasAllExtraFlags(FUNC_Protected));

		UEdGraph* const MacroGraph = FindBlueprintGraphByName(CandidateBlueprint, MacroName);
		UK2Node_EditablePinBase* const MacroEntry = FindEditableGraphEntryNode(MacroGraph);
		UK2Node_EditablePinBase* const MacroExit = FindEditableGraphResultNode(MacroGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("Macro graph should persist.")), MacroGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("Macro entry tunnel should persist.")), MacroEntry);
		TestNotNull(*MakeMessage(Phase, TEXT("Macro exit tunnel should persist.")), MacroExit);
		if (MacroGraph == nullptr || MacroEntry == nullptr || MacroExit == nullptr)
		{
			return false;
		}

		TestNotNull(*MakeMessage(Phase, TEXT("Macro Execute pin should persist.")), MacroEntry->FindPin(TEXT("Execute")));
		TestNotNull(*MakeMessage(Phase, TEXT("Macro TargetActor pin should persist.")), MacroEntry->FindPin(TEXT("TargetActor")));
		TestNotNull(*MakeMessage(Phase, TEXT("Macro Then pin should persist.")), MacroExit->FindPin(TEXT("Then")));
		TestNotNull(*MakeMessage(Phase, TEXT("Macro Count pin should persist.")), MacroExit->FindPin(TEXT("Count")));

		USCS_Node* const RootNode = FindBlueprintComponentNode(CandidateBlueprint, RootComponentName);
		USCS_Node* const VisualNode = FindBlueprintComponentNode(CandidateBlueprint, VisualComponentName);
		TestNotNull(*MakeMessage(Phase, TEXT("Root component should persist.")), RootNode);
		TestNotNull(*MakeMessage(Phase, TEXT("Visual component should persist.")), VisualNode);
		if (RootNode == nullptr || VisualNode == nullptr || CandidateBlueprint->SimpleConstructionScript == nullptr)
		{
			return false;
		}

		TestTrue(*MakeMessage(Phase, TEXT("Visual component should remain attached to the root component.")),
			CandidateBlueprint->SimpleConstructionScript->FindParentNode(VisualNode) == RootNode);
		TestEqual(*MakeMessage(Phase, TEXT("Visual attach socket should persist.")),
			VisualNode->AttachToName,
			FName(TEXT("GripSocket")));

		USceneComponent* const RootTemplate = Cast<USceneComponent>(RootNode->ComponentTemplate);
		UStaticMeshComponent* const VisualTemplate = Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate);
		TestNotNull(*MakeMessage(Phase, TEXT("Root component template should be a scene component.")), RootTemplate);
		TestNotNull(*MakeMessage(Phase, TEXT("Visual component template should be a static mesh component.")), VisualTemplate);
		TestTrue(*MakeMessage(Phase, TEXT("Root component location should persist.")),
			RootTemplate != nullptr
				&& RootTemplate->GetRelativeLocation().Equals(FVector(25.0f, 10.0f, 5.0f), KINDA_SMALL_NUMBER));

		const FBoolProperty* const HiddenInGameProperty = VisualTemplate != nullptr
			? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("bHiddenInGame"))
			: nullptr;
		const FBoolProperty* const CastShadowProperty = VisualTemplate != nullptr
			? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("CastShadow"))
			: nullptr;
		TestNotNull(*MakeMessage(Phase, TEXT("Visual HiddenInGame property should exist.")), HiddenInGameProperty);
		TestNotNull(*MakeMessage(Phase, TEXT("Visual CastShadow property should exist.")), CastShadowProperty);
		TestTrue(*MakeMessage(Phase, TEXT("Visual HiddenInGame value should persist.")),
			HiddenInGameProperty != nullptr && VisualTemplate != nullptr && HiddenInGameProperty->GetPropertyValue_InContainer(VisualTemplate));
		TestTrue(*MakeMessage(Phase, TEXT("Visual CastShadow value should persist.")),
			CastShadowProperty != nullptr && VisualTemplate != nullptr && !CastShadowProperty->GetPropertyValue_InContainer(VisualTemplate));

		TArray<UClass*> ImplementedInterfaces;
		FBlueprintEditorUtils::FindImplementedInterfaces(CandidateBlueprint, true, ImplementedInterfaces);
		TestTrue(*MakeMessage(Phase, TEXT("Implemented interface should persist.")),
			ImplementedInterfaces.Contains(UVergilAutomationTestInterface::StaticClass()));

		AActor* const BlueprintCDO = CandidateBlueprint->GeneratedClass != nullptr
			? Cast<AActor>(CandidateBlueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		TestNotNull(*MakeMessage(Phase, TEXT("Generated class should exist.")), CandidateBlueprint->GeneratedClass.Get());
		TestNotNull(*MakeMessage(Phase, TEXT("Generated class default object should exist.")), BlueprintCDO);
		if (CandidateBlueprint->GeneratedClass == nullptr || BlueprintCDO == nullptr)
		{
			return false;
		}

		const FBoolProperty* const RoundtripFlagProperty = FindFProperty<FBoolProperty>(CandidateBlueprint->GeneratedClass, RoundtripVariableName);
		TestNotNull(*MakeMessage(Phase, TEXT("Generated class should expose the roundtrip variable.")), RoundtripFlagProperty);
		TestTrue(*MakeMessage(Phase, TEXT("Roundtrip variable default should persist on the CDO.")),
			RoundtripFlagProperty != nullptr && RoundtripFlagProperty->GetPropertyValue_InContainer(BlueprintCDO));
		TestTrue(*MakeMessage(Phase, TEXT("Replicates class default should persist.")), BlueprintCDO->GetIsReplicated());
		TestTrue(*MakeMessage(Phase, TEXT("InitialLifeSpan class default should persist.")),
			FMath::IsNearlyEqual(BlueprintCDO->InitialLifeSpan, 4.5f));

		UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(CandidateBlueprint);
		UK2Node_Event* const BeginPlayGraphNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
		UK2Node_ExecutionSequence* const EventSequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, EventSequenceNode.Id);
		TestNotNull(*MakeMessage(Phase, TEXT("Event graph should persist.")), EventGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("BeginPlay node should persist.")), BeginPlayGraphNode);
		TestNotNull(*MakeMessage(Phase, TEXT("Event Sequence node should persist.")), EventSequenceGraphNode);
		if (EventGraph == nullptr || BeginPlayGraphNode == nullptr || EventSequenceGraphNode == nullptr)
		{
			return false;
		}

		UEdGraphPin* const EventThenGraphPin = BeginPlayGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* const EventSequenceGraphPin = EventSequenceGraphNode->GetExecPin();
		TestTrue(*MakeMessage(Phase, TEXT("BeginPlay should remain connected to Sequence.")),
			EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(EventSequenceGraphPin));

		UEdGraph* const ConstructionGraph = FindBlueprintGraphByName(CandidateBlueprint, UEdGraphSchema_K2::FN_UserConstructionScript);
		UK2Node_FunctionEntry* const ConstructionEntry = FindGraphNodeByGuid<UK2Node_FunctionEntry>(ConstructionGraph, ConstructionEventNode.Id);
		UK2Node_ExecutionSequence* const ConstructionSequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(ConstructionGraph, ConstructionSequenceNode.Id);
		TestNotNull(*MakeMessage(Phase, TEXT("Construction script graph should persist.")), ConstructionGraph);
		TestNotNull(*MakeMessage(Phase, TEXT("Construction entry should persist.")), ConstructionEntry);
		TestNotNull(*MakeMessage(Phase, TEXT("Construction Sequence node should persist.")), ConstructionSequenceGraphNode);
		if (ConstructionGraph == nullptr || ConstructionEntry == nullptr || ConstructionSequenceGraphNode == nullptr)
		{
			return false;
		}

		UEdGraphPin* const ConstructionThenGraphPin = ConstructionEntry->FindPin(UEdGraphSchema_K2::PN_Then);
		UEdGraphPin* const ConstructionSequenceGraphPin = ConstructionSequenceGraphNode->GetExecPin();
		TestTrue(*MakeMessage(Phase, TEXT("Construction entry should remain connected to Sequence.")),
			ConstructionThenGraphPin != nullptr && ConstructionThenGraphPin->LinkedTo.Contains(ConstructionSequenceGraphPin));

		return true;
	};

	const FVergilCompileResult InitialEventResult = EditorSubsystem->CompileDocument(Blueprint, EventDocument, false, false, true);
	TestTrue(TEXT("Initial roundtrip event-graph authoring should succeed."), InitialEventResult.bSucceeded);
	TestTrue(TEXT("Initial roundtrip event-graph authoring should apply commands."), InitialEventResult.bApplied);
	if (!InitialEventResult.bSucceeded || !InitialEventResult.bApplied)
	{
		return false;
	}

	const FVergilCompileResult InitialConstructionResult = EditorSubsystem->CompileDocumentToGraph(
		Blueprint,
		ConstructionDocument,
		UEdGraphSchema_K2::FN_UserConstructionScript,
		false,
		false,
		true);
	TestTrue(TEXT("Initial roundtrip construction-script authoring should succeed."), InitialConstructionResult.bSucceeded);
	TestTrue(TEXT("Initial roundtrip construction-script authoring should apply commands."), InitialConstructionResult.bApplied);
	if (!InitialConstructionResult.bSucceeded || !InitialConstructionResult.bApplied)
	{
		return false;
	}

	if (!VerifyBlueprintState(Blueprint, TEXT("Initial authoring")))
	{
		return false;
	}

	FString SaveErrorMessage;
	const bool bSavedPackage = PersistentBlueprint.Save(Blueprint, &SaveErrorMessage);
	TestTrue(TEXT("Persistent roundtrip blueprint package should save cleanly."), bSavedPackage);
	if (!SaveErrorMessage.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("Roundtrip save status: %s"), *SaveErrorMessage));
	}
	if (!bSavedPackage)
	{
		return false;
	}

	FString ReloadErrorMessage;
	UBlueprint* const ReloadedBlueprint = PersistentBlueprint.Reload(&ReloadErrorMessage);
	TestNotNull(TEXT("Persistent roundtrip blueprint should reload from disk."), ReloadedBlueprint);
	if (!ReloadErrorMessage.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("Roundtrip reload status: %s"), *ReloadErrorMessage));
	}
	if (ReloadedBlueprint == nullptr)
	{
		return false;
	}

	if (!VerifyBlueprintState(ReloadedBlueprint, TEXT("Post-reload state")))
	{
		return false;
	}

	const FVergilCompileResult RoundtripEventResult = EditorSubsystem->CompileDocument(ReloadedBlueprint, EventDocument, false, false, false);
	TestTrue(TEXT("Roundtrip event-graph dry-run compile should succeed after reload."), RoundtripEventResult.bSucceeded);
	TestFalse(TEXT("Roundtrip event-graph dry-run compile should not apply commands after reload."), RoundtripEventResult.bApplied);
	if (!RoundtripEventResult.bSucceeded)
	{
		return false;
	}

	const FVergilCompileResult RoundtripConstructionResult = EditorSubsystem->CompileDocumentToGraph(
		ReloadedBlueprint,
		ConstructionDocument,
		UEdGraphSchema_K2::FN_UserConstructionScript,
		false,
		false,
		false);
	TestTrue(TEXT("Roundtrip construction-script dry-run compile should succeed after reload."), RoundtripConstructionResult.bSucceeded);
	TestFalse(TEXT("Roundtrip construction-script dry-run compile should not apply commands after reload."), RoundtripConstructionResult.bApplied);
	if (!RoundtripConstructionResult.bSucceeded)
	{
		return false;
	}

	TestEqual(TEXT("Event-graph roundtrip should preserve command-plan fingerprints."),
		RoundtripEventResult.Statistics.CommandPlanFingerprint,
		InitialEventResult.Statistics.CommandPlanFingerprint);
	TestEqual(TEXT("Construction-script roundtrip should preserve command-plan fingerprints."),
		RoundtripConstructionResult.Statistics.CommandPlanFingerprint,
		InitialConstructionResult.Statistics.CommandPlanFingerprint);
	TestEqual(TEXT("Event-graph roundtrip should preserve planned command counts."),
		RoundtripEventResult.Commands.Num(),
		InitialEventResult.Commands.Num());
	TestEqual(TEXT("Construction-script roundtrip should preserve planned command counts."),
		RoundtripConstructionResult.Commands.Num(),
		InitialConstructionResult.Commands.Num());

	FKismetEditorUtilities::CompileBlueprint(ReloadedBlueprint);
	TestTrue(TEXT("Reloaded blueprint should compile cleanly after roundtrip dry-run verification."), ReloadedBlueprint->IsUpToDate());
	TestNotNull(TEXT("Reloaded blueprint should retain a generated class after compile."), ReloadedBlueprint->GeneratedClass.Get());
	if (ReloadedBlueprint->GeneratedClass == nullptr)
	{
		return false;
	}

	return VerifyBlueprintState(ReloadedBlueprint, TEXT("Post-compile state"));
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
	VisualComponent.TemplateProperties.Add(TEXT("HiddenInGame"), TEXT("True"));
	VisualComponent.TemplateProperties.Add(TEXT("CastShadow"), TEXT("False"));

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
	UStaticMeshComponent* VisualTemplate = Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate);
	TestNotNull(TEXT("Root component template should be a scene component."), RootTemplate);
	TestNotNull(TEXT("Pivot component template should be a scene component."), PivotTemplate);
	TestNotNull(TEXT("Visual component template should be a static mesh component."), VisualTemplate);
	TestTrue(TEXT("Root component should retain its authored relative location."), RootTemplate != nullptr
		&& RootTemplate->GetRelativeLocation().Equals(FVector(10.0f, 20.0f, 30.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Pivot component should retain its authored relative rotation."), PivotTemplate != nullptr
		&& PivotTemplate->GetRelativeRotation().Equals(FRotator(0.0f, 45.0f, 0.0f), KINDA_SMALL_NUMBER));
	FBoolProperty* HiddenInGameProperty = VisualTemplate != nullptr
		? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("bHiddenInGame"))
		: nullptr;
	FBoolProperty* CastShadowProperty = VisualTemplate != nullptr
		? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("CastShadow"))
		: nullptr;
	TestNotNull(TEXT("Visual component should expose bHiddenInGame."), HiddenInGameProperty);
	TestNotNull(TEXT("Visual component should expose CastShadow."), CastShadowProperty);
	TestTrue(TEXT("Visual component should retain its authored HiddenInGame template property."), HiddenInGameProperty != nullptr
		&& VisualTemplate != nullptr
		&& HiddenInGameProperty->GetPropertyValue_InContainer(VisualTemplate));
	TestTrue(TEXT("Visual component should retain its authored CastShadow template property."), CastShadowProperty != nullptr
		&& VisualTemplate != nullptr
		&& !CastShadowProperty->GetPropertyValue_InContainer(VisualTemplate));

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
	UpdatedVisualComponent.TemplateProperties.Add(TEXT("HiddenInGame"), TEXT("False"));
	UpdatedVisualComponent.TemplateProperties.Add(TEXT("CastShadow"), TEXT("True"));

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
	VisualTemplate = Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate);
	TestNotNull(TEXT("Visual component template should be a static mesh component."), VisualTemplate);
	TestTrue(TEXT("Root component should retain its updated relative location."), RootTemplate != nullptr
		&& RootTemplate->GetRelativeLocation().Equals(FVector(40.0f, 0.0f, 10.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Pivot component should retain its updated relative rotation."), PivotTemplate != nullptr
		&& PivotTemplate->GetRelativeRotation().Equals(FRotator(0.0f, 90.0f, 0.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Visual component should retain its authored relative location."), VisualTemplate != nullptr
		&& VisualTemplate->GetRelativeLocation().Equals(FVector(15.0f, 5.0f, 0.0f), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Visual component should retain its authored relative scale."), VisualTemplate != nullptr
		&& VisualTemplate->GetRelativeScale3D().Equals(FVector(1.0f, 1.5f, 1.0f), KINDA_SMALL_NUMBER));
	HiddenInGameProperty = VisualTemplate != nullptr
		? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("bHiddenInGame"))
		: nullptr;
	CastShadowProperty = VisualTemplate != nullptr
		? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("CastShadow"))
		: nullptr;
	TestNotNull(TEXT("Updated visual component should expose bHiddenInGame."), HiddenInGameProperty);
	TestNotNull(TEXT("Updated visual component should expose CastShadow."), CastShadowProperty);
	TestTrue(TEXT("Visual component should update HiddenInGame through template properties."), HiddenInGameProperty != nullptr
		&& VisualTemplate != nullptr
		&& !HiddenInGameProperty->GetPropertyValue_InContainer(VisualTemplate));
	TestTrue(TEXT("Visual component should update CastShadow through template properties."), CastShadowProperty != nullptr
		&& VisualTemplate != nullptr
		&& CastShadowProperty->GetPropertyValue_InContainer(VisualTemplate));

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

bool FVergilClassDefaultAuthoringExecutionTest::RunTest(const FString& Parameters)
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

	FVergilGraphDocument InitialDocument;
	InitialDocument.BlueprintPath = TEXT("/Temp/BP_VergilClassDefaultAuthoring");
	InitialDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("True"));
	InitialDocument.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("2.5"));

	const FVergilCompileResult InitialResult = EditorSubsystem->CompileDocument(Blueprint, InitialDocument, false, false, true);
	TestTrue(TEXT("Initial class default authoring should succeed."), InitialResult.bSucceeded);
	TestTrue(TEXT("Initial class default authoring should apply commands."), InitialResult.bApplied);
	if (!InitialResult.bSucceeded || !InitialResult.bApplied)
	{
		return false;
	}

	AActor* BlueprintCDO = Blueprint->GeneratedClass != nullptr ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	TestNotNull(TEXT("Generated class default object should exist after initial class default authoring."), BlueprintCDO);
	if (BlueprintCDO == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Replicates should apply through class default authoring."), BlueprintCDO->GetIsReplicated());
	TestTrue(TEXT("InitialLifeSpan should apply through class default authoring."), FMath::IsNearlyEqual(BlueprintCDO->InitialLifeSpan, 2.5f));

	FVergilGraphDocument UpdatedDocument;
	UpdatedDocument.BlueprintPath = InitialDocument.BlueprintPath;
	UpdatedDocument.ClassDefaults.Add(TEXT("Replicates"), TEXT("False"));
	UpdatedDocument.ClassDefaults.Add(TEXT("InitialLifeSpan"), TEXT("7.25"));

	const FVergilCompileResult UpdatedResult = EditorSubsystem->CompileDocument(Blueprint, UpdatedDocument, false, false, true);
	TestTrue(TEXT("Class default update should succeed."), UpdatedResult.bSucceeded);
	TestTrue(TEXT("Class default update should apply commands."), UpdatedResult.bApplied);
	if (!UpdatedResult.bSucceeded || !UpdatedResult.bApplied)
	{
		return false;
	}

	BlueprintCDO = Blueprint->GeneratedClass != nullptr ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	TestNotNull(TEXT("Generated class default object should still exist after update."), BlueprintCDO);
	if (BlueprintCDO == nullptr)
	{
		return false;
	}

	TestFalse(TEXT("Replicates should update through class default authoring."), BlueprintCDO->GetIsReplicated());
	TestTrue(TEXT("InitialLifeSpan should update through class default authoring."), FMath::IsNearlyEqual(BlueprintCDO->InitialLifeSpan, 7.25f));

	return true;
}

bool FVergilCommandPlanningTest::RunTest(const FString& Parameters)
{
	FVergilGraphNode EventNode;
	EventNode.Id = FGuid::NewGuid();
	EventNode.Kind = EVergilNodeKind::Event;
	EventNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
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
	CallNode.Descriptor = TEXT("K2.Call.K2_DestroyActor");
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
	Request.bAutoLayout = false;

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
	TestEqual(TEXT("Symbol resolution should normalize the call owner path into the planned command."), Result.Commands[2].StringValue, AActor::StaticClass()->GetPathName());
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
	Request.bAutoLayout = false;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilAutoLayoutExecutionTest,
	"Vergil.Scaffold.AutoLayoutExecution",
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

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilCommentExecution");
	Document.Nodes.Add(CommentNode);

	FVergilCompileRequest Request = EditorSubsystem->MakeCompileRequest(Blueprint, Document, TEXT("EventGraph"), false, true);
	TestEqual(TEXT("The explicit editor request builder should seed the UE_5.7 default comment width."), Request.CommentGeneration.DefaultWidth, 400.0f);
	TestEqual(TEXT("The explicit editor request builder should seed the UE_5.7 default comment height."), Request.CommentGeneration.DefaultHeight, 100.0f);
	TestEqual(TEXT("The explicit editor request builder should seed the UE_5.7 default comment font size."), Request.CommentGeneration.DefaultFontSize, 18);
	TestEqual(TEXT("The explicit editor request builder should seed the UE_5.7 default comment move mode."), Request.CommentGeneration.MoveMode, EVergilCommentMoveMode::GroupMovement);
	TestTrue(TEXT("The explicit editor request builder should seed the UE_5.7 default zoom-bubble visibility."), Request.CommentGeneration.bShowBubbleWhenZoomed);
	TestFalse(TEXT("The explicit editor request builder should seed the UE_5.7 default bubble-color behavior."), Request.CommentGeneration.bColorBubble);

	Request.CommentGeneration.DefaultHeight = 180.0f;
	Request.CommentGeneration.DefaultFontSize = 20;
	Request.CommentGeneration.DefaultColor = FLinearColor(FColor(0x44, 0xAA, 0x66, 0xFF));
	Request.CommentGeneration.bShowBubbleWhenZoomed = false;
	Request.CommentGeneration.bColorBubble = true;
	Request.CommentGeneration.MoveMode = EVergilCommentMoveMode::NoGroupMovement;

	const FVergilCompileResult Result = EditorSubsystem->CompileRequest(Request, true);

	TestTrue(TEXT("Comment document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Command plan should be applied."), Result.bApplied);
	TestTrue(TEXT("At least one command should execute."), Result.ExecutedCommandCount > 0);
	TestTrue(TEXT("Comment planning should emit commands."), Result.Commands.Num() >= 2);
	TestNotNull(
		TEXT("Planner should still use the explicit Vergil comment node command."),
		Result.Commands.FindByPredicate([CommentNode](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddNode
				&& Command.NodeId == CommentNode.Id
				&& Command.Name == TEXT("Vergil.Comment");
		}));

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
	TestEqual(TEXT("Comment height should match the explicit comment-generation defaults when metadata omits it."), Comment->NodeHeight, 180);
	TestEqual(TEXT("Comment font size should match the explicit comment-generation defaults when metadata omits it."), Comment->FontSize, 20);
	TestEqual(TEXT("Comment bubble visibility should match the explicit comment-generation defaults."), static_cast<bool>(Comment->bCommentBubbleVisible_InDetailsPanel), false);
	TestEqual(TEXT("Comment bubble color usage should match the explicit comment-generation defaults."), static_cast<bool>(Comment->bColorCommentBubble), true);
	TestEqual(TEXT("Comment move mode should match the explicit comment-generation defaults."), static_cast<int32>(Comment->MoveMode.GetValue()), static_cast<int32>(ECommentBoxMode::NoGroupMovement));
	TestEqual(TEXT("Comment color should match the explicit comment-generation defaults."), Comment->CommentColor.ToFColor(true), FColor(0x44, 0xAA, 0x66, 0xFF));

	return true;
}

bool FVergilAutoLayoutExecutionTest::RunTest(const FString& Parameters)
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
	BeginPlayNode.Position = FVector2D(840.0f, 320.0f);

	FVergilGraphPin BeginPlayThen;
	BeginPlayThen.Id = FGuid::NewGuid();
	BeginPlayThen.Name = TEXT("Then");
	BeginPlayThen.Direction = EVergilPinDirection::Output;
	BeginPlayThen.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThen);

	FVergilGraphNode SequenceNode;
	SequenceNode.Id = FGuid::NewGuid();
	SequenceNode.Kind = EVergilNodeKind::ControlFlow;
	SequenceNode.Descriptor = TEXT("K2.Sequence");
	SequenceNode.Position = FVector2D(120.0f, 680.0f);

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

	FVergilGraphNode CommentNode;
	CommentNode.Id = FGuid::NewGuid();
	CommentNode.Kind = EVergilNodeKind::Comment;
	CommentNode.Descriptor = TEXT("UI.Comment");
	CommentNode.Position = FVector2D(1024.0f, -180.0f);
	CommentNode.Metadata.Add(TEXT("CommentText"), TEXT("Auto-layout comment"));
	CommentNode.Metadata.Add(TEXT("CommentWidth"), TEXT("520"));
	CommentNode.Metadata.Add(TEXT("CommentHeight"), TEXT("180"));

	FVergilGraphEdge ExecEdge;
	ExecEdge.Id = FGuid::NewGuid();
	ExecEdge.SourceNodeId = BeginPlayNode.Id;
	ExecEdge.SourcePinId = BeginPlayThen.Id;
	ExecEdge.TargetNodeId = SequenceNode.Id;
	ExecEdge.TargetPinId = SequenceExecIn.Id;

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilAutoLayoutExecution");
	Document.Nodes = { BeginPlayNode, SequenceNode, CommentNode };
	Document.Edges.Add(ExecEdge);

	FVergilCompileRequest Request;
	Request.TargetBlueprint = Blueprint;
	Request.Document = Document;
	Request.bAutoLayout = true;
	Request.bGenerateComments = true;
	Request.AutoLayout.HorizontalSpacing = 240.0f;
	Request.AutoLayout.VerticalSpacing = 200.0f;
	Request.AutoLayout.CommentPadding = 72.0f;

	const FVergilBlueprintCompilerService CompilerService;
	const FVergilCompileResult PlannedResult = CompilerService.Compile(Request);
	TestTrue(TEXT("Auto-layout execution coverage should compile successfully."), PlannedResult.bSucceeded);
	if (!PlannedResult.bSucceeded)
	{
		return false;
	}

	TestTrue(
		TEXT("The deterministic auto-layout pass should emit movement commands before apply."),
		PlannedResult.Commands.ContainsByPredicate([](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::MoveNode;
		}));

	const FVergilCompileResult ApplyResult = EditorSubsystem->ExecuteCommandPlan(Blueprint, PlannedResult.Commands);
	TestTrue(TEXT("Auto-layout command plan should apply cleanly."), ApplyResult.bSucceeded);
	TestTrue(TEXT("Auto-layout command plan should mutate the blueprint."), ApplyResult.bApplied);
	if (!ApplyResult.bSucceeded || !ApplyResult.bApplied)
	{
		return false;
	}

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after auto-layout apply."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventGraphNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_ExecutionSequence* const SequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, SequenceNode.Id);
	UEdGraphNode_Comment* const CommentGraphNode = FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, CommentNode.Id);

	TestNotNull(TEXT("Auto-layout apply should create the BeginPlay node."), EventGraphNode);
	TestNotNull(TEXT("Auto-layout apply should create the sequence node."), SequenceGraphNode);
	TestNotNull(TEXT("Auto-layout apply should create the comment node."), CommentGraphNode);
	if (EventGraphNode == nullptr || SequenceGraphNode == nullptr || CommentGraphNode == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Auto-layout should anchor the first event node at the requested origin."), EventGraphNode->NodePosX, 0);
	TestEqual(TEXT("Auto-layout should anchor the first event node at the requested origin Y."), EventGraphNode->NodePosY, 0);
	TestEqual(TEXT("Auto-layout should place downstream nodes one column to the right."), SequenceGraphNode->NodePosX, 240);
	TestEqual(TEXT("Auto-layout should keep the downstream sequence on the first row when it is the only child."), SequenceGraphNode->NodePosY, 0);
	TestEqual(TEXT("Comment auto-layout should place comments to the left of the primary band using the requested padding."), CommentGraphNode->NodePosX, -592);
	TestEqual(TEXT("Comment auto-layout should align the first comment row to the primary origin."), CommentGraphNode->NodePosY, 0);

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

	FVergilCompilerCommand SetBlueprintDescription;
	SetBlueprintDescription.Type = EVergilCommandType::SetBlueprintMetadata;
	SetBlueprintDescription.Name = TEXT("BlueprintDescription");
	SetBlueprintDescription.StringValue = TEXT("Explicit command metadata surface.");
	Commands.Add(SetBlueprintDescription);

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

	TestEqual(TEXT("Explicit blueprint metadata commands should update the Blueprint description."), Blueprint->BlueprintDescription, FString(TEXT("Explicit command metadata surface.")));
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
	FVergilCommandPlanOrderingTest,
	"Vergil.Scaffold.CommandPlanOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilCommandPlanOrderingTest::RunTest(const FString& Parameters)
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

	const FName VariableName(TEXT("DeterministicFlag"));

	FVergilCompilerCommand SetClassDefault;
	SetClassDefault.Type = EVergilCommandType::SetClassDefault;
	SetClassDefault.Name = VariableName;
	SetClassDefault.StringValue = TEXT("True");

	FVergilCompilerCommand CompileBlueprint;
	CompileBlueprint.Type = EVergilCommandType::CompileBlueprint;

	FVergilCompilerCommand EnsureVariable;
	EnsureVariable.Type = EVergilCommandType::EnsureVariable;
	EnsureVariable.SecondaryName = VariableName;
	EnsureVariable.Attributes.Add(TEXT("PinCategory"), TEXT("bool"));

	const TArray<FVergilCompilerCommand> Commands = { SetClassDefault, CompileBlueprint, EnsureVariable };
	const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, Commands);

	TestTrue(TEXT("Phase-normalized command plan should apply cleanly."), Result.bSucceeded);
	TestTrue(TEXT("Phase-normalized command plan should mutate the blueprint."), Result.bApplied);
	TestEqual(TEXT("Every normalized command should execute exactly once."), Result.ExecutedCommandCount, Commands.Num());
	TestEqual(TEXT("Returned command plan should preserve command count."), Result.Commands.Num(), Commands.Num());
	if (!Result.bSucceeded || !Result.bApplied || Result.Commands.Num() != Commands.Num())
	{
		return false;
	}

	TestEqual(TEXT("Blueprint-definition commands should be normalized first."), Result.Commands[0].Type, EVergilCommandType::EnsureVariable);
	TestEqual(TEXT("Explicit compile commands should normalize before post-compile defaults."), Result.Commands[1].Type, EVergilCommandType::CompileBlueprint);
	TestEqual(TEXT("Class default commands should normalize last."), Result.Commands[2].Type, EVergilCommandType::SetClassDefault);

	UClass* const GeneratedClass = Blueprint->GeneratedClass.Get();
	TestNotNull(TEXT("Generated class should exist after normalized command execution."), GeneratedClass);
	FBoolProperty* const VariableProperty = GeneratedClass != nullptr
		? FindFProperty<FBoolProperty>(GeneratedClass, VariableName)
		: nullptr;
	TestNotNull(TEXT("Normalized command execution should create the authored bool variable."), VariableProperty);
	TestTrue(
		TEXT("Normalized command execution should apply the authored class default after compile."),
		VariableProperty != nullptr
			&& GeneratedClass != nullptr
			&& VariableProperty->GetPropertyValue_InContainer(GeneratedClass->GetDefaultObject()));

	return VariableProperty != nullptr && GeneratedClass != nullptr;
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
	FVergilCommandPlanValidationTest,
	"Vergil.Scaffold.CommandPlanValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilCommandPlanValidationTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for preflight-mutation coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FVergilCompilerCommand EnsureFunctionGraph;
		EnsureFunctionGraph.Type = EVergilCommandType::EnsureFunctionGraph;
		EnsureFunctionGraph.GraphName = TEXT("ShouldNotExist");
		EnsureFunctionGraph.SecondaryName = EnsureFunctionGraph.GraphName;

		FVergilCompilerCommand InvalidEnsureComponent;
		InvalidEnsureComponent.Type = EVergilCommandType::EnsureComponent;
		InvalidEnsureComponent.SecondaryName = TEXT("BrokenComponent");

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { EnsureFunctionGraph, InvalidEnsureComponent });

		TestFalse(TEXT("Invalid command plans should fail execution."), Result.bSucceeded);
		TestFalse(TEXT("Invalid command plans should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Preflight failures should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Invalid component command should be reported during preflight."), ContainsDiagnostic(Result.Diagnostics, TEXT("InvalidEnsureComponentCommand")));
		TestNull(TEXT("Preflight failure should not create a function graph."), FindBlueprintGraphByName(Blueprint, TEXT("ShouldNotExist")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for metadata reference coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		const FGuid CommentNodeId = FGuid::NewGuid();

		FVergilCompilerCommand SetMetadataBeforeAdd;
		SetMetadataBeforeAdd.Type = EVergilCommandType::SetNodeMetadata;
		SetMetadataBeforeAdd.GraphName = TEXT("EventGraph");
		SetMetadataBeforeAdd.NodeId = CommentNodeId;
		SetMetadataBeforeAdd.Name = TEXT("CommentText");
		SetMetadataBeforeAdd.StringValue = TEXT("Should never apply");

		FVergilCompilerCommand AddComment;
		AddComment.Type = EVergilCommandType::AddNode;
		AddComment.GraphName = TEXT("EventGraph");
		AddComment.NodeId = CommentNodeId;
		AddComment.Name = TEXT("Vergil.Comment");
		AddComment.Position = FVector2D(64.0f, 64.0f);

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { SetMetadataBeforeAdd, AddComment });

		TestFalse(TEXT("Metadata commands that reference a future AddNode should fail execution."), Result.bSucceeded);
		TestFalse(TEXT("Metadata preflight failures should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Metadata preflight failures should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Metadata preflight should report the missing target node."), ContainsDiagnostic(Result.Diagnostics, TEXT("CommandValidationMetadataTargetMissing")));

		UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		TestNotNull(TEXT("Event graph should still exist on transient blueprints."), EventGraph);
		TestNull(TEXT("Preflight failure should not create the deferred comment node."), EventGraph != nullptr ? FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, CommentNodeId) : nullptr);
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for blueprint metadata validation coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		const FString OriginalBlueprintDescription = Blueprint->BlueprintDescription;

		FVergilCompilerCommand SetSupportedBlueprintMetadata;
		SetSupportedBlueprintMetadata.Type = EVergilCommandType::SetBlueprintMetadata;
		SetSupportedBlueprintMetadata.Name = TEXT("BlueprintDescription");
		SetSupportedBlueprintMetadata.StringValue = TEXT("Should never apply");

		FVergilCompilerCommand SetUnsupportedBlueprintMetadata;
		SetUnsupportedBlueprintMetadata.Type = EVergilCommandType::SetBlueprintMetadata;
		SetUnsupportedBlueprintMetadata.Name = TEXT("UnsupportedKey");
		SetUnsupportedBlueprintMetadata.StringValue = TEXT("Also should never apply");

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(
			Blueprint,
			{ SetSupportedBlueprintMetadata, SetUnsupportedBlueprintMetadata });

		TestFalse(TEXT("Unsupported blueprint metadata commands should fail execution."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported blueprint metadata commands should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Unsupported blueprint metadata commands should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Unsupported blueprint metadata keys should emit a preflight diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("BlueprintMetadataKeyUnsupported")));
		TestEqual(TEXT("Blueprint metadata preflight failure should preserve the original Blueprint description."), Blueprint->BlueprintDescription, OriginalBlueprintDescription);
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for duplicate node coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		const FGuid DuplicateNodeId = FGuid::NewGuid();

		FVergilCompilerCommand AddFirstComment;
		AddFirstComment.Type = EVergilCommandType::AddNode;
		AddFirstComment.GraphName = TEXT("EventGraph");
		AddFirstComment.NodeId = DuplicateNodeId;
		AddFirstComment.Name = TEXT("Vergil.Comment");
		AddFirstComment.Position = FVector2D(0.0f, 0.0f);

		FVergilCompilerCommand AddSecondComment;
		AddSecondComment.Type = EVergilCommandType::AddNode;
		AddSecondComment.GraphName = TEXT("EventGraph");
		AddSecondComment.NodeId = DuplicateNodeId;
		AddSecondComment.Name = TEXT("Vergil.Comment");
		AddSecondComment.Position = FVector2D(200.0f, 0.0f);

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { AddFirstComment, AddSecondComment });

		TestFalse(TEXT("Duplicate AddNode ids should fail execution."), Result.bSucceeded);
		TestFalse(TEXT("Duplicate AddNode ids should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Duplicate AddNode ids should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Duplicate AddNode ids should emit a preflight diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("CommandValidationNodeIdDuplicate")));

		UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		TestNotNull(TEXT("Event graph should exist for duplicate node coverage."), EventGraph);
		TestNull(TEXT("Duplicate node validation should prevent comment creation."), EventGraph != nullptr ? FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, DuplicateNodeId) : nullptr);
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for duplicate pin coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		const FGuid FirstNodeId = FGuid::NewGuid();
		const FGuid SecondNodeId = FGuid::NewGuid();
		const FGuid DuplicatePinId = FGuid::NewGuid();

		FVergilCompilerCommand AddFirstComment;
		AddFirstComment.Type = EVergilCommandType::AddNode;
		AddFirstComment.GraphName = TEXT("EventGraph");
		AddFirstComment.NodeId = FirstNodeId;
		AddFirstComment.Name = TEXT("Vergil.Comment");
		AddFirstComment.Position = FVector2D(0.0f, 0.0f);

		FVergilPlannedPin FirstPlannedPin;
		FirstPlannedPin.PinId = DuplicatePinId;
		FirstPlannedPin.Name = TEXT("SharedPin");
		AddFirstComment.PlannedPins.Add(FirstPlannedPin);

		FVergilCompilerCommand AddSecondComment;
		AddSecondComment.Type = EVergilCommandType::AddNode;
		AddSecondComment.GraphName = TEXT("EventGraph");
		AddSecondComment.NodeId = SecondNodeId;
		AddSecondComment.Name = TEXT("Vergil.Comment");
		AddSecondComment.Position = FVector2D(200.0f, 0.0f);

		FVergilPlannedPin SecondPlannedPin;
		SecondPlannedPin.PinId = DuplicatePinId;
		SecondPlannedPin.Name = TEXT("SharedPin");
		AddSecondComment.PlannedPins.Add(SecondPlannedPin);

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { AddFirstComment, AddSecondComment });

		TestFalse(TEXT("Duplicate planned pin ids should fail execution."), Result.bSucceeded);
		TestFalse(TEXT("Duplicate planned pin ids should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Duplicate planned pin ids should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Duplicate planned pin ids should emit a preflight diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("CommandValidationPinIdDuplicate")));

		UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		TestNotNull(TEXT("Event graph should exist for duplicate pin coverage."), EventGraph);
		TestNull(TEXT("Duplicate pin validation should prevent the first comment creation."), EventGraph != nullptr ? FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, FirstNodeId) : nullptr);
		TestNull(TEXT("Duplicate pin validation should prevent the second comment creation."), EventGraph != nullptr ? FindGraphNodeByGuid<UEdGraphNode_Comment>(EventGraph, SecondNodeId) : nullptr);
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for select index preflight coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FVergilCompilerCommand InvalidSelectCommand;
		InvalidSelectCommand.Type = EVergilCommandType::AddNode;
		InvalidSelectCommand.GraphName = TEXT("EventGraph");
		InvalidSelectCommand.NodeId = FGuid::NewGuid();
		InvalidSelectCommand.Name = TEXT("Vergil.K2.Select");
		InvalidSelectCommand.Attributes.Add(TEXT("IndexPinCategory"), TEXT("string"));
		InvalidSelectCommand.Attributes.Add(TEXT("ValuePinCategory"), TEXT("object"));
		InvalidSelectCommand.Attributes.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());
		InvalidSelectCommand.Attributes.Add(TEXT("NumOptions"), TEXT("2"));

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { InvalidSelectCommand });

		TestFalse(TEXT("Unsupported select index categories should fail command-plan validation."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported select index categories should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Unsupported select index categories should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Unsupported select index categories should emit the dedicated preflight diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSelectIndexTypeCombination")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for impure variable getter preflight coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FEdGraphPinType FloatType;
		FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
		FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		TestTrue(TEXT("FloatValue member variable should be added for impure variable getter validation."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("FloatValue"), FloatType, TEXT("1.0")));

		FVergilCompilerCommand InvalidGetterCommand;
		InvalidGetterCommand.Type = EVergilCommandType::AddNode;
		InvalidGetterCommand.GraphName = TEXT("EventGraph");
		InvalidGetterCommand.NodeId = FGuid::NewGuid();
		InvalidGetterCommand.Name = TEXT("Vergil.K2.VariableGet");
		InvalidGetterCommand.SecondaryName = TEXT("FloatValue");

		FVergilPlannedPin ExecutePin;
		ExecutePin.PinId = FGuid::NewGuid();
		ExecutePin.Name = TEXT("Execute");
		ExecutePin.bIsInput = true;
		ExecutePin.bIsExec = true;
		InvalidGetterCommand.PlannedPins.Add(ExecutePin);

		FVergilPlannedPin ThenPin;
		ThenPin.PinId = FGuid::NewGuid();
		ThenPin.Name = TEXT("Then");
		ThenPin.bIsInput = false;
		ThenPin.bIsExec = true;
		InvalidGetterCommand.PlannedPins.Add(ThenPin);

		FVergilPlannedPin ElsePin;
		ElsePin.PinId = FGuid::NewGuid();
		ElsePin.Name = TEXT("Else");
		ElsePin.bIsInput = false;
		ElsePin.bIsExec = true;
		InvalidGetterCommand.PlannedPins.Add(ElsePin);

		FVergilPlannedPin ValuePin;
		ValuePin.PinId = FGuid::NewGuid();
		ValuePin.Name = TEXT("FloatValue");
		ValuePin.bIsInput = false;
		InvalidGetterCommand.PlannedPins.Add(ValuePin);

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { InvalidGetterCommand });

		TestFalse(TEXT("Unsupported impure variable getter plans should fail command-plan validation."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported impure variable getter plans should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Unsupported impure variable getter plans should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Unsupported impure variable getter plans should emit the dedicated preflight diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedVariableGetVariant")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for spawn actor preflight coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FVergilCompilerCommand InvalidSpawnActorCommand;
		InvalidSpawnActorCommand.Type = EVergilCommandType::AddNode;
		InvalidSpawnActorCommand.GraphName = TEXT("EventGraph");
		InvalidSpawnActorCommand.NodeId = FGuid::NewGuid();
		InvalidSpawnActorCommand.Name = TEXT("Vergil.K2.SpawnActor");
		InvalidSpawnActorCommand.StringValue = UActorComponent::StaticClass()->GetPathName();

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { InvalidSpawnActorCommand });

		TestFalse(TEXT("Non-actor spawn plans should fail command-plan validation."), Result.bSucceeded);
		TestFalse(TEXT("Non-actor spawn plans should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Non-actor spawn plans should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Non-actor spawn plans should emit SpawnActorClassNotActor."), ContainsDiagnostic(Result.Diagnostics, TEXT("SpawnActorClassNotActor")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for spawn actor transform preflight coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FVergilCompilerCommand InvalidSpawnActorCommand;
		InvalidSpawnActorCommand.Type = EVergilCommandType::AddNode;
		InvalidSpawnActorCommand.GraphName = TEXT("EventGraph");
		InvalidSpawnActorCommand.NodeId = FGuid::NewGuid();
		InvalidSpawnActorCommand.Name = TEXT("Vergil.K2.SpawnActor");
		InvalidSpawnActorCommand.StringValue = AActor::StaticClass()->GetPathName();

		FVergilPlannedPin SpawnTransformPin;
		SpawnTransformPin.PinId = FGuid::NewGuid();
		SpawnTransformPin.Name = TEXT("SpawnTransform");
		SpawnTransformPin.bIsInput = true;
		InvalidSpawnActorCommand.PlannedPins.Add(SpawnTransformPin);

		const FVergilCompileResult Result = EditorSubsystem->ExecuteCommandPlan(Blueprint, { InvalidSpawnActorCommand });

		TestFalse(TEXT("Spawn actor plans without a connected SpawnTransform should fail command-plan validation."), Result.bSucceeded);
		TestFalse(TEXT("Spawn actor plans without a connected SpawnTransform should not be marked as applied."), Result.bApplied);
		TestEqual(TEXT("Spawn actor plans without a connected SpawnTransform should execute zero commands."), Result.ExecutedCommandCount, 0);
		TestTrue(TEXT("Spawn actor plans without a connected SpawnTransform should emit SpawnActorTransformConnectionMissing."), ContainsDiagnostic(Result.Diagnostics, TEXT("SpawnActorTransformConnectionMissing")));
	}

	return true;
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
	FVergilSelfFunctionCallExecutionTest,
	"Vergil.Scaffold.SelfFunctionCallExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSelfFunctionCallExecutionTest::RunTest(const FString& Parameters)
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
	FlagVariable.Name = TEXT("InputFlag");
	FlagVariable.Type.PinCategory = TEXT("bool");

	FVergilFunctionDefinition PureFunction;
	PureFunction.Name = TEXT("ComputeLocalResult");
	PureFunction.bPure = true;

	FVergilFunctionParameterDefinition PureInput;
	PureInput.Name = TEXT("Value");
	PureInput.Type.PinCategory = TEXT("bool");
	PureFunction.Inputs.Add(PureInput);

	FVergilFunctionParameterDefinition PureOutput;
	PureOutput.Name = TEXT("Result");
	PureOutput.Type.PinCategory = TEXT("bool");
	PureFunction.Outputs.Add(PureOutput);

	FVergilFunctionDefinition ImpureFunction;
	ImpureFunction.Name = TEXT("ApplyLocalResult");

	FVergilFunctionParameterDefinition ImpureInput;
	ImpureInput.Name = TEXT("Value");
	ImpureInput.Type.PinCategory = TEXT("bool");
	ImpureFunction.Inputs.Add(ImpureInput);

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
	GetterNode.Descriptor = TEXT("K2.VarGet.InputFlag");
	GetterNode.Position = FVector2D(260.0f, -140.0f);

	FVergilGraphPin GetterValue;
	GetterValue.Id = FGuid::NewGuid();
	GetterValue.Name = TEXT("InputFlag");
	GetterValue.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValue);

	FVergilGraphNode PureCallNode;
	PureCallNode.Id = FGuid::NewGuid();
	PureCallNode.Kind = EVergilNodeKind::Call;
	PureCallNode.Descriptor = TEXT("K2.Call.ComputeLocalResult");
	PureCallNode.Position = FVector2D(580.0f, -140.0f);

	FVergilGraphPin PureCallInput;
	PureCallInput.Id = FGuid::NewGuid();
	PureCallInput.Name = TEXT("Value");
	PureCallInput.Direction = EVergilPinDirection::Input;
	PureCallNode.Pins.Add(PureCallInput);

	FVergilGraphPin PureCallOutput;
	PureCallOutput.Id = FGuid::NewGuid();
	PureCallOutput.Name = TEXT("Result");
	PureCallOutput.Direction = EVergilPinDirection::Output;
	PureCallNode.Pins.Add(PureCallOutput);

	FVergilGraphNode ImpureCallNode;
	ImpureCallNode.Id = FGuid::NewGuid();
	ImpureCallNode.Kind = EVergilNodeKind::Call;
	ImpureCallNode.Descriptor = TEXT("K2.Call.ApplyLocalResult");
	ImpureCallNode.Position = FVector2D(580.0f, 0.0f);

	FVergilGraphPin ImpureCallExec;
	ImpureCallExec.Id = FGuid::NewGuid();
	ImpureCallExec.Name = TEXT("Execute");
	ImpureCallExec.Direction = EVergilPinDirection::Input;
	ImpureCallExec.bIsExec = true;
	ImpureCallNode.Pins.Add(ImpureCallExec);

	FVergilGraphPin ImpureCallValue;
	ImpureCallValue.Id = FGuid::NewGuid();
	ImpureCallValue.Name = TEXT("Value");
	ImpureCallValue.Direction = EVergilPinDirection::Input;
	ImpureCallNode.Pins.Add(ImpureCallValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSelfFunctionCall");
	Document.Variables.Add(FlagVariable);
	Document.Functions = { PureFunction, ImpureFunction };
	Document.Nodes = { BeginPlayNode, GetterNode, PureCallNode, ImpureCallNode };

	FVergilGraphEdge EventToImpureCall;
	EventToImpureCall.Id = FGuid::NewGuid();
	EventToImpureCall.SourceNodeId = BeginPlayNode.Id;
	EventToImpureCall.SourcePinId = BeginPlayThen.Id;
	EventToImpureCall.TargetNodeId = ImpureCallNode.Id;
	EventToImpureCall.TargetPinId = ImpureCallExec.Id;
	Document.Edges.Add(EventToImpureCall);

	FVergilGraphEdge GetterToPureCall;
	GetterToPureCall.Id = FGuid::NewGuid();
	GetterToPureCall.SourceNodeId = GetterNode.Id;
	GetterToPureCall.SourcePinId = GetterValue.Id;
	GetterToPureCall.TargetNodeId = PureCallNode.Id;
	GetterToPureCall.TargetPinId = PureCallInput.Id;
	Document.Edges.Add(GetterToPureCall);

	FVergilGraphEdge PureToImpureCall;
	PureToImpureCall.Id = FGuid::NewGuid();
	PureToImpureCall.SourceNodeId = PureCallNode.Id;
	PureToImpureCall.SourcePinId = PureCallOutput.Id;
	PureToImpureCall.TargetNodeId = ImpureCallNode.Id;
	PureToImpureCall.TargetPinId = ImpureCallValue.Id;
	Document.Edges.Add(PureToImpureCall);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Self function call document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Self function call document should be applied."), Result.bApplied);
	TestTrue(TEXT("Self function call document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after self function call execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_VariableGet* const GetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, GetterNode.Id);
	UK2Node_CallFunction* const PureCallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, PureCallNode.Id);
	UK2Node_CallFunction* const ImpureCallGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ImpureCallNode.Id);

	TestNotNull(TEXT("BeginPlay event node should exist."), EventNode);
	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("Pure self call node should exist."), PureCallGraphNode);
	TestNotNull(TEXT("Impure self call node should exist."), ImpureCallGraphNode);
	if (EventNode == nullptr || GetterGraphNode == nullptr || PureCallGraphNode == nullptr || ImpureCallGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const GetterValuePin = GetterGraphNode->FindPin(TEXT("InputFlag"));
	UEdGraphPin* const PureCallValuePin = PureCallGraphNode->FindPin(TEXT("Value"));
	UEdGraphPin* const PureCallResultPin = PureCallGraphNode->FindPin(TEXT("Result"));
	UEdGraphPin* const ImpureCallExecPin = ImpureCallGraphNode->GetExecPin();
	UEdGraphPin* const ImpureCallValuePin = ImpureCallGraphNode->FindPin(TEXT("Value"));

	TestTrue(TEXT("Pure self call should remain pure."), PureCallGraphNode->GetExecPin() == nullptr);
	TestTrue(TEXT("Impure self call should expose an exec pin."), ImpureCallExecPin != nullptr);
	TestTrue(TEXT("BeginPlay should drive the impure self call."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(ImpureCallExecPin));
	TestTrue(TEXT("Getter should feed the pure self call input."), GetterValuePin != nullptr && GetterValuePin->LinkedTo.Contains(PureCallValuePin));
	TestTrue(TEXT("Pure self call result should feed the impure self call input."), PureCallResultPin != nullptr && PureCallResultPin->LinkedTo.Contains(ImpureCallValuePin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilPureDataFlowExecutionTest,
	"Vergil.Scaffold.PureDataFlowExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilVariableGetterVariantsExecutionTest,
	"Vergil.Scaffold.VariableGetterVariantsExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilVariableGetterVariantsExecutionTest::RunTest(const FString& Parameters)
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
	TestTrue(TEXT("StoredFlag member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("StoredFlag"), BoolType, TEXT("false")));

	FEdGraphPinType ActorType;
	ActorType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorType.PinSubCategoryObject = AActor::StaticClass();
	TestTrue(TEXT("TargetActor member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TargetActor"), ActorType, FString()));
	TestTrue(TEXT("StoredActor member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("StoredActor"), ActorType, FString()));

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

	FVergilGraphNode SequenceNode;
	SequenceNode.Id = FGuid::NewGuid();
	SequenceNode.Kind = EVergilNodeKind::Custom;
	SequenceNode.Descriptor = TEXT("K2.Sequence");
	SequenceNode.Position = FVector2D(260.0f, 0.0f);

	FVergilGraphPin SequenceExec;
	SequenceExec.Id = FGuid::NewGuid();
	SequenceExec.Name = TEXT("Execute");
	SequenceExec.Direction = EVergilPinDirection::Input;
	SequenceExec.bIsExec = true;
	SequenceNode.Pins.Add(SequenceExec);

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

	FVergilGraphNode BoolGetterNode;
	BoolGetterNode.Id = FGuid::NewGuid();
	BoolGetterNode.Kind = EVergilNodeKind::VariableGet;
	BoolGetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");
	BoolGetterNode.Position = FVector2D(560.0f, -180.0f);

	FVergilGraphPin BoolGetterExec;
	BoolGetterExec.Id = FGuid::NewGuid();
	BoolGetterExec.Name = TEXT("Execute");
	BoolGetterExec.Direction = EVergilPinDirection::Input;
	BoolGetterExec.bIsExec = true;
	BoolGetterNode.Pins.Add(BoolGetterExec);

	FVergilGraphPin BoolGetterThen;
	BoolGetterThen.Id = FGuid::NewGuid();
	BoolGetterThen.Name = TEXT("Then");
	BoolGetterThen.Direction = EVergilPinDirection::Output;
	BoolGetterThen.bIsExec = true;
	BoolGetterNode.Pins.Add(BoolGetterThen);

	FVergilGraphPin BoolGetterElse;
	BoolGetterElse.Id = FGuid::NewGuid();
	BoolGetterElse.Name = TEXT("Else");
	BoolGetterElse.Direction = EVergilPinDirection::Output;
	BoolGetterElse.bIsExec = true;
	BoolGetterNode.Pins.Add(BoolGetterElse);

	FVergilGraphPin BoolGetterValue;
	BoolGetterValue.Id = FGuid::NewGuid();
	BoolGetterValue.Name = TEXT("TestFlag");
	BoolGetterValue.Direction = EVergilPinDirection::Output;
	BoolGetterNode.Pins.Add(BoolGetterValue);

	FVergilGraphNode BoolSetterNode;
	BoolSetterNode.Id = FGuid::NewGuid();
	BoolSetterNode.Kind = EVergilNodeKind::VariableSet;
	BoolSetterNode.Descriptor = TEXT("K2.VarSet.StoredFlag");
	BoolSetterNode.Position = FVector2D(900.0f, -180.0f);

	FVergilGraphPin BoolSetterExec;
	BoolSetterExec.Id = FGuid::NewGuid();
	BoolSetterExec.Name = TEXT("Execute");
	BoolSetterExec.Direction = EVergilPinDirection::Input;
	BoolSetterExec.bIsExec = true;
	BoolSetterNode.Pins.Add(BoolSetterExec);

	FVergilGraphPin BoolSetterValue;
	BoolSetterValue.Id = FGuid::NewGuid();
	BoolSetterValue.Name = TEXT("StoredFlag");
	BoolSetterValue.Direction = EVergilPinDirection::Input;
	BoolSetterNode.Pins.Add(BoolSetterValue);

	FVergilGraphNode ObjectGetterNode;
	ObjectGetterNode.Id = FGuid::NewGuid();
	ObjectGetterNode.Kind = EVergilNodeKind::VariableGet;
	ObjectGetterNode.Descriptor = TEXT("K2.VarGet.TargetActor");
	ObjectGetterNode.Position = FVector2D(560.0f, 180.0f);

	FVergilGraphPin ObjectGetterExec;
	ObjectGetterExec.Id = FGuid::NewGuid();
	ObjectGetterExec.Name = TEXT("Execute");
	ObjectGetterExec.Direction = EVergilPinDirection::Input;
	ObjectGetterExec.bIsExec = true;
	ObjectGetterNode.Pins.Add(ObjectGetterExec);

	FVergilGraphPin ObjectGetterThen;
	ObjectGetterThen.Id = FGuid::NewGuid();
	ObjectGetterThen.Name = TEXT("Then");
	ObjectGetterThen.Direction = EVergilPinDirection::Output;
	ObjectGetterThen.bIsExec = true;
	ObjectGetterNode.Pins.Add(ObjectGetterThen);

	FVergilGraphPin ObjectGetterElse;
	ObjectGetterElse.Id = FGuid::NewGuid();
	ObjectGetterElse.Name = TEXT("Else");
	ObjectGetterElse.Direction = EVergilPinDirection::Output;
	ObjectGetterElse.bIsExec = true;
	ObjectGetterNode.Pins.Add(ObjectGetterElse);

	FVergilGraphPin ObjectGetterValue;
	ObjectGetterValue.Id = FGuid::NewGuid();
	ObjectGetterValue.Name = TEXT("TargetActor");
	ObjectGetterValue.Direction = EVergilPinDirection::Output;
	ObjectGetterNode.Pins.Add(ObjectGetterValue);

	FVergilGraphNode ObjectSetterNode;
	ObjectSetterNode.Id = FGuid::NewGuid();
	ObjectSetterNode.Kind = EVergilNodeKind::VariableSet;
	ObjectSetterNode.Descriptor = TEXT("K2.VarSet.StoredActor");
	ObjectSetterNode.Position = FVector2D(900.0f, 180.0f);

	FVergilGraphPin ObjectSetterExec;
	ObjectSetterExec.Id = FGuid::NewGuid();
	ObjectSetterExec.Name = TEXT("Execute");
	ObjectSetterExec.Direction = EVergilPinDirection::Input;
	ObjectSetterExec.bIsExec = true;
	ObjectSetterNode.Pins.Add(ObjectSetterExec);

	FVergilGraphPin ObjectSetterValue;
	ObjectSetterValue.Id = FGuid::NewGuid();
	ObjectSetterValue.Name = TEXT("StoredActor");
	ObjectSetterValue.Direction = EVergilPinDirection::Input;
	ObjectSetterNode.Pins.Add(ObjectSetterValue);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilVariableGetterVariants");
	Document.Nodes = { BeginPlayNode, SequenceNode, BoolGetterNode, BoolSetterNode, ObjectGetterNode, ObjectSetterNode };

	FVergilGraphEdge EventToSequence;
	EventToSequence.Id = FGuid::NewGuid();
	EventToSequence.SourceNodeId = BeginPlayNode.Id;
	EventToSequence.SourcePinId = BeginPlayThen.Id;
	EventToSequence.TargetNodeId = SequenceNode.Id;
	EventToSequence.TargetPinId = SequenceExec.Id;
	Document.Edges.Add(EventToSequence);

	FVergilGraphEdge SequenceToBoolGetter;
	SequenceToBoolGetter.Id = FGuid::NewGuid();
	SequenceToBoolGetter.SourceNodeId = SequenceNode.Id;
	SequenceToBoolGetter.SourcePinId = SequenceThen0.Id;
	SequenceToBoolGetter.TargetNodeId = BoolGetterNode.Id;
	SequenceToBoolGetter.TargetPinId = BoolGetterExec.Id;
	Document.Edges.Add(SequenceToBoolGetter);

	FVergilGraphEdge BoolGetterToBoolSetterExec;
	BoolGetterToBoolSetterExec.Id = FGuid::NewGuid();
	BoolGetterToBoolSetterExec.SourceNodeId = BoolGetterNode.Id;
	BoolGetterToBoolSetterExec.SourcePinId = BoolGetterThen.Id;
	BoolGetterToBoolSetterExec.TargetNodeId = BoolSetterNode.Id;
	BoolGetterToBoolSetterExec.TargetPinId = BoolSetterExec.Id;
	Document.Edges.Add(BoolGetterToBoolSetterExec);

	FVergilGraphEdge BoolGetterToBoolSetterValue;
	BoolGetterToBoolSetterValue.Id = FGuid::NewGuid();
	BoolGetterToBoolSetterValue.SourceNodeId = BoolGetterNode.Id;
	BoolGetterToBoolSetterValue.SourcePinId = BoolGetterValue.Id;
	BoolGetterToBoolSetterValue.TargetNodeId = BoolSetterNode.Id;
	BoolGetterToBoolSetterValue.TargetPinId = BoolSetterValue.Id;
	Document.Edges.Add(BoolGetterToBoolSetterValue);

	FVergilGraphEdge SequenceToObjectGetter;
	SequenceToObjectGetter.Id = FGuid::NewGuid();
	SequenceToObjectGetter.SourceNodeId = SequenceNode.Id;
	SequenceToObjectGetter.SourcePinId = SequenceThen1.Id;
	SequenceToObjectGetter.TargetNodeId = ObjectGetterNode.Id;
	SequenceToObjectGetter.TargetPinId = ObjectGetterExec.Id;
	Document.Edges.Add(SequenceToObjectGetter);

	FVergilGraphEdge ObjectGetterToObjectSetterExec;
	ObjectGetterToObjectSetterExec.Id = FGuid::NewGuid();
	ObjectGetterToObjectSetterExec.SourceNodeId = ObjectGetterNode.Id;
	ObjectGetterToObjectSetterExec.SourcePinId = ObjectGetterThen.Id;
	ObjectGetterToObjectSetterExec.TargetNodeId = ObjectSetterNode.Id;
	ObjectGetterToObjectSetterExec.TargetPinId = ObjectSetterExec.Id;
	Document.Edges.Add(ObjectGetterToObjectSetterExec);

	FVergilGraphEdge ObjectGetterToObjectSetterValue;
	ObjectGetterToObjectSetterValue.Id = FGuid::NewGuid();
	ObjectGetterToObjectSetterValue.SourceNodeId = ObjectGetterNode.Id;
	ObjectGetterToObjectSetterValue.SourcePinId = ObjectGetterValue.Id;
	ObjectGetterToObjectSetterValue.TargetNodeId = ObjectSetterNode.Id;
	ObjectGetterToObjectSetterValue.TargetPinId = ObjectSetterValue.Id;
	Document.Edges.Add(ObjectGetterToObjectSetterValue);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Variable getter variants document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Variable getter variants document should be applied."), Result.bApplied);
	TestTrue(TEXT("Variable getter variants document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after variable getter variant execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_ExecutionSequence* const SequenceGraphNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, SequenceNode.Id);
	UK2Node_VariableGet* const BoolGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, BoolGetterNode.Id);
	UK2Node_VariableSet* const BoolSetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, BoolSetterNode.Id);
	UK2Node_VariableGet* const ObjectGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, ObjectGetterNode.Id);
	UK2Node_VariableSet* const ObjectSetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, ObjectSetterNode.Id);

	TestNotNull(TEXT("BeginPlay event node should exist."), EventNode);
	TestNotNull(TEXT("Sequence node should exist."), SequenceGraphNode);
	TestNotNull(TEXT("Bool getter node should exist."), BoolGetterGraphNode);
	TestNotNull(TEXT("Bool setter node should exist."), BoolSetterGraphNode);
	TestNotNull(TEXT("Object getter node should exist."), ObjectGetterGraphNode);
	TestNotNull(TEXT("Object setter node should exist."), ObjectSetterGraphNode);
	if (EventNode == nullptr
		|| SequenceGraphNode == nullptr
		|| BoolGetterGraphNode == nullptr
		|| BoolSetterGraphNode == nullptr
		|| ObjectGetterGraphNode == nullptr
		|| ObjectSetterGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SequenceExecPin = SequenceGraphNode->GetExecPin();
	UEdGraphPin* const SequenceThen0Pin = SequenceGraphNode->FindPin(TEXT("Then_0"));
	UEdGraphPin* const SequenceThen1Pin = SequenceGraphNode->FindPin(TEXT("Then_1"));

	UEdGraphPin* const BoolGetterExecPin = BoolGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const BoolGetterThenPin = BoolGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const BoolGetterElsePin = BoolGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Else);
	UEdGraphPin* const BoolGetterValuePin = BoolGetterGraphNode->FindPin(TEXT("TestFlag"));
	UEdGraphPin* const BoolSetterExecPin = BoolSetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const BoolSetterValuePin = BoolSetterGraphNode->FindPin(TEXT("StoredFlag"));

	UEdGraphPin* const ObjectGetterExecPin = ObjectGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const ObjectGetterThenPin = ObjectGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const ObjectGetterElsePin = ObjectGetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Else);
	UEdGraphPin* const ObjectGetterValuePin = ObjectGetterGraphNode->FindPin(TEXT("TargetActor"));
	UEdGraphPin* const ObjectSetterExecPin = ObjectSetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const ObjectSetterValuePin = ObjectSetterGraphNode->FindPin(TEXT("StoredActor"));

	TestTrue(TEXT("BeginPlay should drive the sequence node."), EventThenPin != nullptr && EventThenPin->LinkedTo.Contains(SequenceExecPin));

	TestFalse(TEXT("Bool variable getter should be impure under the branch variant."), BoolGetterGraphNode->IsNodePure());
	TestTrue(TEXT("Bool getter should expose Execute."), BoolGetterExecPin != nullptr);
	TestTrue(TEXT("Bool getter should expose Then and Else exec outputs."), BoolGetterThenPin != nullptr && BoolGetterElsePin != nullptr);
	TestEqual(TEXT("Bool getter Then pin should use the UE_5.7 True label."), BoolGetterThenPin != nullptr ? BoolGetterThenPin->PinFriendlyName.ToString() : FString(), FString(TEXT("True")));
	TestEqual(TEXT("Bool getter Else pin should use the UE_5.7 False label."), BoolGetterElsePin != nullptr ? BoolGetterElsePin->PinFriendlyName.ToString() : FString(), FString(TEXT("False")));
	TestTrue(TEXT("Sequence Then_0 should drive the bool getter."), SequenceThen0Pin != nullptr && SequenceThen0Pin->LinkedTo.Contains(BoolGetterExecPin));
	TestTrue(TEXT("Bool getter Then should drive the bool setter."), BoolGetterThenPin != nullptr && BoolGetterThenPin->LinkedTo.Contains(BoolSetterExecPin));
	TestTrue(TEXT("Bool getter value should feed the bool setter input."), BoolGetterValuePin != nullptr && BoolGetterValuePin->LinkedTo.Contains(BoolSetterValuePin));

	TestFalse(TEXT("Object variable getter should be impure under the validated-get variant."), ObjectGetterGraphNode->IsNodePure());
	TestTrue(TEXT("Object getter should expose Execute."), ObjectGetterExecPin != nullptr);
	TestTrue(TEXT("Object getter should expose Then and Else exec outputs."), ObjectGetterThenPin != nullptr && ObjectGetterElsePin != nullptr);
	TestEqual(TEXT("Object getter Then pin should use the UE_5.7 Is Valid label."), ObjectGetterThenPin != nullptr ? ObjectGetterThenPin->PinFriendlyName.ToString() : FString(), FString(TEXT("Is Valid")));
	TestEqual(TEXT("Object getter Else pin should use the UE_5.7 Is Not Valid label."), ObjectGetterElsePin != nullptr ? ObjectGetterElsePin->PinFriendlyName.ToString() : FString(), FString(TEXT("Is Not Valid")));
	TestTrue(TEXT("Sequence Then_1 should drive the object getter."), SequenceThen1Pin != nullptr && SequenceThen1Pin->LinkedTo.Contains(ObjectGetterExecPin));
	TestTrue(TEXT("Object getter Then should drive the object setter."), ObjectGetterThenPin != nullptr && ObjectGetterThenPin->LinkedTo.Contains(ObjectSetterExecPin));
	TestTrue(TEXT("Object getter value should feed the object setter input."), ObjectGetterValuePin != nullptr && ObjectGetterValuePin->LinkedTo.Contains(ObjectSetterValuePin));

	return true;
}

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
	FVergilSelectSwitchTypeDiagnosticsTest,
	"Vergil.Scaffold.SelectSwitchTypeDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSelectSwitchTypeDiagnosticsTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	auto ContainsDiagnostic = [](const TArray<FVergilDiagnostic>& Diagnostics, const FName Code)
	{
		return Diagnostics.ContainsByPredicate([Code](const FVergilDiagnostic& Diagnostic)
		{
			return Diagnostic.Code == Code;
		});
	};

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for select mismatch coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FEdGraphPinType BoolType;
		BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		TestTrue(TEXT("TestFlag member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TestFlag"), BoolType, TEXT("false")));

		FEdGraphPinType IntType;
		IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
		TestTrue(TEXT("Mode member variable should be added for select mismatch coverage."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Mode"), IntType, TEXT("0")));

		FVergilGraphNode FlagGetterNode;
		FlagGetterNode.Id = FGuid::NewGuid();
		FlagGetterNode.Kind = EVergilNodeKind::VariableGet;
		FlagGetterNode.Descriptor = TEXT("K2.VarGet.TestFlag");

		FVergilGraphPin FlagGetterValue;
		FlagGetterValue.Id = FGuid::NewGuid();
		FlagGetterValue.Name = TEXT("TestFlag");
		FlagGetterValue.Direction = EVergilPinDirection::Output;
		FlagGetterNode.Pins.Add(FlagGetterValue);

		FVergilGraphNode ModeGetterNode;
		ModeGetterNode.Id = FGuid::NewGuid();
		ModeGetterNode.Kind = EVergilNodeKind::VariableGet;
		ModeGetterNode.Descriptor = TEXT("K2.VarGet.Mode");

		FVergilGraphPin ModeGetterValue;
		ModeGetterValue.Id = FGuid::NewGuid();
		ModeGetterValue.Name = TEXT("Mode");
		ModeGetterValue.Direction = EVergilPinDirection::Output;
		ModeGetterNode.Pins.Add(ModeGetterValue);

		FVergilGraphNode SelectNode;
		SelectNode.Id = FGuid::NewGuid();
		SelectNode.Kind = EVergilNodeKind::Custom;
		SelectNode.Descriptor = TEXT("K2.Select");
		SelectNode.Metadata.Add(TEXT("IndexPinCategory"), TEXT("bool"));
		SelectNode.Metadata.Add(TEXT("ValuePinCategory"), TEXT("object"));
		SelectNode.Metadata.Add(TEXT("ValueObjectPath"), AActor::StaticClass()->GetPathName());

		FVergilGraphPin SelectIndexPin;
		SelectIndexPin.Id = FGuid::NewGuid();
		SelectIndexPin.Name = TEXT("Index");
		SelectIndexPin.Direction = EVergilPinDirection::Input;
		SelectNode.Pins.Add(SelectIndexPin);

		FVergilGraphPin SelectOption0Pin;
		SelectOption0Pin.Id = FGuid::NewGuid();
		SelectOption0Pin.Name = TEXT("Option 0");
		SelectOption0Pin.Direction = EVergilPinDirection::Input;
		SelectNode.Pins.Add(SelectOption0Pin);

		FVergilGraphPin SelectOption1Pin;
		SelectOption1Pin.Id = FGuid::NewGuid();
		SelectOption1Pin.Name = TEXT("Option 1");
		SelectOption1Pin.Direction = EVergilPinDirection::Input;
		SelectNode.Pins.Add(SelectOption1Pin);

		FVergilGraphPin SelectReturnPin;
		SelectReturnPin.Id = FGuid::NewGuid();
		SelectReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
		SelectReturnPin.Direction = EVergilPinDirection::Output;
		SelectNode.Pins.Add(SelectReturnPin);

		FVergilGraphDocument Document;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilSelectTypeDiagnostics");
		Document.Nodes = { FlagGetterNode, ModeGetterNode, SelectNode };

		FVergilGraphEdge FlagToSelectIndex;
		FlagToSelectIndex.Id = FGuid::NewGuid();
		FlagToSelectIndex.SourceNodeId = FlagGetterNode.Id;
		FlagToSelectIndex.SourcePinId = FlagGetterValue.Id;
		FlagToSelectIndex.TargetNodeId = SelectNode.Id;
		FlagToSelectIndex.TargetPinId = SelectIndexPin.Id;
		Document.Edges.Add(FlagToSelectIndex);

		FVergilGraphEdge ModeToSelectValue;
		ModeToSelectValue.Id = FGuid::NewGuid();
		ModeToSelectValue.SourceNodeId = ModeGetterNode.Id;
		ModeToSelectValue.SourcePinId = ModeGetterValue.Id;
		ModeToSelectValue.TargetNodeId = SelectNode.Id;
		ModeToSelectValue.TargetPinId = SelectOption0Pin.Id;
		Document.Edges.Add(ModeToSelectValue);

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
		TestFalse(TEXT("Unsupported select value connections should fail apply-time execution."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported select value connections should not report a successful apply."), Result.bApplied);
		TestTrue(TEXT("Unsupported select value connections should report the dedicated diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSelectValueTypeCombination")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for switch int mismatch coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FEdGraphPinType ArrayBoolType;
		ArrayBoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		ArrayBoolType.ContainerType = EPinContainerType::Array;
		TestTrue(TEXT("Flags member variable should be added for switch int mismatch coverage."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Flags"), ArrayBoolType, FString()));

		FVergilGraphNode GetterNode;
		GetterNode.Id = FGuid::NewGuid();
		GetterNode.Kind = EVergilNodeKind::VariableGet;
		GetterNode.Descriptor = TEXT("K2.VarGet.Flags");

		FVergilGraphPin GetterValue;
		GetterValue.Id = FGuid::NewGuid();
		GetterValue.Name = TEXT("Flags");
		GetterValue.Direction = EVergilPinDirection::Output;
		GetterNode.Pins.Add(GetterValue);

		FVergilGraphNode SwitchNode;
		SwitchNode.Id = FGuid::NewGuid();
		SwitchNode.Kind = EVergilNodeKind::ControlFlow;
		SwitchNode.Descriptor = TEXT("K2.SwitchInt");

		FVergilGraphPin SwitchExecIn;
		SwitchExecIn.Id = FGuid::NewGuid();
		SwitchExecIn.Name = UEdGraphSchema_K2::PN_Execute;
		SwitchExecIn.Direction = EVergilPinDirection::Input;
		SwitchExecIn.bIsExec = true;
		SwitchNode.Pins.Add(SwitchExecIn);

		FVergilGraphPin SwitchSelectionPin;
		SwitchSelectionPin.Id = FGuid::NewGuid();
		SwitchSelectionPin.Name = TEXT("Selection");
		SwitchSelectionPin.Direction = EVergilPinDirection::Input;
		SwitchNode.Pins.Add(SwitchSelectionPin);

		FVergilGraphPin SwitchCase0Pin;
		SwitchCase0Pin.Id = FGuid::NewGuid();
		SwitchCase0Pin.Name = TEXT("0");
		SwitchCase0Pin.Direction = EVergilPinDirection::Output;
		SwitchCase0Pin.bIsExec = true;
		SwitchNode.Pins.Add(SwitchCase0Pin);

		FVergilGraphDocument Document;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilSwitchIntTypeDiagnostics");
		Document.Nodes = { GetterNode, SwitchNode };

		FVergilGraphEdge GetterToSelection;
		GetterToSelection.Id = FGuid::NewGuid();
		GetterToSelection.SourceNodeId = GetterNode.Id;
		GetterToSelection.SourcePinId = GetterValue.Id;
		GetterToSelection.TargetNodeId = SwitchNode.Id;
		GetterToSelection.TargetPinId = SwitchSelectionPin.Id;
		Document.Edges.Add(GetterToSelection);

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
		TestFalse(TEXT("Unsupported switch-int selection connections should fail apply-time execution."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported switch-int selection connections should not report a successful apply."), Result.bApplied);
		TestTrue(TEXT("Unsupported switch-int selection connections should report the dedicated diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSwitchIntSelectionTypeCombination")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for switch string mismatch coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FEdGraphPinType ArrayBoolType;
		ArrayBoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		ArrayBoolType.ContainerType = EPinContainerType::Array;
		TestTrue(TEXT("Flags member variable should be added for switch string mismatch coverage."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Flags"), ArrayBoolType, FString()));

		FVergilGraphNode GetterNode;
		GetterNode.Id = FGuid::NewGuid();
		GetterNode.Kind = EVergilNodeKind::VariableGet;
		GetterNode.Descriptor = TEXT("K2.VarGet.Flags");

		FVergilGraphPin GetterValue;
		GetterValue.Id = FGuid::NewGuid();
		GetterValue.Name = TEXT("Flags");
		GetterValue.Direction = EVergilPinDirection::Output;
		GetterNode.Pins.Add(GetterValue);

		FVergilGraphNode SwitchNode;
		SwitchNode.Id = FGuid::NewGuid();
		SwitchNode.Kind = EVergilNodeKind::ControlFlow;
		SwitchNode.Descriptor = TEXT("K2.SwitchString");

		FVergilGraphPin SwitchExecIn;
		SwitchExecIn.Id = FGuid::NewGuid();
		SwitchExecIn.Name = UEdGraphSchema_K2::PN_Execute;
		SwitchExecIn.Direction = EVergilPinDirection::Input;
		SwitchExecIn.bIsExec = true;
		SwitchNode.Pins.Add(SwitchExecIn);

		FVergilGraphPin SwitchSelectionPin;
		SwitchSelectionPin.Id = FGuid::NewGuid();
		SwitchSelectionPin.Name = TEXT("Selection");
		SwitchSelectionPin.Direction = EVergilPinDirection::Input;
		SwitchNode.Pins.Add(SwitchSelectionPin);

		FVergilGraphPin SwitchCasePin;
		SwitchCasePin.Id = FGuid::NewGuid();
		SwitchCasePin.Name = TEXT("Ready");
		SwitchCasePin.Direction = EVergilPinDirection::Output;
		SwitchCasePin.bIsExec = true;
		SwitchNode.Pins.Add(SwitchCasePin);

		FVergilGraphDocument Document;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilSwitchStringTypeDiagnostics");
		Document.Nodes = { GetterNode, SwitchNode };

		FVergilGraphEdge GetterToSelection;
		GetterToSelection.Id = FGuid::NewGuid();
		GetterToSelection.SourceNodeId = GetterNode.Id;
		GetterToSelection.SourcePinId = GetterValue.Id;
		GetterToSelection.TargetNodeId = SwitchNode.Id;
		GetterToSelection.TargetPinId = SwitchSelectionPin.Id;
		Document.Edges.Add(GetterToSelection);

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
		TestFalse(TEXT("Unsupported switch-string selection connections should fail apply-time execution."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported switch-string selection connections should not report a successful apply."), Result.bApplied);
		TestTrue(TEXT("Unsupported switch-string selection connections should report the dedicated diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSwitchStringSelectionTypeCombination")));
	}

	{
		UBlueprint* const Blueprint = MakeTestBlueprint();
		TestNotNull(TEXT("Transient test blueprint should be created for switch enum mismatch coverage."), Blueprint);
		if (Blueprint == nullptr)
		{
			return false;
		}

		FEdGraphPinType IntType;
		IntType.PinCategory = UEdGraphSchema_K2::PC_Int;
		TestTrue(TEXT("Mode member variable should be added for switch enum mismatch coverage."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Mode"), IntType, TEXT("0")));

		FVergilGraphNode GetterNode;
		GetterNode.Id = FGuid::NewGuid();
		GetterNode.Kind = EVergilNodeKind::VariableGet;
		GetterNode.Descriptor = TEXT("K2.VarGet.Mode");

		FVergilGraphPin GetterValue;
		GetterValue.Id = FGuid::NewGuid();
		GetterValue.Name = TEXT("Mode");
		GetterValue.Direction = EVergilPinDirection::Output;
		GetterNode.Pins.Add(GetterValue);

		FVergilGraphNode SwitchNode;
		SwitchNode.Id = FGuid::NewGuid();
		SwitchNode.Kind = EVergilNodeKind::ControlFlow;
		SwitchNode.Descriptor = TEXT("K2.SwitchEnum");
		SwitchNode.Metadata.Add(TEXT("EnumPath"), TEXT("/Script/Engine.EMovementMode"));

		FVergilGraphPin SwitchExecIn;
		SwitchExecIn.Id = FGuid::NewGuid();
		SwitchExecIn.Name = UEdGraphSchema_K2::PN_Execute;
		SwitchExecIn.Direction = EVergilPinDirection::Input;
		SwitchExecIn.bIsExec = true;
		SwitchNode.Pins.Add(SwitchExecIn);

		FVergilGraphPin SwitchSelectionPin;
		SwitchSelectionPin.Id = FGuid::NewGuid();
		SwitchSelectionPin.Name = TEXT("Selection");
		SwitchSelectionPin.Direction = EVergilPinDirection::Input;
		SwitchNode.Pins.Add(SwitchSelectionPin);

		FVergilGraphPin SwitchCasePin;
		SwitchCasePin.Id = FGuid::NewGuid();
		SwitchCasePin.Name = TEXT("MOVE_None");
		SwitchCasePin.Direction = EVergilPinDirection::Output;
		SwitchCasePin.bIsExec = true;
		SwitchNode.Pins.Add(SwitchCasePin);

		FVergilGraphDocument Document;
		Document.BlueprintPath = TEXT("/Temp/BP_VergilSwitchEnumTypeDiagnostics");
		Document.Nodes = { GetterNode, SwitchNode };

		FVergilGraphEdge GetterToSelection;
		GetterToSelection.Id = FGuid::NewGuid();
		GetterToSelection.SourceNodeId = GetterNode.Id;
		GetterToSelection.SourcePinId = GetterValue.Id;
		GetterToSelection.TargetNodeId = SwitchNode.Id;
		GetterToSelection.TargetPinId = SwitchSelectionPin.Id;
		Document.Edges.Add(GetterToSelection);

		const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);
		TestFalse(TEXT("Unsupported switch-enum selection connections should fail apply-time execution."), Result.bSucceeded);
		TestFalse(TEXT("Unsupported switch-enum selection connections should not report a successful apply."), Result.bApplied);
		TestTrue(TEXT("Unsupported switch-enum selection connections should report the dedicated diagnostic."), ContainsDiagnostic(Result.Diagnostics, TEXT("UnsupportedSwitchEnumSelectionTypeCombination")));
	}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilStandardMacroInstanceExecutionTest,
	"Vergil.Scaffold.StandardMacroInstanceExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilSpawnActorExecutionTest,
	"Vergil.Scaffold.SpawnActorExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilSpawnActorExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddNode && Command.NodeId == NodeId;
		});
	};

	auto ContainsPlannedPin = [](const FVergilCompilerCommand& Command, const FName PinName, const bool bIsInput, const bool bIsExec) -> bool
	{
		return Command.PlannedPins.ContainsByPredicate([PinName, bIsInput, bIsExec](const FVergilPlannedPin& PlannedPin)
		{
			return PlannedPin.Name == PinName && PlannedPin.bIsInput == bIsInput && PlannedPin.bIsExec == bIsExec;
		});
	};

	UBlueprint* const Blueprint = MakeTestBlueprint(APawn::StaticClass());
	TestNotNull(TEXT("Transient pawn test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	UScriptStruct* const TransformStruct = TBaseStructure<FTransform>::Get();
	TestNotNull(TEXT("FTransform base structure should be available."), TransformStruct);
	if (TransformStruct == nullptr)
	{
		return false;
	}

	FEdGraphPinType ActorType;
	ActorType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ActorType.PinSubCategoryObject = AActor::StaticClass();
	TestTrue(TEXT("SpawnedActor member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("SpawnedActor"), ActorType, TEXT("None")));

	FVergilGraphNode BeginPlayNode;
	BeginPlayNode.Id = FGuid::NewGuid();
	BeginPlayNode.Kind = EVergilNodeKind::Event;
	BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
	BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

	FVergilGraphPin BeginPlayThenPin;
	BeginPlayThenPin.Id = FGuid::NewGuid();
	BeginPlayThenPin.Name = UEdGraphSchema_K2::PN_Then;
	BeginPlayThenPin.Direction = EVergilPinDirection::Output;
	BeginPlayThenPin.bIsExec = true;
	BeginPlayNode.Pins.Add(BeginPlayThenPin);

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(220.0f, -180.0f);

	FVergilGraphPin SelfOutputPin;
	SelfOutputPin.Id = FGuid::NewGuid();
	SelfOutputPin.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutputPin.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutputPin);

	FVergilGraphNode MakeTransformNode;
	MakeTransformNode.Id = FGuid::NewGuid();
	MakeTransformNode.Kind = EVergilNodeKind::Custom;
	MakeTransformNode.Descriptor = TEXT("K2.MakeStruct");
	MakeTransformNode.Position = FVector2D(220.0f, 40.0f);
	MakeTransformNode.Metadata.Add(TEXT("StructPath"), TransformStruct->GetPathName());

	FVergilGraphPin MakeTransformResultPin;
	MakeTransformResultPin.Id = FGuid::NewGuid();
	MakeTransformResultPin.Name = TransformStruct->GetFName();
	MakeTransformResultPin.Direction = EVergilPinDirection::Output;
	MakeTransformNode.Pins.Add(MakeTransformResultPin);

	FVergilGraphNode SpawnActorNode;
	SpawnActorNode.Id = FGuid::NewGuid();
	SpawnActorNode.Kind = EVergilNodeKind::Custom;
	SpawnActorNode.Descriptor = TEXT("K2.SpawnActor");
	SpawnActorNode.Position = FVector2D(420.0f, -20.0f);
	SpawnActorNode.Metadata.Add(TEXT("ActorClassPath"), AActor::StaticClass()->GetPathName());

	FVergilGraphPin SpawnExecPin;
	SpawnExecPin.Id = FGuid::NewGuid();
	SpawnExecPin.Name = UEdGraphSchema_K2::PN_Execute;
	SpawnExecPin.Direction = EVergilPinDirection::Input;
	SpawnExecPin.bIsExec = true;
	SpawnActorNode.Pins.Add(SpawnExecPin);

	FVergilGraphPin SpawnTransformPin;
	SpawnTransformPin.Id = FGuid::NewGuid();
	SpawnTransformPin.Name = TEXT("SpawnTransform");
	SpawnTransformPin.Direction = EVergilPinDirection::Input;
	SpawnActorNode.Pins.Add(SpawnTransformPin);

	FVergilGraphPin SpawnThenPin;
	SpawnThenPin.Id = FGuid::NewGuid();
	SpawnThenPin.Name = UEdGraphSchema_K2::PN_Then;
	SpawnThenPin.Direction = EVergilPinDirection::Output;
	SpawnThenPin.bIsExec = true;
	SpawnActorNode.Pins.Add(SpawnThenPin);

	FVergilGraphPin SpawnResultPin;
	SpawnResultPin.Id = FGuid::NewGuid();
	SpawnResultPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	SpawnResultPin.Direction = EVergilPinDirection::Output;
	SpawnActorNode.Pins.Add(SpawnResultPin);

	FVergilGraphPin SpawnInstigatorPin;
	SpawnInstigatorPin.Id = FGuid::NewGuid();
	SpawnInstigatorPin.Name = TEXT("Instigator");
	SpawnInstigatorPin.Direction = EVergilPinDirection::Input;
	SpawnActorNode.Pins.Add(SpawnInstigatorPin);

	FVergilGraphNode SetSpawnedActorNode;
	SetSpawnedActorNode.Id = FGuid::NewGuid();
	SetSpawnedActorNode.Kind = EVergilNodeKind::VariableSet;
	SetSpawnedActorNode.Descriptor = TEXT("K2.VarSet.SpawnedActor");
	SetSpawnedActorNode.Position = FVector2D(760.0f, 40.0f);

	FVergilGraphPin SetSpawnedActorExecPin;
	SetSpawnedActorExecPin.Id = FGuid::NewGuid();
	SetSpawnedActorExecPin.Name = UEdGraphSchema_K2::PN_Execute;
	SetSpawnedActorExecPin.Direction = EVergilPinDirection::Input;
	SetSpawnedActorExecPin.bIsExec = true;
	SetSpawnedActorNode.Pins.Add(SetSpawnedActorExecPin);

	FVergilGraphPin SetSpawnedActorValuePin;
	SetSpawnedActorValuePin.Id = FGuid::NewGuid();
	SetSpawnedActorValuePin.Name = TEXT("SpawnedActor");
	SetSpawnedActorValuePin.Direction = EVergilPinDirection::Input;
	SetSpawnedActorNode.Pins.Add(SetSpawnedActorValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilSpawnActorExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, MakeTransformNode, SpawnActorNode, SetSpawnedActorNode };

	FVergilGraphEdge EventToSpawn;
	EventToSpawn.Id = FGuid::NewGuid();
	EventToSpawn.SourceNodeId = BeginPlayNode.Id;
	EventToSpawn.SourcePinId = BeginPlayThenPin.Id;
	EventToSpawn.TargetNodeId = SpawnActorNode.Id;
	EventToSpawn.TargetPinId = SpawnExecPin.Id;
	Document.Edges.Add(EventToSpawn);

	FVergilGraphEdge TransformToSpawn;
	TransformToSpawn.Id = FGuid::NewGuid();
	TransformToSpawn.SourceNodeId = MakeTransformNode.Id;
	TransformToSpawn.SourcePinId = MakeTransformResultPin.Id;
	TransformToSpawn.TargetNodeId = SpawnActorNode.Id;
	TransformToSpawn.TargetPinId = SpawnTransformPin.Id;
	Document.Edges.Add(TransformToSpawn);

	FVergilGraphEdge SelfToInstigator;
	SelfToInstigator.Id = FGuid::NewGuid();
	SelfToInstigator.SourceNodeId = SelfNode.Id;
	SelfToInstigator.SourcePinId = SelfOutputPin.Id;
	SelfToInstigator.TargetNodeId = SpawnActorNode.Id;
	SelfToInstigator.TargetPinId = SpawnInstigatorPin.Id;
	Document.Edges.Add(SelfToInstigator);

	FVergilGraphEdge SpawnThenToSetter;
	SpawnThenToSetter.Id = FGuid::NewGuid();
	SpawnThenToSetter.SourceNodeId = SpawnActorNode.Id;
	SpawnThenToSetter.SourcePinId = SpawnThenPin.Id;
	SpawnThenToSetter.TargetNodeId = SetSpawnedActorNode.Id;
	SpawnThenToSetter.TargetPinId = SetSpawnedActorExecPin.Id;
	Document.Edges.Add(SpawnThenToSetter);

	FVergilGraphEdge SpawnResultToSetter;
	SpawnResultToSetter.Id = FGuid::NewGuid();
	SpawnResultToSetter.SourceNodeId = SpawnActorNode.Id;
	SpawnResultToSetter.SourcePinId = SpawnResultPin.Id;
	SpawnResultToSetter.TargetNodeId = SetSpawnedActorNode.Id;
	SpawnResultToSetter.TargetPinId = SetSpawnedActorValuePin.Id;
	Document.Edges.Add(SpawnResultToSetter);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Spawn actor document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Spawn actor document should be applied."), Result.bApplied);
	TestTrue(TEXT("Spawn actor document should execute commands."), Result.ExecutedCommandCount > 0);

	const FVergilCompilerCommand* const SpawnCommand = FindNodeCommand(Result.Commands, SpawnActorNode.Id);
	TestNotNull(TEXT("Spawn actor should lower into an AddNode command."), SpawnCommand);
	if (SpawnCommand == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Spawn actor should lower into its dedicated command name."), SpawnCommand->Name, FName(TEXT("Vergil.K2.SpawnActor")));
	TestEqual(TEXT("Spawn actor should retain the normalized actor class path in StringValue."), SpawnCommand->StringValue, AActor::StaticClass()->GetPathName());
	TestEqual(TEXT("Spawn actor should retain the normalized actor class path in attributes."), SpawnCommand->Attributes.FindRef(TEXT("ActorClassPath")), AActor::StaticClass()->GetPathName());
	TestTrue(TEXT("Spawn actor planned pins should include SpawnTransform."), ContainsPlannedPin(*SpawnCommand, TEXT("SpawnTransform"), true, false));
	TestTrue(TEXT("Spawn actor planned pins should include Instigator."), ContainsPlannedPin(*SpawnCommand, TEXT("Instigator"), true, false));
	TestTrue(TEXT("Spawn actor planned pins should include ReturnValue."), ContainsPlannedPin(*SpawnCommand, UEdGraphSchema_K2::PN_ReturnValue, false, false));

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after spawn actor execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventGraphNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_Self* const SelfGraphNode = FindGraphNodeByGuid<UK2Node_Self>(EventGraph, SelfNode.Id);
	UEdGraphNode* const MakeTransformGraphNode = FindGraphNodeByGuid<UEdGraphNode>(EventGraph, MakeTransformNode.Id);
	UK2Node_SpawnActorFromClass* const SpawnActorGraphNode = FindGraphNodeByGuid<UK2Node_SpawnActorFromClass>(EventGraph, SpawnActorNode.Id);
	UK2Node_VariableSet* const SetSpawnedActorGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetSpawnedActorNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventGraphNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("MakeTransform node should exist."), MakeTransformGraphNode);
	TestNotNull(TEXT("Spawn actor node should exist."), SpawnActorGraphNode);
	TestNotNull(TEXT("SpawnedActor setter node should exist."), SetSpawnedActorGraphNode);
	if (EventGraphNode == nullptr || SelfGraphNode == nullptr || MakeTransformGraphNode == nullptr || SpawnActorGraphNode == nullptr || SetSpawnedActorGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const SpawnExecGraphPin = SpawnActorGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SpawnTransformGraphPin = SpawnActorGraphNode->FindPin(TEXT("SpawnTransform"));
	UEdGraphPin* const SpawnThenGraphPin = SpawnActorGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SpawnInstigatorGraphPin = SpawnActorGraphNode->FindPin(TEXT("Instigator"));
	UEdGraphPin* const SpawnResultGraphPin = SpawnActorGraphNode->GetResultPin();
	UEdGraphPin* const SpawnClassGraphPin = SpawnActorGraphNode->GetClassPin();
	UEdGraphPin* const SetExecGraphPin = SetSpawnedActorGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetValueGraphPin = SetSpawnedActorGraphNode->FindPin(TEXT("SpawnedActor"));

	TestNotNull(TEXT("Spawn actor should expose the SpawnTransform pin from the UE_5.7 actor surface."), SpawnTransformGraphPin);
	TestNotNull(TEXT("Spawn actor should expose the Instigator pin from the UE_5.7 actor surface."), SpawnInstigatorGraphPin);
	TestTrue(TEXT("Spawn actor should resolve the class pin to AActor."), SpawnClassGraphPin != nullptr && SpawnClassGraphPin->DefaultObject == AActor::StaticClass());
	TestTrue(TEXT("Spawn actor result pin should resolve to AActor."), SpawnResultGraphPin != nullptr && SpawnResultGraphPin->PinType.PinSubCategoryObject.Get() == AActor::StaticClass());
	TestTrue(TEXT("BeginPlay should drive the spawn actor exec pin."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(SpawnExecGraphPin));
	TestTrue(TEXT("MakeTransform should feed the spawn actor SpawnTransform pin."), SpawnTransformGraphPin != nullptr && SpawnTransformGraphPin->LinkedTo.ContainsByPredicate([MakeTransformGraphNode](const UEdGraphPin* LinkedPin)
	{
		return LinkedPin != nullptr && LinkedPin->GetOwningNode() == MakeTransformGraphNode;
	}));
	TestTrue(TEXT("Self should feed the spawn actor Instigator pin."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(SpawnInstigatorGraphPin));
	TestTrue(TEXT("Spawn actor Then should drive the SpawnedActor setter."), SpawnThenGraphPin != nullptr && SpawnThenGraphPin->LinkedTo.Contains(SetExecGraphPin));
	TestTrue(TEXT("Spawn actor ReturnValue should feed the SpawnedActor setter value."), SpawnResultGraphPin != nullptr && SpawnResultGraphPin->LinkedTo.Contains(SetValueGraphPin));

	return true;
}

bool FVergilStandardMacroInstanceExecutionTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem is available."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	auto FindNodeCommand = [](const TArray<FVergilCompilerCommand>& Commands, const FGuid& NodeId) -> const FVergilCompilerCommand*
	{
		return Commands.FindByPredicate([NodeId](const FVergilCompilerCommand& Command)
		{
			return Command.Type == EVergilCommandType::AddNode && Command.NodeId == NodeId;
		});
	};

	auto ContainsPlannedPin = [](const FVergilCompilerCommand& Command, const FName PinName, const bool bIsInput, const bool bIsExec) -> bool
	{
		return Command.PlannedPins.ContainsByPredicate([PinName, bIsInput, bIsExec](const FVergilPlannedPin& PlannedPin)
		{
			return PlannedPin.Name == PinName && PlannedPin.bIsInput == bIsInput && PlannedPin.bIsExec == bIsExec;
		});
	};

	auto FindFirstExecInputPin = [](UEdGraphNode* Node) -> UEdGraphPin*
	{
		if (Node == nullptr)
		{
			return nullptr;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin != nullptr && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}

		return nullptr;
	};

	UBlueprint* const Blueprint = MakeTestBlueprint();
	TestNotNull(TEXT("Transient test blueprint should be created."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("ShouldStartClosed member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ShouldStartClosed"), BoolType, TEXT("false")));
	TestTrue(TEXT("WasA member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("WasA"), BoolType, TEXT("false")));
	TestTrue(TEXT("DidB member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DidB"), BoolType, TEXT("false")));

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

	FVergilGraphNode GetterNode;
	GetterNode.Id = FGuid::NewGuid();
	GetterNode.Kind = EVergilNodeKind::VariableGet;
	GetterNode.Descriptor = TEXT("K2.VarGet.ShouldStartClosed");
	GetterNode.Position = FVector2D(220.0f, -160.0f);

	FVergilGraphPin GetterValuePin;
	GetterValuePin.Id = FGuid::NewGuid();
	GetterValuePin.Name = TEXT("ShouldStartClosed");
	GetterValuePin.Direction = EVergilPinDirection::Output;
	GetterNode.Pins.Add(GetterValuePin);

	FVergilGraphNode DoOnceNode;
	DoOnceNode.Id = FGuid::NewGuid();
	DoOnceNode.Kind = EVergilNodeKind::Custom;
	DoOnceNode.Descriptor = TEXT("K2.DoOnce");
	DoOnceNode.Position = FVector2D(440.0f, -40.0f);

	FVergilGraphPin DoOnceExecPin;
	DoOnceExecPin.Id = FGuid::NewGuid();
	DoOnceExecPin.Name = UEdGraphSchema_K2::PN_Execute;
	DoOnceExecPin.Direction = EVergilPinDirection::Input;
	DoOnceExecPin.bIsExec = true;
	DoOnceNode.Pins.Add(DoOnceExecPin);

	FVergilGraphPin DoOnceStartClosedPin;
	DoOnceStartClosedPin.Id = FGuid::NewGuid();
	DoOnceStartClosedPin.Name = TEXT("StartClosed");
	DoOnceStartClosedPin.Direction = EVergilPinDirection::Input;
	DoOnceNode.Pins.Add(DoOnceStartClosedPin);

	FVergilGraphPin DoOnceCompletedPin;
	DoOnceCompletedPin.Id = FGuid::NewGuid();
	DoOnceCompletedPin.Name = TEXT("Completed");
	DoOnceCompletedPin.Direction = EVergilPinDirection::Output;
	DoOnceCompletedPin.bIsExec = true;
	DoOnceNode.Pins.Add(DoOnceCompletedPin);

	FVergilGraphNode FlipFlopNode;
	FlipFlopNode.Id = FGuid::NewGuid();
	FlipFlopNode.Kind = EVergilNodeKind::Custom;
	FlipFlopNode.Descriptor = TEXT("K2.FlipFlop");
	FlipFlopNode.Position = FVector2D(720.0f, -20.0f);

	FVergilGraphPin FlipFlopExecPin;
	FlipFlopExecPin.Id = FGuid::NewGuid();
	FlipFlopExecPin.Name = UEdGraphSchema_K2::PN_Execute;
	FlipFlopExecPin.Direction = EVergilPinDirection::Input;
	FlipFlopExecPin.bIsExec = true;
	FlipFlopNode.Pins.Add(FlipFlopExecPin);

	FVergilGraphPin FlipFlopAPin;
	FlipFlopAPin.Id = FGuid::NewGuid();
	FlipFlopAPin.Name = TEXT("A");
	FlipFlopAPin.Direction = EVergilPinDirection::Output;
	FlipFlopAPin.bIsExec = true;
	FlipFlopNode.Pins.Add(FlipFlopAPin);

	FVergilGraphPin FlipFlopBPin;
	FlipFlopBPin.Id = FGuid::NewGuid();
	FlipFlopBPin.Name = TEXT("B");
	FlipFlopBPin.Direction = EVergilPinDirection::Output;
	FlipFlopBPin.bIsExec = true;
	FlipFlopNode.Pins.Add(FlipFlopBPin);

	FVergilGraphPin FlipFlopIsAPin;
	FlipFlopIsAPin.Id = FGuid::NewGuid();
	FlipFlopIsAPin.Name = TEXT("IsA");
	FlipFlopIsAPin.Direction = EVergilPinDirection::Output;
	FlipFlopNode.Pins.Add(FlipFlopIsAPin);

	FVergilGraphNode SetWasANode;
	SetWasANode.Id = FGuid::NewGuid();
	SetWasANode.Kind = EVergilNodeKind::VariableSet;
	SetWasANode.Descriptor = TEXT("K2.VarSet.WasA");
	SetWasANode.Position = FVector2D(1000.0f, -120.0f);

	FVergilGraphPin SetWasAExecPin;
	SetWasAExecPin.Id = FGuid::NewGuid();
	SetWasAExecPin.Name = TEXT("Execute");
	SetWasAExecPin.Direction = EVergilPinDirection::Input;
	SetWasAExecPin.bIsExec = true;
	SetWasANode.Pins.Add(SetWasAExecPin);

	FVergilGraphPin SetWasAValuePin;
	SetWasAValuePin.Id = FGuid::NewGuid();
	SetWasAValuePin.Name = TEXT("WasA");
	SetWasAValuePin.Direction = EVergilPinDirection::Input;
	SetWasANode.Pins.Add(SetWasAValuePin);

	FVergilGraphNode SetDidBNode;
	SetDidBNode.Id = FGuid::NewGuid();
	SetDidBNode.Kind = EVergilNodeKind::VariableSet;
	SetDidBNode.Descriptor = TEXT("K2.VarSet.DidB");
	SetDidBNode.Position = FVector2D(1000.0f, 120.0f);

	FVergilGraphPin SetDidBExecPin;
	SetDidBExecPin.Id = FGuid::NewGuid();
	SetDidBExecPin.Name = TEXT("Execute");
	SetDidBExecPin.Direction = EVergilPinDirection::Input;
	SetDidBExecPin.bIsExec = true;
	SetDidBNode.Pins.Add(SetDidBExecPin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilStandardMacroInstanceExecution");
	Document.Nodes = { BeginPlayNode, GetterNode, DoOnceNode, FlipFlopNode, SetWasANode, SetDidBNode };

	FVergilGraphEdge EventToDoOnce;
	EventToDoOnce.Id = FGuid::NewGuid();
	EventToDoOnce.SourceNodeId = BeginPlayNode.Id;
	EventToDoOnce.SourcePinId = BeginPlayThenPin.Id;
	EventToDoOnce.TargetNodeId = DoOnceNode.Id;
	EventToDoOnce.TargetPinId = DoOnceExecPin.Id;
	Document.Edges.Add(EventToDoOnce);

	FVergilGraphEdge GetterToDoOnce;
	GetterToDoOnce.Id = FGuid::NewGuid();
	GetterToDoOnce.SourceNodeId = GetterNode.Id;
	GetterToDoOnce.SourcePinId = GetterValuePin.Id;
	GetterToDoOnce.TargetNodeId = DoOnceNode.Id;
	GetterToDoOnce.TargetPinId = DoOnceStartClosedPin.Id;
	Document.Edges.Add(GetterToDoOnce);

	FVergilGraphEdge DoOnceToFlipFlop;
	DoOnceToFlipFlop.Id = FGuid::NewGuid();
	DoOnceToFlipFlop.SourceNodeId = DoOnceNode.Id;
	DoOnceToFlipFlop.SourcePinId = DoOnceCompletedPin.Id;
	DoOnceToFlipFlop.TargetNodeId = FlipFlopNode.Id;
	DoOnceToFlipFlop.TargetPinId = FlipFlopExecPin.Id;
	Document.Edges.Add(DoOnceToFlipFlop);

	FVergilGraphEdge FlipFlopAToSetWasA;
	FlipFlopAToSetWasA.Id = FGuid::NewGuid();
	FlipFlopAToSetWasA.SourceNodeId = FlipFlopNode.Id;
	FlipFlopAToSetWasA.SourcePinId = FlipFlopAPin.Id;
	FlipFlopAToSetWasA.TargetNodeId = SetWasANode.Id;
	FlipFlopAToSetWasA.TargetPinId = SetWasAExecPin.Id;
	Document.Edges.Add(FlipFlopAToSetWasA);

	FVergilGraphEdge FlipFlopIsAToSetWasA;
	FlipFlopIsAToSetWasA.Id = FGuid::NewGuid();
	FlipFlopIsAToSetWasA.SourceNodeId = FlipFlopNode.Id;
	FlipFlopIsAToSetWasA.SourcePinId = FlipFlopIsAPin.Id;
	FlipFlopIsAToSetWasA.TargetNodeId = SetWasANode.Id;
	FlipFlopIsAToSetWasA.TargetPinId = SetWasAValuePin.Id;
	Document.Edges.Add(FlipFlopIsAToSetWasA);

	FVergilGraphEdge FlipFlopBToSetDidB;
	FlipFlopBToSetDidB.Id = FGuid::NewGuid();
	FlipFlopBToSetDidB.SourceNodeId = FlipFlopNode.Id;
	FlipFlopBToSetDidB.SourcePinId = FlipFlopBPin.Id;
	FlipFlopBToSetDidB.TargetNodeId = SetDidBNode.Id;
	FlipFlopBToSetDidB.TargetPinId = SetDidBExecPin.Id;
	Document.Edges.Add(FlipFlopBToSetDidB);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Standard macro document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Standard macro document should be applied."), Result.bApplied);
	TestTrue(TEXT("Standard macro document should execute commands."), Result.ExecutedCommandCount > 0);

	const FVergilCompilerCommand* const DoOnceCommand = FindNodeCommand(Result.Commands, DoOnceNode.Id);
	const FVergilCompilerCommand* const FlipFlopCommand = FindNodeCommand(Result.Commands, FlipFlopNode.Id);
	TestNotNull(TEXT("DoOnce should lower into an AddNode command."), DoOnceCommand);
	TestNotNull(TEXT("FlipFlop should lower into an AddNode command."), FlipFlopCommand);
	if (DoOnceCommand == nullptr || FlipFlopCommand == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("DoOnce should lower into its dedicated command name."), DoOnceCommand->Name, FName(TEXT("Vergil.K2.DoOnce")));
	TestEqual(TEXT("FlipFlop should lower into its dedicated command name."), FlipFlopCommand->Name, FName(TEXT("Vergil.K2.FlipFlop")));
	TestEqual(TEXT("DoOnce should default to the UE_5.7 StandardMacros asset."), DoOnceCommand->Attributes.FindRef(TEXT("MacroBlueprintPath")), FString(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")));
	TestEqual(TEXT("DoOnce should default to the DoOnce macro graph."), DoOnceCommand->Attributes.FindRef(TEXT("MacroGraphName")), FString(TEXT("DoOnce")));
	TestEqual(TEXT("FlipFlop should default to the UE_5.7 StandardMacros asset."), FlipFlopCommand->Attributes.FindRef(TEXT("MacroBlueprintPath")), FString(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros")));
	TestEqual(TEXT("FlipFlop should default to the FlipFlop macro graph."), FlipFlopCommand->Attributes.FindRef(TEXT("MacroGraphName")), FString(TEXT("FlipFlop")));
	TestTrue(TEXT("DoOnce planned pins should normalize the StartClosed alias to the engine pin name."), ContainsPlannedPin(*DoOnceCommand, TEXT("Start Closed"), true, false));

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after standard macro execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_VariableGet* const GetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, GetterNode.Id);
	UK2Node_MacroInstance* const DoOnceGraphNode = FindGraphNodeByGuid<UK2Node_MacroInstance>(EventGraph, DoOnceNode.Id);
	UK2Node_MacroInstance* const FlipFlopGraphNode = FindGraphNodeByGuid<UK2Node_MacroInstance>(EventGraph, FlipFlopNode.Id);
	UK2Node_VariableSet* const SetWasAGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetWasANode.Id);
	UK2Node_VariableSet* const SetDidBGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetDidBNode.Id);

	TestNotNull(TEXT("Event node should exist."), EventNode);
	TestNotNull(TEXT("Getter node should exist."), GetterGraphNode);
	TestNotNull(TEXT("DoOnce macro node should exist."), DoOnceGraphNode);
	TestNotNull(TEXT("FlipFlop macro node should exist."), FlipFlopGraphNode);
	TestNotNull(TEXT("WasA setter node should exist."), SetWasAGraphNode);
	TestNotNull(TEXT("DidB setter node should exist."), SetDidBGraphNode);
	if (EventNode == nullptr || GetterGraphNode == nullptr || DoOnceGraphNode == nullptr || FlipFlopGraphNode == nullptr || SetWasAGraphNode == nullptr || SetDidBGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const GetterGraphValuePin = GetterGraphNode->FindPin(TEXT("ShouldStartClosed"));
	UEdGraphPin* const DoOnceExecGraphPin = DoOnceGraphNode->GetExecPin();
	UEdGraphPin* const DoOnceStartClosedGraphPin = DoOnceGraphNode->FindPin(TEXT("Start Closed"));
	UEdGraphPin* const DoOnceCompletedGraphPin = DoOnceGraphNode->FindPin(TEXT("Completed"));
	UEdGraphPin* const FlipFlopExecGraphPin = FindFirstExecInputPin(FlipFlopGraphNode);
	UEdGraphPin* const FlipFlopAGraphPin = FlipFlopGraphNode->FindPin(TEXT("A"));
	UEdGraphPin* const FlipFlopBGraphPin = FlipFlopGraphNode->FindPin(TEXT("B"));
	UEdGraphPin* const FlipFlopIsAGraphPin = FlipFlopGraphNode->FindPin(TEXT("IsA"));
	UEdGraphPin* const SetWasAExecGraphPin = SetWasAGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetWasAValueGraphPin = SetWasAGraphNode->FindPin(TEXT("WasA"));
	UEdGraphPin* const SetDidBExecGraphPin = SetDidBGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);

	TestTrue(TEXT("Getter should remain pure."), GetterGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute) == nullptr);
	TestTrue(TEXT("DoOnce should resolve the UE_5.7 StandardMacros DoOnce graph."), DoOnceGraphNode->GetMacroGraph() != nullptr && DoOnceGraphNode->GetMacroGraph()->GetFName() == TEXT("DoOnce"));
	TestTrue(TEXT("FlipFlop should resolve the UE_5.7 StandardMacros FlipFlop graph."), FlipFlopGraphNode->GetMacroGraph() != nullptr && FlipFlopGraphNode->GetMacroGraph()->GetFName() == TEXT("FlipFlop"));
	TestTrue(TEXT("Event should feed DoOnce exec."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(DoOnceExecGraphPin));
	TestTrue(TEXT("Getter should feed DoOnce Start Closed."), GetterGraphValuePin != nullptr && GetterGraphValuePin->LinkedTo.Contains(DoOnceStartClosedGraphPin));
	TestTrue(TEXT("DoOnce Completed should feed FlipFlop exec."), DoOnceCompletedGraphPin != nullptr && DoOnceCompletedGraphPin->LinkedTo.Contains(FlipFlopExecGraphPin));
	TestTrue(TEXT("FlipFlop A should feed the WasA setter exec."), FlipFlopAGraphPin != nullptr && FlipFlopAGraphPin->LinkedTo.Contains(SetWasAExecGraphPin));
	TestTrue(TEXT("FlipFlop IsA should feed the WasA setter value."), FlipFlopIsAGraphPin != nullptr && FlipFlopIsAGraphPin->LinkedTo.Contains(SetWasAValueGraphPin));
	TestTrue(TEXT("FlipFlop B should feed the DidB setter exec."), FlipFlopBGraphPin != nullptr && FlipFlopBGraphPin->LinkedTo.Contains(SetDidBExecGraphPin));

	return true;
}

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
	TestTrue(TEXT("WasTimerActive member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("WasTimerActive"), BoolType, TEXT("false")));
	TestTrue(TEXT("WasTimerPaused member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("WasTimerPaused"), BoolType, TEXT("false")));
	TestTrue(TEXT("DidTimerExist member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DidTimerExist"), BoolType, TEXT("false")));

	TestTrue(TEXT("ElapsedTimeValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ElapsedTimeValue"), FloatType, TEXT("0.0")));
	TestTrue(TEXT("RemainingTimeValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("RemainingTimeValue"), FloatType, TEXT("0.0")));

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

	FVergilGraphNode PauseTimerNode;
	PauseTimerNode.Id = FGuid::NewGuid();
	PauseTimerNode.Kind = EVergilNodeKind::Call;
	PauseTimerNode.Descriptor = TEXT("K2.Call.K2_PauseTimerHandle");
	PauseTimerNode.Position = FVector2D(1420.0f, -120.0f);
	PauseTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin PauseTimerExecPin;
	PauseTimerExecPin.Id = FGuid::NewGuid();
	PauseTimerExecPin.Name = TEXT("Execute");
	PauseTimerExecPin.Direction = EVergilPinDirection::Input;
	PauseTimerExecPin.bIsExec = true;
	PauseTimerNode.Pins.Add(PauseTimerExecPin);

	FVergilGraphPin PauseTimerThenPin;
	PauseTimerThenPin.Id = FGuid::NewGuid();
	PauseTimerThenPin.Name = TEXT("Then");
	PauseTimerThenPin.Direction = EVergilPinDirection::Output;
	PauseTimerThenPin.bIsExec = true;
	PauseTimerNode.Pins.Add(PauseTimerThenPin);

	FVergilGraphPin PauseTimerHandlePin;
	PauseTimerHandlePin.Id = FGuid::NewGuid();
	PauseTimerHandlePin.Name = TEXT("Handle");
	PauseTimerHandlePin.Direction = EVergilPinDirection::Input;
	PauseTimerNode.Pins.Add(PauseTimerHandlePin);

	FVergilGraphNode UnPauseTimerNode;
	UnPauseTimerNode.Id = FGuid::NewGuid();
	UnPauseTimerNode.Kind = EVergilNodeKind::Call;
	UnPauseTimerNode.Descriptor = TEXT("K2.Call.K2_UnPauseTimerHandle");
	UnPauseTimerNode.Position = FVector2D(1780.0f, -120.0f);
	UnPauseTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin UnPauseTimerExecPin;
	UnPauseTimerExecPin.Id = FGuid::NewGuid();
	UnPauseTimerExecPin.Name = TEXT("Execute");
	UnPauseTimerExecPin.Direction = EVergilPinDirection::Input;
	UnPauseTimerExecPin.bIsExec = true;
	UnPauseTimerNode.Pins.Add(UnPauseTimerExecPin);

	FVergilGraphPin UnPauseTimerThenPin;
	UnPauseTimerThenPin.Id = FGuid::NewGuid();
	UnPauseTimerThenPin.Name = TEXT("Then");
	UnPauseTimerThenPin.Direction = EVergilPinDirection::Output;
	UnPauseTimerThenPin.bIsExec = true;
	UnPauseTimerNode.Pins.Add(UnPauseTimerThenPin);

	FVergilGraphPin UnPauseTimerHandlePin;
	UnPauseTimerHandlePin.Id = FGuid::NewGuid();
	UnPauseTimerHandlePin.Name = TEXT("Handle");
	UnPauseTimerHandlePin.Direction = EVergilPinDirection::Input;
	UnPauseTimerNode.Pins.Add(UnPauseTimerHandlePin);

	FVergilGraphNode IsActiveNode;
	IsActiveNode.Id = FGuid::NewGuid();
	IsActiveNode.Kind = EVergilNodeKind::Call;
	IsActiveNode.Descriptor = TEXT("K2.Call.K2_IsTimerActiveHandle");
	IsActiveNode.Position = FVector2D(1780.0f, 60.0f);
	IsActiveNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin IsActiveHandlePin;
	IsActiveHandlePin.Id = FGuid::NewGuid();
	IsActiveHandlePin.Name = TEXT("Handle");
	IsActiveHandlePin.Direction = EVergilPinDirection::Input;
	IsActiveNode.Pins.Add(IsActiveHandlePin);

	FVergilGraphPin IsActiveReturnPin;
	IsActiveReturnPin.Id = FGuid::NewGuid();
	IsActiveReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	IsActiveReturnPin.Direction = EVergilPinDirection::Output;
	IsActiveNode.Pins.Add(IsActiveReturnPin);

	FVergilGraphNode IsPausedNode;
	IsPausedNode.Id = FGuid::NewGuid();
	IsPausedNode.Kind = EVergilNodeKind::Call;
	IsPausedNode.Descriptor = TEXT("K2.Call.K2_IsTimerPausedHandle");
	IsPausedNode.Position = FVector2D(1780.0f, 220.0f);
	IsPausedNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin IsPausedHandlePin;
	IsPausedHandlePin.Id = FGuid::NewGuid();
	IsPausedHandlePin.Name = TEXT("Handle");
	IsPausedHandlePin.Direction = EVergilPinDirection::Input;
	IsPausedNode.Pins.Add(IsPausedHandlePin);

	FVergilGraphPin IsPausedReturnPin;
	IsPausedReturnPin.Id = FGuid::NewGuid();
	IsPausedReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	IsPausedReturnPin.Direction = EVergilPinDirection::Output;
	IsPausedNode.Pins.Add(IsPausedReturnPin);

	FVergilGraphNode ExistsNode;
	ExistsNode.Id = FGuid::NewGuid();
	ExistsNode.Kind = EVergilNodeKind::Call;
	ExistsNode.Descriptor = TEXT("K2.Call.K2_TimerExistsHandle");
	ExistsNode.Position = FVector2D(1780.0f, 380.0f);
	ExistsNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin ExistsHandlePin;
	ExistsHandlePin.Id = FGuid::NewGuid();
	ExistsHandlePin.Name = TEXT("Handle");
	ExistsHandlePin.Direction = EVergilPinDirection::Input;
	ExistsNode.Pins.Add(ExistsHandlePin);

	FVergilGraphPin ExistsReturnPin;
	ExistsReturnPin.Id = FGuid::NewGuid();
	ExistsReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	ExistsReturnPin.Direction = EVergilPinDirection::Output;
	ExistsNode.Pins.Add(ExistsReturnPin);

	FVergilGraphNode ElapsedNode;
	ElapsedNode.Id = FGuid::NewGuid();
	ElapsedNode.Kind = EVergilNodeKind::Call;
	ElapsedNode.Descriptor = TEXT("K2.Call.K2_GetTimerElapsedTimeHandle");
	ElapsedNode.Position = FVector2D(1780.0f, 540.0f);
	ElapsedNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin ElapsedHandlePin;
	ElapsedHandlePin.Id = FGuid::NewGuid();
	ElapsedHandlePin.Name = TEXT("Handle");
	ElapsedHandlePin.Direction = EVergilPinDirection::Input;
	ElapsedNode.Pins.Add(ElapsedHandlePin);

	FVergilGraphPin ElapsedReturnPin;
	ElapsedReturnPin.Id = FGuid::NewGuid();
	ElapsedReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	ElapsedReturnPin.Direction = EVergilPinDirection::Output;
	ElapsedNode.Pins.Add(ElapsedReturnPin);

	FVergilGraphNode RemainingNode;
	RemainingNode.Id = FGuid::NewGuid();
	RemainingNode.Kind = EVergilNodeKind::Call;
	RemainingNode.Descriptor = TEXT("K2.Call.K2_GetTimerRemainingTimeHandle");
	RemainingNode.Position = FVector2D(1780.0f, 700.0f);
	RemainingNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin RemainingHandlePin;
	RemainingHandlePin.Id = FGuid::NewGuid();
	RemainingHandlePin.Name = TEXT("Handle");
	RemainingHandlePin.Direction = EVergilPinDirection::Input;
	RemainingNode.Pins.Add(RemainingHandlePin);

	FVergilGraphPin RemainingReturnPin;
	RemainingReturnPin.Id = FGuid::NewGuid();
	RemainingReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	RemainingReturnPin.Direction = EVergilPinDirection::Output;
	RemainingNode.Pins.Add(RemainingReturnPin);

	FVergilGraphNode SetActiveNode;
	SetActiveNode.Id = FGuid::NewGuid();
	SetActiveNode.Kind = EVergilNodeKind::VariableSet;
	SetActiveNode.Descriptor = TEXT("K2.VarSet.WasTimerActive");
	SetActiveNode.Position = FVector2D(2200.0f, 40.0f);

	FVergilGraphPin SetActiveExecPin;
	SetActiveExecPin.Id = FGuid::NewGuid();
	SetActiveExecPin.Name = TEXT("Execute");
	SetActiveExecPin.Direction = EVergilPinDirection::Input;
	SetActiveExecPin.bIsExec = true;
	SetActiveNode.Pins.Add(SetActiveExecPin);

	FVergilGraphPin SetActiveThenPin;
	SetActiveThenPin.Id = FGuid::NewGuid();
	SetActiveThenPin.Name = TEXT("Then");
	SetActiveThenPin.Direction = EVergilPinDirection::Output;
	SetActiveThenPin.bIsExec = true;
	SetActiveNode.Pins.Add(SetActiveThenPin);

	FVergilGraphPin SetActiveValuePin;
	SetActiveValuePin.Id = FGuid::NewGuid();
	SetActiveValuePin.Name = TEXT("WasTimerActive");
	SetActiveValuePin.Direction = EVergilPinDirection::Input;
	SetActiveNode.Pins.Add(SetActiveValuePin);

	FVergilGraphNode SetPausedNode;
	SetPausedNode.Id = FGuid::NewGuid();
	SetPausedNode.Kind = EVergilNodeKind::VariableSet;
	SetPausedNode.Descriptor = TEXT("K2.VarSet.WasTimerPaused");
	SetPausedNode.Position = FVector2D(2200.0f, 200.0f);

	FVergilGraphPin SetPausedExecPin;
	SetPausedExecPin.Id = FGuid::NewGuid();
	SetPausedExecPin.Name = TEXT("Execute");
	SetPausedExecPin.Direction = EVergilPinDirection::Input;
	SetPausedExecPin.bIsExec = true;
	SetPausedNode.Pins.Add(SetPausedExecPin);

	FVergilGraphPin SetPausedThenPin;
	SetPausedThenPin.Id = FGuid::NewGuid();
	SetPausedThenPin.Name = TEXT("Then");
	SetPausedThenPin.Direction = EVergilPinDirection::Output;
	SetPausedThenPin.bIsExec = true;
	SetPausedNode.Pins.Add(SetPausedThenPin);

	FVergilGraphPin SetPausedValuePin;
	SetPausedValuePin.Id = FGuid::NewGuid();
	SetPausedValuePin.Name = TEXT("WasTimerPaused");
	SetPausedValuePin.Direction = EVergilPinDirection::Input;
	SetPausedNode.Pins.Add(SetPausedValuePin);

	FVergilGraphNode SetExistsNode;
	SetExistsNode.Id = FGuid::NewGuid();
	SetExistsNode.Kind = EVergilNodeKind::VariableSet;
	SetExistsNode.Descriptor = TEXT("K2.VarSet.DidTimerExist");
	SetExistsNode.Position = FVector2D(2200.0f, 360.0f);

	FVergilGraphPin SetExistsExecPin;
	SetExistsExecPin.Id = FGuid::NewGuid();
	SetExistsExecPin.Name = TEXT("Execute");
	SetExistsExecPin.Direction = EVergilPinDirection::Input;
	SetExistsExecPin.bIsExec = true;
	SetExistsNode.Pins.Add(SetExistsExecPin);

	FVergilGraphPin SetExistsThenPin;
	SetExistsThenPin.Id = FGuid::NewGuid();
	SetExistsThenPin.Name = TEXT("Then");
	SetExistsThenPin.Direction = EVergilPinDirection::Output;
	SetExistsThenPin.bIsExec = true;
	SetExistsNode.Pins.Add(SetExistsThenPin);

	FVergilGraphPin SetExistsValuePin;
	SetExistsValuePin.Id = FGuid::NewGuid();
	SetExistsValuePin.Name = TEXT("DidTimerExist");
	SetExistsValuePin.Direction = EVergilPinDirection::Input;
	SetExistsNode.Pins.Add(SetExistsValuePin);

	FVergilGraphNode SetElapsedNode;
	SetElapsedNode.Id = FGuid::NewGuid();
	SetElapsedNode.Kind = EVergilNodeKind::VariableSet;
	SetElapsedNode.Descriptor = TEXT("K2.VarSet.ElapsedTimeValue");
	SetElapsedNode.Position = FVector2D(2200.0f, 520.0f);

	FVergilGraphPin SetElapsedExecPin;
	SetElapsedExecPin.Id = FGuid::NewGuid();
	SetElapsedExecPin.Name = TEXT("Execute");
	SetElapsedExecPin.Direction = EVergilPinDirection::Input;
	SetElapsedExecPin.bIsExec = true;
	SetElapsedNode.Pins.Add(SetElapsedExecPin);

	FVergilGraphPin SetElapsedThenPin;
	SetElapsedThenPin.Id = FGuid::NewGuid();
	SetElapsedThenPin.Name = TEXT("Then");
	SetElapsedThenPin.Direction = EVergilPinDirection::Output;
	SetElapsedThenPin.bIsExec = true;
	SetElapsedNode.Pins.Add(SetElapsedThenPin);

	FVergilGraphPin SetElapsedValuePin;
	SetElapsedValuePin.Id = FGuid::NewGuid();
	SetElapsedValuePin.Name = TEXT("ElapsedTimeValue");
	SetElapsedValuePin.Direction = EVergilPinDirection::Input;
	SetElapsedNode.Pins.Add(SetElapsedValuePin);

	FVergilGraphNode SetRemainingNode;
	SetRemainingNode.Id = FGuid::NewGuid();
	SetRemainingNode.Kind = EVergilNodeKind::VariableSet;
	SetRemainingNode.Descriptor = TEXT("K2.VarSet.RemainingTimeValue");
	SetRemainingNode.Position = FVector2D(2200.0f, 680.0f);

	FVergilGraphPin SetRemainingExecPin;
	SetRemainingExecPin.Id = FGuid::NewGuid();
	SetRemainingExecPin.Name = TEXT("Execute");
	SetRemainingExecPin.Direction = EVergilPinDirection::Input;
	SetRemainingExecPin.bIsExec = true;
	SetRemainingNode.Pins.Add(SetRemainingExecPin);

	FVergilGraphPin SetRemainingThenPin;
	SetRemainingThenPin.Id = FGuid::NewGuid();
	SetRemainingThenPin.Name = TEXT("Then");
	SetRemainingThenPin.Direction = EVergilPinDirection::Output;
	SetRemainingThenPin.bIsExec = true;
	SetRemainingNode.Pins.Add(SetRemainingThenPin);

	FVergilGraphPin SetRemainingValuePin;
	SetRemainingValuePin.Id = FGuid::NewGuid();
	SetRemainingValuePin.Name = TEXT("RemainingTimeValue");
	SetRemainingValuePin.Direction = EVergilPinDirection::Input;
	SetRemainingNode.Pins.Add(SetRemainingValuePin);

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
	Document.Nodes = { BeginPlayNode, TimerEventNode, SecondsGetterNode, LoopingGetterNode, SetTimerNode, SetHandleNode, GetHandleNode, PauseTimerNode, UnPauseTimerNode, IsActiveNode, IsPausedNode, ExistsNode, ElapsedNode, RemainingNode, SetActiveNode, SetPausedNode, SetExistsNode, SetElapsedNode, SetRemainingNode, ClearTimerNode };

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

	FVergilGraphEdge SetHandleToPauseTimer;
	SetHandleToPauseTimer.Id = FGuid::NewGuid();
	SetHandleToPauseTimer.SourceNodeId = SetHandleNode.Id;
	SetHandleToPauseTimer.SourcePinId = SetHandleThenPin.Id;
	SetHandleToPauseTimer.TargetNodeId = PauseTimerNode.Id;
	SetHandleToPauseTimer.TargetPinId = PauseTimerExecPin.Id;
	Document.Edges.Add(SetHandleToPauseTimer);

	FVergilGraphEdge GetHandleToPauseTimer;
	GetHandleToPauseTimer.Id = FGuid::NewGuid();
	GetHandleToPauseTimer.SourceNodeId = GetHandleNode.Id;
	GetHandleToPauseTimer.SourcePinId = GetHandleValuePin.Id;
	GetHandleToPauseTimer.TargetNodeId = PauseTimerNode.Id;
	GetHandleToPauseTimer.TargetPinId = PauseTimerHandlePin.Id;
	Document.Edges.Add(GetHandleToPauseTimer);

	FVergilGraphEdge PauseTimerToUnPauseTimer;
	PauseTimerToUnPauseTimer.Id = FGuid::NewGuid();
	PauseTimerToUnPauseTimer.SourceNodeId = PauseTimerNode.Id;
	PauseTimerToUnPauseTimer.SourcePinId = PauseTimerThenPin.Id;
	PauseTimerToUnPauseTimer.TargetNodeId = UnPauseTimerNode.Id;
	PauseTimerToUnPauseTimer.TargetPinId = UnPauseTimerExecPin.Id;
	Document.Edges.Add(PauseTimerToUnPauseTimer);

	FVergilGraphEdge GetHandleToUnPauseTimer;
	GetHandleToUnPauseTimer.Id = FGuid::NewGuid();
	GetHandleToUnPauseTimer.SourceNodeId = GetHandleNode.Id;
	GetHandleToUnPauseTimer.SourcePinId = GetHandleValuePin.Id;
	GetHandleToUnPauseTimer.TargetNodeId = UnPauseTimerNode.Id;
	GetHandleToUnPauseTimer.TargetPinId = UnPauseTimerHandlePin.Id;
	Document.Edges.Add(GetHandleToUnPauseTimer);

	FVergilGraphEdge UnPauseTimerToSetActive;
	UnPauseTimerToSetActive.Id = FGuid::NewGuid();
	UnPauseTimerToSetActive.SourceNodeId = UnPauseTimerNode.Id;
	UnPauseTimerToSetActive.SourcePinId = UnPauseTimerThenPin.Id;
	UnPauseTimerToSetActive.TargetNodeId = SetActiveNode.Id;
	UnPauseTimerToSetActive.TargetPinId = SetActiveExecPin.Id;
	Document.Edges.Add(UnPauseTimerToSetActive);

	FVergilGraphEdge GetHandleToIsActive;
	GetHandleToIsActive.Id = FGuid::NewGuid();
	GetHandleToIsActive.SourceNodeId = GetHandleNode.Id;
	GetHandleToIsActive.SourcePinId = GetHandleValuePin.Id;
	GetHandleToIsActive.TargetNodeId = IsActiveNode.Id;
	GetHandleToIsActive.TargetPinId = IsActiveHandlePin.Id;
	Document.Edges.Add(GetHandleToIsActive);

	FVergilGraphEdge IsActiveToSetActive;
	IsActiveToSetActive.Id = FGuid::NewGuid();
	IsActiveToSetActive.SourceNodeId = IsActiveNode.Id;
	IsActiveToSetActive.SourcePinId = IsActiveReturnPin.Id;
	IsActiveToSetActive.TargetNodeId = SetActiveNode.Id;
	IsActiveToSetActive.TargetPinId = SetActiveValuePin.Id;
	Document.Edges.Add(IsActiveToSetActive);

	FVergilGraphEdge SetActiveToSetPaused;
	SetActiveToSetPaused.Id = FGuid::NewGuid();
	SetActiveToSetPaused.SourceNodeId = SetActiveNode.Id;
	SetActiveToSetPaused.SourcePinId = SetActiveThenPin.Id;
	SetActiveToSetPaused.TargetNodeId = SetPausedNode.Id;
	SetActiveToSetPaused.TargetPinId = SetPausedExecPin.Id;
	Document.Edges.Add(SetActiveToSetPaused);

	FVergilGraphEdge GetHandleToIsPaused;
	GetHandleToIsPaused.Id = FGuid::NewGuid();
	GetHandleToIsPaused.SourceNodeId = GetHandleNode.Id;
	GetHandleToIsPaused.SourcePinId = GetHandleValuePin.Id;
	GetHandleToIsPaused.TargetNodeId = IsPausedNode.Id;
	GetHandleToIsPaused.TargetPinId = IsPausedHandlePin.Id;
	Document.Edges.Add(GetHandleToIsPaused);

	FVergilGraphEdge IsPausedToSetPaused;
	IsPausedToSetPaused.Id = FGuid::NewGuid();
	IsPausedToSetPaused.SourceNodeId = IsPausedNode.Id;
	IsPausedToSetPaused.SourcePinId = IsPausedReturnPin.Id;
	IsPausedToSetPaused.TargetNodeId = SetPausedNode.Id;
	IsPausedToSetPaused.TargetPinId = SetPausedValuePin.Id;
	Document.Edges.Add(IsPausedToSetPaused);

	FVergilGraphEdge SetPausedToSetExists;
	SetPausedToSetExists.Id = FGuid::NewGuid();
	SetPausedToSetExists.SourceNodeId = SetPausedNode.Id;
	SetPausedToSetExists.SourcePinId = SetPausedThenPin.Id;
	SetPausedToSetExists.TargetNodeId = SetExistsNode.Id;
	SetPausedToSetExists.TargetPinId = SetExistsExecPin.Id;
	Document.Edges.Add(SetPausedToSetExists);

	FVergilGraphEdge GetHandleToExists;
	GetHandleToExists.Id = FGuid::NewGuid();
	GetHandleToExists.SourceNodeId = GetHandleNode.Id;
	GetHandleToExists.SourcePinId = GetHandleValuePin.Id;
	GetHandleToExists.TargetNodeId = ExistsNode.Id;
	GetHandleToExists.TargetPinId = ExistsHandlePin.Id;
	Document.Edges.Add(GetHandleToExists);

	FVergilGraphEdge ExistsToSetExists;
	ExistsToSetExists.Id = FGuid::NewGuid();
	ExistsToSetExists.SourceNodeId = ExistsNode.Id;
	ExistsToSetExists.SourcePinId = ExistsReturnPin.Id;
	ExistsToSetExists.TargetNodeId = SetExistsNode.Id;
	ExistsToSetExists.TargetPinId = SetExistsValuePin.Id;
	Document.Edges.Add(ExistsToSetExists);

	FVergilGraphEdge SetExistsToSetElapsed;
	SetExistsToSetElapsed.Id = FGuid::NewGuid();
	SetExistsToSetElapsed.SourceNodeId = SetExistsNode.Id;
	SetExistsToSetElapsed.SourcePinId = SetExistsThenPin.Id;
	SetExistsToSetElapsed.TargetNodeId = SetElapsedNode.Id;
	SetExistsToSetElapsed.TargetPinId = SetElapsedExecPin.Id;
	Document.Edges.Add(SetExistsToSetElapsed);

	FVergilGraphEdge GetHandleToElapsed;
	GetHandleToElapsed.Id = FGuid::NewGuid();
	GetHandleToElapsed.SourceNodeId = GetHandleNode.Id;
	GetHandleToElapsed.SourcePinId = GetHandleValuePin.Id;
	GetHandleToElapsed.TargetNodeId = ElapsedNode.Id;
	GetHandleToElapsed.TargetPinId = ElapsedHandlePin.Id;
	Document.Edges.Add(GetHandleToElapsed);

	FVergilGraphEdge ElapsedToSetElapsed;
	ElapsedToSetElapsed.Id = FGuid::NewGuid();
	ElapsedToSetElapsed.SourceNodeId = ElapsedNode.Id;
	ElapsedToSetElapsed.SourcePinId = ElapsedReturnPin.Id;
	ElapsedToSetElapsed.TargetNodeId = SetElapsedNode.Id;
	ElapsedToSetElapsed.TargetPinId = SetElapsedValuePin.Id;
	Document.Edges.Add(ElapsedToSetElapsed);

	FVergilGraphEdge SetElapsedToSetRemaining;
	SetElapsedToSetRemaining.Id = FGuid::NewGuid();
	SetElapsedToSetRemaining.SourceNodeId = SetElapsedNode.Id;
	SetElapsedToSetRemaining.SourcePinId = SetElapsedThenPin.Id;
	SetElapsedToSetRemaining.TargetNodeId = SetRemainingNode.Id;
	SetElapsedToSetRemaining.TargetPinId = SetRemainingExecPin.Id;
	Document.Edges.Add(SetElapsedToSetRemaining);

	FVergilGraphEdge GetHandleToRemaining;
	GetHandleToRemaining.Id = FGuid::NewGuid();
	GetHandleToRemaining.SourceNodeId = GetHandleNode.Id;
	GetHandleToRemaining.SourcePinId = GetHandleValuePin.Id;
	GetHandleToRemaining.TargetNodeId = RemainingNode.Id;
	GetHandleToRemaining.TargetPinId = RemainingHandlePin.Id;
	Document.Edges.Add(GetHandleToRemaining);

	FVergilGraphEdge RemainingToSetRemaining;
	RemainingToSetRemaining.Id = FGuid::NewGuid();
	RemainingToSetRemaining.SourceNodeId = RemainingNode.Id;
	RemainingToSetRemaining.SourcePinId = RemainingReturnPin.Id;
	RemainingToSetRemaining.TargetNodeId = SetRemainingNode.Id;
	RemainingToSetRemaining.TargetPinId = SetRemainingValuePin.Id;
	Document.Edges.Add(RemainingToSetRemaining);

	FVergilGraphEdge SetRemainingToClearTimer;
	SetRemainingToClearTimer.Id = FGuid::NewGuid();
	SetRemainingToClearTimer.SourceNodeId = SetRemainingNode.Id;
	SetRemainingToClearTimer.SourcePinId = SetRemainingThenPin.Id;
	SetRemainingToClearTimer.TargetNodeId = ClearTimerNode.Id;
	SetRemainingToClearTimer.TargetPinId = ClearTimerExecPin.Id;
	Document.Edges.Add(SetRemainingToClearTimer);

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
	UK2Node_CallFunction* const PauseTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, PauseTimerNode.Id);
	UK2Node_CallFunction* const UnPauseTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, UnPauseTimerNode.Id);
	UK2Node_CallFunction* const IsActiveGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, IsActiveNode.Id);
	UK2Node_CallFunction* const IsPausedGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, IsPausedNode.Id);
	UK2Node_CallFunction* const ExistsGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ExistsNode.Id);
	UK2Node_CallFunction* const ElapsedGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ElapsedNode.Id);
	UK2Node_CallFunction* const RemainingGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, RemainingNode.Id);
	UK2Node_VariableSet* const SetActiveGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetActiveNode.Id);
	UK2Node_VariableSet* const SetPausedGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetPausedNode.Id);
	UK2Node_VariableSet* const SetExistsGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetExistsNode.Id);
	UK2Node_VariableSet* const SetElapsedGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetElapsedNode.Id);
	UK2Node_VariableSet* const SetRemainingGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetRemainingNode.Id);
	UK2Node_CallFunction* const ClearTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ClearTimerNode.Id);

	TestNotNull(TEXT("BeginPlay event should exist."), EventNode);
	TestNotNull(TEXT("Timer custom event should exist."), TimerEventGraphNode);
	TestNotNull(TEXT("TimerSeconds getter should exist."), SecondsGetterGraphNode);
	TestNotNull(TEXT("Looping getter should exist."), LoopingGetterGraphNode);
	TestNotNull(TEXT("SetTimerDelegate call should exist."), SetTimerGraphNode);
	TestNotNull(TEXT("TimerHandle setter should exist."), SetHandleGraphNode);
	TestNotNull(TEXT("TimerHandle getter should exist."), GetHandleGraphNode);
	TestNotNull(TEXT("PauseTimerHandle call should exist."), PauseTimerGraphNode);
	TestNotNull(TEXT("UnPauseTimerHandle call should exist."), UnPauseTimerGraphNode);
	TestNotNull(TEXT("IsTimerActiveHandle call should exist."), IsActiveGraphNode);
	TestNotNull(TEXT("IsTimerPausedHandle call should exist."), IsPausedGraphNode);
	TestNotNull(TEXT("TimerExistsHandle call should exist."), ExistsGraphNode);
	TestNotNull(TEXT("GetTimerElapsedTimeHandle call should exist."), ElapsedGraphNode);
	TestNotNull(TEXT("GetTimerRemainingTimeHandle call should exist."), RemainingGraphNode);
	TestNotNull(TEXT("WasTimerActive setter should exist."), SetActiveGraphNode);
	TestNotNull(TEXT("WasTimerPaused setter should exist."), SetPausedGraphNode);
	TestNotNull(TEXT("DidTimerExist setter should exist."), SetExistsGraphNode);
	TestNotNull(TEXT("ElapsedTimeValue setter should exist."), SetElapsedGraphNode);
	TestNotNull(TEXT("RemainingTimeValue setter should exist."), SetRemainingGraphNode);
	TestNotNull(TEXT("ClearAndInvalidateTimerHandle call should exist."), ClearTimerGraphNode);
	if (EventNode == nullptr || TimerEventGraphNode == nullptr || SecondsGetterGraphNode == nullptr || LoopingGetterGraphNode == nullptr || SetTimerGraphNode == nullptr || SetHandleGraphNode == nullptr || GetHandleGraphNode == nullptr || PauseTimerGraphNode == nullptr || UnPauseTimerGraphNode == nullptr || IsActiveGraphNode == nullptr || IsPausedGraphNode == nullptr || ExistsGraphNode == nullptr || ElapsedGraphNode == nullptr || RemainingGraphNode == nullptr || SetActiveGraphNode == nullptr || SetPausedGraphNode == nullptr || SetExistsGraphNode == nullptr || SetElapsedGraphNode == nullptr || SetRemainingGraphNode == nullptr || ClearTimerGraphNode == nullptr)
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
	UEdGraphPin* const PauseTimerExecGraphPin = PauseTimerGraphNode->GetExecPin();
	UEdGraphPin* const PauseTimerHandleGraphPin = PauseTimerGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const PauseTimerThenGraphPin = PauseTimerGraphNode->GetThenPin();
	UEdGraphPin* const UnPauseTimerExecGraphPin = UnPauseTimerGraphNode->GetExecPin();
	UEdGraphPin* const UnPauseTimerHandleGraphPin = UnPauseTimerGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const UnPauseTimerThenGraphPin = UnPauseTimerGraphNode->GetThenPin();
	UEdGraphPin* const IsActiveHandleGraphPin = IsActiveGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const IsActiveReturnGraphPin = IsActiveGraphNode->GetReturnValuePin();
	UEdGraphPin* const IsPausedHandleGraphPin = IsPausedGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const IsPausedReturnGraphPin = IsPausedGraphNode->GetReturnValuePin();
	UEdGraphPin* const ExistsHandleGraphPin = ExistsGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const ExistsReturnGraphPin = ExistsGraphNode->GetReturnValuePin();
	UEdGraphPin* const ElapsedHandleGraphPin = ElapsedGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const ElapsedReturnGraphPin = ElapsedGraphNode->GetReturnValuePin();
	UEdGraphPin* const RemainingHandleGraphPin = RemainingGraphNode->FindPin(TEXT("Handle"));
	UEdGraphPin* const RemainingReturnGraphPin = RemainingGraphNode->GetReturnValuePin();
	UEdGraphPin* const SetActiveExecGraphPin = SetActiveGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetActiveThenGraphPin = SetActiveGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetActiveValueGraphPin = SetActiveGraphNode->FindPin(TEXT("WasTimerActive"));
	UEdGraphPin* const SetPausedExecGraphPin = SetPausedGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetPausedThenGraphPin = SetPausedGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetPausedValueGraphPin = SetPausedGraphNode->FindPin(TEXT("WasTimerPaused"));
	UEdGraphPin* const SetExistsExecGraphPin = SetExistsGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetExistsThenGraphPin = SetExistsGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetExistsValueGraphPin = SetExistsGraphNode->FindPin(TEXT("DidTimerExist"));
	UEdGraphPin* const SetElapsedExecGraphPin = SetElapsedGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetElapsedThenGraphPin = SetElapsedGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetElapsedValueGraphPin = SetElapsedGraphNode->FindPin(TEXT("ElapsedTimeValue"));
	UEdGraphPin* const SetRemainingExecGraphPin = SetRemainingGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetRemainingThenGraphPin = SetRemainingGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetRemainingValueGraphPin = SetRemainingGraphNode->FindPin(TEXT("RemainingTimeValue"));
	UEdGraphPin* const ClearTimerExecGraphPin = ClearTimerGraphNode->GetExecPin();
	UEdGraphPin* const ClearTimerHandleGraphPin = ClearTimerGraphNode->FindPin(TEXT("Handle"));

	TestTrue(TEXT("Timer event should expose OutputDelegate."), TimerDelegateGraphPin != nullptr);
	TestTrue(TEXT("BeginPlay should feed SetTimerDelegate execute."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(SetTimerExecGraphPin));
	TestTrue(TEXT("Custom event delegate should feed SetTimerDelegate delegate input."), TimerDelegateGraphPin != nullptr && TimerDelegateGraphPin->LinkedTo.Contains(SetTimerDelegateGraphInput));
	TestTrue(TEXT("TimerSeconds getter should feed SetTimerDelegate Time."), SecondsGraphPin != nullptr && SecondsGraphPin->LinkedTo.Contains(SetTimerTimeGraphInput));
	TestTrue(TEXT("Looping getter should feed SetTimerDelegate bLooping."), LoopingGraphPin != nullptr && LoopingGraphPin->LinkedTo.Contains(SetTimerLoopingGraphInput));
	TestTrue(TEXT("SetTimerDelegate return value should feed TimerHandle setter."), SetTimerReturnGraphPin != nullptr && SetTimerReturnGraphPin->LinkedTo.Contains(SetHandleValueGraphPin));
	TestTrue(TEXT("SetTimerDelegate Then should feed TimerHandle setter execute."), SetTimerThenGraphPin != nullptr && SetTimerThenGraphPin->LinkedTo.Contains(SetHandleExecGraphPin));
	TestTrue(TEXT("PauseTimerHandle should resolve UKismetSystemLibrary::K2_PauseTimerHandle."), PauseTimerGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_PauseTimerHandle)));
	TestTrue(TEXT("UnPauseTimerHandle should resolve UKismetSystemLibrary::K2_UnPauseTimerHandle."), UnPauseTimerGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_UnPauseTimerHandle)));
	TestTrue(TEXT("IsTimerActiveHandle should resolve UKismetSystemLibrary::K2_IsTimerActiveHandle."), IsActiveGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_IsTimerActiveHandle)));
	TestTrue(TEXT("IsTimerPausedHandle should resolve UKismetSystemLibrary::K2_IsTimerPausedHandle."), IsPausedGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_IsTimerPausedHandle)));
	TestTrue(TEXT("TimerExistsHandle should resolve UKismetSystemLibrary::K2_TimerExistsHandle."), ExistsGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_TimerExistsHandle)));
	TestTrue(TEXT("GetTimerElapsedTimeHandle should resolve UKismetSystemLibrary::K2_GetTimerElapsedTimeHandle."), ElapsedGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_GetTimerElapsedTimeHandle)));
	TestTrue(TEXT("GetTimerRemainingTimeHandle should resolve UKismetSystemLibrary::K2_GetTimerRemainingTimeHandle."), RemainingGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_GetTimerRemainingTimeHandle)));
	TestTrue(TEXT("Handle query nodes should remain pure."), IsActiveGraphNode->GetExecPin() == nullptr && IsPausedGraphNode->GetExecPin() == nullptr && ExistsGraphNode->GetExecPin() == nullptr && ElapsedGraphNode->GetExecPin() == nullptr && RemainingGraphNode->GetExecPin() == nullptr);
	TestTrue(TEXT("TimerHandle setter Then should feed PauseTimerHandle execute."), SetHandleThenGraphPin != nullptr && SetHandleThenGraphPin->LinkedTo.Contains(PauseTimerExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed PauseTimerHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(PauseTimerHandleGraphPin));
	TestTrue(TEXT("PauseTimerHandle Then should feed UnPauseTimerHandle execute."), PauseTimerThenGraphPin != nullptr && PauseTimerThenGraphPin->LinkedTo.Contains(UnPauseTimerExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed UnPauseTimerHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(UnPauseTimerHandleGraphPin));
	TestTrue(TEXT("UnPauseTimerHandle Then should feed WasTimerActive setter execute."), UnPauseTimerThenGraphPin != nullptr && UnPauseTimerThenGraphPin->LinkedTo.Contains(SetActiveExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed IsTimerActiveHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(IsActiveHandleGraphPin));
	TestTrue(TEXT("IsTimerActiveHandle return value should feed WasTimerActive setter."), IsActiveReturnGraphPin != nullptr && IsActiveReturnGraphPin->LinkedTo.Contains(SetActiveValueGraphPin));
	TestTrue(TEXT("WasTimerActive setter Then should feed WasTimerPaused setter execute."), SetActiveThenGraphPin != nullptr && SetActiveThenGraphPin->LinkedTo.Contains(SetPausedExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed IsTimerPausedHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(IsPausedHandleGraphPin));
	TestTrue(TEXT("IsTimerPausedHandle return value should feed WasTimerPaused setter."), IsPausedReturnGraphPin != nullptr && IsPausedReturnGraphPin->LinkedTo.Contains(SetPausedValueGraphPin));
	TestTrue(TEXT("WasTimerPaused setter Then should feed DidTimerExist setter execute."), SetPausedThenGraphPin != nullptr && SetPausedThenGraphPin->LinkedTo.Contains(SetExistsExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed TimerExistsHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(ExistsHandleGraphPin));
	TestTrue(TEXT("TimerExistsHandle return value should feed DidTimerExist setter."), ExistsReturnGraphPin != nullptr && ExistsReturnGraphPin->LinkedTo.Contains(SetExistsValueGraphPin));
	TestTrue(TEXT("DidTimerExist setter Then should feed ElapsedTimeValue setter execute."), SetExistsThenGraphPin != nullptr && SetExistsThenGraphPin->LinkedTo.Contains(SetElapsedExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed GetTimerElapsedTimeHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(ElapsedHandleGraphPin));
	TestTrue(TEXT("GetTimerElapsedTimeHandle return value should feed ElapsedTimeValue setter."), ElapsedReturnGraphPin != nullptr && ElapsedReturnGraphPin->LinkedTo.Contains(SetElapsedValueGraphPin));
	TestTrue(TEXT("ElapsedTimeValue setter Then should feed RemainingTimeValue setter execute."), SetElapsedThenGraphPin != nullptr && SetElapsedThenGraphPin->LinkedTo.Contains(SetRemainingExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed GetTimerRemainingTimeHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(RemainingHandleGraphPin));
	TestTrue(TEXT("GetTimerRemainingTimeHandle return value should feed RemainingTimeValue setter."), RemainingReturnGraphPin != nullptr && RemainingReturnGraphPin->LinkedTo.Contains(SetRemainingValueGraphPin));
	TestTrue(TEXT("RemainingTimeValue setter Then should feed ClearAndInvalidateTimerHandle execute."), SetRemainingThenGraphPin != nullptr && SetRemainingThenGraphPin->LinkedTo.Contains(ClearTimerExecGraphPin));
	TestTrue(TEXT("TimerHandle getter should feed ClearAndInvalidateTimerHandle Handle."), GetHandleGraphValuePin != nullptr && GetHandleGraphValuePin->LinkedTo.Contains(ClearTimerHandleGraphPin));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilTimerFunctionNameExecutionTest,
	"Vergil.Scaffold.TimerFunctionNameExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilTimerFunctionNameExecutionTest::RunTest(const FString& Parameters)
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
	TestTrue(TEXT("ElapsedTimeValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ElapsedTimeValue"), FloatType, TEXT("0.0")));
	TestTrue(TEXT("RemainingTimeValue member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("RemainingTimeValue"), FloatType, TEXT("0.0")));

	FEdGraphPinType BoolType;
	BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	TestTrue(TEXT("Looping member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Looping"), BoolType, TEXT("false")));
	TestTrue(TEXT("WasTimerActive member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("WasTimerActive"), BoolType, TEXT("false")));
	TestTrue(TEXT("WasTimerPaused member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("WasTimerPaused"), BoolType, TEXT("false")));
	TestTrue(TEXT("DidTimerExist member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DidTimerExist"), BoolType, TEXT("false")));

	FEdGraphPinType StringType;
	StringType.PinCategory = UEdGraphSchema_K2::PC_String;
	TestTrue(TEXT("TimerFunctionName member variable should be added."), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("TimerFunctionName"), StringType, TEXT("HandleTimer")));

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

	FVergilGraphNode SelfNode;
	SelfNode.Id = FGuid::NewGuid();
	SelfNode.Kind = EVergilNodeKind::Custom;
	SelfNode.Descriptor = TEXT("K2.Self");
	SelfNode.Position = FVector2D(0.0f, 200.0f);

	FVergilGraphPin SelfOutputPin;
	SelfOutputPin.Id = FGuid::NewGuid();
	SelfOutputPin.Name = UEdGraphSchema_K2::PN_Self;
	SelfOutputPin.Direction = EVergilPinDirection::Output;
	SelfNode.Pins.Add(SelfOutputPin);

	FVergilGraphNode TimerEventNode;
	TimerEventNode.Id = FGuid::NewGuid();
	TimerEventNode.Kind = EVergilNodeKind::Custom;
	TimerEventNode.Descriptor = TEXT("K2.CustomEvent.HandleTimer");
	TimerEventNode.Position = FVector2D(0.0f, 420.0f);

	FVergilGraphNode FunctionNameGetterNode;
	FunctionNameGetterNode.Id = FGuid::NewGuid();
	FunctionNameGetterNode.Kind = EVergilNodeKind::VariableGet;
	FunctionNameGetterNode.Descriptor = TEXT("K2.VarGet.TimerFunctionName");
	FunctionNameGetterNode.Position = FVector2D(240.0f, -220.0f);

	FVergilGraphPin FunctionNameValuePin;
	FunctionNameValuePin.Id = FGuid::NewGuid();
	FunctionNameValuePin.Name = TEXT("TimerFunctionName");
	FunctionNameValuePin.Direction = EVergilPinDirection::Output;
	FunctionNameGetterNode.Pins.Add(FunctionNameValuePin);

	FVergilGraphNode SecondsGetterNode;
	SecondsGetterNode.Id = FGuid::NewGuid();
	SecondsGetterNode.Kind = EVergilNodeKind::VariableGet;
	SecondsGetterNode.Descriptor = TEXT("K2.VarGet.TimerSeconds");
	SecondsGetterNode.Position = FVector2D(240.0f, -60.0f);

	FVergilGraphPin SecondsGetterValuePin;
	SecondsGetterValuePin.Id = FGuid::NewGuid();
	SecondsGetterValuePin.Name = TEXT("TimerSeconds");
	SecondsGetterValuePin.Direction = EVergilPinDirection::Output;
	SecondsGetterNode.Pins.Add(SecondsGetterValuePin);

	FVergilGraphNode LoopingGetterNode;
	LoopingGetterNode.Id = FGuid::NewGuid();
	LoopingGetterNode.Kind = EVergilNodeKind::VariableGet;
	LoopingGetterNode.Descriptor = TEXT("K2.VarGet.Looping");
	LoopingGetterNode.Position = FVector2D(240.0f, 100.0f);

	FVergilGraphPin LoopingGetterValuePin;
	LoopingGetterValuePin.Id = FGuid::NewGuid();
	LoopingGetterValuePin.Name = TEXT("Looping");
	LoopingGetterValuePin.Direction = EVergilPinDirection::Output;
	LoopingGetterNode.Pins.Add(LoopingGetterValuePin);

	FVergilGraphNode SetTimerNode;
	SetTimerNode.Id = FGuid::NewGuid();
	SetTimerNode.Kind = EVergilNodeKind::Call;
	SetTimerNode.Descriptor = TEXT("K2.Call.K2_SetTimer");
	SetTimerNode.Position = FVector2D(660.0f, -20.0f);
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

	FVergilGraphPin SetTimerObjectPin;
	SetTimerObjectPin.Id = FGuid::NewGuid();
	SetTimerObjectPin.Name = TEXT("Object");
	SetTimerObjectPin.Direction = EVergilPinDirection::Input;
	SetTimerNode.Pins.Add(SetTimerObjectPin);

	FVergilGraphPin SetTimerFunctionNamePin;
	SetTimerFunctionNamePin.Id = FGuid::NewGuid();
	SetTimerFunctionNamePin.Name = TEXT("FunctionName");
	SetTimerFunctionNamePin.Direction = EVergilPinDirection::Input;
	SetTimerNode.Pins.Add(SetTimerFunctionNamePin);

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
	SetHandleNode.Position = FVector2D(1060.0f, -20.0f);

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

	FVergilGraphNode PauseTimerNode;
	PauseTimerNode.Id = FGuid::NewGuid();
	PauseTimerNode.Kind = EVergilNodeKind::Call;
	PauseTimerNode.Descriptor = TEXT("K2.Call.K2_PauseTimer");
	PauseTimerNode.Position = FVector2D(1460.0f, -20.0f);
	PauseTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin PauseTimerExecPin;
	PauseTimerExecPin.Id = FGuid::NewGuid();
	PauseTimerExecPin.Name = TEXT("Execute");
	PauseTimerExecPin.Direction = EVergilPinDirection::Input;
	PauseTimerExecPin.bIsExec = true;
	PauseTimerNode.Pins.Add(PauseTimerExecPin);

	FVergilGraphPin PauseTimerThenPin;
	PauseTimerThenPin.Id = FGuid::NewGuid();
	PauseTimerThenPin.Name = TEXT("Then");
	PauseTimerThenPin.Direction = EVergilPinDirection::Output;
	PauseTimerThenPin.bIsExec = true;
	PauseTimerNode.Pins.Add(PauseTimerThenPin);

	FVergilGraphPin PauseTimerObjectPin;
	PauseTimerObjectPin.Id = FGuid::NewGuid();
	PauseTimerObjectPin.Name = TEXT("Object");
	PauseTimerObjectPin.Direction = EVergilPinDirection::Input;
	PauseTimerNode.Pins.Add(PauseTimerObjectPin);

	FVergilGraphPin PauseTimerFunctionNamePin;
	PauseTimerFunctionNamePin.Id = FGuid::NewGuid();
	PauseTimerFunctionNamePin.Name = TEXT("FunctionName");
	PauseTimerFunctionNamePin.Direction = EVergilPinDirection::Input;
	PauseTimerNode.Pins.Add(PauseTimerFunctionNamePin);

	FVergilGraphNode UnPauseTimerNode;
	UnPauseTimerNode.Id = FGuid::NewGuid();
	UnPauseTimerNode.Kind = EVergilNodeKind::Call;
	UnPauseTimerNode.Descriptor = TEXT("K2.Call.K2_UnPauseTimer");
	UnPauseTimerNode.Position = FVector2D(1840.0f, -20.0f);
	UnPauseTimerNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin UnPauseTimerExecPin;
	UnPauseTimerExecPin.Id = FGuid::NewGuid();
	UnPauseTimerExecPin.Name = TEXT("Execute");
	UnPauseTimerExecPin.Direction = EVergilPinDirection::Input;
	UnPauseTimerExecPin.bIsExec = true;
	UnPauseTimerNode.Pins.Add(UnPauseTimerExecPin);

	FVergilGraphPin UnPauseTimerThenPin;
	UnPauseTimerThenPin.Id = FGuid::NewGuid();
	UnPauseTimerThenPin.Name = TEXT("Then");
	UnPauseTimerThenPin.Direction = EVergilPinDirection::Output;
	UnPauseTimerThenPin.bIsExec = true;
	UnPauseTimerNode.Pins.Add(UnPauseTimerThenPin);

	FVergilGraphPin UnPauseTimerObjectPin;
	UnPauseTimerObjectPin.Id = FGuid::NewGuid();
	UnPauseTimerObjectPin.Name = TEXT("Object");
	UnPauseTimerObjectPin.Direction = EVergilPinDirection::Input;
	UnPauseTimerNode.Pins.Add(UnPauseTimerObjectPin);

	FVergilGraphPin UnPauseTimerFunctionNamePin;
	UnPauseTimerFunctionNamePin.Id = FGuid::NewGuid();
	UnPauseTimerFunctionNamePin.Name = TEXT("FunctionName");
	UnPauseTimerFunctionNamePin.Direction = EVergilPinDirection::Input;
	UnPauseTimerNode.Pins.Add(UnPauseTimerFunctionNamePin);

	FVergilGraphNode IsActiveNode;
	IsActiveNode.Id = FGuid::NewGuid();
	IsActiveNode.Kind = EVergilNodeKind::Call;
	IsActiveNode.Descriptor = TEXT("K2.Call.K2_IsTimerActive");
	IsActiveNode.Position = FVector2D(1840.0f, 200.0f);
	IsActiveNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin IsActiveObjectPin;
	IsActiveObjectPin.Id = FGuid::NewGuid();
	IsActiveObjectPin.Name = TEXT("Object");
	IsActiveObjectPin.Direction = EVergilPinDirection::Input;
	IsActiveNode.Pins.Add(IsActiveObjectPin);

	FVergilGraphPin IsActiveFunctionNamePin;
	IsActiveFunctionNamePin.Id = FGuid::NewGuid();
	IsActiveFunctionNamePin.Name = TEXT("FunctionName");
	IsActiveFunctionNamePin.Direction = EVergilPinDirection::Input;
	IsActiveNode.Pins.Add(IsActiveFunctionNamePin);

	FVergilGraphPin IsActiveReturnPin;
	IsActiveReturnPin.Id = FGuid::NewGuid();
	IsActiveReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	IsActiveReturnPin.Direction = EVergilPinDirection::Output;
	IsActiveNode.Pins.Add(IsActiveReturnPin);

	FVergilGraphNode IsPausedNode;
	IsPausedNode.Id = FGuid::NewGuid();
	IsPausedNode.Kind = EVergilNodeKind::Call;
	IsPausedNode.Descriptor = TEXT("K2.Call.K2_IsTimerPaused");
	IsPausedNode.Position = FVector2D(1840.0f, 360.0f);
	IsPausedNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin IsPausedObjectPin;
	IsPausedObjectPin.Id = FGuid::NewGuid();
	IsPausedObjectPin.Name = TEXT("Object");
	IsPausedObjectPin.Direction = EVergilPinDirection::Input;
	IsPausedNode.Pins.Add(IsPausedObjectPin);

	FVergilGraphPin IsPausedFunctionNamePin;
	IsPausedFunctionNamePin.Id = FGuid::NewGuid();
	IsPausedFunctionNamePin.Name = TEXT("FunctionName");
	IsPausedFunctionNamePin.Direction = EVergilPinDirection::Input;
	IsPausedNode.Pins.Add(IsPausedFunctionNamePin);

	FVergilGraphPin IsPausedReturnPin;
	IsPausedReturnPin.Id = FGuid::NewGuid();
	IsPausedReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	IsPausedReturnPin.Direction = EVergilPinDirection::Output;
	IsPausedNode.Pins.Add(IsPausedReturnPin);

	FVergilGraphNode ExistsNode;
	ExistsNode.Id = FGuid::NewGuid();
	ExistsNode.Kind = EVergilNodeKind::Call;
	ExistsNode.Descriptor = TEXT("K2.Call.K2_TimerExists");
	ExistsNode.Position = FVector2D(1840.0f, 520.0f);
	ExistsNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin ExistsObjectPin;
	ExistsObjectPin.Id = FGuid::NewGuid();
	ExistsObjectPin.Name = TEXT("Object");
	ExistsObjectPin.Direction = EVergilPinDirection::Input;
	ExistsNode.Pins.Add(ExistsObjectPin);

	FVergilGraphPin ExistsFunctionNamePin;
	ExistsFunctionNamePin.Id = FGuid::NewGuid();
	ExistsFunctionNamePin.Name = TEXT("FunctionName");
	ExistsFunctionNamePin.Direction = EVergilPinDirection::Input;
	ExistsNode.Pins.Add(ExistsFunctionNamePin);

	FVergilGraphPin ExistsReturnPin;
	ExistsReturnPin.Id = FGuid::NewGuid();
	ExistsReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	ExistsReturnPin.Direction = EVergilPinDirection::Output;
	ExistsNode.Pins.Add(ExistsReturnPin);

	FVergilGraphNode ElapsedNode;
	ElapsedNode.Id = FGuid::NewGuid();
	ElapsedNode.Kind = EVergilNodeKind::Call;
	ElapsedNode.Descriptor = TEXT("K2.Call.K2_GetTimerElapsedTime");
	ElapsedNode.Position = FVector2D(1840.0f, 680.0f);
	ElapsedNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin ElapsedObjectPin;
	ElapsedObjectPin.Id = FGuid::NewGuid();
	ElapsedObjectPin.Name = TEXT("Object");
	ElapsedObjectPin.Direction = EVergilPinDirection::Input;
	ElapsedNode.Pins.Add(ElapsedObjectPin);

	FVergilGraphPin ElapsedFunctionNamePin;
	ElapsedFunctionNamePin.Id = FGuid::NewGuid();
	ElapsedFunctionNamePin.Name = TEXT("FunctionName");
	ElapsedFunctionNamePin.Direction = EVergilPinDirection::Input;
	ElapsedNode.Pins.Add(ElapsedFunctionNamePin);

	FVergilGraphPin ElapsedReturnPin;
	ElapsedReturnPin.Id = FGuid::NewGuid();
	ElapsedReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	ElapsedReturnPin.Direction = EVergilPinDirection::Output;
	ElapsedNode.Pins.Add(ElapsedReturnPin);

	FVergilGraphNode RemainingNode;
	RemainingNode.Id = FGuid::NewGuid();
	RemainingNode.Kind = EVergilNodeKind::Call;
	RemainingNode.Descriptor = TEXT("K2.Call.K2_GetTimerRemainingTime");
	RemainingNode.Position = FVector2D(1840.0f, 840.0f);
	RemainingNode.Metadata.Add(TEXT("OwnerClassPath"), UKismetSystemLibrary::StaticClass()->GetPathName());

	FVergilGraphPin RemainingObjectPin;
	RemainingObjectPin.Id = FGuid::NewGuid();
	RemainingObjectPin.Name = TEXT("Object");
	RemainingObjectPin.Direction = EVergilPinDirection::Input;
	RemainingNode.Pins.Add(RemainingObjectPin);

	FVergilGraphPin RemainingFunctionNamePin;
	RemainingFunctionNamePin.Id = FGuid::NewGuid();
	RemainingFunctionNamePin.Name = TEXT("FunctionName");
	RemainingFunctionNamePin.Direction = EVergilPinDirection::Input;
	RemainingNode.Pins.Add(RemainingFunctionNamePin);

	FVergilGraphPin RemainingReturnPin;
	RemainingReturnPin.Id = FGuid::NewGuid();
	RemainingReturnPin.Name = UEdGraphSchema_K2::PN_ReturnValue;
	RemainingReturnPin.Direction = EVergilPinDirection::Output;
	RemainingNode.Pins.Add(RemainingReturnPin);

	FVergilGraphNode SetActiveNode;
	SetActiveNode.Id = FGuid::NewGuid();
	SetActiveNode.Kind = EVergilNodeKind::VariableSet;
	SetActiveNode.Descriptor = TEXT("K2.VarSet.WasTimerActive");
	SetActiveNode.Position = FVector2D(2240.0f, 180.0f);

	FVergilGraphPin SetActiveExecPin;
	SetActiveExecPin.Id = FGuid::NewGuid();
	SetActiveExecPin.Name = TEXT("Execute");
	SetActiveExecPin.Direction = EVergilPinDirection::Input;
	SetActiveExecPin.bIsExec = true;
	SetActiveNode.Pins.Add(SetActiveExecPin);

	FVergilGraphPin SetActiveThenPin;
	SetActiveThenPin.Id = FGuid::NewGuid();
	SetActiveThenPin.Name = TEXT("Then");
	SetActiveThenPin.Direction = EVergilPinDirection::Output;
	SetActiveThenPin.bIsExec = true;
	SetActiveNode.Pins.Add(SetActiveThenPin);

	FVergilGraphPin SetActiveValuePin;
	SetActiveValuePin.Id = FGuid::NewGuid();
	SetActiveValuePin.Name = TEXT("WasTimerActive");
	SetActiveValuePin.Direction = EVergilPinDirection::Input;
	SetActiveNode.Pins.Add(SetActiveValuePin);

	FVergilGraphNode SetPausedNode;
	SetPausedNode.Id = FGuid::NewGuid();
	SetPausedNode.Kind = EVergilNodeKind::VariableSet;
	SetPausedNode.Descriptor = TEXT("K2.VarSet.WasTimerPaused");
	SetPausedNode.Position = FVector2D(2240.0f, 340.0f);

	FVergilGraphPin SetPausedExecPin;
	SetPausedExecPin.Id = FGuid::NewGuid();
	SetPausedExecPin.Name = TEXT("Execute");
	SetPausedExecPin.Direction = EVergilPinDirection::Input;
	SetPausedExecPin.bIsExec = true;
	SetPausedNode.Pins.Add(SetPausedExecPin);

	FVergilGraphPin SetPausedThenPin;
	SetPausedThenPin.Id = FGuid::NewGuid();
	SetPausedThenPin.Name = TEXT("Then");
	SetPausedThenPin.Direction = EVergilPinDirection::Output;
	SetPausedThenPin.bIsExec = true;
	SetPausedNode.Pins.Add(SetPausedThenPin);

	FVergilGraphPin SetPausedValuePin;
	SetPausedValuePin.Id = FGuid::NewGuid();
	SetPausedValuePin.Name = TEXT("WasTimerPaused");
	SetPausedValuePin.Direction = EVergilPinDirection::Input;
	SetPausedNode.Pins.Add(SetPausedValuePin);

	FVergilGraphNode SetExistsNode;
	SetExistsNode.Id = FGuid::NewGuid();
	SetExistsNode.Kind = EVergilNodeKind::VariableSet;
	SetExistsNode.Descriptor = TEXT("K2.VarSet.DidTimerExist");
	SetExistsNode.Position = FVector2D(2240.0f, 500.0f);

	FVergilGraphPin SetExistsExecPin;
	SetExistsExecPin.Id = FGuid::NewGuid();
	SetExistsExecPin.Name = TEXT("Execute");
	SetExistsExecPin.Direction = EVergilPinDirection::Input;
	SetExistsExecPin.bIsExec = true;
	SetExistsNode.Pins.Add(SetExistsExecPin);

	FVergilGraphPin SetExistsThenPin;
	SetExistsThenPin.Id = FGuid::NewGuid();
	SetExistsThenPin.Name = TEXT("Then");
	SetExistsThenPin.Direction = EVergilPinDirection::Output;
	SetExistsThenPin.bIsExec = true;
	SetExistsNode.Pins.Add(SetExistsThenPin);

	FVergilGraphPin SetExistsValuePin;
	SetExistsValuePin.Id = FGuid::NewGuid();
	SetExistsValuePin.Name = TEXT("DidTimerExist");
	SetExistsValuePin.Direction = EVergilPinDirection::Input;
	SetExistsNode.Pins.Add(SetExistsValuePin);

	FVergilGraphNode SetElapsedNode;
	SetElapsedNode.Id = FGuid::NewGuid();
	SetElapsedNode.Kind = EVergilNodeKind::VariableSet;
	SetElapsedNode.Descriptor = TEXT("K2.VarSet.ElapsedTimeValue");
	SetElapsedNode.Position = FVector2D(2240.0f, 660.0f);

	FVergilGraphPin SetElapsedExecPin;
	SetElapsedExecPin.Id = FGuid::NewGuid();
	SetElapsedExecPin.Name = TEXT("Execute");
	SetElapsedExecPin.Direction = EVergilPinDirection::Input;
	SetElapsedExecPin.bIsExec = true;
	SetElapsedNode.Pins.Add(SetElapsedExecPin);

	FVergilGraphPin SetElapsedThenPin;
	SetElapsedThenPin.Id = FGuid::NewGuid();
	SetElapsedThenPin.Name = TEXT("Then");
	SetElapsedThenPin.Direction = EVergilPinDirection::Output;
	SetElapsedThenPin.bIsExec = true;
	SetElapsedNode.Pins.Add(SetElapsedThenPin);

	FVergilGraphPin SetElapsedValuePin;
	SetElapsedValuePin.Id = FGuid::NewGuid();
	SetElapsedValuePin.Name = TEXT("ElapsedTimeValue");
	SetElapsedValuePin.Direction = EVergilPinDirection::Input;
	SetElapsedNode.Pins.Add(SetElapsedValuePin);

	FVergilGraphNode SetRemainingNode;
	SetRemainingNode.Id = FGuid::NewGuid();
	SetRemainingNode.Kind = EVergilNodeKind::VariableSet;
	SetRemainingNode.Descriptor = TEXT("K2.VarSet.RemainingTimeValue");
	SetRemainingNode.Position = FVector2D(2240.0f, 820.0f);

	FVergilGraphPin SetRemainingExecPin;
	SetRemainingExecPin.Id = FGuid::NewGuid();
	SetRemainingExecPin.Name = TEXT("Execute");
	SetRemainingExecPin.Direction = EVergilPinDirection::Input;
	SetRemainingExecPin.bIsExec = true;
	SetRemainingNode.Pins.Add(SetRemainingExecPin);

	FVergilGraphPin SetRemainingValuePin;
	SetRemainingValuePin.Id = FGuid::NewGuid();
	SetRemainingValuePin.Name = TEXT("RemainingTimeValue");
	SetRemainingValuePin.Direction = EVergilPinDirection::Input;
	SetRemainingNode.Pins.Add(SetRemainingValuePin);

	FVergilGraphDocument Document;
	Document.BlueprintPath = TEXT("/Temp/BP_VergilTimerFunctionNameExecution");
	Document.Nodes = { BeginPlayNode, SelfNode, TimerEventNode, FunctionNameGetterNode, SecondsGetterNode, LoopingGetterNode, SetTimerNode, SetHandleNode, PauseTimerNode, UnPauseTimerNode, IsActiveNode, IsPausedNode, ExistsNode, ElapsedNode, RemainingNode, SetActiveNode, SetPausedNode, SetExistsNode, SetElapsedNode, SetRemainingNode };

	auto AddEdge = [&Document](const FGuid& SourceNodeId, const FGuid& SourcePinId, const FGuid& TargetNodeId, const FGuid& TargetPinId)
	{
		FVergilGraphEdge Edge;
		Edge.Id = FGuid::NewGuid();
		Edge.SourceNodeId = SourceNodeId;
		Edge.SourcePinId = SourcePinId;
		Edge.TargetNodeId = TargetNodeId;
		Edge.TargetPinId = TargetPinId;
		Document.Edges.Add(Edge);
	};

	AddEdge(BeginPlayNode.Id, BeginPlayThen.Id, SetTimerNode.Id, SetTimerExecPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, SetTimerNode.Id, SetTimerObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, PauseTimerNode.Id, PauseTimerObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, UnPauseTimerNode.Id, UnPauseTimerObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, IsActiveNode.Id, IsActiveObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, IsPausedNode.Id, IsPausedObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, ExistsNode.Id, ExistsObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, ElapsedNode.Id, ElapsedObjectPin.Id);
	AddEdge(SelfNode.Id, SelfOutputPin.Id, RemainingNode.Id, RemainingObjectPin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, SetTimerNode.Id, SetTimerFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, PauseTimerNode.Id, PauseTimerFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, UnPauseTimerNode.Id, UnPauseTimerFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, IsActiveNode.Id, IsActiveFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, IsPausedNode.Id, IsPausedFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, ExistsNode.Id, ExistsFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, ElapsedNode.Id, ElapsedFunctionNamePin.Id);
	AddEdge(FunctionNameGetterNode.Id, FunctionNameValuePin.Id, RemainingNode.Id, RemainingFunctionNamePin.Id);
	AddEdge(SecondsGetterNode.Id, SecondsGetterValuePin.Id, SetTimerNode.Id, SetTimerTimePin.Id);
	AddEdge(LoopingGetterNode.Id, LoopingGetterValuePin.Id, SetTimerNode.Id, SetTimerLoopingPin.Id);
	AddEdge(SetTimerNode.Id, SetTimerThenPin.Id, SetHandleNode.Id, SetHandleExecPin.Id);
	AddEdge(SetTimerNode.Id, SetTimerReturnPin.Id, SetHandleNode.Id, SetHandleValuePin.Id);
	AddEdge(SetHandleNode.Id, SetHandleThenPin.Id, PauseTimerNode.Id, PauseTimerExecPin.Id);
	AddEdge(PauseTimerNode.Id, PauseTimerThenPin.Id, UnPauseTimerNode.Id, UnPauseTimerExecPin.Id);
	AddEdge(UnPauseTimerNode.Id, UnPauseTimerThenPin.Id, SetActiveNode.Id, SetActiveExecPin.Id);
	AddEdge(IsActiveNode.Id, IsActiveReturnPin.Id, SetActiveNode.Id, SetActiveValuePin.Id);
	AddEdge(SetActiveNode.Id, SetActiveThenPin.Id, SetPausedNode.Id, SetPausedExecPin.Id);
	AddEdge(IsPausedNode.Id, IsPausedReturnPin.Id, SetPausedNode.Id, SetPausedValuePin.Id);
	AddEdge(SetPausedNode.Id, SetPausedThenPin.Id, SetExistsNode.Id, SetExistsExecPin.Id);
	AddEdge(ExistsNode.Id, ExistsReturnPin.Id, SetExistsNode.Id, SetExistsValuePin.Id);
	AddEdge(SetExistsNode.Id, SetExistsThenPin.Id, SetElapsedNode.Id, SetElapsedExecPin.Id);
	AddEdge(ElapsedNode.Id, ElapsedReturnPin.Id, SetElapsedNode.Id, SetElapsedValuePin.Id);
	AddEdge(SetElapsedNode.Id, SetElapsedThenPin.Id, SetRemainingNode.Id, SetRemainingExecPin.Id);
	AddEdge(RemainingNode.Id, RemainingReturnPin.Id, SetRemainingNode.Id, SetRemainingValuePin.Id);

	const FVergilCompileResult Result = EditorSubsystem->CompileDocument(Blueprint, Document, false, false, true);

	TestTrue(TEXT("Timer function-name document should compile successfully."), Result.bSucceeded);
	TestTrue(TEXT("Timer function-name document should be applied."), Result.bApplied);
	TestTrue(TEXT("Timer function-name document should execute commands."), Result.ExecutedCommandCount > 0);

	UEdGraph* const EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	TestNotNull(TEXT("Event graph should exist after timer function-name execution."), EventGraph);
	if (EventGraph == nullptr)
	{
		return false;
	}

	UK2Node_Event* const EventNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, BeginPlayNode.Id);
	UK2Node_Self* const SelfGraphNode = FindGraphNodeByGuid<UK2Node_Self>(EventGraph, SelfNode.Id);
	UK2Node_CustomEvent* const TimerEventGraphNode = FindGraphNodeByGuid<UK2Node_CustomEvent>(EventGraph, TimerEventNode.Id);
	UK2Node_VariableGet* const FunctionNameGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, FunctionNameGetterNode.Id);
	UK2Node_VariableGet* const SecondsGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, SecondsGetterNode.Id);
	UK2Node_VariableGet* const LoopingGetterGraphNode = FindGraphNodeByGuid<UK2Node_VariableGet>(EventGraph, LoopingGetterNode.Id);
	UK2Node_CallFunction* const SetTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, SetTimerNode.Id);
	UK2Node_VariableSet* const SetHandleGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetHandleNode.Id);
	UK2Node_CallFunction* const PauseTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, PauseTimerNode.Id);
	UK2Node_CallFunction* const UnPauseTimerGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, UnPauseTimerNode.Id);
	UK2Node_CallFunction* const IsActiveGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, IsActiveNode.Id);
	UK2Node_CallFunction* const IsPausedGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, IsPausedNode.Id);
	UK2Node_CallFunction* const ExistsGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ExistsNode.Id);
	UK2Node_CallFunction* const ElapsedGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, ElapsedNode.Id);
	UK2Node_CallFunction* const RemainingGraphNode = FindGraphNodeByGuid<UK2Node_CallFunction>(EventGraph, RemainingNode.Id);
	UK2Node_VariableSet* const SetActiveGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetActiveNode.Id);
	UK2Node_VariableSet* const SetPausedGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetPausedNode.Id);
	UK2Node_VariableSet* const SetExistsGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetExistsNode.Id);
	UK2Node_VariableSet* const SetElapsedGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetElapsedNode.Id);
	UK2Node_VariableSet* const SetRemainingGraphNode = FindGraphNodeByGuid<UK2Node_VariableSet>(EventGraph, SetRemainingNode.Id);

	TestNotNull(TEXT("BeginPlay event should exist."), EventNode);
	TestNotNull(TEXT("Self node should exist."), SelfGraphNode);
	TestNotNull(TEXT("Target timer event should exist."), TimerEventGraphNode);
	TestNotNull(TEXT("TimerFunctionName getter should exist."), FunctionNameGetterGraphNode);
	TestNotNull(TEXT("TimerSeconds getter should exist."), SecondsGetterGraphNode);
	TestNotNull(TEXT("Looping getter should exist."), LoopingGetterGraphNode);
	TestNotNull(TEXT("SetTimer call should exist."), SetTimerGraphNode);
	TestNotNull(TEXT("TimerHandle setter should exist."), SetHandleGraphNode);
	TestNotNull(TEXT("PauseTimer call should exist."), PauseTimerGraphNode);
	TestNotNull(TEXT("UnPauseTimer call should exist."), UnPauseTimerGraphNode);
	TestNotNull(TEXT("IsTimerActive call should exist."), IsActiveGraphNode);
	TestNotNull(TEXT("IsTimerPaused call should exist."), IsPausedGraphNode);
	TestNotNull(TEXT("TimerExists call should exist."), ExistsGraphNode);
	TestNotNull(TEXT("GetTimerElapsedTime call should exist."), ElapsedGraphNode);
	TestNotNull(TEXT("GetTimerRemainingTime call should exist."), RemainingGraphNode);
	TestNotNull(TEXT("WasTimerActive setter should exist."), SetActiveGraphNode);
	TestNotNull(TEXT("WasTimerPaused setter should exist."), SetPausedGraphNode);
	TestNotNull(TEXT("DidTimerExist setter should exist."), SetExistsGraphNode);
	TestNotNull(TEXT("ElapsedTimeValue setter should exist."), SetElapsedGraphNode);
	TestNotNull(TEXT("RemainingTimeValue setter should exist."), SetRemainingGraphNode);
	if (EventNode == nullptr || SelfGraphNode == nullptr || TimerEventGraphNode == nullptr || FunctionNameGetterGraphNode == nullptr || SecondsGetterGraphNode == nullptr || LoopingGetterGraphNode == nullptr || SetTimerGraphNode == nullptr || SetHandleGraphNode == nullptr || PauseTimerGraphNode == nullptr || UnPauseTimerGraphNode == nullptr || IsActiveGraphNode == nullptr || IsPausedGraphNode == nullptr || ExistsGraphNode == nullptr || ElapsedGraphNode == nullptr || RemainingGraphNode == nullptr || SetActiveGraphNode == nullptr || SetPausedGraphNode == nullptr || SetExistsGraphNode == nullptr || SetElapsedGraphNode == nullptr || SetRemainingGraphNode == nullptr)
	{
		return false;
	}

	UEdGraphPin* const EventThenGraphPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SelfGraphPin = SelfGraphNode->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* const FunctionNameGraphValuePin = FunctionNameGetterGraphNode->FindPin(TEXT("TimerFunctionName"));
	UEdGraphPin* const SecondsGraphPin = SecondsGetterGraphNode->FindPin(TEXT("TimerSeconds"));
	UEdGraphPin* const LoopingGraphPin = LoopingGetterGraphNode->FindPin(TEXT("Looping"));
	UEdGraphPin* const SetTimerExecGraphPin = SetTimerGraphNode->GetExecPin();
	UEdGraphPin* const SetTimerObjectGraphPin = SetTimerGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const SetTimerFunctionGraphPin = SetTimerGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const SetTimerTimeGraphPin = SetTimerGraphNode->FindPin(TEXT("Time"));
	UEdGraphPin* const SetTimerLoopingGraphPin = SetTimerGraphNode->FindPin(TEXT("bLooping"));
	UEdGraphPin* const SetTimerThenGraphPin = SetTimerGraphNode->GetThenPin();
	UEdGraphPin* const SetTimerReturnGraphPin = SetTimerGraphNode->GetReturnValuePin();
	UEdGraphPin* const SetHandleExecGraphPin = SetHandleGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetHandleThenGraphPin = SetHandleGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetHandleValueGraphPin = SetHandleGraphNode->FindPin(TEXT("TimerHandleVar"));
	UEdGraphPin* const PauseExecGraphPin = PauseTimerGraphNode->GetExecPin();
	UEdGraphPin* const PauseObjectGraphPin = PauseTimerGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const PauseFunctionNameGraphPin = PauseTimerGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const PauseThenGraphPin = PauseTimerGraphNode->GetThenPin();
	UEdGraphPin* const UnPauseExecGraphPin = UnPauseTimerGraphNode->GetExecPin();
	UEdGraphPin* const UnPauseObjectGraphPin = UnPauseTimerGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const UnPauseFunctionNameGraphPin = UnPauseTimerGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const UnPauseThenGraphPin = UnPauseTimerGraphNode->GetThenPin();
	UEdGraphPin* const IsActiveObjectGraphPin = IsActiveGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const IsActiveFunctionNameGraphPin = IsActiveGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const IsActiveReturnGraphPin = IsActiveGraphNode->GetReturnValuePin();
	UEdGraphPin* const IsPausedObjectGraphPin = IsPausedGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const IsPausedFunctionNameGraphPin = IsPausedGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const IsPausedReturnGraphPin = IsPausedGraphNode->GetReturnValuePin();
	UEdGraphPin* const ExistsObjectGraphPin = ExistsGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const ExistsFunctionNameGraphPin = ExistsGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const ExistsReturnGraphPin = ExistsGraphNode->GetReturnValuePin();
	UEdGraphPin* const ElapsedObjectGraphPin = ElapsedGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const ElapsedFunctionNameGraphPin = ElapsedGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const ElapsedReturnGraphPin = ElapsedGraphNode->GetReturnValuePin();
	UEdGraphPin* const RemainingObjectGraphPin = RemainingGraphNode->FindPin(TEXT("Object"));
	UEdGraphPin* const RemainingFunctionNameGraphPin = RemainingGraphNode->FindPin(TEXT("FunctionName"));
	UEdGraphPin* const RemainingReturnGraphPin = RemainingGraphNode->GetReturnValuePin();
	UEdGraphPin* const SetActiveExecGraphPin = SetActiveGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetActiveThenGraphPin = SetActiveGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetActiveValueGraphPin = SetActiveGraphNode->FindPin(TEXT("WasTimerActive"));
	UEdGraphPin* const SetPausedExecGraphPin = SetPausedGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetPausedThenGraphPin = SetPausedGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetPausedValueGraphPin = SetPausedGraphNode->FindPin(TEXT("WasTimerPaused"));
	UEdGraphPin* const SetExistsExecGraphPin = SetExistsGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetExistsThenGraphPin = SetExistsGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetExistsValueGraphPin = SetExistsGraphNode->FindPin(TEXT("DidTimerExist"));
	UEdGraphPin* const SetElapsedExecGraphPin = SetElapsedGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetElapsedThenGraphPin = SetElapsedGraphNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* const SetElapsedValueGraphPin = SetElapsedGraphNode->FindPin(TEXT("ElapsedTimeValue"));
	UEdGraphPin* const SetRemainingExecGraphPin = SetRemainingGraphNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* const SetRemainingValueGraphPin = SetRemainingGraphNode->FindPin(TEXT("RemainingTimeValue"));

	TestTrue(TEXT("SetTimer should resolve UKismetSystemLibrary::K2_SetTimer."), SetTimerGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_SetTimer)));
	TestTrue(TEXT("PauseTimer should resolve UKismetSystemLibrary::K2_PauseTimer."), PauseTimerGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_PauseTimer)));
	TestTrue(TEXT("UnPauseTimer should resolve UKismetSystemLibrary::K2_UnPauseTimer."), UnPauseTimerGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_UnPauseTimer)));
	TestTrue(TEXT("IsTimerActive should resolve UKismetSystemLibrary::K2_IsTimerActive."), IsActiveGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_IsTimerActive)));
	TestTrue(TEXT("IsTimerPaused should resolve UKismetSystemLibrary::K2_IsTimerPaused."), IsPausedGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_IsTimerPaused)));
	TestTrue(TEXT("TimerExists should resolve UKismetSystemLibrary::K2_TimerExists."), ExistsGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_TimerExists)));
	TestTrue(TEXT("GetTimerElapsedTime should resolve UKismetSystemLibrary::K2_GetTimerElapsedTime."), ElapsedGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_GetTimerElapsedTime)));
	TestTrue(TEXT("GetTimerRemainingTime should resolve UKismetSystemLibrary::K2_GetTimerRemainingTime."), RemainingGraphNode->GetTargetFunction() == UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, K2_GetTimerRemainingTime)));
	TestTrue(TEXT("SetTimer should expose a timer-handle return value."), SetTimerReturnGraphPin != nullptr
		&& SetTimerReturnGraphPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
		&& SetTimerReturnGraphPin->PinType.PinSubCategoryObject.Get() == TBaseStructure<FTimerHandle>::Get());
	TestTrue(TEXT("Function-name query nodes should remain pure."), IsActiveGraphNode->GetExecPin() == nullptr && IsPausedGraphNode->GetExecPin() == nullptr && ExistsGraphNode->GetExecPin() == nullptr && ElapsedGraphNode->GetExecPin() == nullptr && RemainingGraphNode->GetExecPin() == nullptr);
	TestTrue(TEXT("BeginPlay should feed SetTimer execute."), EventThenGraphPin != nullptr && EventThenGraphPin->LinkedTo.Contains(SetTimerExecGraphPin));
	TestTrue(TEXT("Self should feed SetTimer Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(SetTimerObjectGraphPin));
	TestTrue(TEXT("Self should feed PauseTimer Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(PauseObjectGraphPin));
	TestTrue(TEXT("Self should feed UnPauseTimer Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(UnPauseObjectGraphPin));
	TestTrue(TEXT("Self should feed IsTimerActive Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(IsActiveObjectGraphPin));
	TestTrue(TEXT("Self should feed IsTimerPaused Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(IsPausedObjectGraphPin));
	TestTrue(TEXT("Self should feed TimerExists Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(ExistsObjectGraphPin));
	TestTrue(TEXT("Self should feed GetTimerElapsedTime Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(ElapsedObjectGraphPin));
	TestTrue(TEXT("Self should feed GetTimerRemainingTime Object."), SelfGraphPin != nullptr && SelfGraphPin->LinkedTo.Contains(RemainingObjectGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed SetTimer FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(SetTimerFunctionGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed PauseTimer FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(PauseFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed UnPauseTimer FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(UnPauseFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed IsTimerActive FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(IsActiveFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed IsTimerPaused FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(IsPausedFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed TimerExists FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(ExistsFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed GetTimerElapsedTime FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(ElapsedFunctionNameGraphPin));
	TestTrue(TEXT("TimerFunctionName getter should feed GetTimerRemainingTime FunctionName."), FunctionNameGraphValuePin != nullptr && FunctionNameGraphValuePin->LinkedTo.Contains(RemainingFunctionNameGraphPin));
	TestTrue(TEXT("TimerSeconds getter should feed SetTimer Time."), SecondsGraphPin != nullptr && SecondsGraphPin->LinkedTo.Contains(SetTimerTimeGraphPin));
	TestTrue(TEXT("Looping getter should feed SetTimer bLooping."), LoopingGraphPin != nullptr && LoopingGraphPin->LinkedTo.Contains(SetTimerLoopingGraphPin));
	TestTrue(TEXT("SetTimer Then should feed TimerHandle setter execute."), SetTimerThenGraphPin != nullptr && SetTimerThenGraphPin->LinkedTo.Contains(SetHandleExecGraphPin));
	TestTrue(TEXT("SetTimer return value should feed TimerHandle setter."), SetTimerReturnGraphPin != nullptr && SetTimerReturnGraphPin->LinkedTo.Contains(SetHandleValueGraphPin));
	TestTrue(TEXT("TimerHandle setter Then should feed PauseTimer execute."), SetHandleThenGraphPin != nullptr && SetHandleThenGraphPin->LinkedTo.Contains(PauseExecGraphPin));
	TestTrue(TEXT("PauseTimer Then should feed UnPauseTimer execute."), PauseThenGraphPin != nullptr && PauseThenGraphPin->LinkedTo.Contains(UnPauseExecGraphPin));
	TestTrue(TEXT("UnPauseTimer Then should feed WasTimerActive setter execute."), UnPauseThenGraphPin != nullptr && UnPauseThenGraphPin->LinkedTo.Contains(SetActiveExecGraphPin));
	TestTrue(TEXT("IsTimerActive return value should feed WasTimerActive setter."), IsActiveReturnGraphPin != nullptr && IsActiveReturnGraphPin->LinkedTo.Contains(SetActiveValueGraphPin));
	TestTrue(TEXT("WasTimerActive setter Then should feed WasTimerPaused setter execute."), SetActiveThenGraphPin != nullptr && SetActiveThenGraphPin->LinkedTo.Contains(SetPausedExecGraphPin));
	TestTrue(TEXT("IsTimerPaused return value should feed WasTimerPaused setter."), IsPausedReturnGraphPin != nullptr && IsPausedReturnGraphPin->LinkedTo.Contains(SetPausedValueGraphPin));
	TestTrue(TEXT("WasTimerPaused setter Then should feed DidTimerExist setter execute."), SetPausedThenGraphPin != nullptr && SetPausedThenGraphPin->LinkedTo.Contains(SetExistsExecGraphPin));
	TestTrue(TEXT("TimerExists return value should feed DidTimerExist setter."), ExistsReturnGraphPin != nullptr && ExistsReturnGraphPin->LinkedTo.Contains(SetExistsValueGraphPin));
	TestTrue(TEXT("DidTimerExist setter Then should feed ElapsedTimeValue setter execute."), SetExistsThenGraphPin != nullptr && SetExistsThenGraphPin->LinkedTo.Contains(SetElapsedExecGraphPin));
	TestTrue(TEXT("GetTimerElapsedTime return value should feed ElapsedTimeValue setter."), ElapsedReturnGraphPin != nullptr && ElapsedReturnGraphPin->LinkedTo.Contains(SetElapsedValueGraphPin));
	TestTrue(TEXT("ElapsedTimeValue setter Then should feed RemainingTimeValue setter execute."), SetElapsedThenGraphPin != nullptr && SetElapsedThenGraphPin->LinkedTo.Contains(SetRemainingExecGraphPin));
	TestTrue(TEXT("GetTimerRemainingTime return value should feed RemainingTimeValue setter."), RemainingReturnGraphPin != nullptr && RemainingReturnGraphPin->LinkedTo.Contains(SetRemainingValueGraphPin));

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

