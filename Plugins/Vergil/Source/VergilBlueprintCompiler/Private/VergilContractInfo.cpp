#include "VergilContractInfo.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "VergilCommandTypes.h"
#include "VergilCompilerTypes.h"
#include "VergilVersion.h"

namespace
{
	inline constexpr TCHAR SupportedContractManifestFormatName[] = TEXT("Vergil.ContractManifest");
	inline constexpr int32 SupportedContractManifestVersion = 1;

	TArray<FName> BuildSupportedDocumentFields()
	{
		return {
			TEXT("SchemaVersion"),
			TEXT("BlueprintPath"),
			TEXT("Metadata"),
			TEXT("Variables"),
			TEXT("Functions"),
			TEXT("Dispatchers"),
			TEXT("Macros"),
			TEXT("Components"),
			TEXT("Interfaces"),
			TEXT("ClassDefaults"),
			TEXT("ConstructionScriptNodes"),
			TEXT("ConstructionScriptEdges"),
			TEXT("Nodes"),
			TEXT("Edges"),
			TEXT("Tags")
		};
	}

	TArray<FName> BuildSupportedTargetGraphs()
	{
		return {
			TEXT("EventGraph"),
			TEXT("UserConstructionScript")
		};
	}

	TArray<FName> BuildSupportedBlueprintMetadataKeys()
	{
		return {
			TEXT("BlueprintDisplayName"),
			TEXT("BlueprintDescription"),
			TEXT("BlueprintCategory"),
			TEXT("HideCategories")
		};
	}

	TArray<FString> BuildSupportedTypeCategories()
	{
		return {
			TEXT("bool"),
			TEXT("int"),
			TEXT("float"),
			TEXT("double"),
			TEXT("string"),
			TEXT("name"),
			TEXT("text"),
			TEXT("enum"),
			TEXT("object"),
			TEXT("class"),
			TEXT("struct")
		};
	}

	TArray<FString> BuildSupportedContainerTypes()
	{
		return {
			TEXT("None"),
			TEXT("Array"),
			TEXT("Set"),
			TEXT("Map")
		};
	}

	FName GetCommandTypeName(const EVergilCommandType CommandType)
	{
		const UEnum* const CommandTypeEnum = StaticEnum<EVergilCommandType>();
		check(CommandTypeEnum != nullptr);
		return FName(*CommandTypeEnum->GetNameStringByValue(static_cast<int64>(CommandType)));
	}

	FString GetDescriptorMatchKindName(const EVergilDescriptorMatchKind MatchKind)
	{
		const UEnum* const MatchKindEnum = StaticEnum<EVergilDescriptorMatchKind>();
		check(MatchKindEnum != nullptr);
		return MatchKindEnum->GetNameStringByValue(static_cast<int64>(MatchKind));
	}

	FString GetNodeSupportCoverageName(const EVergilNodeSupportCoverage Coverage)
	{
		const UEnum* const CoverageEnum = StaticEnum<EVergilNodeSupportCoverage>();
		check(CoverageEnum != nullptr);
		return CoverageEnum->GetNameStringByValue(static_cast<int64>(Coverage));
	}

	FString GetNodeSupportHandlingName(const EVergilNodeSupportHandling Handling)
	{
		const UEnum* const HandlingEnum = StaticEnum<EVergilNodeSupportHandling>();
		check(HandlingEnum != nullptr);
		return HandlingEnum->GetNameStringByValue(static_cast<int64>(Handling));
	}

	TArray<FName> BuildSupportedCommandTypes()
	{
		return {
			GetCommandTypeName(EVergilCommandType::EnsureDispatcher),
			GetCommandTypeName(EVergilCommandType::AddDispatcherParameter),
			GetCommandTypeName(EVergilCommandType::SetBlueprintMetadata),
			GetCommandTypeName(EVergilCommandType::EnsureVariable),
			GetCommandTypeName(EVergilCommandType::SetVariableMetadata),
			GetCommandTypeName(EVergilCommandType::SetVariableDefault),
			GetCommandTypeName(EVergilCommandType::EnsureFunctionGraph),
			GetCommandTypeName(EVergilCommandType::EnsureMacroGraph),
			GetCommandTypeName(EVergilCommandType::EnsureComponent),
			GetCommandTypeName(EVergilCommandType::AttachComponent),
			GetCommandTypeName(EVergilCommandType::SetComponentProperty),
			GetCommandTypeName(EVergilCommandType::EnsureInterface),
			GetCommandTypeName(EVergilCommandType::SetClassDefault),
			GetCommandTypeName(EVergilCommandType::EnsureGraph),
			GetCommandTypeName(EVergilCommandType::AddNode),
			GetCommandTypeName(EVergilCommandType::SetNodeMetadata),
			GetCommandTypeName(EVergilCommandType::ConnectPins),
			GetCommandTypeName(EVergilCommandType::RemoveNode),
			GetCommandTypeName(EVergilCommandType::RenameMember),
			GetCommandTypeName(EVergilCommandType::MoveNode),
			GetCommandTypeName(EVergilCommandType::FinalizeNode),
			GetCommandTypeName(EVergilCommandType::CompileBlueprint)
		};
	}

	FVergilSupportedDescriptorContract MakeDescriptorContract(
		const FString& DescriptorContract,
		const EVergilDescriptorMatchKind MatchKind,
		const FString& ExpectedNodeKind,
		const TArray<FName>& SupportedTargetGraphs,
		const TArray<FName>& RequiredMetadataKeys,
		const FString& Notes)
	{
		FVergilSupportedDescriptorContract Contract;
		Contract.DescriptorContract = DescriptorContract;
		Contract.MatchKind = MatchKind;
		Contract.ExpectedNodeKind = ExpectedNodeKind;
		Contract.SupportedTargetGraphs = SupportedTargetGraphs;
		Contract.RequiredMetadataKeys = RequiredMetadataKeys;
		Contract.Notes = Notes;
		return Contract;
	}

	FVergilNodeSupportMatrixEntry MakeSupportMatrixEntry(
		const FString& Family,
		const EVergilNodeSupportCoverage Coverage,
		const EVergilNodeSupportHandling Handling,
		const TArray<FString>& DescriptorCoverage,
		const FString& Notes)
	{
		FVergilNodeSupportMatrixEntry Entry;
		Entry.Family = Family;
		Entry.Coverage = Coverage;
		Entry.Handling = Handling;
		Entry.DescriptorCoverage = DescriptorCoverage;
		Entry.Notes = Notes;
		return Entry;
	}

	TArray<FVergilSupportedDescriptorContract> BuildSupportedDescriptorContracts()
	{
		const TArray<FName> SupportedGraphs = BuildSupportedTargetGraphs();
		const TArray<FName> EventGraphOnly = { TEXT("EventGraph") };

		return {
			MakeDescriptorContract(
				TEXT("any non-empty descriptor"),
				EVergilDescriptorMatchKind::NodeKind,
				TEXT("Comment"),
				SupportedGraphs,
				{},
				TEXT("Comment nodes are matched by kind, not a fixed descriptor string. CommentText or Title sets the text. CommentWidth, CommentHeight, FontSize, Color, CommentColor, ShowBubbleWhenZoomed, ColorBubble, and MoveMode are supported. When those style keys are omitted, the dedicated comment post-pass fills deterministic defaults from FVergilCompileRequest.CommentGeneration using UE_5.7 comment-node defaults as the baseline.")),
			MakeDescriptorContract(
				TEXT("K2.Event.<FunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("Event"),
				SupportedGraphs,
				{},
				TEXT("Binds a standard event by function-name suffix. When targeting UserConstructionScript, the only supported event descriptor is K2.Event.UserConstructionScript.")),
			MakeDescriptorContract(
				TEXT("K2.CustomEvent.<EventName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Creates a custom event named by descriptor suffix. Optional DelegatePropertyName and DelegateOwnerClassPath metadata is resolved during compilation when authored.")),
			MakeDescriptorContract(
				TEXT("K2.Call.<FunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("Call"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains function resolution. When omitted, the current scaffold resolves document-authored functions first, then existing Blueprint-local functions, then inherited/native functions. Self-owned resolutions keep an empty owner path; inherited/native resolutions normalize the owner path into the planned command. Headless UE_5.7 coverage now explicitly re-verifies timer-by-function-name helpers plus handle-based timer pause/query helpers through this generic call path.")),
			MakeDescriptorContract(
				TEXT("K2.InterfaceCall.<FunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("Call"),
				SupportedGraphs,
				{ TEXT("InterfaceClassPath") },
				TEXT("InterfaceClassPath must resolve to a UInterface-derived class and is normalized during symbol/type resolution. Under UE_5.7 this lowers to UK2Node_CallFunction bound against the interface function owner, preserving the interface-typed self pin instead of the generic object-target call surface.")),
			MakeDescriptorContract(
				TEXT("K2.InterfaceMessage.<FunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("Call"),
				SupportedGraphs,
				{ TEXT("InterfaceClassPath") },
				TEXT("InterfaceClassPath must resolve to a UInterface-derived class and is normalized during symbol/type resolution. Under UE_5.7 this lowers to UK2Node_Message bound against the interface function owner, exposing the object-typed self/Target pin and the engine's no-op-if-unimplemented message semantics.")),
			MakeDescriptorContract(
				TEXT("K2.ClassCast"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("TargetClassPath") },
				TEXT("TargetClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_ClassDynamicCast and preserves pure versus impure class-cast parity from the authored exec-pin surface.")),
			MakeDescriptorContract(
				TEXT("K2.GetClassDefaults"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ClassPath") },
				TEXT("ClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_GetClassDefaults with property-output pins sourced deterministically from the metadata-selected class; the dynamic Class pin is intentionally not part of the authored contract.")),
			MakeDescriptorContract(
				TEXT("K2.AsyncAction.<FactoryFunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				EventGraphOnly,
				{ TEXT("FactoryClassPath") },
				TEXT("FactoryClassPath must resolve to the owner class of a static BlueprintInternalUseOnly factory function, and the descriptor suffix names that function. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_AsyncAction for generic UBlueprintAsyncActionBase factories that do not advertise HasDedicatedAsyncNode. The authored deterministic surface includes the visible factory input pins, Then/delegate exec outputs, and delegate payload outputs; hidden pins such as WorldContextObject remain engine-driven and are not part of the authored contract.")),
			MakeDescriptorContract(
				TEXT("K2.AIMoveTo"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				EventGraphOnly,
				{},
				TEXT("Under UE_5.7 this resolves through the dedicated specialized async-task handler and then instantiates UK2Node_AIMoveTo through the generic node-spawner path, resolving UAIBlueprintHelperLibrary::CreateMoveToProxyObject instead of the generic async-action path. The authored deterministic surface includes the visible factory input pins plus Then, delegate exec outputs, and delegate payload outputs such as MovementResult; hidden pins such as WorldContextObject remain engine-driven and are not part of the authored contract.")),
			MakeDescriptorContract(
				TEXT("K2.PlayMontage"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				EventGraphOnly,
				{},
				TEXT("Under UE_5.7 this resolves through the dedicated specialized async-task handler and then instantiates UK2Node_PlayMontage through the generic node-spawner path, resolving UPlayMontageCallbackProxy::CreateProxyObjectForPlayMontage instead of the generic async-action path. The authored deterministic surface includes the visible factory input pins plus Then, delegate exec outputs, and delegate payload outputs such as NotifyName.")),
			MakeDescriptorContract(
				TEXT("K2.LoadAsset"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				EventGraphOnly,
				{ TEXT("AssetClassPath") },
				TEXT("AssetClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_LoadAsset with the latent Execute, Then, Completed, Asset, and Object surface. The Asset input pin is conformed to the metadata-selected soft-object type, while Object intentionally stays on the node's native UObject result family so the engine expansion remains compile-safe.")),
			MakeDescriptorContract(
				TEXT("K2.LoadAssetClass"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				EventGraphOnly,
				{ TEXT("AssetClassPath") },
				TEXT("AssetClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_LoadAssetClass with the latent Execute, Then, Completed, AssetClass, and Class surface, and the input/output pins are conformed to the metadata-selected class type.")),
			MakeDescriptorContract(
				TEXT("K2.LoadAssets"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				EventGraphOnly,
				{ TEXT("AssetClassPath") },
				TEXT("AssetClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_LoadAssets with the latent Execute, Then, Completed, Assets, and Objects surface. The Assets input array element type is conformed to the metadata-selected soft-object type, while Objects intentionally stays on the node's native UObject array-result family so the engine expansion remains compile-safe.")),
			MakeDescriptorContract(
				TEXT("K2.ConvertAsset"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_ConvertAsset with the deterministic wildcard-free supported surface Input and Output. Type promotion comes from connected hard or soft object/class references rather than authored metadata.")),
			MakeDescriptorContract(
				TEXT("K2.VarGet.<VariableName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("VariableGet"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members. UE_5.7 getter variants now support pure reads, bool branch getters, and validated object/class/soft-reference getters. Impure getter shapes must use Execute, Then, and Else exec pins.")),
			MakeDescriptorContract(
				TEXT("K2.VarSet.<VariableName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("VariableSet"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members.")),
			MakeDescriptorContract(
				TEXT("K2.Self"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Creates a self node through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.Branch"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Standard branch node instantiated through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.Sequence"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Output exec pins named like Then_0, Then_1, and so on determine sequence width, and the backing UK2Node_ExecutionSequence is instantiated through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.ForLoop"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros ForLoop and are validated before planning.")),
			MakeDescriptorContract(
				TEXT("K2.ForLoopWithBreak"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros ForLoopWithBreak and are validated before planning.")),
			MakeDescriptorContract(
				TEXT("K2.DoOnce"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros DoOnce and are validated before planning. The bool input follows the engine pin name 'Start Closed'.")),
			MakeDescriptorContract(
				TEXT("K2.FlipFlop"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros FlipFlop and are validated before planning. The input exec pin maps to the engine macro's unnamed entry exec pin, while outputs remain A, B, and IsA.")),
			MakeDescriptorContract(
				TEXT("K2.Gate"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros Gate and are validated before planning. The primary exec input follows the engine name 'Enter', and authored StartClosed pins normalize to the engine bool input 'bStartClosed'.")),
			MakeDescriptorContract(
				TEXT("K2.WhileLoop"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros WhileLoop and are validated before planning.")),
			MakeDescriptorContract(
				TEXT("K2.AddComponentByClass"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ComponentClassPath") },
				TEXT("ComponentClassPath must resolve to a UActorComponent-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_AddComponentByClass with the fixed deterministic surface: Self, ReturnValue, scene-component-only bManualAttachment and RelativeTransform pins, and class-specific ExposeOnSpawn property pins. The dynamic Class pin is intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.GetComponentByClass"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ComponentClassPath") },
				TEXT("ComponentClassPath must resolve to a UActorComponent-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers to AActor::GetComponentByClass with the return pin conformed from the metadata-driven ComponentClass input. The dynamic ComponentClass pin is intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.GetComponentsByClass"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ComponentClassPath") },
				TEXT("ComponentClassPath must resolve to a UActorComponent-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers to AActor::K2_GetComponentsByClass with the array element type conformed from the metadata-driven ComponentClass input. The dynamic ComponentClass pin is intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.FindComponentByTag"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ComponentClassPath") },
				TEXT("ComponentClassPath must resolve to a UActorComponent-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers to AActor::FindComponentByTag with a typed Tag input and the return pin conformed from the metadata-driven ComponentClass input. The dynamic ComponentClass pin is intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.GetComponentsByTag"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ComponentClassPath") },
				TEXT("ComponentClassPath must resolve to a UActorComponent-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers to AActor::GetComponentsByTag with a typed Tag input and the array element type conformed from the metadata-driven ComponentClass input. The dynamic ComponentClass pin is intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.SpawnActor"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ActorClassPath") },
				TEXT("ActorClassPath must resolve to an AActor-derived class during type resolution and is normalized before planning. Under UE_5.7 this lowers through the generic node-spawner path to UK2Node_SpawnActorFromClass with the fixed SpawnActorFromClass surface: SpawnTransform, CollisionHandlingOverride, TransformScaleMethod, Owner, ReturnValue, and class-specific ExposeOnSpawn property pins. SpawnTransform must be authored and connected because the UE_5.7 node expands through by-reference transform calls. The dynamic Class and WorldContextObject pins are intentionally not part of the authored deterministic contract.")),
			MakeDescriptorContract(
				TEXT("K2.Delay"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Lowers to UKismetSystemLibrary::Delay.")),
			MakeDescriptorContract(
				TEXT("K2.Cast"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("TargetClassPath") },
				TEXT("TargetClassPath must resolve to a class during type resolution and is normalized before planning. Under UE_5.7 the backing UK2Node_DynamicCast is instantiated through the generic node-spawner path, preserving pure versus impure cast parity from the authored exec-pin surface.")),
			MakeDescriptorContract(
				TEXT("K2.Reroute"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Creates a knot node through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.Select"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("IndexPinCategory"), TEXT("ValuePinCategory") },
				TEXT("IndexPinCategory currently supports only bool, int, or enum under UE 5.7. Optional IndexObjectPath, ValueObjectPath, and NumOptions metadata refine the wildcard shape. Enum index selects use IndexObjectPath for the enum, explicit type metadata is resolved before planning, and the backing UK2Node_Select is instantiated through the generic node-spawner path. Unsupported index/value connections now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchInt"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Planned exec output pins define the case labels and must parse as integers. The backing UK2Node_SwitchInteger is instantiated through the generic node-spawner path, and unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchString"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Planned exec output pins define the case labels. Optional CaseSensitive metadata configures comparison behavior, the backing UK2Node_SwitchString is instantiated through the generic node-spawner path, and unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchEnum"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("EnumPath") },
				TEXT("EnumPath must resolve to a UEnum during type resolution and is normalized before planning. The backing UK2Node_SwitchEnum is instantiated through the generic node-spawner path, and unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.FormatText"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("FormatPattern") },
				TEXT("Creates a format-text node through the generic node-spawner path and reconstructs argument pins from the format pattern.")),
			MakeDescriptorContract(
				TEXT("K2.MakeStruct"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("StructPath") },
				TEXT("StructPath must resolve to a UScriptStruct during type resolution and is normalized before planning.")),
			MakeDescriptorContract(
				TEXT("K2.BreakStruct"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("StructPath") },
				TEXT("StructPath must resolve to a UScriptStruct during type resolution and is normalized before planning.")),
			MakeDescriptorContract(
				TEXT("K2.MakeArray"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ValuePinCategory") },
				TEXT("Optional ValueObjectPath and NumInputs metadata refine the container shape. NumInputs must be at least 1, explicit value-type metadata resolves before planning, and the backing UK2Node_MakeArray is instantiated through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.MakeSet"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ValuePinCategory") },
				TEXT("Optional ValueObjectPath and NumInputs metadata refine the container shape. NumInputs must be at least 1, explicit value-type metadata resolves before planning, and the backing UK2Node_MakeSet is instantiated through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.MakeMap"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("KeyPinCategory"), TEXT("ValuePinCategory") },
				TEXT("Optional KeyObjectPath, ValueObjectPath, and NumPairs metadata refine the map shape. NumPairs must be at least 1, explicit key/value type metadata resolves before planning, and the backing UK2Node_MakeMap is instantiated through the generic node-spawner path.")),
			MakeDescriptorContract(
				TEXT("K2.BindDelegate.<PropertyName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains delegate-property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties.")),
			MakeDescriptorContract(
				TEXT("K2.RemoveDelegate.<PropertyName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains delegate-property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties.")),
			MakeDescriptorContract(
				TEXT("K2.ClearDelegate.<PropertyName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains delegate-property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties.")),
			MakeDescriptorContract(
				TEXT("K2.CallDelegate.<PropertyName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional OwnerClassPath constrains delegate-property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties.")),
			MakeDescriptorContract(
				TEXT("K2.CreateDelegate.<FunctionName>"),
				EVergilDescriptorMatchKind::Prefix,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Creates a delegate node and uses a finalize pass to assign the selected function after initial graph compilation. The selected function resolves during compilation against target-graph custom events, document-authored functions, and existing Blueprint or parent-class functions."))
		};
	}

	TArray<FVergilNodeSupportMatrixEntry> BuildNodeSupportMatrix()
	{
		return {
			MakeSupportMatrixEntry(
				TEXT("Event and custom-event entrypoints"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::DirectLowering,
				{ TEXT("K2.Event.<FunctionName>"), TEXT("K2.CustomEvent.<EventName>") },
				TEXT("Standard events and authored custom events use explicit event-node lowering instead of the generic UK2Node spawner path.")),
			MakeSupportMatrixEntry(
				TEXT("Call, message, and component-lookup nodes"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::DirectLowering,
				{
					TEXT("K2.Call.<FunctionName>"),
					TEXT("K2.InterfaceCall.<FunctionName>"),
					TEXT("K2.InterfaceMessage.<FunctionName>"),
					TEXT("K2.Delay"),
					TEXT("K2.GetComponentByClass"),
					TEXT("K2.GetComponentsByClass"),
					TEXT("K2.FindComponentByTag"),
					TEXT("K2.GetComponentsByTag")
				},
				TEXT("These families lower through explicit call/message handlers that resolve the target function surface deterministically instead of spawning a generic UK2Node class.")),
			MakeSupportMatrixEntry(
				TEXT("Variable, struct, and delegate helpers"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::DirectLowering,
				{
					TEXT("K2.VarGet.<VariableName>"),
					TEXT("K2.VarSet.<VariableName>"),
					TEXT("K2.MakeStruct"),
					TEXT("K2.BreakStruct"),
					TEXT("K2.BindDelegate.<PropertyName>"),
					TEXT("K2.RemoveDelegate.<PropertyName>"),
					TEXT("K2.ClearDelegate.<PropertyName>"),
					TEXT("K2.CallDelegate.<PropertyName>"),
					TEXT("K2.CreateDelegate.<FunctionName>")
				},
				TEXT("These families keep their dedicated deterministic handlers because they depend on variable-node variants, struct pin synthesis, or finalize-time delegate binding rather than plain UK2Node spawning.")),
			MakeSupportMatrixEntry(
				TEXT("Standard macro families"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::DirectLowering,
				{
					TEXT("K2.ForLoop"),
					TEXT("K2.ForLoopWithBreak"),
					TEXT("K2.DoOnce"),
					TEXT("K2.FlipFlop"),
					TEXT("K2.Gate"),
					TEXT("K2.WhileLoop")
				},
				TEXT("Flow-control macro families resolve against UE_5.7 StandardMacros and materialize through the existing macro-instance lowering path.")),
			MakeSupportMatrixEntry(
				TEXT("Generic UK2Node spawner families"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::GenericNodeSpawner,
				{
					TEXT("K2.Self"),
					TEXT("K2.Branch"),
					TEXT("K2.Sequence"),
					TEXT("K2.Reroute"),
					TEXT("K2.Select"),
					TEXT("K2.SwitchInt"),
					TEXT("K2.SwitchString"),
					TEXT("K2.SwitchEnum"),
					TEXT("K2.FormatText"),
					TEXT("K2.MakeArray"),
					TEXT("K2.MakeSet"),
					TEXT("K2.MakeMap"),
					TEXT("K2.SpawnActor"),
					TEXT("K2.AddComponentByClass"),
					TEXT("K2.ClassCast"),
					TEXT("K2.Cast"),
					TEXT("K2.GetClassDefaults"),
					TEXT("K2.LoadAsset"),
					TEXT("K2.LoadAssetClass"),
					TEXT("K2.LoadAssets"),
					TEXT("K2.ConvertAsset"),
					TEXT("K2.AsyncAction.<FactoryFunctionName>")
				},
				TEXT("These supported descriptor contracts now validate and instantiate their backing UK2Node classes through the shared generic spawner registry, with per-family setup for wildcard, class, asset, spawn, async, and expose-on-spawn surfaces.")),
			MakeSupportMatrixEntry(
				TEXT("Specialized async-task families"),
				EVergilNodeSupportCoverage::Supported,
				EVergilNodeSupportHandling::SpecializedHandler,
				{ TEXT("K2.AIMoveTo"), TEXT("K2.PlayMontage") },
				TEXT("These dedicated async-node families still require explicit specialized handlers to resolve the engine-owned factory function and delegate surface before the final UK2Node class is instantiated.")),
			MakeSupportMatrixEntry(
				TEXT("Dedicated async nodes without a specialized handler"),
				EVergilNodeSupportCoverage::Unsupported,
				EVergilNodeSupportHandling::Unsupported,
				{ TEXT("K2.AsyncAction.<FactoryFunctionName> where the resolved factory class advertises HasDedicatedAsyncNode") },
				TEXT("Factories that advertise HasDedicatedAsyncNode are intentionally rejected from the generic async-action path until they have an explicit specialized handler.")),
			MakeSupportMatrixEntry(
				TEXT("Arbitrary unsupported descriptor-backed UK2Node families"),
				EVergilNodeSupportCoverage::Unsupported,
				EVergilNodeSupportHandling::Unsupported,
				{ TEXT("Descriptors outside the supported-contract table") },
				TEXT("The generic fallback planner is not a blanket support promise; unsupported descriptor-backed UK2Node families still fail explicitly until they are added to the manifest and a deterministic handler path.")),
		};
	}

	FVergilNodeSupportMatrixSummary BuildNodeSupportMatrixSummary(const TArray<FVergilNodeSupportMatrixEntry>& Entries)
	{
		FVergilNodeSupportMatrixSummary Summary;
		Summary.TotalFamilies = Entries.Num();

		for (const FVergilNodeSupportMatrixEntry& Entry : Entries)
		{
			if (Entry.Coverage == EVergilNodeSupportCoverage::Supported)
			{
				++Summary.SupportedFamilies;
			}
			else
			{
				++Summary.UnsupportedFamilies;
			}

			switch (Entry.Handling)
			{
			case EVergilNodeSupportHandling::GenericNodeSpawner:
				++Summary.GenericNodeSpawnerFamilies;
				break;
			case EVergilNodeSupportHandling::SpecializedHandler:
				++Summary.SpecializedHandlerFamilies;
				break;
			case EVergilNodeSupportHandling::DirectLowering:
				++Summary.DirectLoweringFamilies;
				break;
			case EVergilNodeSupportHandling::Unsupported:
				break;
			default:
				break;
			}
		}

		return Summary;
	}

	FVergilSupportedContractManifest BuildSupportedContractManifest()
	{
		FVergilSupportedContractManifest Manifest;
		Manifest.ManifestVersion = SupportedContractManifestVersion;
		Manifest.PluginDescriptorVersion = Vergil::PluginDescriptorVersion;
		Manifest.PluginSemanticVersion = Vergil::GetSemanticVersionString();
		Manifest.SchemaVersion = Vergil::SchemaVersion;
		Manifest.CommandPlanFormat = Vergil::GetCommandPlanFormatName();
		Manifest.CommandPlanFormatVersion = Vergil::GetCommandPlanFormatVersion();
		Manifest.SupportedSchemaMigrationPaths = Vergil::GetSupportedSchemaMigrationPaths();
		Manifest.SupportedDocumentFields = BuildSupportedDocumentFields();
		Manifest.SupportedTargetGraphs = BuildSupportedTargetGraphs();
		Manifest.SupportedBlueprintMetadataKeys = BuildSupportedBlueprintMetadataKeys();
		Manifest.SupportedTypeCategories = BuildSupportedTypeCategories();
		Manifest.SupportedContainerTypes = BuildSupportedContainerTypes();
		Manifest.SupportedCommandTypes = BuildSupportedCommandTypes();
		Manifest.NodeSupportMatrix = BuildNodeSupportMatrix();
		Manifest.NodeSupportMatrixSummary = BuildNodeSupportMatrixSummary(Manifest.NodeSupportMatrix);
		Manifest.SupportedDescriptors = BuildSupportedDescriptorContracts();
		return Manifest;
	}

	FString JoinNameArray(const TArray<FName>& Values)
	{
		TArray<FString> Tokens;
		Tokens.Reserve(Values.Num());
		for (const FName Value : Values)
		{
			Tokens.Add(Value.ToString());
		}
		return FString::Join(Tokens, TEXT(", "));
	}

	FString JoinStringArray(const TArray<FString>& Values)
	{
		return FString::Join(Values, TEXT(", "));
	}

	template <typename PrintPolicy>
	void WriteNameArray(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const TArray<FName>& Values)
	{
		Writer.WriteArrayStart(FieldName);
		for (const FName Value : Values)
		{
			Writer.WriteValue(Value.ToString());
		}
		Writer.WriteArrayEnd();
	}

	template <typename PrintPolicy>
	void WriteStringArray(TJsonWriter<TCHAR, PrintPolicy>& Writer, const TCHAR* FieldName, const TArray<FString>& Values)
	{
		Writer.WriteArrayStart(FieldName);
		for (const FString& Value : Values)
		{
			Writer.WriteValue(Value);
		}
		Writer.WriteArrayEnd();
	}

	template <typename PrintPolicy>
	void WriteSupportedDescriptorContractJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilSupportedDescriptorContract& Contract)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("descriptorContract"), Contract.DescriptorContract);
		Writer.WriteValue(TEXT("matchKind"), GetDescriptorMatchKindName(Contract.MatchKind));
		Writer.WriteValue(TEXT("expectedNodeKind"), Contract.ExpectedNodeKind);
		WriteNameArray(Writer, TEXT("supportedTargetGraphs"), Contract.SupportedTargetGraphs);
		WriteNameArray(Writer, TEXT("requiredMetadataKeys"), Contract.RequiredMetadataKeys);
		Writer.WriteValue(TEXT("notes"), Contract.Notes);
		Writer.WriteObjectEnd();
	}

	template <typename PrintPolicy>
	void WriteNodeSupportMatrixEntryJson(TJsonWriter<TCHAR, PrintPolicy>& Writer, const FVergilNodeSupportMatrixEntry& Entry)
	{
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("family"), Entry.Family);
		Writer.WriteValue(TEXT("coverage"), GetNodeSupportCoverageName(Entry.Coverage));
		Writer.WriteValue(TEXT("handling"), GetNodeSupportHandlingName(Entry.Handling));
		WriteStringArray(Writer, TEXT("descriptorCoverage"), Entry.DescriptorCoverage);
		Writer.WriteValue(TEXT("notes"), Entry.Notes);
		Writer.WriteObjectEnd();
	}

	FString EscapeMarkdownTableCell(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\r\n"), TEXT(" "));
		Escaped.ReplaceInline(TEXT("\n"), TEXT(" "));
		Escaped.ReplaceInline(TEXT("\r"), TEXT(" "));
		Escaped.ReplaceInline(TEXT("|"), TEXT("\\|"));
		return Escaped;
	}

	FString FormatMarkdownInlineCode(const FString& Value)
	{
		return FString::Printf(TEXT("`%s`"), *EscapeMarkdownTableCell(Value));
	}

	FString FormatMarkdownNameArray(const TArray<FName>& Values)
	{
		if (Values.IsEmpty())
		{
			return TEXT("`none`");
		}

		TArray<FString> Tokens;
		Tokens.Reserve(Values.Num());
		for (const FName Value : Values)
		{
			Tokens.Add(FormatMarkdownInlineCode(Value.ToString()));
		}

		return FString::Join(Tokens, TEXT(", "));
	}

	FString FormatMarkdownStringArray(const TArray<FString>& Values)
	{
		if (Values.IsEmpty())
		{
			return TEXT("`none`");
		}

		TArray<FString> Tokens;
		Tokens.Reserve(Values.Num());
		for (const FString& Value : Values)
		{
			Tokens.Add(FormatMarkdownInlineCode(Value));
		}

		return FString::Join(Tokens, TEXT(", "));
	}
}

FString FVergilSupportedDescriptorContract::ToDisplayString() const
{
	TArray<FString> Parts;
	Parts.Add(FString::Printf(TEXT("descriptor=%s"), *DescriptorContract));
	Parts.Add(FString::Printf(TEXT("match=%s"), *GetDescriptorMatchKindName(MatchKind)));
	Parts.Add(FString::Printf(TEXT("expectedKind=%s"), ExpectedNodeKind.IsEmpty() ? TEXT("any") : *ExpectedNodeKind));

	if (SupportedTargetGraphs.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("graphs=[%s]"), *JoinNameArray(SupportedTargetGraphs)));
	}

	if (RequiredMetadataKeys.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("requiredMetadata=[%s]"), *JoinNameArray(RequiredMetadataKeys)));
	}

	if (!Notes.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("notes=%s"), *Notes));
	}

	return FString::Join(Parts, TEXT(" "));
}

FString FVergilNodeSupportMatrixEntry::ToDisplayString() const
{
	TArray<FString> Parts;
	Parts.Add(FString::Printf(TEXT("family=%s"), *Family));
	Parts.Add(FString::Printf(TEXT("coverage=%s"), *GetNodeSupportCoverageName(Coverage)));
	Parts.Add(FString::Printf(TEXT("handling=%s"), *GetNodeSupportHandlingName(Handling)));

	if (DescriptorCoverage.Num() > 0)
	{
		Parts.Add(FString::Printf(TEXT("descriptors=[%s]"), *JoinStringArray(DescriptorCoverage)));
	}

	if (!Notes.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("notes=%s"), *Notes));
	}

	return FString::Join(Parts, TEXT(" "));
}

FString FVergilNodeSupportMatrixSummary::ToDisplayString() const
{
	return FString::Printf(
		TEXT("families=%d supported=%d unsupported=%d generic=%d specialized=%d direct=%d"),
		TotalFamilies,
		SupportedFamilies,
		UnsupportedFamilies,
		GenericNodeSpawnerFamilies,
		SpecializedHandlerFamilies,
		DirectLoweringFamilies);
}

FString FVergilSupportedContractManifest::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s version=%d plugin=%s descriptorVersion=%d schema=%d commandPlanFormat=%s:%d schemaMigrations=%d documentFields=%d targetGraphs=%d metadataKeys=%d typeCategories=%d containerTypes=%d commandTypes=%d matrixFamilies=%d descriptors=%d"),
		SupportedContractManifestFormatName,
		ManifestVersion,
		*PluginSemanticVersion,
		PluginDescriptorVersion,
		SchemaVersion,
		*CommandPlanFormat,
		CommandPlanFormatVersion,
		SupportedSchemaMigrationPaths.Num(),
		SupportedDocumentFields.Num(),
		SupportedTargetGraphs.Num(),
		SupportedBlueprintMetadataKeys.Num(),
		SupportedTypeCategories.Num(),
		SupportedContainerTypes.Num(),
		SupportedCommandTypes.Num(),
		NodeSupportMatrix.Num(),
		SupportedDescriptors.Num());
}

const FVergilSupportedContractManifest& Vergil::GetSupportedContractManifest()
{
	static const FVergilSupportedContractManifest Manifest = BuildSupportedContractManifest();
	return Manifest;
}

FString Vergil::DescribeSupportedContractManifest()
{
	const FVergilSupportedContractManifest& Manifest = GetSupportedContractManifest();

	TArray<FString> Lines;
	Lines.Reserve(8 + Manifest.NodeSupportMatrix.Num() + Manifest.SupportedDescriptors.Num());
	Lines.Add(Manifest.ToDisplayString());
	Lines.Add(FString::Printf(TEXT("pluginSemanticVersion: %s"), *Manifest.PluginSemanticVersion));
	Lines.Add(FString::Printf(TEXT("pluginDescriptorVersion: %d"), Manifest.PluginDescriptorVersion));
	Lines.Add(FString::Printf(TEXT("schemaMigrationPaths: %s"), *JoinStringArray(Manifest.SupportedSchemaMigrationPaths)));
	Lines.Add(FString::Printf(TEXT("documentFields: %s"), *JoinNameArray(Manifest.SupportedDocumentFields)));
	Lines.Add(FString::Printf(TEXT("targetGraphs: %s"), *JoinNameArray(Manifest.SupportedTargetGraphs)));
	Lines.Add(FString::Printf(TEXT("blueprintMetadataKeys: %s"), *JoinNameArray(Manifest.SupportedBlueprintMetadataKeys)));
	Lines.Add(FString::Printf(TEXT("typeCategories: %s"), *JoinStringArray(Manifest.SupportedTypeCategories)));
	Lines.Add(FString::Printf(TEXT("containerTypes: %s"), *JoinStringArray(Manifest.SupportedContainerTypes)));
	Lines.Add(FString::Printf(TEXT("commandTypes: %s"), *JoinNameArray(Manifest.SupportedCommandTypes)));
	Lines.Add(FString::Printf(TEXT("nodeSupportMatrixSummary: %s"), *Manifest.NodeSupportMatrixSummary.ToDisplayString()));
	Lines.Add(TEXT("nodeSupportMatrix:"));
	for (const FVergilNodeSupportMatrixEntry& Entry : Manifest.NodeSupportMatrix)
	{
		Lines.Add(FString::Printf(TEXT("- %s"), *Entry.ToDisplayString()));
	}
	Lines.Add(TEXT("descriptors:"));
	for (const FVergilSupportedDescriptorContract& Contract : Manifest.SupportedDescriptors)
	{
		Lines.Add(FString::Printf(TEXT("- %s"), *Contract.ToDisplayString()));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::SerializeSupportedContractManifest(const bool bPrettyPrint)
{
	const FVergilSupportedContractManifest& Manifest = GetSupportedContractManifest();

	FString SerializedManifest;
	if (bPrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&SerializedManifest);
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("format"), SupportedContractManifestFormatName);
		Writer->WriteValue(TEXT("version"), Manifest.ManifestVersion);
		Writer->WriteValue(TEXT("pluginDescriptorVersion"), Manifest.PluginDescriptorVersion);
		Writer->WriteValue(TEXT("pluginSemanticVersion"), Manifest.PluginSemanticVersion);
		Writer->WriteValue(TEXT("schemaVersion"), Manifest.SchemaVersion);
		Writer->WriteValue(TEXT("commandPlanFormat"), Manifest.CommandPlanFormat);
		Writer->WriteValue(TEXT("commandPlanFormatVersion"), Manifest.CommandPlanFormatVersion);
		WriteStringArray(*Writer, TEXT("supportedSchemaMigrationPaths"), Manifest.SupportedSchemaMigrationPaths);
		WriteNameArray(*Writer, TEXT("supportedDocumentFields"), Manifest.SupportedDocumentFields);
		WriteNameArray(*Writer, TEXT("supportedTargetGraphs"), Manifest.SupportedTargetGraphs);
		WriteNameArray(*Writer, TEXT("supportedBlueprintMetadataKeys"), Manifest.SupportedBlueprintMetadataKeys);
		WriteStringArray(*Writer, TEXT("supportedTypeCategories"), Manifest.SupportedTypeCategories);
		WriteStringArray(*Writer, TEXT("supportedContainerTypes"), Manifest.SupportedContainerTypes);
		WriteNameArray(*Writer, TEXT("supportedCommandTypes"), Manifest.SupportedCommandTypes);
		Writer->WriteObjectStart(TEXT("nodeSupportMatrixSummary"));
		Writer->WriteValue(TEXT("totalFamilies"), Manifest.NodeSupportMatrixSummary.TotalFamilies);
		Writer->WriteValue(TEXT("supportedFamilies"), Manifest.NodeSupportMatrixSummary.SupportedFamilies);
		Writer->WriteValue(TEXT("unsupportedFamilies"), Manifest.NodeSupportMatrixSummary.UnsupportedFamilies);
		Writer->WriteValue(TEXT("genericNodeSpawnerFamilies"), Manifest.NodeSupportMatrixSummary.GenericNodeSpawnerFamilies);
		Writer->WriteValue(TEXT("specializedHandlerFamilies"), Manifest.NodeSupportMatrixSummary.SpecializedHandlerFamilies);
		Writer->WriteValue(TEXT("directLoweringFamilies"), Manifest.NodeSupportMatrixSummary.DirectLoweringFamilies);
		Writer->WriteObjectEnd();
		Writer->WriteArrayStart(TEXT("nodeSupportMatrix"));
		for (const FVergilNodeSupportMatrixEntry& Entry : Manifest.NodeSupportMatrix)
		{
			WriteNodeSupportMatrixEntryJson(*Writer, Entry);
		}
		Writer->WriteArrayEnd();
		Writer->WriteArrayStart(TEXT("supportedDescriptors"));
		for (const FVergilSupportedDescriptorContract& Contract : Manifest.SupportedDescriptors)
		{
			WriteSupportedDescriptorContractJson(*Writer, Contract);
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		Writer->Close();
		return SerializedManifest;
	}

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&SerializedManifest);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("format"), SupportedContractManifestFormatName);
	Writer->WriteValue(TEXT("version"), Manifest.ManifestVersion);
	Writer->WriteValue(TEXT("pluginDescriptorVersion"), Manifest.PluginDescriptorVersion);
	Writer->WriteValue(TEXT("pluginSemanticVersion"), Manifest.PluginSemanticVersion);
	Writer->WriteValue(TEXT("schemaVersion"), Manifest.SchemaVersion);
	Writer->WriteValue(TEXT("commandPlanFormat"), Manifest.CommandPlanFormat);
	Writer->WriteValue(TEXT("commandPlanFormatVersion"), Manifest.CommandPlanFormatVersion);
	WriteStringArray(*Writer, TEXT("supportedSchemaMigrationPaths"), Manifest.SupportedSchemaMigrationPaths);
	WriteNameArray(*Writer, TEXT("supportedDocumentFields"), Manifest.SupportedDocumentFields);
	WriteNameArray(*Writer, TEXT("supportedTargetGraphs"), Manifest.SupportedTargetGraphs);
	WriteNameArray(*Writer, TEXT("supportedBlueprintMetadataKeys"), Manifest.SupportedBlueprintMetadataKeys);
	WriteStringArray(*Writer, TEXT("supportedTypeCategories"), Manifest.SupportedTypeCategories);
	WriteStringArray(*Writer, TEXT("supportedContainerTypes"), Manifest.SupportedContainerTypes);
	WriteNameArray(*Writer, TEXT("supportedCommandTypes"), Manifest.SupportedCommandTypes);
	Writer->WriteObjectStart(TEXT("nodeSupportMatrixSummary"));
	Writer->WriteValue(TEXT("totalFamilies"), Manifest.NodeSupportMatrixSummary.TotalFamilies);
	Writer->WriteValue(TEXT("supportedFamilies"), Manifest.NodeSupportMatrixSummary.SupportedFamilies);
	Writer->WriteValue(TEXT("unsupportedFamilies"), Manifest.NodeSupportMatrixSummary.UnsupportedFamilies);
	Writer->WriteValue(TEXT("genericNodeSpawnerFamilies"), Manifest.NodeSupportMatrixSummary.GenericNodeSpawnerFamilies);
	Writer->WriteValue(TEXT("specializedHandlerFamilies"), Manifest.NodeSupportMatrixSummary.SpecializedHandlerFamilies);
	Writer->WriteValue(TEXT("directLoweringFamilies"), Manifest.NodeSupportMatrixSummary.DirectLoweringFamilies);
	Writer->WriteObjectEnd();
	Writer->WriteArrayStart(TEXT("nodeSupportMatrix"));
	for (const FVergilNodeSupportMatrixEntry& Entry : Manifest.NodeSupportMatrix)
	{
		WriteNodeSupportMatrixEntryJson(*Writer, Entry);
	}
	Writer->WriteArrayEnd();
	Writer->WriteArrayStart(TEXT("supportedDescriptors"));
	for (const FVergilSupportedDescriptorContract& Contract : Manifest.SupportedDescriptors)
	{
		WriteSupportedDescriptorContractJson(*Writer, Contract);
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	Writer->Close();
	return SerializedManifest;
}

FString Vergil::DescribeNodeSupportMatrixAsMarkdownTable()
{
	const FVergilSupportedContractManifest& Manifest = GetSupportedContractManifest();

	TArray<FString> Lines;
	Lines.Reserve(2 + Manifest.NodeSupportMatrix.Num());
	Lines.Add(TEXT("| Family | Coverage | Handling | Descriptor coverage | Notes |"));
	Lines.Add(TEXT("| --- | --- | --- | --- | --- |"));

	for (const FVergilNodeSupportMatrixEntry& Entry : Manifest.NodeSupportMatrix)
	{
		Lines.Add(FString::Printf(
			TEXT("| %s | %s | %s | %s | %s |"),
			*EscapeMarkdownTableCell(Entry.Family),
			*FormatMarkdownInlineCode(GetNodeSupportCoverageName(Entry.Coverage)),
			*FormatMarkdownInlineCode(GetNodeSupportHandlingName(Entry.Handling)),
			*FormatMarkdownStringArray(Entry.DescriptorCoverage),
			*EscapeMarkdownTableCell(Entry.Notes.IsEmpty() ? TEXT("none") : Entry.Notes)));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString Vergil::DescribeSupportedDescriptorContractsAsMarkdownTable()
{
	const FVergilSupportedContractManifest& Manifest = GetSupportedContractManifest();

	TArray<FString> Lines;
	Lines.Reserve(2 + Manifest.SupportedDescriptors.Num());
	Lines.Add(TEXT("| Descriptor contract | Match | Expected kind | Target graphs | Required metadata | Notes |"));
	Lines.Add(TEXT("| --- | --- | --- | --- | --- | --- |"));

	for (const FVergilSupportedDescriptorContract& Contract : Manifest.SupportedDescriptors)
	{
		Lines.Add(FString::Printf(
			TEXT("| %s | %s | %s | %s | %s | %s |"),
			*FormatMarkdownInlineCode(Contract.DescriptorContract),
			*FormatMarkdownInlineCode(GetDescriptorMatchKindName(Contract.MatchKind)),
			*FormatMarkdownInlineCode(Contract.ExpectedNodeKind.IsEmpty() ? TEXT("any") : Contract.ExpectedNodeKind),
			*FormatMarkdownNameArray(Contract.SupportedTargetGraphs),
			*FormatMarkdownNameArray(Contract.RequiredMetadataKeys),
			*EscapeMarkdownTableCell(Contract.Notes.IsEmpty() ? TEXT("none") : Contract.Notes)));
	}

	return FString::Join(Lines, TEXT("\n"));
}
