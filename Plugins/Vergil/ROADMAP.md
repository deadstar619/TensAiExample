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

- `VGR-1001` Add Blueprint-level metadata
- [x] `VGR-1002` Add variable definitions with type, flags, metadata, and defaults
- [x] `VGR-1003` Add function definitions with purity, access, inputs, and outputs
- [x] `VGR-1004` Add macro graph definitions
- [x] `VGR-1005` Add component definitions with parent/attach/transform/template properties
- [x] `VGR-1006` Add implemented interface definitions
- [x] `VGR-1007` Add class default definitions
- [x] `VGR-1008` Add construction script graph definition
- `VGR-1009` Add schema migration helpers
- `VGR-1010` Extend structural validation for all new model types

Session note for `VGR-1008` (2026-03-06):

- `FVergilGraphDocument` now carries dedicated `ConstructionScriptNodes` and `ConstructionScriptEdges` surfaces with structural validation coverage and explicit planning support when `TargetGraphName` is `UserConstructionScript`.
- New `Vergil.Scaffold.ConstructionScriptDefinitionModel` and `Vergil.Scaffold.ConstructionScriptDefinitionPlanning` automation coverage exists for the new canonical model surface.

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
- `VGR-2002` Add command validation prior to execution
- `VGR-2003` Add deterministic command ordering rules
- `VGR-2004` Add command debug printing/serialization

Acceptance criteria:

- asset-level mutations no longer require ad hoc editor-side logic outside commands
- commands can be logged, inspected, and replayed deterministically

## Milestone 3: Harden The Compiler Pipeline
Goal:

- move from a scaffold compiler to a real deterministic compiler

Tickets:

- `VGR-3001` Add schema migration pass
- `VGR-3002` Add semantic validation pass
- `VGR-3003` Add symbol resolution pass
- `VGR-3004` Add type resolution and wildcard resolution pass
- `VGR-3005` Add node lowering pass
- `VGR-3006` Add connection legality validation pass
- `VGR-3007` Add post-compile finalize pass
- `VGR-3008` Split layout/comment work into explicit post-passes
- `VGR-3009` Add compile statistics and richer result metadata
- `VGR-3010` Guarantee dry-run and apply share the same planning path

Acceptance criteria:

- failures identify pass, node, and cause
- wildcard-heavy nodes resolve without heuristics
- dry-run and apply produce identical command plans

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
- `VGR-4008` Implement construction script authoring
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

1. `VGR-4008`
2. `VGR-1009`
3. `VGR-2002`
4. `VGR-1001`
5. `VGR-3001`
6. `VGR-4009`

This moves Vergil from explicit command coverage toward document-driven asset authoring.

## Definition Of Complete
Vergil should only be considered complete when:

- one document can define a full Blueprint asset
- compile/apply is deterministic
- all writes go through explicit commands
- unsupported cases fail explicitly
- supported K2 families are documented and measured
- headless automation and PIE validation are in CI
- agent orchestration is optional, audited, and non-authoritative
