#include "Misc/AutomationTest.h"

#include "Algo/Sort.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "UObject/SavePackage.h"
#include "VergilAutomationTestInterface.h"
#include "VergilEditorSubsystem.h"
#include "VergilGraphDocument.h"

namespace
{
	inline constexpr TCHAR GoldenAssetName[] = TEXT("BP_VergilGoldenAsset");
	inline constexpr TCHAR GoldenSnapshotRelativePath[] = TEXT("Plugins/Vergil/Tests/GoldenAssets/BP_VergilGoldenAsset.txt");
	inline constexpr TCHAR ActualSnapshotRelativePath[] = TEXT("Vergil/GoldenAssets/BP_VergilGoldenAsset.actual.txt");

	struct FScopedGoldenAssetBlueprint final
	{
		explicit FScopedGoldenAssetBlueprint(const FString& InAssetName)
			: AssetName(InAssetName)
		{
			PackagePath = FString::Printf(TEXT("/Game/Tests/%s"), *AssetName);
			ObjectPath = FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName);
			verify(FPackageName::TryConvertLongPackageNameToFilename(
				PackagePath,
				PackageFilename,
				FPackageName::GetAssetPackageExtension()));
			Cleanup();
		}

		~FScopedGoldenAssetBlueprint()
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

	FString NormalizeLineEndings(FString Text)
	{
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Text.ReplaceInline(TEXT("\r"), TEXT("\n"));
		while (Text.EndsWith(TEXT("\n")))
		{
			Text.LeftChopInline(1, EAllowShrinking::No);
		}
		return Text;
	}

	FString GetGoldenSnapshotPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), GoldenSnapshotRelativePath));
	}

	FString GetActualSnapshotPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), ActualSnapshotRelativePath));
	}

	void WriteActualSnapshot(const FString& Snapshot)
	{
		const FString ActualPath = GetActualSnapshotPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ActualPath), true);
		FFileHelper::SaveStringToFile(Snapshot, *ActualPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	FString DescribePinType(const UEdGraphPin* Pin)
	{
		if (Pin == nullptr)
		{
			return TEXT("<missing>");
		}

		FString Description = Pin->PinType.PinCategory.ToString();
		if (Pin->PinType.PinSubCategoryObject.IsValid())
		{
			Description += FString::Printf(TEXT(":%s"), *Pin->PinType.PinSubCategoryObject->GetPathName());
		}

		return Description;
	}

	FString DescribeBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString DescribeVector(const FVector& Value)
	{
		return FString::Printf(TEXT("%.1f,%.1f,%.1f"), Value.X, Value.Y, Value.Z);
	}

	FString DescribePosition(const UEdGraphNode* Node)
	{
		return Node != nullptr
			? FString::Printf(TEXT("%d,%d"), Node->NodePosX, Node->NodePosY)
			: FString(TEXT("<missing>"));
	}

	struct FGoldenAssetFixture
	{
		FName VariableName = TEXT("RoundtripFlag");
		FName FunctionName = TEXT("ComputeStatus");
		FName MacroName = TEXT("RouteTarget");
		FName RootComponentName = TEXT("Root");
		FName VisualComponentName = TEXT("VisualMesh");
		FGuid BeginPlayNodeId = FGuid(0x10010010, 0x20020020, 0x30030030, 0x40040040);
		FGuid BeginPlayThenPinId = FGuid(0x10010011, 0x20020021, 0x30030031, 0x40040041);
		FGuid EventSequenceNodeId = FGuid(0x10010012, 0x20020022, 0x30030032, 0x40040042);
		FGuid EventSequenceExecPinId = FGuid(0x10010013, 0x20020023, 0x30030033, 0x40040043);
		FGuid EventGraphEdgeId = FGuid(0x10010014, 0x20020024, 0x30030034, 0x40040044);
		FGuid ConstructionEventNodeId = FGuid(0x10010015, 0x20020025, 0x30030035, 0x40040045);
		FGuid ConstructionThenPinId = FGuid(0x10010016, 0x20020026, 0x30030036, 0x40040046);
		FGuid ConstructionSequenceNodeId = FGuid(0x10010017, 0x20020027, 0x30030037, 0x40040047);
		FGuid ConstructionSequenceExecPinId = FGuid(0x10010018, 0x20020028, 0x30030038, 0x40040048);
		FGuid ConstructionEdgeId = FGuid(0x10010019, 0x20020029, 0x30030039, 0x40040049);

		FVergilGraphDocument BuildEventDocument(const FString& PackagePath) const
		{
			FVergilVariableDefinition RoundtripVariable;
			RoundtripVariable.Name = VariableName;
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
			BeginPlayNode.Id = BeginPlayNodeId;
			BeginPlayNode.Kind = EVergilNodeKind::Event;
			BeginPlayNode.Descriptor = TEXT("K2.Event.ReceiveBeginPlay");
			BeginPlayNode.Position = FVector2D(0.0f, 0.0f);

			FVergilGraphPin BeginPlayThenPin;
			BeginPlayThenPin.Id = BeginPlayThenPinId;
			BeginPlayThenPin.Name = TEXT("Then");
			BeginPlayThenPin.Direction = EVergilPinDirection::Output;
			BeginPlayThenPin.bIsExec = true;
			BeginPlayNode.Pins.Add(BeginPlayThenPin);

			FVergilGraphNode EventSequenceNode;
			EventSequenceNode.Id = EventSequenceNodeId;
			EventSequenceNode.Kind = EVergilNodeKind::Custom;
			EventSequenceNode.Descriptor = TEXT("K2.Sequence");
			EventSequenceNode.Position = FVector2D(320.0f, 0.0f);

			FVergilGraphPin EventSequenceExecPin;
			EventSequenceExecPin.Id = EventSequenceExecPinId;
			EventSequenceExecPin.Name = TEXT("Execute");
			EventSequenceExecPin.Direction = EVergilPinDirection::Input;
			EventSequenceExecPin.bIsExec = true;
			EventSequenceNode.Pins.Add(EventSequenceExecPin);

			FVergilGraphEdge EventGraphEdge;
			EventGraphEdge.Id = EventGraphEdgeId;
			EventGraphEdge.SourceNodeId = BeginPlayNode.Id;
			EventGraphEdge.SourcePinId = BeginPlayThenPin.Id;
			EventGraphEdge.TargetNodeId = EventSequenceNode.Id;
			EventGraphEdge.TargetPinId = EventSequenceExecPin.Id;

			FVergilGraphDocument EventDocument;
			EventDocument.SchemaVersion = Vergil::SchemaVersion;
			EventDocument.BlueprintPath = PackagePath;
			EventDocument.Metadata.Add(TEXT("BlueprintDisplayName"), TEXT("Vergil Golden Asset"));
			EventDocument.Metadata.Add(TEXT("BlueprintDescription"), TEXT("Golden asset snapshot coverage."));
			EventDocument.Metadata.Add(TEXT("BlueprintCategory"), TEXT("Vergil|Golden"));
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
			return EventDocument;
		}

		FVergilGraphDocument BuildConstructionDocument(const FString& PackagePath) const
		{
			FVergilGraphNode ConstructionEventNode;
			ConstructionEventNode.Id = ConstructionEventNodeId;
			ConstructionEventNode.Kind = EVergilNodeKind::Event;
			ConstructionEventNode.Descriptor = TEXT("K2.Event.UserConstructionScript");
			ConstructionEventNode.Position = FVector2D(0.0f, 0.0f);

			FVergilGraphPin ConstructionThenPin;
			ConstructionThenPin.Id = ConstructionThenPinId;
			ConstructionThenPin.Name = TEXT("Then");
			ConstructionThenPin.Direction = EVergilPinDirection::Output;
			ConstructionThenPin.bIsExec = true;
			ConstructionEventNode.Pins.Add(ConstructionThenPin);

			FVergilGraphNode ConstructionSequenceNode;
			ConstructionSequenceNode.Id = ConstructionSequenceNodeId;
			ConstructionSequenceNode.Kind = EVergilNodeKind::Custom;
			ConstructionSequenceNode.Descriptor = TEXT("K2.Sequence");
			ConstructionSequenceNode.Position = FVector2D(320.0f, 0.0f);

			FVergilGraphPin ConstructionSequenceExecPin;
			ConstructionSequenceExecPin.Id = ConstructionSequenceExecPinId;
			ConstructionSequenceExecPin.Name = TEXT("Execute");
			ConstructionSequenceExecPin.Direction = EVergilPinDirection::Input;
			ConstructionSequenceExecPin.bIsExec = true;
			ConstructionSequenceNode.Pins.Add(ConstructionSequenceExecPin);

			FVergilGraphEdge ConstructionEdge;
			ConstructionEdge.Id = ConstructionEdgeId;
			ConstructionEdge.SourceNodeId = ConstructionEventNode.Id;
			ConstructionEdge.SourcePinId = ConstructionThenPin.Id;
			ConstructionEdge.TargetNodeId = ConstructionSequenceNode.Id;
			ConstructionEdge.TargetPinId = ConstructionSequenceExecPin.Id;

			FVergilGraphDocument ConstructionDocument;
			ConstructionDocument.SchemaVersion = Vergil::SchemaVersion;
			ConstructionDocument.BlueprintPath = PackagePath;
			ConstructionDocument.ConstructionScriptNodes = { ConstructionEventNode, ConstructionSequenceNode };
			ConstructionDocument.ConstructionScriptEdges.Add(ConstructionEdge);
			return ConstructionDocument;
		}
	};

	FString BuildGoldenAssetSnapshot(
		UBlueprint* Blueprint,
		const FVergilCompileResult& EventDryRunResult,
		const FVergilCompileResult& ConstructionDryRunResult,
		const FGoldenAssetFixture& Fixture)
	{
		TArray<FString> Lines;

		Lines.Add(FString::Printf(TEXT("blueprintPath=%s"), *Blueprint->GetPathName()));
		Lines.Add(FString::Printf(TEXT("displayName=%s"), *Blueprint->BlueprintDisplayName));
		Lines.Add(FString::Printf(TEXT("description=%s"), *Blueprint->BlueprintDescription));
		Lines.Add(FString::Printf(TEXT("category=%s"), *Blueprint->BlueprintCategory));

		TArray<FString> HideCategories = Blueprint->HideCategories;
		Algo::Sort(HideCategories);
		Lines.Add(FString::Printf(TEXT("hideCategories=%s"), *FString::Join(HideCategories, TEXT("|"))));

		const FBPVariableDescription* const VariableDescription = FindBlueprintVariableDescription(Blueprint, Fixture.VariableName);
		const FString VariableCategory = VariableDescription != nullptr
			? FBlueprintEditorUtils::GetBlueprintVariableCategory(Blueprint, Fixture.VariableName, nullptr).ToString()
			: TEXT("<missing>");
		FString VariableTooltip;
		if (VariableDescription != nullptr)
		{
			FBlueprintEditorUtils::GetBlueprintVariableMetaData(Blueprint, Fixture.VariableName, nullptr, TEXT("Tooltip"), VariableTooltip);
		}

		AActor* const BlueprintCDO = Blueprint->GeneratedClass != nullptr
			? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
		const FBoolProperty* const RoundtripFlagProperty = Blueprint->GeneratedClass != nullptr
			? FindFProperty<FBoolProperty>(Blueprint->GeneratedClass, Fixture.VariableName)
			: nullptr;
		Lines.Add(FString::Printf(
			TEXT("variable=%s|type=%s|defaultOnCDO=%s|category=%s|tooltip=%s"),
			*Fixture.VariableName.ToString(),
			VariableDescription != nullptr ? *VariableDescription->VarType.PinCategory.ToString() : TEXT("<missing>"),
			(RoundtripFlagProperty != nullptr && BlueprintCDO != nullptr && RoundtripFlagProperty->GetPropertyValue_InContainer(BlueprintCDO))
				? TEXT("true")
				: TEXT("false"),
			*VariableCategory,
			*VariableTooltip));

		UEdGraph* const FunctionGraph = FindBlueprintGraphByName(Blueprint, Fixture.FunctionName);
		UK2Node_FunctionEntry* const FunctionEntry = FindFunctionEntryNode(FunctionGraph);
		UK2Node_FunctionResult* const FunctionResult = FindFunctionResultNode(FunctionGraph);
		Lines.Add(FString::Printf(
			TEXT("function=%s|pure=%s|protected=%s|input=%s|output=%s"),
			*Fixture.FunctionName.ToString(),
			FunctionEntry != nullptr && FunctionEntry->HasAllExtraFlags(FUNC_BlueprintPure) ? TEXT("true") : TEXT("false"),
			FunctionEntry != nullptr && FunctionEntry->HasAllExtraFlags(FUNC_Protected) ? TEXT("true") : TEXT("false"),
			*DescribePinType(FunctionEntry != nullptr ? FunctionEntry->FindPin(TEXT("Threshold")) : nullptr),
			*DescribePinType(FunctionResult != nullptr ? FunctionResult->FindPin(TEXT("Result")) : nullptr)));

		UEdGraph* const MacroGraph = FindBlueprintGraphByName(Blueprint, Fixture.MacroName);
		UK2Node_EditablePinBase* const MacroEntry = FindEditableGraphEntryNode(MacroGraph);
		UK2Node_EditablePinBase* const MacroExit = FindEditableGraphResultNode(MacroGraph);
		Lines.Add(FString::Printf(
			TEXT("macro=%s|execute=%s|target=%s|then=%s|count=%s"),
			*Fixture.MacroName.ToString(),
			*DescribePinType(MacroEntry != nullptr ? MacroEntry->FindPin(TEXT("Execute")) : nullptr),
			*DescribePinType(MacroEntry != nullptr ? MacroEntry->FindPin(TEXT("TargetActor")) : nullptr),
			*DescribePinType(MacroExit != nullptr ? MacroExit->FindPin(TEXT("Then")) : nullptr),
			*DescribePinType(MacroExit != nullptr ? MacroExit->FindPin(TEXT("Count")) : nullptr)));

		USCS_Node* const RootNode = FindBlueprintComponentNode(Blueprint, Fixture.RootComponentName);
		USCS_Node* const VisualNode = FindBlueprintComponentNode(Blueprint, Fixture.VisualComponentName);
		USceneComponent* const RootTemplate = RootNode != nullptr ? Cast<USceneComponent>(RootNode->ComponentTemplate) : nullptr;
		UStaticMeshComponent* const VisualTemplate = VisualNode != nullptr ? Cast<UStaticMeshComponent>(VisualNode->ComponentTemplate) : nullptr;
		const FBoolProperty* const HiddenInGameProperty = VisualTemplate != nullptr
			? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("bHiddenInGame"))
			: nullptr;
		const FBoolProperty* const CastShadowProperty = VisualTemplate != nullptr
			? FindFProperty<FBoolProperty>(VisualTemplate->GetClass(), TEXT("CastShadow"))
			: nullptr;
		Lines.Add(FString::Printf(
			TEXT("component=%s|class=%s|parent=<none>|socket=<none>|location=%s"),
			*Fixture.RootComponentName.ToString(),
			RootNode != nullptr && RootNode->ComponentClass != nullptr ? *RootNode->ComponentClass->GetClassPathName().ToString() : TEXT("<missing>"),
			RootTemplate != nullptr ? *DescribeVector(RootTemplate->GetRelativeLocation()) : TEXT("<missing>")));
		Lines.Add(FString::Printf(
			TEXT("component=%s|class=%s|parent=%s|socket=%s|hiddenInGame=%s|castShadow=%s"),
			*Fixture.VisualComponentName.ToString(),
			VisualNode != nullptr && VisualNode->ComponentClass != nullptr ? *VisualNode->ComponentClass->GetClassPathName().ToString() : TEXT("<missing>"),
			*Fixture.RootComponentName.ToString(),
			VisualNode != nullptr ? *VisualNode->AttachToName.ToString() : TEXT("<missing>"),
			HiddenInGameProperty != nullptr && VisualTemplate != nullptr && HiddenInGameProperty->GetPropertyValue_InContainer(VisualTemplate)
				? TEXT("true")
				: TEXT("false"),
			CastShadowProperty != nullptr && VisualTemplate != nullptr && CastShadowProperty->GetPropertyValue_InContainer(VisualTemplate)
				? TEXT("true")
				: TEXT("false")));

		TArray<UClass*> ImplementedInterfaces;
		FBlueprintEditorUtils::FindImplementedInterfaces(Blueprint, true, ImplementedInterfaces);
		ImplementedInterfaces.Sort([](const UClass& A, const UClass& B)
		{
			return A.GetClassPathName().ToString() < B.GetClassPathName().ToString();
		});
		TArray<FString> InterfacePaths;
		for (UClass* InterfaceClass : ImplementedInterfaces)
		{
			if (InterfaceClass != nullptr)
			{
				InterfacePaths.Add(InterfaceClass->GetClassPathName().ToString());
			}
		}
		Lines.Add(FString::Printf(TEXT("interfaces=%s"), *FString::Join(InterfacePaths, TEXT("|"))));
		Lines.Add(FString::Printf(
			TEXT("classDefaults|replicates=%s|initialLifeSpan=%.1f"),
			BlueprintCDO != nullptr && BlueprintCDO->GetIsReplicated() ? TEXT("true") : TEXT("false"),
			BlueprintCDO != nullptr ? BlueprintCDO->InitialLifeSpan : -1.0f));

		UEdGraph* const EventGraph = FindBlueprintGraphByName(Blueprint, TEXT("EventGraph"));
		UK2Node_Event* const BeginPlayNode = FindGraphNodeByGuid<UK2Node_Event>(EventGraph, Fixture.BeginPlayNodeId);
		UK2Node_ExecutionSequence* const EventSequenceNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(EventGraph, Fixture.EventSequenceNodeId);
		Lines.Add(FString::Printf(
			TEXT("eventGraph|beginPlayPos=%s|sequencePos=%s|edge=%s"),
			*DescribePosition(BeginPlayNode),
			*DescribePosition(EventSequenceNode),
			(BeginPlayNode != nullptr
				&& EventSequenceNode != nullptr
				&& BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then) != nullptr
				&& BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then)->LinkedTo.Contains(EventSequenceNode->GetExecPin()))
				? TEXT("BeginPlay.Then->Sequence.Execute")
				: TEXT("<missing>")));

		UEdGraph* const ConstructionGraph = FindBlueprintGraphByName(Blueprint, UEdGraphSchema_K2::FN_UserConstructionScript);
		UK2Node_FunctionEntry* const ConstructionEntryNode = FindGraphNodeByGuid<UK2Node_FunctionEntry>(ConstructionGraph, Fixture.ConstructionEventNodeId);
		UK2Node_ExecutionSequence* const ConstructionSequenceNode = FindGraphNodeByGuid<UK2Node_ExecutionSequence>(ConstructionGraph, Fixture.ConstructionSequenceNodeId);
		Lines.Add(FString::Printf(
			TEXT("constructionGraph|entryPos=%s|sequencePos=%s|edge=%s"),
			*DescribePosition(ConstructionEntryNode),
			*DescribePosition(ConstructionSequenceNode),
			(ConstructionEntryNode != nullptr
				&& ConstructionSequenceNode != nullptr
				&& ConstructionEntryNode->FindPin(UEdGraphSchema_K2::PN_Then) != nullptr
				&& ConstructionEntryNode->FindPin(UEdGraphSchema_K2::PN_Then)->LinkedTo.Contains(ConstructionSequenceNode->GetExecPin()))
				? TEXT("UserConstructionScript.Then->Sequence.Execute")
				: TEXT("<missing>")));

		Lines.Add(FString::Printf(TEXT("eventDryRun|fingerprint=%s|commands=%d"),
			*EventDryRunResult.Statistics.CommandPlanFingerprint,
			EventDryRunResult.Commands.Num()));
		Lines.Add(FString::Printf(TEXT("constructionDryRun|fingerprint=%s|commands=%d"),
			*ConstructionDryRunResult.Statistics.CommandPlanFingerprint,
			ConstructionDryRunResult.Commands.Num()));

		return FString::Join(Lines, TEXT("\n"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FVergilGoldenAssetSnapshotTest,
	"Vergil.Scaffold.GoldenAssetSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVergilGoldenAssetSnapshotTest::RunTest(const FString& Parameters)
{
	UVergilEditorSubsystem* const EditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UVergilEditorSubsystem>() : nullptr;
	TestNotNull(TEXT("Vergil editor subsystem should be available for golden-asset coverage."), EditorSubsystem);
	if (EditorSubsystem == nullptr)
	{
		return false;
	}

	const FGoldenAssetFixture Fixture;
	FScopedGoldenAssetBlueprint GoldenBlueprint(GoldenAssetName);
	UBlueprint* const Blueprint = GoldenBlueprint.CreateBlueprintAsset();
	TestNotNull(TEXT("Golden-asset coverage should create a persistent Blueprint asset."), Blueprint);
	if (Blueprint == nullptr)
	{
		return false;
	}

	const FVergilGraphDocument EventDocument = Fixture.BuildEventDocument(GoldenBlueprint.PackagePath);
	const FVergilGraphDocument ConstructionDocument = Fixture.BuildConstructionDocument(GoldenBlueprint.PackagePath);

	const FVergilCompileResult InitialEventResult = EditorSubsystem->CompileDocument(Blueprint, EventDocument, false, false, true);
	TestTrue(TEXT("Golden-asset event-graph authoring should succeed."), InitialEventResult.bSucceeded && InitialEventResult.bApplied);
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
	TestTrue(TEXT("Golden-asset construction-script authoring should succeed."), InitialConstructionResult.bSucceeded && InitialConstructionResult.bApplied);
	if (!InitialConstructionResult.bSucceeded || !InitialConstructionResult.bApplied)
	{
		return false;
	}

	FString SaveErrorMessage;
	TestTrue(TEXT("Golden-asset Blueprint package should save cleanly."), GoldenBlueprint.Save(Blueprint, &SaveErrorMessage));
	if (!SaveErrorMessage.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("Golden save status: %s"), *SaveErrorMessage));
	}

	FString ReloadErrorMessage;
	UBlueprint* const ReloadedBlueprint = GoldenBlueprint.Reload(&ReloadErrorMessage);
	TestNotNull(TEXT("Golden-asset Blueprint should reload from disk."), ReloadedBlueprint);
	if (!ReloadErrorMessage.IsEmpty())
	{
		AddInfo(FString::Printf(TEXT("Golden reload status: %s"), *ReloadErrorMessage));
	}
	if (ReloadedBlueprint == nullptr)
	{
		return false;
	}

	const FVergilCompileResult EventDryRunResult = EditorSubsystem->CompileDocument(ReloadedBlueprint, EventDocument, false, false, false);
	TestTrue(TEXT("Golden-asset event-graph dry-run should succeed after reload."), EventDryRunResult.bSucceeded && !EventDryRunResult.bApplied);
	if (!EventDryRunResult.bSucceeded || EventDryRunResult.bApplied)
	{
		return false;
	}

	const FVergilCompileResult ConstructionDryRunResult = EditorSubsystem->CompileDocumentToGraph(
		ReloadedBlueprint,
		ConstructionDocument,
		UEdGraphSchema_K2::FN_UserConstructionScript,
		false,
		false,
		false);
	TestTrue(TEXT("Golden-asset construction-script dry-run should succeed after reload."), ConstructionDryRunResult.bSucceeded && !ConstructionDryRunResult.bApplied);
	if (!ConstructionDryRunResult.bSucceeded || ConstructionDryRunResult.bApplied)
	{
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(ReloadedBlueprint);
	TestTrue(TEXT("Golden-asset Blueprint should compile cleanly after reload verification."), ReloadedBlueprint->IsUpToDate());
	if (!ReloadedBlueprint->IsUpToDate())
	{
		return false;
	}

	const FString ActualSnapshot = NormalizeLineEndings(BuildGoldenAssetSnapshot(
		ReloadedBlueprint,
		EventDryRunResult,
		ConstructionDryRunResult,
		Fixture));
	const FString GoldenSnapshotPath = GetGoldenSnapshotPath();

	FString ExpectedSnapshot;
	if (!FFileHelper::LoadFileToString(ExpectedSnapshot, *GoldenSnapshotPath))
	{
		WriteActualSnapshot(ActualSnapshot);
		AddError(FString::Printf(
			TEXT("Golden asset snapshot file is missing at '%s'. Wrote the current snapshot to '%s'."),
			*GoldenSnapshotPath,
			*GetActualSnapshotPath()));
		return false;
	}

	ExpectedSnapshot = NormalizeLineEndings(ExpectedSnapshot);
	if (ExpectedSnapshot != ActualSnapshot)
	{
		WriteActualSnapshot(ActualSnapshot);
		AddError(FString::Printf(
			TEXT("Golden asset snapshot mismatch. Expected '%s'; wrote actual snapshot to '%s'."),
			*GoldenSnapshotPath,
			*GetActualSnapshotPath()));
		return false;
	}

	return true;
}
