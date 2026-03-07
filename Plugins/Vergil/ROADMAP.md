# Vergil Roadmap

## Scope
Vergil is intended to become a deterministic, studio-grade Blueprint authoring plugin with:

- a canonical asset model
- a deterministic compiler and command executor
- explicit diagnostics for unsupported cases
- optional agent orchestration layered on top of engine-safe execution

This roadmap tracks what remains between the current scaffold and that target.

## Current Baseline
Branch baseline:

- `Experimental`

Known current state:

- `Vergil.Scaffold.*` passes headlessly
- module split is in place
- a canonical graph document exists
- deterministic command planning exists
- editor command execution exists for a limited K2 subset plus an explicit asset-mutation command surface for variables, function/macro graphs, components, interfaces, class defaults, member renames, node moves/removals, and explicit compile
- the agent layer is still a minimal audit subsystem

Current major gaps:

- no complete Blueprint asset model
- no multi-graph/full-asset compilation
- no full K2 coverage
- no production agent workflow
- no studio-grade CI, perf, migration, and release discipline

## Guiding Rules
- AI proposes documents or commands; it does not mutate Blueprints directly.
- Unsupported behavior must fail explicitly with diagnostics.
- Every editor mutation should be represented as a command.
- Layout and comments are post-passes, not compile prerequisites.
- Coverage must be measured by tests, not assumed.

## Milestone 0: Stabilize The Base
Goal:

- remove known debt before broadening scope

Current status:

- Milestone 0 complete
- `Vergil.Scaffold.*` now passes headlessly without Blueprint deprecation warnings
- document-defined member variables are supported end-to-end with structural validation, command planning, execution, and automation coverage

Tickets:

- [x] `VGR-0001` Replace deprecated timer usage with `Clear and Invalidate Timer by Handle`
- [x] `VGR-0002` Add a single documented command for running `Vergil.Scaffold.*` headlessly
- [x] `VGR-0003` Add compile/apply/test diagnostics summary utilities
- [x] `VGR-0004` Document the currently supported descriptor contracts

Acceptance criteria:

- [x] no deprecation warnings in `Vergil.Scaffold.*`
- [x] one documented headless automation command exists
- [x] current supported descriptor contracts are documented in-repo

## Milestone 1: Expand The Canonical Asset Model
Goal:

- allow one document to describe a whole Blueprint asset

Tickets:

- [x] `VGR-1001` Add Blueprint-level metadata
- [x] `VGR-1002` Add variable definitions with type, flags, metadata, and defaults
- [x] `VGR-1003` Add function definitions with purity, access, inputs, and outputs
- [x] `VGR-1004` Add macro graph definitions
- [x] `VGR-1005` Add component definitions with parent/attach/transform/template properties
- [x] `VGR-1006` Add implemented interface definitions
- [x] `VGR-1007` Add class default definitions
- [x] `VGR-1008` Add construction script graph definition
- [x] `VGR-1009` Add schema migration helpers
- [x] `VGR-1010` Extend structural validation for all new model types

Session note for `VGR-1001` (2026-03-06):

- `FVergilGraphDocument` now carries a top-level `Metadata` map for Blueprint-level authoring, currently supporting `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories`.
- The planner lowers supported document metadata into deterministic `SetBlueprintMetadata` commands, and the editor executor applies the same command surface directly to `UBlueprint`.
- `Vergil::SchemaVersion` is now `3`, and the additive `2 -> 3` migration preserves authored document fields while enabling the new metadata surface.
- `Vergil.Scaffold.BlueprintMetadataModel`, `Vergil.Scaffold.BlueprintMetadataPlanning`, and `Vergil.Scaffold.BlueprintMetadataAuthoringExecution` now cover model retention, deterministic planning, and end-to-end execution, with explicit-command and preflight-validation coverage extended alongside them.

Session note for `VGR-1008` (2026-03-06):

- `FVergilGraphDocument` now carries dedicated `ConstructionScriptNodes` and `ConstructionScriptEdges` surfaces with structural validation coverage and explicit planning support when `TargetGraphName` is `UserConstructionScript`.
- New `Vergil.Scaffold.ConstructionScriptDefinitionModel` and `Vergil.Scaffold.ConstructionScriptDefinitionPlanning` automation coverage exists for the new canonical model surface.

Session note for `VGR-1009` (2026-03-06):

- Under `VGR-1009`, `Vergil::SchemaVersion` advanced to `2`, and the model module exposed `CanMigrateSchemaVersion(...)`, `MigrateDocumentSchema(...)`, and `MigrateDocumentToCurrentSchema(...)` as explicit forward-migration helpers.
- The current `1 -> 2` step is additive and preserves authored document fields while advancing the schema stamp, which keeps the whole-asset document surface backward-compatible with schema `1`.
- `Vergil.Scaffold.SchemaMigrationHelpers` now covers successful forward migration, same-version no-op migration, downgrade rejection, and missing-path diagnostics. Compiler-pass orchestration is now complete under `VGR-3001`.

Session note for `VGR-1010` (2026-03-06):

- `FVergilGraphDocument::IsStructurallyValid(...)` now validates dispatcher parameter type categories and required object paths with the same typed-shape rules used by other authored member definitions, and whitespace-only typed object paths now fail validation instead of reaching execution.
- Variable metadata keys must be non-empty, component class paths must be non-whitespace, and graph edges must connect pins that actually belong to their declared source and target nodes.
- `Vergil.Scaffold.GraphDocumentValidation` now covers dispatcher type-shape failures, empty variable metadata keys, whitespace-only component class paths, and graph-edge pin ownership mismatches.

Acceptance criteria:

- one document can define parent class, variables, class defaults, functions, dispatchers, components, construction script, and graphs
- invalid asset definitions fail during structural validation with precise diagnostics

## Milestone 2: Expand The Command Surface
Goal:

- represent all asset mutations explicitly

Tickets:

- [x] `VGR-2001` Extend command types with:
  - `EnsureVariable`
  - `SetVariableMetadata`
  - `SetVariableDefault`
  - `EnsureFunctionGraph`
  - `EnsureMacroGraph`
  - `EnsureComponent`
  - `AttachComponent`
  - `SetComponentProperty`
  - `EnsureInterface`
  - `SetClassDefault`
  - `RemoveNode`
  - `RenameMember`
  - `MoveNode`
  - `CompileBlueprint`
- [x] `VGR-2002` Add command validation prior to execution
- [x] `VGR-2003` Add deterministic command ordering rules
- [x] `VGR-2004` Add command debug printing/serialization

Acceptance criteria:

- asset-level mutations no longer require ad hoc editor-side logic outside commands
- commands can be logged, inspected, and replayed deterministically

Session note for `VGR-2002` (2026-03-06):

- `FVergilCommandExecutor::Execute(...)` now preflight-validates command-plan shape and intra-plan references before opening an editor transaction, which prevents malformed plans from partially mutating the Blueprint before failure.
- `Vergil.Scaffold.CommandPlanValidation` now covers zero-mutation rejection for malformed component commands, broken metadata references, and duplicate node/pin ids, and the full `Vergil.Scaffold` suite re-verified cleanly after the change.

Session note for `VGR-2003` (2026-03-06):

- Compiler output and direct `ExecuteCommandPlan` input now normalize into deterministic execution-phase order before validation and apply, so logged plans match replay order across blueprint definition, graph structure, connection, finalize, compile, and post-compile class-default phases.
- `Vergil.Scaffold.CommandPlanOrdering` now covers the direct command surface ordering contract, and class-default planning coverage now asserts the normalized post-compile ordering.

Session note for `VGR-2004` (2026-03-06):

- `FVergilCompilerCommand` and `FVergilPlannedPin` now expose stable debug strings, and the compiler module now provides `Vergil::DescribeCommandPlan(...)`, `Vergil::SerializeCommandPlan(...)`, and `Vergil::DeserializeCommandPlan(...)` as versioned inspection/replay helpers.
- The current serialized command-plan format is `format="Vergil.CommandPlan"` with `version=1`, a deterministic field order, and bare-array parsing support for convenience.
- `UVergilEditorSubsystem` now exposes `SerializeCommandPlan(...)` plus `ExecuteSerializedCommandPlan(...)`, and compile/direct-command paths log normalized plans at debug verbosity.
- `Vergil.Scaffold.CommandSerializationUtilities` now covers deterministic serialization, deserialization diagnostics, and replay through the serialized editor-subsystem path.

## Milestone 3: Harden The Compiler Pipeline
Goal:

- move from a scaffold compiler to a real deterministic compiler

Tickets:

- [x] `VGR-3001` Add schema migration pass
- [x] `VGR-3002` Add semantic validation pass
- [x] `VGR-3003` Add symbol resolution pass
- [x] `VGR-3004` Add type resolution and wildcard resolution pass
- [x] `VGR-3005` Add node lowering pass
- [x] `VGR-3006` Add connection legality validation pass
- [x] `VGR-3007` Add post-compile finalize pass
- [x] `VGR-3008` Split layout/comment work into explicit post-passes
- [x] `VGR-3009` Add compile statistics and richer result metadata
- [x] `VGR-3010` Guarantee dry-run and apply share the same planning path

Acceptance criteria:

- failures identify pass, node, and cause
- wildcard-heavy nodes resolve without heuristics
- dry-run and apply produce identical command plans

Session note for `VGR-3001` (2026-03-06):

- `FVergilSchemaMigrationPass` now runs first in `FVergilBlueprintCompilerService::Compile(...)`, upgrades supported legacy documents into a compiler working copy, and feeds that migrated view into structural validation plus command planning.
- `FVergilCompilerContext` now carries the active document view so later passes consume the migrated copy instead of the raw request payload when migration happens.
- `Vergil.Scaffold.CompilerSchemaMigrationPass` now verifies legacy-schema compile requests emit `SchemaMigrationApplied`, avoid future-schema warnings after migration, and still plan authored commands deterministically.

Session note for `VGR-3002` (2026-03-06):

- `FVergilSemanticValidationPass` now runs after structural validation and before command planning, which turns known descriptor-shape failures into explicit compiler-phase diagnostics instead of late planning failures.
- The semantic pass currently rejects unsupported compile target graphs, descriptor/kind mismatches for authored event/call/variable nodes, missing required metadata on known K2 descriptors, and invalid `K2.Event.*` usage on the `UserConstructionScript` surface.
- `Vergil.Scaffold.SemanticValidationPass` now covers unsupported target graphs, descriptor/kind mismatches, missing required node metadata, and construction-script event restrictions with zero planned commands on failure.

Session note for `VGR-3003` (2026-03-06):

- `FVergilSymbolResolutionPass` now runs after semantic validation and before command planning, resolving callable/member symbols against the document, the target Blueprint, and inherited/native owners before any commands are emitted.
- Successful inherited/native resolutions now normalize owner metadata back into the compiler working document, so planned `K2.Call.*`, variable, delegate, and `K2.ForLoop` commands stop relying on late implicit owner defaults.
- `Vergil.Scaffold.SymbolResolutionPass` now covers normalized native-call owner paths plus explicit failure coverage for missing function, variable, delegate-signature, create-delegate, and for-loop macro symbols with zero planned commands on failure.

Session note for `VGR-3004` (2026-03-06):

- `FVergilTypeResolutionPass` now runs after symbol resolution and before command planning, resolving and normalizing authored type metadata across variable/function/macro/dispatcher definitions, component and interface class paths, and typed or wildcard-heavy K2 node metadata.
- Object-backed type references now normalize to canonical class, enum, or struct paths in the compiler working document, so planned commands stop depending on raw authored whitespace or alternate path spellings for typed surfaces.
- `Vergil.Scaffold.TypeResolutionPass` now covers successful normalization for definitions plus typed K2 nodes and explicit zero-command failures for invalid variable, function, dispatcher, macro, component, interface, cast, and wildcard-node type references.

Session note for `VGR-3005` (2026-03-06):

- `FVergilNodeLoweringPass` now runs after type resolution and before final command planning, so handler-driven node lowering executes once against the normalized compiler working document instead of being interleaved with document-definition planning.
- `FVergilCompilerContext` now buffers lowered node commands separately, and `FVergilCommandPlanningPass` now only assembles the final deterministic plan from document-level commands, ensured graph setup, lowered nodes, edges, and later finalize phases.
- Node metadata commands now emit in deterministic key order, and `Vergil.Scaffold.NodeLoweringPass` covers successful create-delegate node lowering plus explicit zero-command pass failures for handler errors. `VGR-3007` later split create-delegate finalization into its own finalize pass.

Session note for `VGR-3006` (2026-03-06):

- `FVergilConnectionLegalityPass` now runs after node lowering and before final command planning, validating target-graph edges against lowered node pins instead of raw authored pin ids alone.
- The connection pass currently rejects lowered-pin drift, source/target direction mistakes, exec/data mismatches, graph mismatches, and multiply-driven input pins before any `ConnectPins` commands are assembled.
- `Vergil.Scaffold.ConnectionLegalityPass` now covers a successful legal edge plus explicit zero-command failures for wrong source direction, exec/data mixing, multiply-driven target pins, and custom handlers that drop planned pins during lowering.

Session note for `VGR-3007` (2026-03-06):

- `FVergilPostCompileFinalizePass` now runs after connection legality and before final command planning, so deferred `FinalizeNode` work is emitted from its own compiler stage instead of being bundled into node lowering.
- `FVergilCompilerContext` now buffers post-compile finalize commands separately from lowered node commands, and `FVergilCommandPlanningPass` assembles the final deterministic plan from document-level commands, lowered nodes, edges, and that finalize buffer.
- `K2.CreateDelegate.*` finalization now lowers through the dedicated finalize pass, and `Vergil.Scaffold.PostCompileFinalizePass` covers deterministic `FinalizeNode` emission for valid create-delegate nodes.

Session note for `VGR-3008` (2026-03-06):

- `FVergilCommentPostPass` and `FVergilLayoutPostPass` now run after post-compile finalize lowering and before final command planning, which makes comment/layout work explicit optional post-passes instead of part of core node lowering.
- Authored comment nodes now lower through the dedicated comment post-pass when `FVergilCompileRequest.bGenerateComments` is true, while the dedicated layout pass stays isolated as the later extension point that `VGR-7002` wires into deterministic `MoveNode` planning.
- `Vergil.Scaffold.LayoutCommentPostPasses` now verifies comment-node emission only happens through the post-pass, and `VGR-7002` later extends that coverage to assert deterministic `MoveNode` output when `bAutoLayout` is enabled.

Session note for `VGR-3009` (2026-03-06):

- `FVergilCompileResult` now carries structured `Statistics` plus ordered `PassRecords`, so compile/apply callers can inspect target graph, requested versus effective schema version, request flags, deterministic per-phase command counts, and the last completed or failed compiler pass without scraping logs.
- `FVergilBlueprintCompilerService::Compile(...)` now records one pass record per attempted compiler pass, while `UVergilEditorSubsystem` preserves the same statistics surface for dry-run compile, compile+apply, direct command execution, and serialized replay and distinguishes `bApplyRequested` from `bExecutionAttempted`.
- `Vergil.Scaffold.CompileResultMetadata` now covers successful compile metadata, failed-pass metadata, and compile+apply execution metadata.

Session note for `VGR-3010` (2026-03-06):

- `UVergilEditorSubsystem` now routes dry-run compile and compile+apply through the same internal planning helper before any optional execution, and both compile+apply plus direct command execution use a shared apply helper that executes the `Commands` array already on the result.
- `FVergilCompileResult.Statistics` now exposes a normalized command-plan fingerprint, planning/apply invocation counts, and a `bExecutionUsedReturnedCommandPlan` flag so callers can verify that apply reused the returned plan instead of silently replanning.
- `Vergil.Scaffold.DryRunApplyPlanningParity` now verifies dry-run compile and compile+apply return identical normalized command plans for the same request while still recording a single planning invocation and a single apply invocation only on the apply path.

## Milestone 4: Blueprint Asset Authoring
Goal:

- author complete Blueprints, not only graph fragments

Tickets:

- [x] `VGR-4001` Implement variable creation/default application
- [x] `VGR-4002` Implement function graph creation/update
- [x] `VGR-4003` Implement macro graph creation/update
- [x] `VGR-4004` Implement component hierarchy authoring
- [x] `VGR-4005` Implement component template property setting
- [x] `VGR-4006` Implement interface application
- [x] `VGR-4007` Implement class default writing
- [x] `VGR-4008` Implement construction script authoring
- [x] `VGR-4009` Add save/reload/compile roundtrip tests

Session note for `VGR-4002` (2026-03-06):

- Current branch already contains a first pass: document-authored `Functions` lower into `EnsureFunctionGraph`, the executor synchronizes function purity/access/signature pins, and new `Vergil.Scaffold.FunctionDefinitionPlanning` plus `Vergil.Scaffold.FunctionAuthoringExecution` coverage exists.
- `mcp__tensai__build_project` succeeded in `Development`, and the documented headless runner now re-verifies `Vergil.Scaffold.*` cleanly in this workspace.

Session note for `VGR-4003` (2026-03-06):

- Current branch now adds document-authored `Macros` to the canonical model and structural validation surface, lowers them into `EnsureMacroGraph`, and synchronizes entry/exit tunnel pins for exec/data inputs and outputs.
- New `Vergil.Scaffold.MacroDefinitionModel`, `Vergil.Scaffold.MacroDefinitionPlanning`, and `Vergil.Scaffold.MacroAuthoringExecution` coverage exists, and `mcp__tensai__build_project` succeeded in `Development` after the change.
- The documented headless `Invoke-VergilScaffoldAutomation.ps1` runner now re-verifies `Vergil.Scaffold.*` cleanly in this workspace.

Session note for `VGR-4004` (2026-03-06):

- Current branch now lowers document-authored `Components` into explicit `EnsureComponent`, `AttachComponent`, and relative-transform `SetComponentProperty` commands. This covers component creation, parent attachment, attach sockets, and relative location/rotation/scale updates through `CompileDocument`.
- New `Vergil.Scaffold.ComponentDefinitionPlanning` plus `Vergil.Scaffold.ComponentAuthoringExecution` coverage was added alongside the existing explicit-command component test. Component template-property lowering landed separately under `VGR-4005`.
- `mcp__tensai__build_project` succeeded in `Development`, and the documented headless runner now re-verifies `Vergil.Scaffold.*` cleanly in this workspace.

Session note for `VGR-4005` (2026-03-06):

- Current branch now lowers document-authored component `TemplateProperties` into deterministic `SetComponentProperty` commands with lexical property-key ordering.
- `CompileDocument(..., bApplyCommands=true)` now applies component template-property values end-to-end on Blueprint component templates, and structured `RelativeTransform` fields still lower after template-property commands so the dedicated transform fields remain authoritative on overlap.
- `Vergil.Scaffold.ComponentDefinitionPlanning` and `Vergil.Scaffold.ComponentAuthoringExecution` now cover template-property lowering, deterministic ordering, and create/update execution.
- `mcp__tensai__build_project` succeeded in `Development`, and the documented headless runner now re-verifies `Vergil.Scaffold.*` cleanly in this workspace.

Session note for `VGR-4006` (2026-03-06):

- Current branch now adds document-authored `Interfaces` to the canonical model and structural validation surface, lowers them into explicit `EnsureInterface` commands, and applies them through `CompileDocument(..., bApplyCommands=true)` using the existing explicit executor path.
- New `Vergil.Scaffold.InterfaceDefinitionModel`, `Vergil.Scaffold.InterfaceDefinitionPlanning`, and `Vergil.Scaffold.InterfaceAuthoringExecution` coverage exists alongside the pre-existing explicit command-surface interface test.
- `mcp__tensai__build_project` succeeded in `Development`, `Vergil.Scaffold.Interface*` passed headlessly, and a clean full `Vergil.Scaffold` rerun was re-verified from the documented headless runner in this workspace.

Session note for `VGR-4007` (2026-03-06):

- Current branch now adds document-authored `ClassDefaults` to the canonical model and structural validation surface, lowers them into deterministic post-compile `SetClassDefault` commands, and applies them through `CompileDocument(..., bApplyCommands=true)` using the existing explicit executor path.
- New `Vergil.Scaffold.ClassDefaultDefinitionModel`, `Vergil.Scaffold.ClassDefaultDefinitionPlanning`, and `Vergil.Scaffold.ClassDefaultAuthoringExecution` coverage exists alongside the pre-existing explicit command-surface class-default test.
- `mcp__tensai__build_project` succeeded in `Development`, `Vergil.Scaffold.ClassDefault*` passed headlessly, and a clean full `Vergil.Scaffold` rerun was re-verified from the documented headless runner in this workspace.

Session note for `VGR-4008` (2026-03-06):

- `UVergilEditorSubsystem` now exposes `CompileDocumentToGraph(...)`, which forwards the requested target graph into `FVergilCompileRequest` while preserving `CompileDocument(...)` as the simpler no-graph helper. `VGR-7006` later made that helper use the configured developer-settings default graph instead of hard-coding `EventGraph`.
- The command executor now resolves `UserConstructionScript` through Unreal's dedicated construction-script graph helpers, creates it through the construction-script utility path when required, and reuses the graph's existing function-entry node for authored `K2.Event.UserConstructionScript` entries instead of spawning a regular override event node.
- New `Vergil.Scaffold.ConstructionScriptAuthoringExecution` coverage exists alongside the earlier model/planning tests, and `mcp__tensai__build_project` plus the documented headless scaffold runner re-verified cleanly in this workspace.

Session note for `VGR-4009` (2026-03-06):

- `Vergil.Scaffold.SaveReloadCompileRoundtrip` now creates a real `/Game/Tests/...` Blueprint asset, authors the current milestone-4 whole-asset surface into it, saves the package to disk, reloads it through Unreal's package reload path, and then verifies the authored state survives that reload.
- The roundtrip test currently covers Blueprint metadata, variable defaults/metadata, function and macro signatures, component hierarchy plus template properties, implemented interfaces, class defaults, the primary event graph, and the construction script on one persisted Blueprint.
- After reload, the test re-runs dry-run planning against the persisted Blueprint to verify stable normalized command-plan fingerprints and then native-compiles the reloaded asset cleanly before deleting the temporary content package.

Acceptance criteria:

- Vergil can generate a usable Actor Blueprint with variables, class defaults, components, functions, dispatchers, construction script, and event graph from one document

## Milestone 5: Core K2 Coverage
Goal:

- cover the major general-purpose Blueprint node families

Tickets:

- [x] `VGR-5001` Complete pure/impure call parity
- [x] `VGR-5002` Add remaining variable node variants
- [x] `VGR-5003` Add common macro-instance families such as `DoOnce` and `FlipFlop`
- [x] `VGR-5004` Add deterministic `SpawnActor`
- [x] `VGR-5005` Add timer families beyond current delegate-driven coverage
- [x] `VGR-5006` Add more flow-control families
- [x] `VGR-5007` Add select/switch diagnostics for unsupported type combinations
- [x] `VGR-5008` Document all supported node contracts

Acceptance criteria:

- common gameplay Blueprints can be authored without manual graph cleanup
- every supported family has headless automation coverage

Session note for `VGR-5007` (2026-03-06):

- `UE_5.7` select behavior is now enforced explicitly: semantic validation plus direct command-plan preflight reject `K2.Select` index categories outside `bool`, `int`, or `enum` before any node creation or transaction work starts.
- Apply-time pin-connection failures for `K2.Select`, `K2.SwitchInt`, `K2.SwitchString`, and `K2.SwitchEnum` now emit dedicated unsupported-type diagnostics instead of only falling back to generic schema rejection text.
- `Vergil.Scaffold.SemanticValidationPass`, `Vergil.Scaffold.CommandPlanValidation`, and `Vergil.Scaffold.SelectSwitchTypeDiagnostics` now cover the compiler, direct-command, and editor-execution paths for these unsupported type combinations.

Session note for `VGR-5005` (2026-03-07):

- `UE_5.7` timer coverage now extends past the existing delegate-driven `K2_SetTimerDelegate` plus `K2_ClearAndInvalidateTimerHandle` path to the non-delegate timer helpers that still lower through the generic `K2.Call.*` surface.
- New `Vergil.Scaffold.TimerFunctionNameExecution` coverage authors `K2_SetTimer`, `K2_PauseTimer`, `K2_UnPauseTimer`, `K2_IsTimerActive`, `K2_IsTimerPaused`, `K2_TimerExists`, `K2_GetTimerElapsedTime`, and `K2_GetTimerRemainingTime` end-to-end against the real `UKismetSystemLibrary` nodes under `UE_5.7`.
- `Vergil.Scaffold.TimerDelegateExecution` now also re-verifies the handle-based follow-on family end-to-end: `K2_PauseTimerHandle`, `K2_UnPauseTimerHandle`, `K2_IsTimerActiveHandle`, `K2_IsTimerPausedHandle`, `K2_TimerExistsHandle`, `K2_GetTimerElapsedTimeHandle`, `K2_GetTimerRemainingTimeHandle`, and the existing `K2_ClearAndInvalidateTimerHandle` path.
- `mcp__tensai__build_project` succeeded in `Development`, targeted headless timer runs passed, and the full documented `Invoke-VergilScaffoldAutomation.ps1` rerun re-verified `Vergil.Scaffold.*` cleanly in this workspace.

Session note for `VGR-5006` (2026-03-07):

- `UE_5.7` StandardMacros-backed `K2.ForLoopWithBreak`, `K2.Gate`, and `K2.WhileLoop` now share the existing macro-instance resolution/apply path with `K2.ForLoop`, `K2.DoOnce`, and `K2.FlipFlop`, including optional `MacroBlueprintPath` / `MacroGraphName` overrides and symbol-pass validation of the selected macro graph.
- Planning now emits dedicated `Vergil.K2.ForLoopWithBreak`, `Vergil.K2.Gate`, and `Vergil.K2.WhileLoop` add-node commands, while editor execution instantiates real `UK2Node_MacroInstance` nodes against the resolved `StandardMacros` graphs under `UE_5.7`.
- The Gate contract now normalizes authored `StartClosed` pins to the engine bool input `bStartClosed`, and `Vergil.Scaffold.SymbolResolutionPass` plus the new `Vergil.Scaffold.FlowControlMacroExecution` test now cover invalid override diagnostics and end-to-end authoring of these additional flow-control macro families.

Session note for `VGR-5003` (2026-03-07):

- `UE_5.7` StandardMacros-backed `K2.DoOnce` and `K2.FlipFlop` now share the existing macro-instance resolution/apply path with `K2.ForLoop`, including optional `MacroBlueprintPath` / `MacroGraphName` overrides and symbol-pass validation of the selected macro graph.
- Planning now emits dedicated `Vergil.K2.DoOnce` and `Vergil.K2.FlipFlop` add-node commands, while editor execution instantiates `UK2Node_MacroInstance` nodes against the resolved `StandardMacros` graph under `UE_5.7`.
- `Vergil.Scaffold.SymbolResolutionPass` and `Vergil.Scaffold.StandardMacroInstanceExecution` now cover invalid override diagnostics plus end-to-end authoring of the real `DoOnce` and `FlipFlop` macro node surfaces.

Session note for `VGR-5004` (2026-03-07):

- `UE_5.7` `K2.SpawnActor` now uses a deterministic `ActorClassPath` contract over `UK2Node_SpawnActorFromClass`, with compile-time type resolution normalizing the selected actor class and rejecting non-actor classes plus unsupported dynamic `Class` and `WorldContextObject` pin authoring. The deterministic contract now also requires an authored, connected `SpawnTransform` input because the engine node expands through by-reference transform calls.
- Planning now emits dedicated `Vergil.K2.SpawnActor` add-node commands, while editor execution creates a real `UK2Node_SpawnActorFromClass`, sets its class pin through the `UE_5.7` schema path, and exposes class-specific `ExposeOnSpawn` property pins such as `Instigator`.
- `Vergil.Scaffold.SemanticValidationPass`, `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.CommandPlanValidation`, `Vergil.Scaffold.SupportedContractInspection`, and `Vergil.Scaffold.SpawnActorExecution` now cover the compiler, manifest, direct-plan preflight, and end-to-end editor-authoring paths for the supported `SpawnActor` surface.

Session note for `VGR-5001` (2026-03-07):

- `K2.Call.*` now resolves against document-authored function definitions and existing Blueprint-local functions before falling back to inherited/native functions, matching the existing variable, delegate, and `K2.CreateDelegate.*` symbol-resolution behavior.
- Empty-owner `Vergil.K2.Call` execution now resolves Blueprint self functions under `UE_5.7`, and self-owned call nodes bind through `SetSelfMember(...)` for both pure and impure Blueprint functions instead of only parent-class or custom-event targets.
- `Vergil.Scaffold.SymbolResolutionPass` and `Vergil.Scaffold.SelfFunctionCallExecution` now cover the compiler and editor-apply paths for pure and impure self-function calls.

Session note for `VGR-5002` (2026-03-07):

- `UE_5.7` variable getters now support the remaining built-in variants: bool branch getters plus validated object/class/soft-reference getters, in addition to the existing pure read shape.
- Compiler symbol resolution, direct command-plan preflight, and editor execution now all reject unsupported impure getter types explicitly instead of falling back to generic pin-registration failures.
- `Vergil.Scaffold.SymbolResolutionPass`, `Vergil.Scaffold.CommandPlanValidation`, and `Vergil.Scaffold.VariableGetterVariantsExecution` now cover the compiler, direct-command, and editor-apply paths for these getter variants.

Session note for `VGR-5008` (2026-03-07):

- `SUPPORTED_DESCRIPTOR_CONTRACTS.md` now carries a full supported-node contract table with descriptor match kind, expected node kind, supported target graphs, required metadata keys, and the current code-backed notes for every supported descriptor family.
- `Vergil::DescribeSupportedDescriptorContractsAsMarkdownTable()` now renders that table directly from the code-backed contract manifest, so the checked-in markdown can be kept in sync without manually re-deriving rows from `VergilContractInfo.cpp`.
- `Vergil.Scaffold.SupportedNodeContractDocs` now loads the markdown file and fails if the generated contract-table section drifts from the manifest-backed markdown export, turning `VGR-5008` into an enforced documentation contract instead of a one-off doc pass.

## Milestone 6: Advanced K2 Coverage
Goal:

- broaden toward complete engine-facing Blueprint authoring

Tickets:

- [x] `VGR-6001` Add component-related nodes
- [x] `VGR-6002` Add interface call/message nodes
- [x] `VGR-6003` Add object/class/soft-reference families
- [x] `VGR-6004` Add async task node families where deterministic setup is possible
- [x] `VGR-6005` Add specialized handlers for nodes that cannot use the generic path
- [x] `VGR-6006` Add a generic node-spawner path for arbitrary supported `UK2Node` classes
- [x] `VGR-6007` Build an explicit support matrix of generic vs specialized coverage
- [x] `VGR-6008` Add unsupported-node reporting that enumerates exact missing families

Acceptance criteria:

- supported vs unsupported coverage is measurable
- non-trivial K2 families are handled by either a generic spawner path or an explicit specialized handler

Session note for `VGR-6001` (2026-03-07):

- `UE_5.7` deterministic support now exists for `K2.AddComponentByClass`, `K2.GetComponentByClass`, `K2.GetComponentsByClass`, `K2.FindComponentByTag`, and `K2.GetComponentsByTag` through explicit specialized handlers instead of relying on the generic `K2.Call.*` path to infer dynamic component-class behavior.
- Type resolution now normalizes `ComponentClassPath`, rejects non-`UActorComponent` classes, and validates the authored deterministic pin surface, while direct command-plan preflight and editor execution both instantiate the real `UK2Node_AddComponentByClass` or typed `AActor` lookup nodes and conform their return pins from the metadata-driven component class.
- `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, and the new `Vergil.Scaffold.ComponentNodeExecution` test now cover the compiler manifest, markdown contract table, command planning, and end-to-end editor-authoring path for this component-node family.

Session note for `VGR-6002` (2026-03-07):

- `UE_5.7` deterministic support now exists for explicit Blueprint-interface invocation descriptors `K2.InterfaceCall.*` and `K2.InterfaceMessage.*`, each requiring `InterfaceClassPath` instead of overloading the generic `K2.Call.*` path with hidden interface semantics.
- Symbol and type resolution now normalize `InterfaceClassPath` against the real interface owner class, direct command-plan preflight validates the resolved interface function surface, and editor execution now materializes the real `UK2Node_CallFunction` versus `UK2Node_Message` node classes for direct call versus message semantics.
- `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, and the new `Vergil.Scaffold.InterfaceInvocationExecution` test now cover the manifest, markdown contract table, normalized planning, and end-to-end editor-authoring path for this interface-invocation family.

Session note for `VGR-6003` (2026-03-07):

- `UE_5.7` deterministic support now exists for `K2.ClassCast`, `K2.GetClassDefaults`, `K2.LoadAsset`, `K2.LoadAssetClass`, `K2.LoadAssets`, and `K2.ConvertAsset` through explicit specialized handlers instead of relying on the generic fallback path to infer object/class/soft-reference behavior.
- Type resolution now normalizes `TargetClassPath`, `ClassPath`, and `AssetClassPath`, rejects unsupported authored surfaces such as the dynamic `GetClassDefaults` Class pin, and validates the deterministic pin surface for class-default inspection plus the async load families. Editor execution now materializes the real `UK2Node_ClassDynamicCast`, `UK2Node_GetClassDefaults`, `UK2Node_LoadAsset*`, and `UK2Node_ConvertAsset` nodes, while preserving the native `UObject` result families required for `LoadAsset` and `LoadAssets` to compile cleanly under UE_5.7.
- `Vergil.Scaffold.SemanticValidationPass`, `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, and the new `Vergil.Scaffold.ObjectClassReferenceExecution` test now cover the manifest, markdown contract table, normalized planning, and end-to-end editor-authoring path for this object/class/soft-reference family.

Session note for `VGR-6004` (2026-03-07):

- `UE_5.7` deterministic support now exists for generic Blueprint async-action descriptors `K2.AsyncAction.*`, each using `FactoryClassPath` plus the descriptor suffix to resolve a static `BlueprintInternalUseOnly` factory function that returns `UBlueprintAsyncActionBase`.
- Type resolution now normalizes `FactoryClassPath`, rejects dedicated async-node families that advertise `HasDedicatedAsyncNode`, validates the authored visible pin surface against the real generic `UK2Node_AsyncAction` shape, and keeps hidden pins such as `WorldContextObject` outside the authored deterministic contract.
- Direct command-plan preflight and editor execution now materialize the real `UK2Node_AsyncAction` node class for supported generic factories, while `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, and the new `Vergil.Scaffold.AsyncActionExecution` test now cover the manifest, markdown contract table, normalized planning, and end-to-end editor-authoring path for this async-action family.

Session note for `VGR-6005` (2026-03-07):

- `UE_5.7` deterministic support now also exists for specialized async-task families `K2.AIMoveTo` and `K2.PlayMontage`, each using an explicit dedicated handler instead of trying to force `HasDedicatedAsyncNode` factories through the generic `K2.AsyncAction.*` path.
- Type resolution now resolves the backing factory functions `UAIBlueprintHelperLibrary::CreateMoveToProxyObject` and `UPlayMontageCallbackProxy::CreateProxyObjectForPlayMontage`, validates the authored visible pin surface against the real dedicated node classes, and rejects these latent families on `UserConstructionScript`.
- Direct command-plan preflight and editor execution now materialize the real `UK2Node_AIMoveTo` and `UK2Node_PlayMontage` node classes, while `Vergil.Scaffold.TypeResolutionPass`, `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, and the new `Vergil.Scaffold.SpecializedAsyncTaskExecution` test now cover the manifest, markdown contract table, normalized planning, and end-to-end editor-authoring path for this specialized async-task family.

Session note for `VGR-6006` (2026-03-07):

- `UE_5.7` editor execution now routes supported `UK2Node`-backed add-node commands through a shared generic node-spawner path instead of open-coding each class instantiation branch, while leaving macros, `K2.Call.*`, delegate helpers, variable nodes, and other non-`UK2Node` families on their existing explicit paths.
- The generic node-spawner path now covers straightforward node classes such as `K2.Self`, `K2.Branch`, `K2.Sequence`, `K2.Reroute`, `K2.Select`, `K2.Switch*`, `K2.FormatText`, `K2.MakeArray`, `K2.MakeSet`, `K2.MakeMap`, plus the already-supported `UK2Node` families `K2.AsyncAction.*`, `K2.AIMoveTo`, `K2.PlayMontage`, `K2.SpawnActor`, `K2.AddComponentByClass`, `K2.ClassCast`, `K2.GetClassDefaults`, `K2.LoadAsset*`, and `K2.ConvertAsset`.
- Direct command-plan preflight now validates those generic-spawn families through the same shared registry, and `Vergil.Scaffold.SupportedContractInspection`, `Vergil.Scaffold.SupportedNodeContractDocs`, `Vergil.Scaffold.AsyncActionExecution`, `Vergil.Scaffold.SpecializedAsyncTaskExecution`, `Vergil.Scaffold.ObjectClassReferenceExecution`, `Vergil.Scaffold.ComponentNodeExecution`, `Vergil.Scaffold.SelectExecution`, `Vergil.Scaffold.SwitchIntExecution`, `Vergil.Scaffold.SwitchEnumExecution`, `Vergil.Scaffold.FormatTextExecution`, `Vergil.Scaffold.MakeArrayExecution`, `Vergil.Scaffold.MakeSetExecution`, and `Vergil.Scaffold.MakeMapExecution` now cover that shared path end to end.

Session note for `VGR-6007` (2026-03-07):

- The supported-contract manifest now exposes a code-backed node-support matrix summary plus explicit family rows, so tooling can measure supported versus intentionally unsupported K2-family coverage and distinguish generic node-spawner coverage, specialized-handler coverage, and other direct-lowering paths without scraping roadmap prose.
- The current matrix rows group event/custom-event entrypoints, call/message/component-lookup nodes, variable/struct/delegate helpers, standard macro families, generic `UK2Node` spawner families, specialized async-task families, dedicated async nodes that still require explicit handlers, and the remaining unsupported descriptor-backed `UK2Node` bucket that still fails explicitly.
- `SUPPORTED_DESCRIPTOR_CONTRACTS.md` now includes a generated support-matrix table alongside the generated descriptor-contract table, and `Vergil.Scaffold.SupportedContractInspection` plus `Vergil.Scaffold.SupportedNodeContractDocs` now cover the structured manifest fields, JSON/description output, and markdown sync for that matrix surface.

Session note for `VGR-6008` (2026-03-07):

- Unsupported generic add-node execution and dedicated async-action rejection now both name the exact unsupported family from the code-backed node-support matrix instead of stopping at anonymous generic errors.
- The compiler now routes `HasDedicatedAsyncNode` generic async-action failures through the `Dedicated async nodes without a specialized handler` family, while direct command-plan preflight and execution route unsupported descriptor-backed `UK2Node` add-node commands through the `Arbitrary unsupported descriptor-backed UK2Node families` bucket.
- `Vergil.Scaffold.UnsupportedNodeReporting` now covers both paths end to end, so future support-matrix edits will fail automation if the emitted diagnostics drift away from the manifest family labels.

## Milestone 7: Editor Tooling
Goal:

- make Vergil inspectable and practical for real users

Tickets:

- [x] `VGR-7001` Add document/command/diagnostic inspector tooling
- [x] `VGR-7002` Add deterministic auto-layout pass API
- [x] `VGR-7003` Add explicit comment generation pass API
- [x] `VGR-7004` Add document diff and command-plan preview tooling
- [x] `VGR-7005` Add stronger undo/redo transaction auditing
- [x] `VGR-7006` Expand developer settings for compiler/layout/validation behavior
- [x] `VGR-7007` Add runtime/reflection inspection/discovery utilities

Acceptance criteria:

- users can inspect planned mutations before apply
- layout/comments remain optional post-passes

Session note for `VGR-7001` (2026-03-07):

- The existing inspection helper declarations are now implemented end-to-end: `Vergil::DescribeGraphDocument(...)` / `SerializeGraphDocument(...)`, `Vergil::DescribeDiagnostics(...)` / `SerializeDiagnostics(...)`, and `Vergil::DescribeCompileResult(...)` / `SerializeCompileResult(...)` now expose stable document, diagnostics, and compile-result inspection surfaces alongside the pre-existing command-plan inspection helpers.
- `UVergilEditorSubsystem` now exposes read-only `DescribeCommandPlan()`, `DescribeDocument()`, `SerializeDocument()`, `DescribeDiagnostics()`, `SerializeDiagnostics()`, `DescribeCompileResult()`, and `SerializeCompileResult()` helpers, while `UVergilAgentSubsystem` mirrors the same surfaces plus `InspectCommandPlanAsJson()` for tool-facing inspection without scraping logs.
- `Vergil.Scaffold.InspectorTooling` now covers namespace, editor-subsystem, and agent-subsystem inspection parity for command plans, canonical documents, diagnostics, and compile results, including deterministic JSON payload checks.

Session note for `VGR-7002` (2026-03-07):

- `FVergilLayoutPostPass` now emits deterministic `MoveNode` commands instead of acting as a no-op boundary. Primary nodes are laid out into dependency-driven columns and rows, and authored comment nodes are placed into a deterministic left-side band when comment generation is enabled.
- `FVergilCompileRequest` now carries explicit `AutoLayout` settings for `Origin`, `HorizontalSpacing`, `VerticalSpacing`, and `CommentPadding`, while `UVergilEditorSubsystem` seeds those values from `UVergilDeveloperSettings` so the existing editor-facing compile helpers pick up project defaults automatically.
- `Vergil.Scaffold.LayoutCommentPostPasses` now covers deterministic layout planning, and `Vergil.Scaffold.AutoLayoutExecution` now proves the planned `MoveNode` work is applied end-to-end under the `UE_5.7` editor pipeline.

Session note for `VGR-7003` (2026-03-07):

- `FVergilCompileRequest` now carries explicit `CommentGeneration` settings for comment width, height, font size, title color, zoom bubble visibility, bubble coloring, and move mode, using `UE_5.7` `UEdGraphNode_Comment` defaults as the deterministic baseline instead of inheriting per-user graph-editor defaults.
- The comment post-pass now emits default `SetNodeMetadata` work for any missing comment style fields, so authored comment nodes stay explicit in the returned command plan while still allowing authored metadata to override request defaults on a field-by-field basis.
- `UVergilEditorSubsystem` now exposes `MakeCompileRequest(...)` plus `CompileRequest(...)`, and `Vergil.Scaffold.LayoutCommentPostPasses` / `Vergil.Scaffold.CommentExecution` now cover request-level comment-pass customization through planning and end-to-end `UE_5.7` execution.

Session note for `VGR-7004` (2026-03-07):

- Vergil now exposes a deterministic `Vergil.DocumentDiff` inspection contract through `Vergil::DiffGraphDocuments(...)`, `DescribeDocumentDiff(...)`, and `SerializeDocumentDiff(...)`, reporting added, removed, and modified canonical-document paths plus before/after fingerprints without depending on Blueprint asset scraping.
- `UVergilEditorSubsystem` now exposes `PreviewCompileRequest(...)`, `PreviewDocument(...)`, and `PreviewDocumentToGraph(...)`, returning a versioned `Vergil.CommandPlanPreview` payload that captures the authored request document, the effective post-migration working document, the document diff between them, and the existing dry-run `FVergilCompileResult`.
- `UVergilAgentSubsystem` mirrors the new diff/preview inspection helpers, and `Vergil.Scaffold.DiffPreviewTooling` now covers namespace, editor-subsystem, and agent-subsystem parity for document diffs plus command-plan preview parity against the existing dry-run compile path.

Session note for `VGR-7005` (2026-03-07):

- `FVergilCompileResult.Statistics` now carries a code-backed `TransactionAudit` payload for apply paths, capturing whether Vergil opened a scoped editor transaction plus before/after undo-stack snapshots including queue length, undo count, next undo/redo transaction ids, titles, contexts, primary objects, and target-Blueprint undo-buffer presence.
- `FVergilCommandExecutor::Execute(...)` now opens editor transactions with the explicit Vergil context and primary Blueprint object instead of the anonymous scoped-transaction path, which makes the resulting undo entry auditable through the returned compile/apply result and the editor's own undo history.
- `Vergil.Scaffold.CompileResultMetadata` now asserts the new audit surface, and `Vergil.Scaffold.TransactionAudit` now proves the recorded transaction can be undone and redone end to end under the `UE_5.7` editor pipeline.

Session note for `VGR-7006` (2026-03-07):

- `UVergilDeveloperSettings` now carries the default compile target graph, full auto-layout defaults, full comment-generation defaults, and a `bTreatStructuralWarningsAsErrors` toggle. `UVergilEditorSubsystem` now also exposes `MakeDefaultCompileRequest(...)` and uses the configured default graph for `CompileDocument(...)` / `PreviewDocument(...)` when callers omit an explicit graph.
- `FVergilCompileRequest` now carries `bTreatStructuralWarningsAsErrors`, and the compiler promotes structural-validation warnings to errors before later passes when that strictness is enabled, so projects can tighten validation behavior without disabling any of the existing safety checks.
- `Vergil.Scaffold.DeveloperSettingsDefaults` plus `Vergil.Scaffold.StructuralWarningStrictValidation` now cover seeded request defaults, configured construction-script default targeting, and warning-to-error structural-validation behavior.

Session note for `VGR-7007` (2026-03-07):

- Vergil now exposes code-backed runtime/reflection inspection helpers for classes, structs, enums, and Blueprint generated classes through `Vergil::InspectReflectionSymbol(...)` plus bridge-friendly `UVergilEditorSubsystem` / `UVergilAgentSubsystem` wrappers that accept simple string queries.
- A matching discovery surface now exists through `Vergil::DiscoverReflectionSymbols(...)`, returning deterministic substring matches across the currently loaded reflected type set so future tooling can discover likely script/object paths without depending on TensAi-only editor tools.
- The new versioned inspection formats are `Vergil.ReflectionSymbol` and `Vergil.ReflectionDiscovery`, each at version `1`, and `Vergil.Scaffold.ReflectionInspection` now covers namespace, editor-subsystem, and agent-subsystem parity for class, enum, struct, missing-symbol, and search-result inspection.

## Milestone 8: Real Agent Layer
Goal:

- replace the current audit-only subsystem with a real orchestration layer

Tickets:

- [x] `VGR-8001` Define agent request/response contracts
- [x] `VGR-8002` Persist audit data instead of keeping it transient only
- [x] `VGR-8003` Add plan/apply separation
- [x] `VGR-8004` Add permission gates around write/apply actions
- [x] `VGR-8005` Add inspection tools for supported descriptors/contracts
- [x] `VGR-8006` Add partial-apply recovery flows
- [x] `VGR-8007` Add provenance/session correlation IDs
- [x] `VGR-8008` Keep agent orchestration separate from deterministic compile/execute logic

Acceptance criteria:

- AI remains optional and non-authoritative
- all apply operations are auditable and replayable

Session note for `VGR-8001` (2026-03-07):

- The agent module now defines explicit `PlanDocument` and `ApplyCommandPlan` request envelopes plus typed response and audit-entry contracts in `FVergilAgentRequest`, `FVergilAgentResponse`, and `FVergilAgentAuditEntry`, instead of treating agent work as only an unstructured summary blob plus transient state string.
- The new versioned inspection formats are `Vergil.AgentRequest`, `Vergil.AgentResponse`, and `Vergil.AgentAuditEntry`, each at version `1`, with deterministic JSON and human-readable descriptions exposed through both the namespace helpers and `UVergilAgentSubsystem`.
- `UVergilAgentSubsystem::RecordAuditEntry(...)` now normalizes missing response request ids, missing response operations, and missing UTC timestamps before appending the transient audit trail, and `Vergil.Scaffold.AgentRequestResponseContracts` covers request/response/audit inspection parity plus audit normalization.

Session note for `VGR-8002` (2026-03-07):

- `UVergilAgentSubsystem` now persists normalized audit entries to `Saved/Vergil/AgentAuditTrail.json` instead of keeping them in transient memory only, and it exposes `GetAuditTrailPersistencePath()`, `FlushAuditTrailToDisk()`, and `ReloadAuditTrailFromDisk()` for explicit operational control.
- The persisted on-disk wrapper is versioned as `Vergil.AgentAuditLog` `v1`, so future agent audit-log changes have a dedicated migration/versioning boundary separate from the request/response/audit-entry inspection formats.
- `Vergil.Scaffold.AgentAuditPersistence` now covers disk write, explicit reload, corruption handling, and clear/delete behavior while preserving any pre-existing audit-log file around the automation run.

Session note for `VGR-8003` (2026-03-07):

- `UVergilAgentSubsystem` now executes real `PlanDocument` and `ApplyCommandPlan` requests through `ExecuteRequest(...)` instead of only exposing inspection and persistence helpers, while still keeping the deterministic planning/apply logic in the existing editor subsystem.
- `PlanDocument` now resolves the target Blueprint from the explicit request path, runs a dry-run compile only, and records the normalized read-only request plus response in the persisted audit trail. `ApplyCommandPlan` now replays only the explicit provided command plan after its expected normalized fingerprint matches, so apply stays a separate reviewed phase.
- `UVergilAgentSubsystem::MakeApplyRequestFromPlan(...)` now packages the reviewed normalized plan plus fingerprint into the second-phase apply request, and `Vergil.Scaffold.AgentPlanApplySeparation` covers dry-run planning, rejected mismatched apply, successful explicit apply, and per-phase audit entries.

Session note for `VGR-8004` (2026-03-07):

- The agent request contract now carries explicit write approval metadata through `FVergilAgentRequestContext.WriteAuthorization`, and `Vergil.AgentRequest` advanced to version `2` so audited apply requests can record who approved a write plus the approval note.
- `UVergilDeveloperSettings` now exposes `AgentWritePermissionPolicy` with `AllowAll`, `RequireExplicitApproval`, and `DenyAll` modes, and `UVergilAgentSubsystem::ExecuteApplyRequest(...)` rejects blocked writes before editor execution while still auditing the rejected request.
- `Vergil.Scaffold.AgentRequestResponseContracts`, `Vergil.Scaffold.AgentPlanApplySeparation`, and the new `Vergil.Scaffold.AgentWritePermissionGates` coverage now verify request-format inspection, explicit approval replay, permission-denied rejection, and deny-all policy behavior.

Session note for `VGR-8005` (2026-03-06):

- The compiler module now exposes a code-backed `FVergilSupportedContractManifest` plus `Vergil::GetSupportedContractManifest()`, `DescribeSupportedContractManifest()`, and `SerializeSupportedContractManifest(...)`, so the current supported contract surface is inspectable without scraping markdown.
- `UVergilAgentSubsystem` now exposes read-only `InspectSupportedContracts()`, `InspectSupportedDescriptorContracts()`, `InspectSupportedContractsAsJson()`, and `DescribeSupportedContracts()` helpers for the current document fields, target graphs, metadata keys, type categories, command types, and node-descriptor table.
- `Vergil.Scaffold.SupportedContractInspection` now covers the structured manifest, descriptor-table inspection, and deterministic JSON/summary output.

Session note for `VGR-8006` (2026-03-07):

- Failed apply execution now records partial-apply recovery state in `FVergilCompileResult.Statistics.TransactionAudit`, including whether recovery was required, whether Vergil attempted rollback through its recorded editor transaction, whether that rollback succeeded, and the failure snapshot captured before recovery.
- `FVergilCommandExecutor::Execute(...)` now automatically undoes its own failed Vergil transaction with `bCanRedo=false` when the latest undo entry still matches the recorded apply transaction, so failed apply requests no longer silently leave half-applied Blueprint mutations behind.
- `Vergil.Scaffold.PartialApplyRecovery` and `Vergil.Scaffold.AgentPartialApplyRecovery` now cover both direct editor execution and audited agent apply requests, proving a runtime failure after one successful mutation is rolled back and reported through the inspection/audit surface.

Session note for `VGR-8007` (2026-03-07):

- `FVergilAgentRequestContext` and `FVergilAgentResponse` now carry explicit `SessionId` and `ParentRequestId` fields alongside the per-request `RequestId`, so plan/apply orchestration can correlate one interaction end-to-end while still preserving request lineage for audited replay.
- The versioned inspection payloads advanced to `Vergil.AgentRequest` `v3` plus `Vergil.AgentResponse` and `Vergil.AgentAuditEntry` `v2`, with deterministic JSON and human-readable descriptions now surfacing the new provenance ids through both the namespace helpers and `UVergilAgentSubsystem`.
- `UVergilAgentSubsystem::ExecuteRequest(...)` now synthesizes missing session ids from the normalized request id, `MakeApplyRequestFromPlan(...)` inherits the plan session id and stamps the plan request id as the default apply parent id, and audit recording normalizes missing response correlation ids before persistence. `Vergil.Scaffold.AgentRequestResponseContracts`, `Vergil.Scaffold.AgentAuditPersistence`, and `Vergil.Scaffold.AgentPlanApplySeparation` now cover the full lineage behavior.

Session note for `VGR-8008` (2026-03-07):

- Shared deterministic execution helpers now live in the editor module through `VergilEditorExecutionUtils`, covering Blueprint-reference normalization, Blueprint resolution from package or object paths, and command-plan preparation/fingerprinting before direct execution.
- `UVergilEditorSubsystem::ExecuteCommandPlan(...)` now routes through that shared command-plan preparation path, so direct editor execution and agent-driven apply replay use the same normalization and target-graph inference boundary instead of each copy reshaping command plans independently.
- `UVergilAgentSubsystem` now limits itself to request orchestration, permission checks, provenance/audit handling, and phase-specific error reporting while delegating Blueprint-path normalization and command-plan preparation back to the shared editor-layer utility surface. `Vergil.Scaffold.AgentPlanApplySeparation` now also covers object-path normalization plus reordered-plan normalization through `MakeApplyRequestFromPlan(...)`.

## Milestone 9: Studio-Grade Release Hardening
Goal:

- meet the reliability bar for team and CI use

Tickets:

- [x] `VGR-9001` Add golden asset tests
- [x] `VGR-9002` Add PIE runtime validation suites
- [x] `VGR-9003` Add large-graph performance benchmarks
- [x] `VGR-9004` Add schema migration tests
- [x] `VGR-9005` Add source-control diff tests
- [x] `VGR-9006` Add crash/recovery tests around compile/apply
- [x] `VGR-9007` Add semantic versioning and migration docs
- [x] `VGR-9008` Add extension docs for custom handlers
- [x] `VGR-9009` Add CI pipelines for build, headless automation, golden tests, and perf smoke
- [x] `VGR-9010` Add runtime-safe automation fixtures

Acceptance criteria:

- CI catches regressions before manual testing
- persisted whole-asset golden snapshots catch supported authoring regressions in reviewed fixture files
- migrations are versioned and tested
- performance is measured on large graphs

Session note for `VGR-9001` (2026-03-07):

- `Vergil.Scaffold.GoldenAssetSnapshot` now authors a persisted `/Game/Tests/BP_VergilGoldenAsset` Blueprint across the supported milestone-4 whole-asset surface, saves and reloads it through Unreal's package path, native-compiles the reloaded asset, and compares a deterministic plain-text snapshot against the checked-in fixture at `Plugins/Vergil/Tests/GoldenAssets/BP_VergilGoldenAsset.txt`.
- The golden snapshot currently covers Blueprint metadata, variable defaults plus metadata, function and macro signatures, component hierarchy plus template properties, implemented interfaces, class defaults, the primary event graph, the construction script, and dry-run command-plan fingerprints after reload. On mismatch, the test writes the actual snapshot to `Saved/Vergil/GoldenAssets/BP_VergilGoldenAsset.actual.txt`.
- `mcp__tensai__build_project` succeeded in `Development`, `Vergil.Scaffold.GoldenAssetSnapshot` passed repeatedly with stable output, and the full headless `Vergil.Scaffold.*` suite re-verified cleanly at `98/98` in this workspace.

Session note for `VGR-9002` (2026-03-07):

- `Vergil.Scaffold.PIERuntime.FVergilPIERuntimeValidationTests.EventFlowRuntimeValidation` and `WorldMutationRuntimeValidation` now provide dedicated PIE-world validation under the existing `Vergil.Scaffold` automation surface through Unreal's native `CQTest` `FMapTestSpawner` harness instead of editor-graph-only assertions.
- The event-flow suite compiles a transient Blueprint entirely through Vergil's document model, then proves `K2.Delay`, `K2.Call.K2_SetTimer`, `K2.CustomEvent.*`, `K2.BindDelegate.*`, and `K2.CallDelegate.*` produce real runtime side effects inside PIE by waiting on authored Blueprint state.
- The world-mutation suite compiles a second transient Blueprint and proves `K2.AddComponentByClass` plus `K2.SpawnActor` create real runtime objects in the PIE world, with the authored Blueprint instance retaining the created component and spawned actor references for inspection.
- `Build.bat` `Development` succeeded, targeted `Vergil.Scaffold.PIERuntime` passed `2/2`, and the full headless `Vergil.Scaffold.*` suite re-verified cleanly at `99/99` with zero Vergil, Blueprint, or automation warnings in this workspace.

Session note for `VGR-9003` (2026-03-07):

- `Vergil.Scaffold.LargeGraphBenchmark` now generates large synthetic event-graph documents with repeated variable-get, math-call, variable-set, and authored comment nodes, then measures both dry-run planning and direct command-plan apply on fresh transient Blueprints.
- The benchmark writes a structured JSON summary to `Saved/Vergil/Benchmarks/LargeGraphBenchmarkSummary.json`, and the new `Plugins/Vergil/Tools/Invoke-VergilLargeGraphBenchmarks.ps1` entrypoint copies that summary into the dedicated CI artifact directory under `Saved/Logs/VergilCI/Benchmarks/`.
- `Invoke-VergilCILane.ps1`, `.github/workflows/vergil-ci.yml`, and `README.md` now expose a first-class `Benchmarks` lane instead of the temporary `PerfSmoke` guard, while keeping `PerfSmoke` as a deprecated local alias that forwards to the real benchmark lane.

Session note for `VGR-9005` (2026-03-07):

- `Vergil.Scaffold.SourceControlDiff` now authors two persisted revisions of the same `/Game/Tests/BP_VergilGoldenAssetSourceControlDiff` Blueprint package path, saves and reloads each revision through Unreal's package path, native-compiles both reloaded revisions, and compares a deterministic `UKismetStringLibrary::DiffString(...)` output against the checked-in fixture at `Plugins/Vergil/Tests/GoldenAssets/BP_VergilGoldenAssetSourceControlDiff.diff.txt`.
- The source-control diff fixture currently proves reviewed persisted changes across Blueprint metadata, variable defaults plus metadata, component transform and template properties, class defaults, primary event-graph layout, construction-script layout, and the resulting dry-run command-plan fingerprints.
- The same test also cross-checks those persisted review-line changes against `Vergil.DocumentDiff`, asserting the update remains a modified-only canonical document diff with the expected metadata, variable, component, class-default, and node-position paths.
- `Build.bat` `Development` succeeded, targeted `Vergil.Scaffold.SourceControlDiff` passed `1/1`, and the full headless `Vergil.Scaffold.*` suite re-verified cleanly at `100/100` with zero Vergil, Blueprint, or automation warnings in this workspace.

Session note for `VGR-9006` (2026-03-07):

- `Vergil.Scaffold.CompileApplyRecoveryRoundtrip` now authors a real persisted `/Game/Tests/BP_CompileApplyRecovery_*` Blueprint baseline through the public compile/apply surface, then compiles a second update plan, forces a late apply failure with an injected invalid class-default command, and verifies Vergil's transaction recovery rolls the asset back before save.
- The same regression explicitly saves and reloads the recovered Blueprint package after the failed apply, proving the rolled-back baseline description and stable variable survive the restart boundary while the transient failed-apply variable never persists to disk or the recompiled generated class.
- `Build.bat` `Development` succeeded, targeted `CompileApplyRecoveryRoundtrip` passed `1/1`, and the full headless `Vergil.Scaffold.*` suite re-verified cleanly at `101/101` with zero Vergil, Blueprint, or automation warnings in this workspace.

Session note for `VGR-9007` (2026-03-06):

- `Vergil::GetSemanticVersionString()` plus `Vergil::PluginDescriptorVersion` now make the plugin release version a code-backed surface instead of a markdown-only convention, and `Vergil.Scaffold.VersioningAndMigrationContracts` verifies those values stay aligned with `Vergil.uplugin`.
- `Vergil::GetSupportedSchemaMigrationPaths()` now exposes the exact forward migration steps implemented by the model layer, and `FVergilSupportedContractManifest` now reports plugin semantic version, descriptor version, and supported schema migration paths for agent/tool inspection.
- `VERSIONING.md`, `README.md`, and `SUPPORTED_DESCRIPTOR_CONTRACTS.md` now document semantic-versioning policy, migration rules, and the release-update checklist for schema, command-plan, and inspection-manifest version changes.

Session note for `VGR-9008` (2026-03-07):

- `EXTENDING_NODE_HANDLERS.md` now documents the supported custom-handler workflow around `IVergilNodeHandler` and `FVergilNodeRegistry`, including how new families should choose between the shared generic node spawner, a specialized handler, direct lowering, or an intentionally unsupported manifest row.
- `README.md` and `SUPPORTED_DESCRIPTOR_CONTRACTS.md` now link that guide back to the code-backed node-support matrix and the existing node-lowering contracts, so extension work has one explicit checklist covering manifest updates, earlier-pass normalization, executor integration, diagnostics, and automation.
- `Vergil.Scaffold.SupportedNodeContractDocs` re-verified cleanly after the doc updates in this workspace.

Session note for `VGR-9009` (2026-03-07):

- Repo-level GitHub Actions coverage now exists in `.github/workflows/vergil-ci.yml`, targeting a self-hosted Windows Unreal runner and reusing checked-in PowerShell entrypoints instead of hardcoding engine commands directly into the workflow.
- `Plugins/Vergil/Tools/Invoke-VergilProjectBuild.ps1` now builds the `TensAiExampleEditor` target through `UnrealBuildTool.exe`, `Invoke-VergilCILane.ps1` exposes explicit `Build`, `HeadlessAutomation`, `GoldenTests`, `PIRuntime`, `PerfSmoke`, and `All` lanes, and the workflow uploads `Saved/Logs/VergilCI` plus any saved golden-artifact mismatches on every run.
- The current `PerfSmoke` lane time-bounds representative persisted and PIE regressions (`GoldenAssetSnapshot`, `SourceControlDiff`, `CompileApplyRecoveryRoundtrip`, and `PIERuntime`) as a release-hardening guard until `VGR-9003` lands dedicated large-graph benchmarks.

Session note for `VGR-9004` (2026-03-07):

- `Vergil.Scaffold.LegacySchemaExecutionCoverage` now iterates every supported legacy starting schema and proves representative legacy documents survive migration, compile, and apply through the current `UE_5.7` editor pipeline.
- The release-hardening migration bar now covers three layers: model-level helper migration, compiler-pass migration, and end-to-end legacy execution after migration.
- `README.md` and `VERSIONING.md` now explicitly call out that supported migration paths should stay aligned with end-to-end execution coverage, not just helper or planning coverage.

Session note for `VGR-9010` (2026-03-07):

- The shared automation interface fixture `UVergilAutomationTestInterface` now lives in the runtime `VergilCore` module instead of the editor-only `VergilAutomation` module, so runtime Blueprint compilation no longer rejects interface-invocation coverage as editor-only.
- The fixture also now marks `VergilAutomationInterfacePing` as explicitly impure for Blueprint graph authoring, matching the exec-pin interface call/message execution flow covered by the scaffold tests.
- `Vergil.Scaffold.InterfaceInvocationExecution` now runs against a runtime-safe interface owner, and the full `Vergil.Scaffold.*` suite continues to pass after the fixture move.

## Critical Path
The main dependency chain is:

1. canonical model expansion
2. command surface expansion
3. compiler pass hardening
4. asset authoring
5. K2 breadth expansion

If those are weak, later coverage work will turn into one-off patches.

## Recommended Next Sprint
The current roadmap ticket list is complete. Add a new ticket only when a new requirement or missing prerequisite shows up.

## Definition Of Complete
Vergil should only be considered complete when:

- one document can define a full Blueprint asset
- compile/apply is deterministic
- all writes go through explicit commands
- unsupported cases fail explicitly
- supported K2 families are documented and measured
- headless automation and PIE validation are in CI
- agent orchestration is optional, audited, and non-authoritative
