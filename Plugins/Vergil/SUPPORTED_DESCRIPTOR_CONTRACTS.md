# Vergil Supported Descriptor Contracts

This document describes the current scaffold contracts implemented in code today. It is intentionally narrower than the roadmap target.

## Current document scope

- `FVergilGraphDocument` currently supports `SchemaVersion`, `BlueprintPath`, `Metadata`, `Variables`, `Functions`, `Dispatchers`, `Macros`, `Components`, `Interfaces`, `ClassDefaults`, `ConstructionScriptNodes`, `ConstructionScriptEdges`, `Nodes`, `Edges`, and `Tags`.
- `BlueprintPath` is document identity only. Compile/apply still requires `FVergilCompileRequest.TargetBlueprint`.
- `Metadata` currently supports Blueprint-level keys `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories`.
- `FVergilCompileRequest.TargetGraphName` selects one graph per compile. The default is `EventGraph`. The primary graph surface still uses top-level `Nodes` and `Edges`, while `ConstructionScriptNodes` and `ConstructionScriptEdges` are the dedicated document surface for `UserConstructionScript`.
- `Tags` are accepted by the model but currently ignored by compile/apply.
- `Functions` now lower into Blueprint function graph/signature authoring for function name, purity, access, and typed inputs/outputs. Function-body authoring is still separate future work.
- `Macros` now lower into Blueprint macro graph/signature authoring for exec/data inputs and outputs. Macro-body authoring is still separate future work.
- `Components` now lower into Blueprint component hierarchy authoring for component creation, parent attachment, attach sockets, template properties, and relative transforms.
- `Interfaces` now lower into Blueprint interface application for authored interface class paths.
- `ClassDefaults` now lower into post-compile Blueprint class default writes for authored property names and serialized values.
- Construction script definitions now lower into construction-script graph authoring when `FVergilCompileRequest.TargetGraphName` is `UserConstructionScript`. `UVergilEditorSubsystem::CompileDocument` still defaults to `EventGraph`; use `CompileDocumentToGraph(..., UserConstructionScript, ...)` to author the construction script through the editor subsystem helper.
- The current schema version is `3`. Older document schemas can be upgraded explicitly through `Vergil::MigrateDocumentSchema(...)` / `Vergil::MigrateDocumentToCurrentSchema(...)`, and the compiler now runs that upgrade path automatically before structural validation and planning.
- The compiler pipeline now runs schema migration, structural validation, semantic validation, symbol resolution, type resolution, node lowering, connection legality validation, and then command planning.
- Direct `ExecuteCommandPlan` execution now supports explicit asset-mutation commands for Blueprint metadata, function graphs, macro graphs, components, interfaces, class defaults, member renames, node removal/movement, and explicit blueprint compilation.
- Direct `ExecuteCommandPlan` execution now preflight-validates command-plan shape and intra-plan references before opening an editor transaction.
- Compiler-produced plans and direct `ExecuteCommandPlan` input now normalize into deterministic execution-phase order before validation and apply.
- Command plans now expose stable debug strings plus versioned JSON serialization/deserialization helpers for inspection and replay.

## Structural validation rules

- `SchemaVersion` must be greater than zero.
- Older document schemas can be upgraded explicitly when a supported forward migration path exists, and the compiler now attempts that upgrade automatically before later passes run. A newer document schema than the compiler schema still emits a warning, and downgrades are not attempted.
- Blueprint metadata entries must use non-empty supported keys.
- Every variable must have a unique non-empty name and cannot conflict with a dispatcher name.
- Every variable must declare a supported type category.
- Variable metadata entries must use non-empty keys.
- `ExposeOnSpawn` requires `bInstanceEditable`.
- Every dispatcher must have a unique name.
- Every dispatcher parameter must have a unique name inside that dispatcher, a supported `PinCategory`, and an `ObjectPath` for `enum`, `object`, `class`, and `struct` categories.
- Every macro must have a unique non-empty name and cannot conflict with authored variable, function, dispatcher, or component names.
- Macro signature member names must be unique within a macro across both inputs and outputs.
- Exec macro pins must not also declare data-type metadata.
- Every component must have a unique non-empty name and a non-empty, non-whitespace `ComponentClassPath`.
- Component names cannot conflict with authored variables, functions, macros, or dispatchers.
- Component parent self-references and authored parent cycles fail structural validation.
- Component parents that are not authored in the same document emit warnings because they may target inherited components on the Blueprint.
- Authored relative transform overrides must use finite numeric values.
- Component template properties must use non-empty property names.
- Every implemented interface must declare a non-empty `InterfaceClassPath`.
- Implemented interface class paths must be unique within one document.
- Class defaults must use non-empty property names.
- Every node must have a valid unique GUID and a non-empty descriptor.
- Every pin must have a valid unique GUID.
- Every edge must reference existing node IDs and pin IDs.
- Every edge must connect pins owned by its declared source node and target node.
- Node and pin GUIDs must remain unique across both the primary graph surface and the construction-script graph surface.
- `ConstructionScriptEdges` may only reference nodes and pins authored in `ConstructionScriptNodes`.

## Schema migration contracts

- `Vergil::CanMigrateSchemaVersion(SourceSchemaVersion, TargetSchemaVersion)` reports whether a forward-only migration path exists.
- `Vergil::MigrateDocumentSchema(...)` copies the source document, applies each supported forward migration step in order, and updates `SchemaVersion` on the migrated copy.
- `Vergil::MigrateDocumentToCurrentSchema(...)` is the convenience helper for upgrading to the current scaffold schema version.
- The current `1 -> 2` and `2 -> 3` migrations are additive: they advance the schema stamp while preserving authored document fields because the expanded whole-asset model remains backward-compatible with older document revisions.
- `FVergilSchemaMigrationPass` now runs first in the compiler pipeline, upgrades older documents into a working document copy, and feeds that migrated view into later validation, lowering, and command-planning passes.
- Current-schema documents are left unchanged, and newer-than-compiler schemas still flow through unchanged so structural validation can emit the existing future-schema warning without attempting a downgrade.

## Semantic validation contracts

- `FVergilSemanticValidationPass` now runs after structural validation and before later lowering and command-planning passes.
- The canonical graph-surface compile targets are currently `EventGraph` and `UserConstructionScript`; other `FVergilCompileRequest.TargetGraphName` values are rejected during semantic validation.
- Known descriptor families now validate their expected authored shape before planning. This includes descriptor suffixes for `K2.Event.*`, `K2.CustomEvent.*`, `K2.Call.*`, `K2.VarGet.*`, `K2.VarSet.*`, delegate helper descriptors, and required metadata for `K2.Cast`, `K2.Select`, `K2.SwitchEnum`, `K2.FormatText`, `K2.MakeStruct`, `K2.BreakStruct`, `K2.MakeArray`, `K2.MakeSet`, and `K2.MakeMap`.
- `K2.Event.*` descriptors now validate against node kind `Event`, `K2.Call.*` against kind `Call`, `K2.VarGet.*` against kind `VariableGet`, and `K2.VarSet.*` against kind `VariableSet`, which prevents those authored nodes from silently falling through to generic planning.
- When compiling `UserConstructionScript`, the only supported authored `K2.Event.*` descriptor is `K2.Event.UserConstructionScript`.

## Symbol resolution contracts

- `FVergilSymbolResolutionPass` now runs after semantic validation and before later lowering and command-planning passes.
- The symbol pass resolves callable/member references for `K2.Event.*`, `K2.Call.*`, `K2.VarGet.*`, `K2.VarSet.*`, `K2.BindDelegate.*`, `K2.RemoveDelegate.*`, `K2.ClearDelegate.*`, `K2.CallDelegate.*`, `K2.CreateDelegate.*`, custom-event delegate-signature metadata, and `K2.ForLoop` macro references.
- Explicit `OwnerClassPath` and `DelegateOwnerClassPath` metadata is authoritative. If authored, the symbol pass resolves only against that owner path and fails explicitly when the owner or member cannot be found.
- Without explicit owner metadata, variable and delegate symbols resolve in this order: document-authored members first, then existing Blueprint-local members, then inherited/native members.
- Without explicit owner metadata, `K2.Call.*` currently resolves against the target Blueprint parent class, matching the supported scaffold execution path. Successful inherited/native resolutions are normalized back into `OwnerClassPath` on the compiler working document so planned commands become explicit.
- `K2.CustomEvent.*` may optionally declare `DelegatePropertyName` plus `DelegateOwnerClassPath` metadata for delegate signatures. The symbol pass resolves those signatures before planning, and external-owner resolutions are normalized back into `DelegateOwnerClassPath`.
- `K2.CreateDelegate.*` currently resolves against target-graph custom events, document-authored function definitions, and existing Blueprint or parent-class functions. Ambiguous local matches fail explicitly.
- `K2.ForLoop` macro references now resolve during the symbol pass, and omitted `MacroBlueprintPath` / `MacroGraphName` metadata is normalized to the engine `StandardMacros` `ForLoop` default before planning.

## Type resolution contracts

- `FVergilTypeResolutionPass` now runs after symbol resolution and before node lowering, connection legality validation, and final command planning.
- The type pass normalizes authored type metadata across variable definitions, function signatures, macro signatures, dispatcher parameters, component class paths, interface class paths, and explicit typed-node metadata on the active graph surface.
- Supported logical type categories remain `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `enum`, `object`, `class`, and `struct` references now resolve to canonical object paths before planning, so planned commands stop depending on raw authored whitespace or alternate path spellings.
- The type pass currently resolves explicit type metadata for `K2.Cast`, `K2.Select`, `K2.SwitchEnum`, `K2.MakeStruct`, `K2.BreakStruct`, `K2.MakeArray`, `K2.MakeSet`, and `K2.MakeMap`.
- Failed type resolution stops compilation before node lowering, connection legality validation, or command planning, so invalid authored type references now return zero planned commands.

## Node lowering contracts

- `FVergilNodeLoweringPass` now runs after type resolution and before final command planning.
- The node-lowering pass resolves handlers against the normalized working document and emits node-scoped `AddNode`, `SetNodeMetadata`, and `FinalizeNode` commands into the compiler context before the final plan is assembled.
- `K2.CreateDelegate.*` finalization is now owned by node lowering instead of command planning, so function-name selection and normalized metadata travel with the lowered node output.
- Comment and generic metadata-lowering commands are emitted in deterministic key order.
- Failed node lowering stops compilation before later connection legality validation or final command planning, so handler failures now return zero planned commands.

## Connection legality contracts

- `FVergilConnectionLegalityPass` now runs after node lowering and before final command planning.
- The connection pass validates target-graph edges against the lowered `AddNode` output, not just raw authored node pins, so later passes only assemble connections that still exist on the lowered pin surface.
- Source pins must lower as output pins, target pins must lower as input pins, exec pins may only connect to exec pins, and data pins may only connect to data pins.
- Input pins may only have one incoming authored edge during compile-time legality validation.
- Lowered source and target pins must stay on the compile target graph, and both sides of one connection must lower into the same graph.
- Failed connection legality validation stops compilation before final command planning, so invalid authored edges now return zero planned commands.

## Blueprint metadata contracts

- Blueprint metadata is authored from `Metadata` on the document.
- Supported keys are `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories`.
- The planner lowers supported document metadata into deterministic `SetBlueprintMetadata` commands before graph-authoring commands for the requested target graph.
- `HideCategories` is authored as a single string on the document and, during direct command execution, accepts comma, semicolon, or newline separators before normalizing to unique sorted category entries on the Blueprint.

## Variable definition contracts

- Variables are authored from `Variables` on the document.
- Each variable definition uses `Name`, `Type`, `Flags`, `Category`, `Metadata`, and `DefaultValue`.
- `Type` currently supports these logical categories: `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `Type.ContainerType` currently supports `None`, `Array`, `Set`, and `Map`.
- `enum`, `object`, `class`, and `struct` variable categories require `Type.ObjectPath`.
- Whitespace-only object paths are treated as missing during structural validation.
- Map variables must declare `Type.ValuePinCategory`. If the value category is `enum`, `object`, `class`, or `struct`, `Type.ValueObjectPath` is also required.
- Non-map variables must not declare map value-type fields.
- `Flags` currently supports `bInstanceEditable`, `bBlueprintReadOnly`, `bExposeOnSpawn`, `bPrivate`, `bTransient`, `bSaveGame`, `bAdvancedDisplay`, `bDeprecated`, and `bExposeToCinematics`.
- `ExposeOnSpawn` is only accepted when `bInstanceEditable` is also true.
- `Metadata` is applied as Blueprint variable metadata key/value pairs and must use non-empty keys.
- `DefaultValue` is applied through Blueprint compilation to the generated class default object. The editor-side `FBPVariableDescription.DefaultValue` string is not treated as a stable post-compile source of truth.

## Function definition contracts

- Functions are authored from `Functions` on the document.
- Each function definition currently uses `Name`, `bPure`, `AccessSpecifier`, `Inputs`, and `Outputs`.
- `AccessSpecifier` currently supports `Public`, `Protected`, and `Private`.
- Each input/output parameter uses `Name` plus the same `Type` shape as document-authored variables.
- Function names must be unique and cannot conflict with authored variable or dispatcher names.
- Signature member names must be unique within a function across both inputs and outputs.
- Function parameter types currently support the same logical categories and container rules as document-authored variables.
- Function definitions now lower into `EnsureFunctionGraph` commands that create or update Blueprint function graphs and synchronize function purity, access flags, and typed inputs/outputs.
- Function definitions do not yet author function bodies. Document `Nodes` and `Edges` still target `FVergilCompileRequest.TargetGraphName`, not individual authored functions.

## Macro definition contracts

- Macros are authored from `Macros` on the document.
- Each macro definition currently uses `Name`, `Inputs`, and `Outputs`.
- Each input/output parameter uses `Name`, `bIsExec`, and, for data pins, the same `Type` shape as document-authored variables.
- Exec pins set `bIsExec=true` and must not also declare type metadata.
- Data macro pins currently support the same logical categories and container rules as document-authored variables.
- Macro definitions now lower into `EnsureMacroGraph` commands that create or update Blueprint macro graphs and synchronize entry/exit tunnel pins for exec/data inputs and outputs.
- Macro definitions do not yet author macro bodies. Document `Nodes` and `Edges` still target `FVergilCompileRequest.TargetGraphName`, not individual authored macros.

## Component definition contracts

- Components are authored from `Components` on the document.
- Each component definition currently uses `Name`, `ComponentClassPath`, `ParentComponentName`, `AttachSocketName`, `RelativeTransform`, and `TemplateProperties`.
- `ComponentClassPath` is expected to name a component class and now lowers into `EnsureComponent` for Blueprint component creation/update. Structural validation requires a non-empty, non-whitespace path.
- `ParentComponentName` may reference another authored component by name or an inherited component expected on the target Blueprint.
- `AttachSocketName` optionally names the parent socket or bone and now lowers into `AttachComponent` when a parent component is authored.
- `RelativeTransform` stores optional relative location, rotation, and scale overrides through `bHasRelativeLocation`, `bHasRelativeRotation`, and `bHasRelativeScale`.
- `TemplateProperties` stores raw component-template property overrides as property-name to serialized-value string pairs and now lowers into `SetComponentProperty`.
- Component definitions now lower into `EnsureComponent`, `AttachComponent`, template-property `SetComponentProperty`, and relative-transform `SetComponentProperty` commands.
- Template-property command emission sorts property names lexically for deterministic plans.
- Structured `RelativeTransform` fields still lower through their dedicated fields after template-property commands, so `RelativeLocation`, `RelativeRotation`, and `RelativeScale3D` remain authoritative if both surfaces target the same property.

## Interface definition contracts

- Implemented interfaces are authored from `Interfaces` on the document.
- Each interface definition currently uses `InterfaceClassPath`.
- `InterfaceClassPath` must be non-empty and unique within one document.
- Interface definitions now lower into `EnsureInterface` commands that apply Blueprint interfaces on the target Blueprint.

## Class default contracts

- Class defaults are authored from `ClassDefaults` on the document.
- `ClassDefaults` maps generated-class property names to serialized text values.
- Class-default property names must be non-empty.
- Class defaults now lower into post-compile `SetClassDefault` commands.
- Class-default property lookup is case-insensitive and tolerates Unreal bool-prefix naming such as `Replicates` vs `bReplicates`.
- Class defaults may target inherited native properties or authored Blueprint members on the compiled generated class.
- If `ClassDefaults` and `Variables[].DefaultValue` both target the same authored Blueprint member, the post-compile class-default write wins.

## Construction script contracts

- Construction script authoring is described from `ConstructionScriptNodes` and `ConstructionScriptEdges` on the document.
- `ConstructionScriptNodes` and `ConstructionScriptEdges` use the same node and edge shapes as the primary graph surface.
- When `FVergilCompileRequest.TargetGraphName` is `UserConstructionScript`, the planner lowers `ConstructionScriptNodes` and `ConstructionScriptEdges` through the existing generic graph-planning path.
- The executor resolves the dedicated construction-script graph, creates it through Unreal's construction-script utility path when needed, and reuses the existing function-entry node for the authored `K2.Event.UserConstructionScript` entry.
- `UVergilEditorSubsystem::CompileDocumentToGraph(..., UserConstructionScript, ..., bApplyCommands=true)` is the supported editor-subsystem path for end-to-end construction-script authoring.

## Explicit command plan contracts

- `FVergilCompilerCommand::ToDisplayString()` returns a stable human-readable summary for one command, and `Vergil::DescribeCommandPlan(...)` formats a whole plan as indexed lines for debug logging.
- `Vergil::SerializeCommandPlan(...)` emits a versioned JSON wrapper with `format="Vergil.CommandPlan"`, `version=1`, and a `commands` array.
- `Vergil::DeserializeCommandPlan(...)` accepts that wrapper back into `FVergilCompilerCommand` arrays. Bare JSON command arrays are also accepted for convenience.
- `UVergilEditorSubsystem::SerializeCommandPlan(...)` exposes the serializer on the editor subsystem, and `UVergilEditorSubsystem::ExecuteSerializedCommandPlan(...)` is the supported replay helper for applying serialized plans to a target Blueprint.
- `SetBlueprintMetadata` is supported through direct command-plan execution and currently accepts `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories` through `Name` plus `StringValue`.
- `EnsureVariable`, `SetVariableMetadata`, and `SetVariableDefault` are supported through direct command-plan execution and remain the current end-to-end path for document-authored variable definitions.
- `EnsureFunctionGraph` creates or resolves a Blueprint function graph. Use `GraphName` as the preferred graph identifier; `SecondaryName` is also accepted when `GraphName` is left at its default value.
- `EnsureFunctionGraph` also synchronizes function purity, access flags, and signature pins when its attributes include `bPure`, `AccessSpecifier`, `InputCount`, or `OutputCount` plus the indexed `Input_<n>_*` / `Output_<n>_*` type metadata emitted by the compiler.
- `EnsureMacroGraph` creates or resolves a Blueprint macro graph. Use `GraphName` as the preferred graph identifier; `SecondaryName` is also accepted when `GraphName` is left at its default value.
- `EnsureMacroGraph` also synchronizes macro entry/exit tunnel pins when its attributes include `InputCount` or `OutputCount` plus the indexed `Input_<n>_*` / `Output_<n>_*` pin metadata emitted by the compiler. Exec pins are represented by `Input_<n>_bExec=true` or `Output_<n>_bExec=true`.
- `EnsureComponent` requires `SecondaryName` for the component name and `StringValue` for the component class path.
- `AttachComponent` requires `SecondaryName` for the child component name, `Name` for the parent component name, and optionally `StringValue` for the attach socket or bone name.
- `SetComponentProperty` requires `SecondaryName` for the component name, `Name` for the property name, and `StringValue` for the serialized property value. Property lookup is case-insensitive and tolerates Unreal bool-prefix naming such as `HiddenInGame` vs `bHiddenInGame`. Standard `FVector` and `FRotator` text forms are accepted.
- `EnsureInterface` requires `StringValue` to resolve to a Blueprint interface class path.
- `SetClassDefault` requires `Name` for the generated-class property name and `StringValue` for the serialized default value. The target Blueprint must already have a compiled generated class.
- `RenameMember` requires `Name` for the existing member name, `SecondaryName` for the new name, and `Attributes["MemberType"]` set to one of `Variable`, `Dispatcher`, `FunctionGraph`, `MacroGraph`, or `Component`.
- `MoveNode` and `RemoveNode` require `GraphName` plus `NodeId` to identify the existing node to mutate.
- `CompileBlueprint` performs an explicit editor compile of the target Blueprint.
- Malformed command plans fail during preflight with diagnostics and execute zero commands.
- Deterministic command ordering currently normalizes at the execution-phase boundary: blueprint definition commands first, then graph structure, then `ConnectPins`, then `FinalizeNode`, then explicit `CompileBlueprint`, then post-compile `SetClassDefault`.
- `SetNodeMetadata`, `ConnectPins`, and `FinalizeNode` currently require their target node or pin ids to come from earlier `AddNode` commands in the same plan.
- These explicit commands are the current command-surface support for `VGR-2001`. The document compiler now lowers `Metadata` into `SetBlueprintMetadata`, `Functions` into `EnsureFunctionGraph`, `Macros` into `EnsureMacroGraph`, component hierarchy definitions into `EnsureComponent` / `AttachComponent` / template-and-transform `SetComponentProperty` commands, implemented interfaces into `EnsureInterface`, and class defaults into post-compile `SetClassDefault` commands.

## Dispatcher contracts

- Dispatchers are authored from `Dispatchers` on the document.
- Each dispatcher parameter uses `Name`, `PinCategory`, optional `PinSubCategory`, optional `ObjectPath`, and optional `bIsArray`.
- The current parameter type resolver supports these logical categories: `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `enum`, `object`, `class`, and `struct` categories require `ObjectPath` to resolve the referenced type, and whitespace-only object paths are treated as missing during structural validation.
- Dispatchers are supported on regular Blueprints. Macro libraries are rejected explicitly.

## Supported node descriptor contracts

| Descriptor contract | Expected kind | Required metadata | Notes |
| --- | --- | --- | --- |
| any non-empty descriptor | `Comment` | none | Comment nodes are matched by kind, not a fixed descriptor string. `CommentText` or `Title` sets the text. `CommentWidth`, `CommentHeight`, `FontSize`, `Color`, and `CommentColor` are supported. |
| `K2.Event.<FunctionName>` | `Event` | none | Binds a standard event by function name suffix and now resolves against the target Blueprint parent class during compilation. |
| `K2.CustomEvent.<EventName>` | any | none | Creates a custom event named by descriptor suffix. Optional `DelegatePropertyName` and `DelegateOwnerClassPath` metadata is resolved during compilation when authored. |
| `K2.Call.<FunctionName>` | `Call` | none | Optional `OwnerClassPath` constrains function resolution. When omitted, the current scaffold resolves against the target Blueprint parent class and normalizes that owner path into the planned command. |
| `K2.VarGet.<VariableName>` | `VariableGet` | none | Optional `OwnerClassPath` constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members. Only pure getter shapes are supported; exec pins on variable gets fail explicitly. |
| `K2.VarSet.<VariableName>` | `VariableSet` | none | Optional `OwnerClassPath` constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members. |
| `K2.Self` | any | none | Creates a self node. |
| `K2.Branch` | any | none | Standard branch node. |
| `K2.Sequence` | any | none | Output exec pins named like `Then_0`, `Then_1`, and so on determine sequence width. |
| `K2.ForLoop` | any | none | Optional `MacroBlueprintPath` and `MacroGraphName`. Defaults resolve to the engine `ForLoop` macro in `StandardMacros`, and the symbol pass validates the selected macro graph before planning. |
| `K2.Delay` | any | none | Lowers to `UKismetSystemLibrary::Delay`. |
| `K2.Cast` | any | `TargetClassPath` | Target class path must resolve to a class during type resolution and is normalized before planning. |
| `K2.Reroute` | any | none | Creates a knot node. |
| `K2.Select` | any | `IndexPinCategory`, `ValuePinCategory` | Optional `IndexObjectPath`, `ValueObjectPath`, and `NumOptions`. Enum index selects use `IndexObjectPath` for the enum, and explicit type metadata is resolved before planning. |
| `K2.SwitchInt` | any | none | Planned exec output pins define the case labels and must parse as integers. |
| `K2.SwitchString` | any | none | Planned exec output pins define the case labels. Optional `CaseSensitive` metadata configures comparison behavior. |
| `K2.SwitchEnum` | any | `EnumPath` | Enum path must resolve to a `UEnum` during type resolution and is normalized before planning. |
| `K2.FormatText` | any | `FormatPattern` | Creates a format text node and reconstructs argument pins from the format pattern. |
| `K2.MakeStruct` | any | `StructPath` | Struct path must resolve to a `UScriptStruct` during type resolution and is normalized before planning. |
| `K2.BreakStruct` | any | `StructPath` | Struct path must resolve to a `UScriptStruct` during type resolution and is normalized before planning. |
| `K2.MakeArray` | any | `ValuePinCategory` | Optional `ValueObjectPath` and `NumInputs`. `NumInputs` must be at least `1`. Explicit value-type metadata resolves before planning. |
| `K2.MakeSet` | any | `ValuePinCategory` | Optional `ValueObjectPath` and `NumInputs`. `NumInputs` must be at least `1`. Explicit value-type metadata resolves before planning. |
| `K2.MakeMap` | any | `KeyPinCategory`, `ValuePinCategory` | Optional `KeyObjectPath`, `ValueObjectPath`, and `NumPairs`. `NumPairs` must be at least `1`. Explicit key/value type metadata resolves before planning. |
| `K2.BindDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties. |
| `K2.RemoveDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties. |
| `K2.ClearDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties. |
| `K2.CallDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. Without it, the symbol pass resolves document-authored dispatchers first, then existing Blueprint dispatchers, then inherited/native delegate properties. |
| `K2.CreateDelegate.<FunctionName>` | any | none | Creates a delegate node and uses a finalize pass to assign the selected function after initial graph compilation. The selected function now resolves during compilation against target-graph custom events, document-authored functions, and existing Blueprint/parent-class functions. |

## Type metadata conventions

- The current type resolver accepts `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `enum`, `object`, `class`, and `struct` variants require an accompanying object-path field for the specific contract:
  - `ObjectPath` for variable definitions
  - `ObjectPath` for dispatcher parameters
  - `IndexObjectPath` and `ValueObjectPath` for select nodes
  - `KeyObjectPath` and `ValueObjectPath` for make-map nodes
  - `ValueObjectPath` for make-array and make-set nodes

## Important limits

- The generic fallback planner is not a support promise. Descriptors outside the table above may still plan, but most will fail during execution with `UnsupportedNodeExecution`.
- Comment metadata only applies to executed comment nodes. Arbitrary metadata on other nodes is not a generic editor-side mutation surface.
- One compile request currently targets one graph plus optional variable, dispatcher, function-signature, macro-signature, component-hierarchy, component-template-property, class-default, and implemented-interface definitions. Function bodies and macro bodies remain separate future work. Construction-script definitions participate in compile/apply when the target graph is `UserConstructionScript`.
