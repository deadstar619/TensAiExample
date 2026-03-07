# Vergil

Vergil is a clean-room plugin scaffold for deterministic Blueprint authoring.

## Module layout

- `VergilCore`: logging, versioning, diagnostics.
- `VergilBlueprintModel`: canonical graph document and serializable node/pin/edge data.
- `VergilBlueprintCompiler`: registry-backed compiler surface and validation pipeline entry point.
- `VergilEditor`: editor subsystem and developer settings.
- `VergilAgent`: agent-facing orchestration, audit trail, and supported-contract inspection.
- `VergilAutomation`: automation coverage for the scaffold and future compiler passes.

## Design rules

- AI produces documents or commands, never direct hidden editor mutations.
- Compiler behavior must be deterministic and testable.
- Editor-only functionality stays out of runtime-facing modules.
- Unsupported features must fail explicitly with diagnostics.

## Headless scaffold automation

Run the current scaffold suite headlessly with one documented command:

```powershell
powershell -ExecutionPolicy Bypass -File .\Plugins\Vergil\Tools\Invoke-VergilScaffoldAutomation.ps1
```

Useful variants:

- Run one test: `powershell -ExecutionPolicy Bypass -File .\Plugins\Vergil\Tools\Invoke-VergilScaffoldAutomation.ps1 -TestFilter 'Vergil.Scaffold.TimerDelegateExecution'`
- Summarize an existing log without re-running: `powershell -ExecutionPolicy Bypass -File .\Plugins\Vergil\Tools\Get-VergilDiagnosticsSummary.ps1 -LogPath .\Saved\Logs\VergilAutomation_All.log`

The runner writes a headless automation log and prints a compile/apply/test summary from that log.

## Versioning and migration

- See [VERSIONING.md](VERSIONING.md) for the current semantic-versioning policy, schema-migration policy, and release-update checklist.
- The current plugin semantic version is `0.1.0`, the document schema version is `3`, and supported document migrations are currently `1->2` and `2->3`.
- `UVergilAgentSubsystem` contract inspection now exposes the plugin semantic version, plugin descriptor version, schema version, command-plan format version, and supported schema migration paths from code-backed data.
- The current inspection JSON formats are `Vergil.GraphDocument`, `Vergil.DocumentDiff`, `Vergil.Diagnostics`, `Vergil.CompileResult`, `Vergil.CommandPlanPreview`, and `Vergil.ReflectionSymbol`, each at version `1`, plus `Vergil.ReflectionDiscovery` at version `1`, `Vergil.AgentRequest` at version `3`, and `Vergil.AgentResponse` plus `Vergil.AgentAuditEntry` at version `2`. Persisted agent audit logs currently use the separate on-disk wrapper format `Vergil.AgentAuditLog` at version `1`.

## Current supported contracts

- The scaffold only supports the document fields and descriptor families documented in [SUPPORTED_DESCRIPTOR_CONTRACTS.md](SUPPORTED_DESCRIPTOR_CONTRACTS.md). The supported-node contract table in that file is kept aligned with the code-backed manifest by `Vergil.Scaffold.SupportedNodeContractDocs`.
- `UVergilAgentSubsystem` now exposes read-only supported-contract inspection helpers backed by code, so callers can inspect the current plugin semantic version, schema migration paths, supported document fields, target graphs, metadata keys, command types, the current node-support matrix summary, and descriptor contracts without scraping markdown.
- `UVergilEditorSubsystem` and `UVergilAgentSubsystem` now also expose read-only inspection helpers for command plans, graph documents, document diffs, diagnostics, compile results, and command-plan previews, including deterministic JSON exports for the document-diff and preview surfaces.
- `UVergilEditorSubsystem` and `UVergilAgentSubsystem` now also expose read-only runtime/reflection inspection and discovery helpers for classes, structs, and enums through simple string queries, with versioned `Vergil.ReflectionSymbol` and `Vergil.ReflectionDiscovery` payloads that remain usable from a generic Python bridge call.
- Deterministic component-related K2 node support now exists for `K2.AddComponentByClass`, `K2.GetComponentByClass`, `K2.GetComponentsByClass`, `K2.FindComponentByTag`, and `K2.GetComponentsByTag` through a fixed `ComponentClassPath` metadata contract rather than authored dynamic class pins.
- Deterministic Blueprint-interface invocation support now exists for `K2.InterfaceCall.*` and `K2.InterfaceMessage.*` through an explicit `InterfaceClassPath` metadata contract, lowering to the real `UE_5.7` `UK2Node_CallFunction` and `UK2Node_Message` surfaces instead of treating interface messages as generic calls.
- Deterministic generic Blueprint async-action support now exists for `K2.AsyncAction.*` through an explicit `FactoryClassPath` metadata contract plus factory-function descriptor suffix, lowering to the real `UE_5.7` `UK2Node_AsyncAction` surface while leaving dedicated async nodes for specialized handlers.
- Deterministic specialized async-task support now also exists for `K2.AIMoveTo` and `K2.PlayMontage`, each lowering through a dedicated `UE_5.7` handler to the real node class instead of trying to force these `HasDedicatedAsyncNode` families through the generic async-action path.
- Deterministic object/class/soft-reference K2 support now exists for `K2.ClassCast`, `K2.GetClassDefaults`, `K2.LoadAsset`, `K2.LoadAssetClass`, `K2.LoadAssets`, and `K2.ConvertAsset` through explicit `TargetClassPath`, `ClassPath`, and `AssetClassPath` metadata contracts instead of authored dynamic type pins. `K2.LoadAsset` and `K2.LoadAssets` keep the engine's native `UObject` result families, while `K2.LoadAssetClass` preserves typed class output.
- Supported `UK2Node`-backed add-node execution now routes through a shared generic node-spawner path with per-family setup hooks, covering straightforward node classes such as `K2.Self`, `K2.Branch`, `K2.Sequence`, `K2.Reroute`, `K2.Select`, `K2.Switch*`, `K2.FormatText`, `K2.MakeArray`, `K2.MakeSet`, `K2.MakeMap`, `K2.SpawnActor`, `K2.AddComponentByClass`, `K2.ClassCast`, `K2.GetClassDefaults`, `K2.LoadAsset*`, `K2.ConvertAsset`, `K2.AsyncAction.*`, `K2.AIMoveTo`, and `K2.PlayMontage`.
- Unsupported generic and dedicated async-node failures now name the exact unsupported node-support family from the code-backed manifest instead of falling back to anonymous generic errors.
- `UVergilAgentSubsystem` now also exposes typed request/response/audit inspection helpers for the real agent-layer contract surface. `PlanDocument` requests wrap the canonical document plus target graph and compile flags, `ApplyCommandPlan` requests wrap an explicit command plan plus target Blueprint path and expected fingerprint, `FVergilAgentRequestContext` now carries `RequestId`, `SessionId`, `ParentRequestId`, and `WriteAuthorization` for provenance plus audited apply approval, `UVergilDeveloperSettings::AgentWritePermissionPolicy` controls whether that approval is required or all agent writes are denied, responses wrap the existing `FVergilCompileResult` instead of inventing a second result shape, and `ExecuteRequest(...)` now keeps planning read-only while requiring an explicit second-phase apply request before any mutation runs.
- `UVergilAgentSubsystem` now persists its normalized audit trail to `Saved/Vergil/AgentAuditTrail.json`, exposes `GetAuditTrailPersistencePath()`, `FlushAuditTrailToDisk()`, and `ReloadAuditTrailFromDisk()`, and keeps the persisted wrapper versioned as `Vergil.AgentAuditLog` `v1`.
- Document-authored Blueprint metadata is now part of that supported contract surface for `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories`.
- Document-authored member variables are now part of that supported contract surface, including type/flag/metadata/default authoring.
- Document-authored function definitions now lower into Blueprint function graph/signature authoring for function name, purity, access, and typed inputs/outputs. Function body authoring remains future work.
- Document-authored macro definitions now lower into Blueprint macro graph/signature authoring for exec/data inputs and outputs. Macro body authoring remains future work.
- Document-authored component definitions now lower into Blueprint component hierarchy authoring for component creation, parent attachment, attach sockets, template properties, and relative transforms.
- Document-authored implemented interfaces now lower into Blueprint interface application for authored interface class paths.
- Document-authored class defaults now lower into post-compile Blueprint class default writes for authored property names and serialized values.
- Document-authored construction script definitions now lower into construction-script graph authoring when the compile target graph is `UserConstructionScript`.
- The current whole-asset authoring surface now has persisted save/reload/native-compile roundtrip coverage on a real `/Game/Tests/...` Blueprint package.
- Legacy document schemas now have explicit model-level forward-migration helpers.
- Structural validation now also rejects unsupported Blueprint metadata keys, empty variable metadata keys, whitespace-only typed object/class paths, invalid dispatcher parameter type shapes, and graph edges that reference pins outside their declared source/target nodes.
- Direct command plans are now preflight-validated before any editor transaction starts, so malformed plans fail with diagnostics and execute zero commands.
- Select/switch authoring now also emits dedicated diagnostics for unsupported `UE_5.7` type combinations, including `K2.Select` index/value mismatches and invalid selection-pin types on `K2.SwitchInt`, `K2.SwitchString`, and `K2.SwitchEnum`.
- Compiler-emitted and direct command plans are now normalized into deterministic execution-phase order before apply/logging.
- Command plans now have stable debug-print strings plus versioned JSON serialization/deserialization for inspection and replay.
- Direct command-plan execution now supports explicit Blueprint metadata writes, function/macro graph creation, component creation/attachment/property mutation, interface application, class default writes, member renames, node removal/movement, and explicit Blueprint compile commands.
- Generic fallback planning is not a guarantee that execution exists. The contract document is the source of truth for what the current scaffold actually supports.

## Current baseline

- Milestone 0 is complete.
- `VGR-1001`, `VGR-1002`, `VGR-1003`, `VGR-1004`, `VGR-1005`, `VGR-1006`, `VGR-1007`, `VGR-1008`, `VGR-1009`, `VGR-1010`, `VGR-2001`, `VGR-2002`, `VGR-2003`, `VGR-2004`, `VGR-3001`, `VGR-3002`, `VGR-3003`, `VGR-3004`, `VGR-3005`, `VGR-3006`, `VGR-3007`, `VGR-3008`, `VGR-4001`, `VGR-4002`, `VGR-4003`, `VGR-4004`, `VGR-4005`, `VGR-4006`, `VGR-4007`, `VGR-4008`, `VGR-4009`, `VGR-5001`, `VGR-5002`, `VGR-5003`, `VGR-5004`, `VGR-5005`, `VGR-5006`, `VGR-5007`, `VGR-5008`, `VGR-6001`, `VGR-6002`, `VGR-6003`, `VGR-6004`, `VGR-6005`, `VGR-6006`, `VGR-6007`, `VGR-6008`, `VGR-7001`, `VGR-7002`, `VGR-7003`, `VGR-7004`, `VGR-7005`, `VGR-7006`, `VGR-7007`, `VGR-8001`, `VGR-8002`, `VGR-8003`, `VGR-8004`, `VGR-8005`, `VGR-8006`, `VGR-8007`, `VGR-8008`, `VGR-9004`, `VGR-9007`, and `VGR-9010` are complete.
- Document-authored Blueprint metadata now has structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored member variables now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored function and macro definitions now have structural validation plus deterministic command planning and editor execution for graph/signature creation and updates.
- `K2.Call.*` now resolves document-authored functions, existing Blueprint-local functions, and inherited/native functions with matching pure/impure self-call execution support under `UE_5.7`, and headless coverage now explicitly re-verifies timer-by-function-name helpers plus handle-based timer pause/query helpers through that generic call path.
- `K2.InterfaceCall.*` and `K2.InterfaceMessage.*` now use a fixed `InterfaceClassPath` metadata contract over the real `UE_5.7` interface invocation surfaces, exposing interface-typed call targets plus object-typed message targets while preserving native message-node semantics.
- `K2.AsyncAction.*` now uses a fixed `FactoryClassPath` metadata contract plus factory-function descriptor suffix over the real `UE_5.7` `UK2Node_AsyncAction` surface, and the shared generic node-spawner path now owns the final node instantiation while still excluding hidden pins such as `WorldContextObject` and dedicated async-node families from the authored contract.
- `K2.AIMoveTo` and `K2.PlayMontage` now use dedicated specialized async-task handlers to resolve their factories, then instantiate the real `UE_5.7` `UK2Node_AIMoveTo` and `UK2Node_PlayMontage` surfaces through the shared generic node-spawner path while leaving engine-hidden setup such as `WorldContextObject` outside the authored contract.
- `K2.ClassCast`, `K2.GetClassDefaults`, `K2.LoadAsset`, `K2.LoadAssetClass`, `K2.LoadAssets`, and `K2.ConvertAsset` now use explicit `TargetClassPath`, `ClassPath`, and `AssetClassPath` metadata contracts over the real `UE_5.7` node families, with the shared generic node-spawner path now handling the final `UK2Node` instantiation for class-default inspection, async load authoring, class-cast execution, and object/class/soft-reference conversion.
- The supported-contract manifest now also exposes an explicit node-support matrix summary plus family rows, so tooling can measure which descriptor families currently route through the shared generic node spawner, an explicit specialized handler, another direct-lowering path, or remain intentionally unsupported.
- Runtime-safe automation fixtures now live in `VergilCore`, so headless end-to-end runtime Blueprint coverage can exercise interface-driven node families without depending on editor-only automation module types.
- `K2.VarGet.*` now supports the full current `UE_5.7` getter surface for supported member variables: pure reads, bool branch getters, and validated object/class/soft-reference getters, with explicit failures for unsupported impure shapes.
- `K2.ForLoop`, `K2.ForLoopWithBreak`, `K2.DoOnce`, `K2.FlipFlop`, `K2.Gate`, and `K2.WhileLoop` now resolve against the matching `UE_5.7` `StandardMacros` graphs during symbol resolution and author deterministically into `UK2Node_MacroInstance` nodes during apply.
- `K2.AddComponentByClass`, `K2.GetComponentByClass`, `K2.GetComponentsByClass`, `K2.FindComponentByTag`, and `K2.GetComponentsByTag` now use a fixed `ComponentClassPath` metadata contract over the real `UE_5.7` node/function surfaces, exposing scene-component-only add-component pins plus conformed typed lookup return pins while intentionally excluding the dynamic `Class` and `ComponentClass` pin paths.
- `K2.SpawnActor` now uses a fixed `ActorClassPath` metadata contract over `UE_5.7` `UK2Node_SpawnActorFromClass`, exposing deterministic spawn pins plus class-specific `ExposeOnSpawn` property pins while intentionally excluding the dynamic `Class` and `WorldContextObject` pin paths. `SpawnTransform` is a required connected input because the engine node expands through by-reference transform calls.
- Document-authored component definitions now have structural validation plus deterministic command planning and editor execution for component creation, attachment, attach sockets, template properties, and relative transforms.
- Document-authored implemented interfaces now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored class defaults now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored construction script definitions now have structural validation, deterministic command planning, editor execution, and headless automation coverage when targeting `UserConstructionScript`.
- Whole-Blueprint authoring now also has persisted save/reload/native-compile roundtrip coverage across the supported milestone-4 surfaces, including metadata, variables, function and macro signatures, components, interfaces, class defaults, the primary event graph, and the construction script.
- Vergil now has an explicit semantic-versioning and migration policy document, and the supported-contract manifest now reports the plugin semantic version, plugin descriptor version, and supported schema migration paths directly from code-backed helpers.
- Schema migration helpers now exist for older documents, and the compiler now runs schema migration as its first pass so supported legacy documents upgrade automatically before validation and planning.
- Every supported legacy schema starting version now also has headless end-to-end execution coverage, proving representative legacy documents survive migration, compile, and apply through the current `UE_5.7` pipeline.
- The compiler now runs a dedicated semantic validation pass before planning, rejecting unsupported compile targets plus invalid known-node descriptor/kind/metadata combinations before command generation.
- The compiler now runs a dedicated symbol resolution pass after semantic validation and before planning, so callable/member references fail explicitly before command generation and inherited owner paths are normalized into planned commands.
- The compiler now runs a dedicated type resolution pass after symbol resolution and before planning, normalizing authored type metadata for definitions, components/interfaces, and typed or wildcard-heavy K2 nodes before command generation.
- The compiler now runs a dedicated node lowering pass after type resolution and before final command planning, so handler-driven `AddNode` and `SetNodeMetadata` commands are emitted once from the normalized working document and node-lowering failures stop with zero planned commands.
- The compiler now runs a dedicated connection-legality pass after node lowering and before final command planning, rejecting impossible source/target pin directions, exec/data mismatches, lowered-pin drift, graph mismatches, and multiply-driven input pins before any plan is returned.
- Select and switch authoring now fail with explicit diagnostics for unsupported `UE_5.7` type combinations instead of surfacing only generic schema-rejection messages during apply.
- The compiler now runs a dedicated post-compile finalize pass after connection legality and before final command planning, so `FinalizeNode` work such as `K2.CreateDelegate.*` is emitted separately from node lowering while preserving deterministic plan ordering.
- The compiler now runs dedicated optional comment and layout post-passes after finalize lowering and before final command planning. Authored comment nodes are emitted only when `bGenerateComments` is true, `FVergilCompileRequest.CommentGeneration` now supplies explicit UE_5.7-backed defaults for comment width, height, font, color, bubble, and move mode, and `bAutoLayout` now emits deterministic `MoveNode` work using `FVergilCompileRequest.AutoLayout` settings for origin, spacing, and comment padding.
- `UVergilDeveloperSettings` now also carries the default compile target graph, full default auto-layout/comment-generation settings, and a strict structural-validation toggle that promotes structural warnings to errors before semantic validation when enabled.
- `UVergilEditorSubsystem` now exposes `MakeDefaultCompileRequest(...)`, `MakeCompileRequest(...)`, and `CompileRequest(...)`, so callers can seed requests from project defaults, inspect or override explicit graph/comment/layout flags, and then run the same deterministic compile/apply path as the simpler document helpers. `CompileDocument(...)` and `PreviewDocument(...)` now use the configured default target graph when no graph is passed explicitly.
- Compile and apply results now carry structured metadata for target graph, requested versus effective schema version, requested auto-layout/comment/apply flags, execution-attempt state, deterministic per-phase command counts, ordered compiler pass records, normalized-plan fingerprints, planning/apply invocation counts, and an explicit before/after undo-redo transaction audit for apply paths.
- Read-only inspection tooling now exists across the editor and agent subsystems for command plans, canonical graph documents, diagnostics, and compile results, with deterministic JSON exports for the document, diagnostics, and compile-result surfaces.
- Read-only document-diff and command-plan-preview tooling now exists across the editor and agent subsystems. `Vergil.DocumentDiff` reports deterministic added/removed/modified paths between two canonical documents, and `Vergil.CommandPlanPreview` captures the authored request document, the effective post-migration working document, the diff between them, and the dry-run compile result so planned mutations can be reviewed before apply.
- Read-only runtime/reflection inspection and discovery tooling now exists across the editor and agent subsystems for loaded classes, structs, and enums, with deterministic JSON exports and short-name/path lookup support intended for generic Python bridge workflows.
- Dry-run compile and compile+apply now route through the same planning helper in the editor subsystem, and apply metadata records when execution consumed the exact returned normalized command plan.
- Structural validation now checks dispatcher parameter type shapes, variable metadata keys, trimmed object/class paths, and graph-edge pin ownership across both graph surfaces.
- Command execution now validates command-plan shape and intra-plan references before opening a transaction, preventing partial mutation from malformed plans.
- Command plans now normalize into deterministic execution-phase order before they are returned or applied, so compile output and direct `ExecuteCommandPlan` replay share the same visible ordering.
- Command plans now also have stable debug printing plus JSON serialization/deserialization, and the editor subsystem can replay serialized command plans directly.
- The explicit editor command surface now covers Blueprint metadata, function graphs, macro graphs, components, interfaces, class defaults, member renames, node moves/removals, and explicit compile commands. Document lowering now exists for Blueprint metadata, variables, dispatchers, function definitions, macro definitions, component hierarchy data, component template properties, class defaults, implemented interfaces, and construction-script graphs; the remaining asset-model slices are still future work.
- The agent layer now has explicit versioned request/response/audit contracts for `PlanDocument` and `ApplyCommandPlan`, backed by deterministic JSON and human-readable inspection helpers on `UVergilAgentSubsystem`, with request/session/parent correlation ids carried through the audited payloads.
- The agent subsystem now persists normalized audit entries to `Saved/Vergil/AgentAuditTrail.json`, so audit history survives editor restarts and can be reloaded explicitly without depending on transient subsystem state.
- The agent subsystem now executes `PlanDocument` and `ApplyCommandPlan` through `ExecuteRequest(...)`, exposes `MakeApplyRequestFromPlan(...)` for explicit plan-to-apply handoff, rejects mismatched command-plan fingerprints before mutation, keeps each phase as its own audited request, normalizes missing session ids from the request id, stamps plan request ids onto apply requests as provenance, and now delegates Blueprint-reference normalization plus command-plan preparation back to shared editor-module utilities so agent orchestration stays distinct from deterministic compile/execute behavior.
- Agent apply now also consults `UVergilDeveloperSettings::AgentWritePermissionPolicy`, requires explicit `WriteAuthorization` approval metadata when the policy demands it, and rejects denied writes before any editor execution while still auditing the rejected request.
- Agent apply now also auto-recovers failed partial mutations when the recorded Vergil transaction can be safely undone, records that recovery outcome in the compile-result transaction audit, and reports rollback status through the audited agent response instead of silently leaving half-applied Blueprint state behind.
- The agent subsystem now also exposes read-only inspection helpers for the code-backed supported-contract manifest, descriptor table, JSON manifest export, and a human-readable summary of the current contract surface.
- `Vergil.Scaffold.*` currently passes headlessly with zero Vergil, Blueprint, or automation warnings.

## Planning

- See `ROADMAP.md` for the tracked implementation roadmap and milestone breakdown.
