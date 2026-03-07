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

	TArray<FVergilSupportedDescriptorContract> BuildSupportedDescriptorContracts()
	{
		const TArray<FName> SupportedGraphs = BuildSupportedTargetGraphs();

		return {
			MakeDescriptorContract(
				TEXT("any non-empty descriptor"),
				EVergilDescriptorMatchKind::NodeKind,
				TEXT("Comment"),
				SupportedGraphs,
				{},
				TEXT("Comment nodes are matched by kind, not a fixed descriptor string. CommentText or Title sets the text. CommentWidth, CommentHeight, FontSize, Color, and CommentColor are supported.")),
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
				TEXT("Optional OwnerClassPath constrains function resolution. When omitted, the current scaffold resolves document-authored functions first, then existing Blueprint-local functions, then inherited/native functions. Self-owned resolutions keep an empty owner path; inherited/native resolutions normalize the owner path into the planned command.")),
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
				TEXT("Creates a self node.")),
			MakeDescriptorContract(
				TEXT("K2.Branch"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Standard branch node.")),
			MakeDescriptorContract(
				TEXT("K2.Sequence"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Output exec pins named like Then_0, Then_1, and so on determine sequence width.")),
			MakeDescriptorContract(
				TEXT("K2.ForLoop"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Optional MacroBlueprintPath and MacroGraphName metadata selects the backing macro. Defaults resolve to the engine StandardMacros ForLoop and are validated before planning.")),
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
				TEXT("TargetClassPath must resolve to a class during type resolution and is normalized before planning.")),
			MakeDescriptorContract(
				TEXT("K2.Reroute"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Creates a knot node.")),
			MakeDescriptorContract(
				TEXT("K2.Select"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("IndexPinCategory"), TEXT("ValuePinCategory") },
				TEXT("IndexPinCategory currently supports only bool, int, or enum under UE 5.7. Optional IndexObjectPath, ValueObjectPath, and NumOptions metadata refine the wildcard shape. Enum index selects use IndexObjectPath for the enum, explicit type metadata is resolved before planning, and unsupported index/value connections now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchInt"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Planned exec output pins define the case labels and must parse as integers. Unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchString"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{},
				TEXT("Planned exec output pins define the case labels. Optional CaseSensitive metadata configures comparison behavior, and unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.SwitchEnum"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("EnumPath") },
				TEXT("EnumPath must resolve to a UEnum during type resolution and is normalized before planning. Unsupported selection-pin type combinations now fail with dedicated apply-time diagnostics.")),
			MakeDescriptorContract(
				TEXT("K2.FormatText"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("FormatPattern") },
				TEXT("Creates a format-text node and reconstructs argument pins from the format pattern.")),
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
				TEXT("Optional ValueObjectPath and NumInputs metadata refine the container shape. NumInputs must be at least 1, and explicit value-type metadata resolves before planning.")),
			MakeDescriptorContract(
				TEXT("K2.MakeSet"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("ValuePinCategory") },
				TEXT("Optional ValueObjectPath and NumInputs metadata refine the container shape. NumInputs must be at least 1, and explicit value-type metadata resolves before planning.")),
			MakeDescriptorContract(
				TEXT("K2.MakeMap"),
				EVergilDescriptorMatchKind::Exact,
				TEXT("any"),
				SupportedGraphs,
				{ TEXT("KeyPinCategory"), TEXT("ValuePinCategory") },
				TEXT("Optional KeyObjectPath, ValueObjectPath, and NumPairs metadata refine the map shape. NumPairs must be at least 1, and explicit key/value type metadata resolves before planning.")),
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

FString FVergilSupportedContractManifest::ToDisplayString() const
{
	return FString::Printf(
		TEXT("%s version=%d plugin=%s descriptorVersion=%d schema=%d commandPlanFormat=%s:%d schemaMigrations=%d documentFields=%d targetGraphs=%d metadataKeys=%d typeCategories=%d containerTypes=%d commandTypes=%d descriptors=%d"),
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
	Lines.Reserve(6 + Manifest.SupportedDescriptors.Num());
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
