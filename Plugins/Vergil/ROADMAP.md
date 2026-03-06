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
- `VGR-3008` Split layout/comment work into explicit post-passes
- `VGR-3009` Add compile statistics and richer result metadata
- `VGR-3010` Guarantee dry-run and apply share the same planning path

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
- `VGR-4009` Add save/reload/compile roundtrip tests

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

- `UVergilEditorSubsystem` now exposes `CompileDocumentToGraph(...)`, which forwards the requested target graph into `FVergilCompileRequest` while preserving the existing `CompileDocument(...)` default to `EventGraph`.
- The command executor now resolves `UserConstructionScript` through Unreal's dedicated construction-script graph helpers, creates it through the construction-script utility path when required, and reuses the graph's existing function-entry node for authored `K2.Event.UserConstructionScript` entries instead of spawning a regular override event node.
- New `Vergil.Scaffold.ConstructionScriptAuthoringExecution` coverage exists alongside the earlier model/planning tests, and `mcp__tensai__build_project` plus the documented headless scaffold runner re-verified cleanly in this workspace.

Acceptance criteria:

- Vergil can generate a usable Actor Blueprint with variables, class defaults, components, functions, dispatchers, construction script, and event graph from one document

## Milestone 5: Core K2 Coverage
Goal:

- cover the major general-purpose Blueprint node families

Tickets:

- `VGR-5001` Complete pure/impure call parity
- `VGR-5002` Add remaining variable node variants
- `VGR-5003` Add common macro-instance families such as `DoOnce` and `FlipFlop`
- `VGR-5004` Add deterministic `SpawnActor`
- `VGR-5005` Add timer families beyond current delegate-driven coverage
- `VGR-5006` Add more flow-control families
- `VGR-5007` Add select/switch diagnostics for unsupported type combinations
- `VGR-5008` Document all supported node contracts

Acceptance criteria:

- common gameplay Blueprints can be authored without manual graph cleanup
- every supported family has headless automation coverage

## Milestone 6: Advanced K2 Coverage
Goal:

- broaden toward complete engine-facing Blueprint authoring

Tickets:

- `VGR-6001` Add component-related nodes
- `VGR-6002` Add interface call/message nodes
- `VGR-6003` Add object/class/soft-reference families
- `VGR-6004` Add async task node families where deterministic setup is possible
- `VGR-6005` Add specialized handlers for nodes that cannot use the generic path
- `VGR-6006` Add a generic node-spawner path for arbitrary supported `UK2Node` classes
- `VGR-6007` Build an explicit support matrix of generic vs specialized coverage
- `VGR-6008` Add unsupported-node reporting that enumerates exact missing families

Acceptance criteria:

- supported vs unsupported coverage is measurable
- non-trivial K2 families are handled by either a generic spawner path or an explicit specialized handler

## Milestone 7: Editor Tooling
Goal:

- make Vergil inspectable and practical for real users

Tickets:

- `VGR-7001` Add document/command/diagnostic inspector tooling
- `VGR-7002` Add deterministic auto-layout pass API
- `VGR-7003` Add explicit comment generation pass API
- `VGR-7004` Add document diff and command-plan preview tooling
- `VGR-7005` Add stronger undo/redo transaction auditing
- `VGR-7006` Expand developer settings for compiler/layout/validation behavior

Acceptance criteria:

- users can inspect planned mutations before apply
- layout/comments remain optional post-passes

## Milestone 8: Real Agent Layer
Goal:

- replace the current audit-only subsystem with a real orchestration layer

Tickets:

- `VGR-8001` Define agent request/response contracts
- `VGR-8002` Persist audit data instead of keeping it transient only
- `VGR-8003` Add plan/apply separation
- `VGR-8004` Add permission gates around write/apply actions
- `VGR-8005` Add inspection tools for supported descriptors/contracts
- `VGR-8006` Add partial-apply recovery flows
- `VGR-8007` Add provenance/session correlation IDs
- `VGR-8008` Keep agent orchestration separate from deterministic compile/execute logic

Acceptance criteria:

- AI remains optional and non-authoritative
- all apply operations are auditable and replayable

## Milestone 9: Studio-Grade Release Hardening
Goal:

- meet the reliability bar for team and CI use

Tickets:

- `VGR-9001` Add golden asset tests
- `VGR-9002` Add PIE runtime validation suites
- `VGR-9003` Add large-graph performance benchmarks
- `VGR-9004` Add schema migration tests
- `VGR-9005` Add source-control diff tests
- `VGR-9006` Add crash/recovery tests around compile/apply
- `VGR-9007` Add semantic versioning and migration docs
- `VGR-9008` Add extension docs for custom handlers
- `VGR-9009` Add CI pipelines for build, headless automation, golden tests, and perf smoke

Acceptance criteria:

- CI catches regressions before manual testing
- migrations are versioned and tested
- performance is measured on large graphs

## Critical Path
The main dependency chain is:

1. canonical model expansion
2. command surface expansion
3. compiler pass hardening
4. asset authoring
5. K2 breadth expansion

If those are weak, later coverage work will turn into one-off patches.

## Recommended Next Sprint
Best next sprint from the current baseline:

1. `VGR-3008`
2. `VGR-4009`
3. `VGR-8005`
4. `VGR-9007`
5. `VGR-3010`

This keeps pressure on the remaining post-pass and dry-run pipeline hardening, remaining asset authoring, and release/documentation work now that the compiler has migration, semantic validation, symbol resolution, type resolution, dedicated node lowering, compile-time connection legality, and a dedicated post-compile finalize stage.

## Definition Of Complete
Vergil should only be considered complete when:

- one document can define a full Blueprint asset
- compile/apply is deterministic
- all writes go through explicit commands
- unsupported cases fail explicitly
- supported K2 families are documented and measured
- headless automation and PIE validation are in CI
- agent orchestration is optional, audited, and non-authoritative
