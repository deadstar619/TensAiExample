# Vergil Supported Descriptor Contracts

This document describes the current scaffold contracts implemented in code today. It is intentionally narrower than the roadmap target.

## Current document scope

- `FVergilGraphDocument` currently supports `SchemaVersion`, `BlueprintPath`, `Variables`, `Functions`, `Dispatchers`, `Components`, `Nodes`, `Edges`, and `Tags`.
- `BlueprintPath` is document identity only. Compile/apply still requires `FVergilCompileRequest.TargetBlueprint`.
- `FVergilCompileRequest.TargetGraphName` selects one graph per compile. The default is `EventGraph`.
- `Tags` are accepted by the model but currently ignored by compile/apply.
- `Functions` are currently part of the canonical model and structural validation surface only. Compile/apply does not author function graphs or signatures yet.
- `Components` are currently part of the canonical model and structural validation surface only. Compile/apply does not author Blueprint component hierarchies or template properties yet.
- Asset-level authoring beyond variables and dispatchers is not implemented yet. There is no current compile/apply support for Blueprint metadata, functions, macros, components, interfaces, class defaults, or construction script definitions.

## Structural validation rules

- `SchemaVersion` must be greater than zero.
- A newer document schema than the compiler schema emits a warning, not an automatic migration.
- Every variable must have a unique non-empty name and cannot conflict with a dispatcher name.
- Every variable must declare a supported type category.
- `ExposeOnSpawn` requires `bInstanceEditable`.
- Every dispatcher must have a unique name.
- Every dispatcher parameter must have a unique name inside that dispatcher and a non-empty `PinCategory`.
- Every component must have a unique non-empty name and a non-empty `ComponentClassPath`.
- Component names cannot conflict with authored variables, functions, or dispatchers.
- Component parent self-references and authored parent cycles fail structural validation.
- Component parents that are not authored in the same document emit warnings because they may target inherited components on the Blueprint.
- Authored relative transform overrides must use finite numeric values.
- Component template properties must use non-empty property names.
- Every node must have a valid unique GUID and a non-empty descriptor.
- Every pin must have a valid unique GUID.
- Every edge must reference existing node IDs and pin IDs.

## Variable definition contracts

- Variables are authored from `Variables` on the document.
- Each variable definition uses `Name`, `Type`, `Flags`, `Category`, `Metadata`, and `DefaultValue`.
- `Type` currently supports these logical categories: `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `Type.ContainerType` currently supports `None`, `Array`, `Set`, and `Map`.
- `enum`, `object`, `class`, and `struct` variable categories require `Type.ObjectPath`.
- Map variables must declare `Type.ValuePinCategory`. If the value category is `enum`, `object`, `class`, or `struct`, `Type.ValueObjectPath` is also required.
- Non-map variables must not declare map value-type fields.
- `Flags` currently supports `bInstanceEditable`, `bBlueprintReadOnly`, `bExposeOnSpawn`, `bPrivate`, `bTransient`, `bSaveGame`, `bAdvancedDisplay`, `bDeprecated`, and `bExposeToCinematics`.
- `ExposeOnSpawn` is only accepted when `bInstanceEditable` is also true.
- `Metadata` is applied as Blueprint variable metadata key/value pairs.
- `DefaultValue` is applied through Blueprint compilation to the generated class default object. The editor-side `FBPVariableDescription.DefaultValue` string is not treated as a stable post-compile source of truth.

## Function definition contracts

- Functions are authored from `Functions` on the document.
- Each function definition currently uses `Name`, `bPure`, `AccessSpecifier`, `Inputs`, and `Outputs`.
- `AccessSpecifier` currently supports `Public`, `Protected`, and `Private`.
- Each input/output parameter uses `Name` plus the same `Type` shape as document-authored variables.
- Function names must be unique and cannot conflict with authored variable or dispatcher names.
- Signature member names must be unique within a function across both inputs and outputs.
- Function parameter types currently support the same logical categories and container rules as document-authored variables.
- Function definitions currently participate in structural validation only. They are not yet lowered into commands or applied to Blueprint function graphs.

## Component definition contracts

- Components are authored from `Components` on the document.
- Each component definition currently uses `Name`, `ComponentClassPath`, `ParentComponentName`, `AttachSocketName`, `RelativeTransform`, and `TemplateProperties`.
- `ComponentClassPath` is expected to name a component class for future authoring work. Structural validation currently requires only that it be non-empty.
- `ParentComponentName` may reference another authored component by name or an inherited component expected on the target Blueprint.
- `AttachSocketName` optionally names the parent socket or bone for future attachment authoring.
- `RelativeTransform` stores optional relative location, rotation, and scale overrides through `bHasRelativeLocation`, `bHasRelativeRotation`, and `bHasRelativeScale`.
- `TemplateProperties` stores raw component-template property overrides as property-name to serialized-value string pairs for future application work.
- Component definitions currently participate in structural validation only. They are not yet lowered into commands or applied to Blueprint component hierarchies or template properties.

## Dispatcher contracts

- Dispatchers are authored from `Dispatchers` on the document.
- Each dispatcher parameter uses `Name`, `PinCategory`, optional `PinSubCategory`, optional `ObjectPath`, and optional `bIsArray`.
- The current parameter type resolver supports these logical categories: `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `enum`, `object`, `class`, and `struct` categories require `ObjectPath` to resolve the referenced type.
- Dispatchers are supported on regular Blueprints. Macro libraries are rejected explicitly.

## Supported node descriptor contracts

| Descriptor contract | Expected kind | Required metadata | Notes |
| --- | --- | --- | --- |
| any non-empty descriptor | `Comment` | none | Comment nodes are matched by kind, not a fixed descriptor string. `CommentText` or `Title` sets the text. `CommentWidth`, `CommentHeight`, `FontSize`, `Color`, and `CommentColor` are supported. |
| `K2.Event.<FunctionName>` | `Event` | none | Binds a standard event by function name suffix. |
| `K2.CustomEvent.<EventName>` | any | none | Creates a custom event named by descriptor suffix. |
| `K2.Call.<FunctionName>` | `Call` | none | Optional `OwnerClassPath` constrains function resolution. |
| `K2.VarGet.<VariableName>` | `VariableGet` | none | Optional `OwnerClassPath` constrains property lookup. Only pure getter shapes are supported; exec pins on variable gets fail explicitly. |
| `K2.VarSet.<VariableName>` | `VariableSet` | none | Optional `OwnerClassPath` constrains property lookup. |
| `K2.Self` | any | none | Creates a self node. |
| `K2.Branch` | any | none | Standard branch node. |
| `K2.Sequence` | any | none | Output exec pins named like `Then_0`, `Then_1`, and so on determine sequence width. |
| `K2.ForLoop` | any | none | Optional `MacroBlueprintPath` and `MacroGraphName`. Defaults resolve to the engine `ForLoop` macro in `StandardMacros`. |
| `K2.Delay` | any | none | Lowers to `UKismetSystemLibrary::Delay`. |
| `K2.Cast` | any | `TargetClassPath` | Target class path must resolve to a class. |
| `K2.Reroute` | any | none | Creates a knot node. |
| `K2.Select` | any | `IndexPinCategory`, `ValuePinCategory` | Optional `IndexObjectPath`, `ValueObjectPath`, and `NumOptions`. Enum index selects use `IndexObjectPath` for the enum. |
| `K2.SwitchInt` | any | none | Planned exec output pins define the case labels and must parse as integers. |
| `K2.SwitchString` | any | none | Planned exec output pins define the case labels. Optional `CaseSensitive` metadata configures comparison behavior. |
| `K2.SwitchEnum` | any | `EnumPath` | Enum path must resolve to a `UEnum`. |
| `K2.FormatText` | any | `FormatPattern` | Creates a format text node and reconstructs argument pins from the format pattern. |
| `K2.MakeStruct` | any | `StructPath` | Struct path must resolve to a `UScriptStruct`. |
| `K2.BreakStruct` | any | `StructPath` | Struct path must resolve to a `UScriptStruct`. |
| `K2.MakeArray` | any | `ValuePinCategory` | Optional `ValueObjectPath` and `NumInputs`. `NumInputs` must be at least `1`. |
| `K2.MakeSet` | any | `ValuePinCategory` | Optional `ValueObjectPath` and `NumInputs`. `NumInputs` must be at least `1`. |
| `K2.MakeMap` | any | `KeyPinCategory`, `ValuePinCategory` | Optional `KeyObjectPath`, `ValueObjectPath`, and `NumPairs`. `NumPairs` must be at least `1`. |
| `K2.BindDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. |
| `K2.RemoveDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. |
| `K2.ClearDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. |
| `K2.CallDelegate.<PropertyName>` | any | none | Optional `OwnerClassPath` constrains delegate property resolution. |
| `K2.CreateDelegate.<FunctionName>` | any | none | Creates a delegate node and uses a finalize pass to assign the selected function after initial graph compilation. |

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
- One compile request currently targets one graph plus optional variable and dispatcher definitions. Function and component definitions are modeled and validated, but they are not yet part of compile/apply execution.
