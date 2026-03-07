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
- The current supported whole-asset authoring surface is now covered by a persisted save/reload/native-compile roundtrip automation test on a real `/Game/Tests/...` Blueprint package, spanning Blueprint metadata, variables, function and macro signatures, components, interfaces, class defaults, the primary event graph, and the construction script.
- The current schema version is `3`. Older document schemas can be upgraded explicitly through `Vergil::MigrateDocumentSchema(...)` / `Vergil::MigrateDocumentToCurrentSchema(...)`, and the compiler now runs that upgrade path automatically before structural validation and planning.
- The current plugin semantic version is `0.1.0`, and the current supported document migration steps are `1->2` and `2->3`. See [VERSIONING.md](VERSIONING.md) for the release/versioning policy that governs schema, command-plan, and manifest version changes.
- The compiler pipeline now runs schema migration, structural validation, semantic validation, symbol resolution, type resolution, node lowering, connection legality validation, post-compile finalize lowering, comment post-pass lowering, layout post-pass lowering, and then command planning.
- `FVergilCompileRequest.bGenerateComments` now controls whether authored comment nodes are emitted through the dedicated comment post-pass, and `FVergilCompileRequest.CommentGeneration` now carries explicit `UE_5.7`-backed defaults for comment width, height, font size, title color, zoom bubble visibility, bubble coloring, and move mode. `bAutoLayout` plus `AutoLayout` still control deterministic `MoveNode` generation through the dedicated layout post-pass. `AutoLayout` currently supports `Origin`, `HorizontalSpacing`, `VerticalSpacing`, and `CommentPadding`, and `UVergilEditorSubsystem` seeds those values from `UVergilDeveloperSettings`.
- `UVergilEditorSubsystem::MakeCompileRequest(...)` returns the same seeded `FVergilCompileRequest` shape used by the simpler `CompileDocument(...)` helpers, and `CompileRequest(...)` lets callers run that explicit request after tweaking pass settings.
- `FVergilCompileResult` now includes `Statistics` plus ordered `PassRecords`. Compiler-produced results report target graph, requested/effective schema version, requested auto-layout/comment flags, normalized per-phase command counts, plan fingerprints, planning/apply invocation counts, and the last completed or failed compiler pass without requiring log scraping.
- Direct `ExecuteCommandPlan` execution now supports explicit asset-mutation commands for Blueprint metadata, function graphs, macro graphs, components, interfaces, class defaults, member renames, node removal/movement, and explicit blueprint compilation.
- Direct `ExecuteCommandPlan` execution now preflight-validates command-plan shape and intra-plan references before opening an editor transaction.
- Compiler-produced plans and direct `ExecuteCommandPlan` input now normalize into deterministic execution-phase order before validation and apply.
- Command plans now expose stable debug strings plus versioned JSON serialization/deserialization helpers for inspection and replay.
- `Vergil::DescribeGraphDocument(...)` / `SerializeGraphDocument(...)`, `Vergil::DescribeDiagnostics(...)` / `SerializeDiagnostics(...)`, and `Vergil::DescribeCompileResult(...)` / `SerializeCompileResult(...)` now expose the canonical document, diagnostic list, and compile result through stable human-readable descriptions plus deterministic JSON inspection payloads.
- `UVergilEditorSubsystem` now exposes read-only command-plan, document, diagnostic, and compile-result inspection helpers, while `UVergilAgentSubsystem` mirrors those same helpers for tool-facing inspection without requiring callers to scrape logs.
- `Vergil::GetSupportedContractManifest()` now exposes the current supported-contract surface as code-backed data, and `UVergilAgentSubsystem` exposes that manifest through read-only inspection helpers for structured data, descriptor-only inspection, deterministic JSON, and a human-readable summary.
- The real agent-layer contract is now explicit and versioned. `PlanDocument` requests wrap `FVergilGraphDocument` plus target graph and compile flags, `ApplyCommandPlan` requests wrap explicit command plans plus target Blueprint path and expected fingerprint, responses wrap `FVergilCompileResult`, audit entries store the request/response pair plus a UTC timestamp, and the persisted audit-log wrapper stores those normalized entries on disk.
- `UVergilAgentSubsystem::ExecuteRequest(...)` is the supported orchestration entry point for those agent requests. Planning stays read-only, apply requests replay only the explicit provided command plan, and each execution appends an audit entry automatically.

## Inspection manifest contracts

- The current inspection manifest format is `format="Vergil.ContractManifest"` with `version=1`.
- `FVergilSupportedContractManifest` currently reports the plugin descriptor version, plugin semantic version, scaffold schema version, supported schema migration paths, command-plan serialization format/version, supported document fields, supported compile target graphs, supported Blueprint metadata keys, supported logical type categories, supported container types, supported explicit command types, and the supported node-descriptor contract table.
- `FVergilSupportedDescriptorContract` currently reports the descriptor contract string, how it matches authored nodes (`Exact`, `Prefix`, or `NodeKind`), the expected authored node kind label, required metadata keys, supported target graphs, and notes about the current scaffold behavior or limits.
- `UVergilAgentSubsystem::InspectSupportedContracts()` returns the full structured manifest, `InspectSupportedDescriptorContracts()` returns just the descriptor table, `InspectSupportedContractsAsJson()` returns the deterministic JSON export, and `DescribeSupportedContracts()` returns a human-readable summary string.
- The manifest is read-only inspection data. It does not loosen validation or execution behavior; unsupported descriptors outside the documented table still fail explicitly.

## Inspector tooling contracts

- The graph-document inspection format is `format="Vergil.GraphDocument"` with `version=1`, sourced from `Vergil::GetDocumentInspectionFormatName()` and `Vergil::GetDocumentInspectionFormatVersion()`.
- The diagnostics inspection format is `format="Vergil.Diagnostics"` with `version=1`, sourced from `Vergil::GetDiagnosticsInspectionFormatName()` and `Vergil::GetDiagnosticsInspectionFormatVersion()`.
- The compile-result inspection format is `format="Vergil.CompileResult"` with `version=1`, sourced from `Vergil::GetCompileResultInspectionFormatName()` and `Vergil::GetCompileResultInspectionFormatVersion()`.
- The agent request format is `format="Vergil.AgentRequest"` with `version=1`, sourced from `Vergil::GetAgentRequestFormatName()` and `Vergil::GetAgentRequestFormatVersion()`.
- The agent response format is `format="Vergil.AgentResponse"` with `version=1`, sourced from `Vergil::GetAgentResponseFormatName()` and `Vergil::GetAgentResponseFormatVersion()`.
- The agent audit-entry format is `format="Vergil.AgentAuditEntry"` with `version=1`, sourced from `Vergil::GetAgentAuditEntryFormatName()` and `Vergil::GetAgentAuditEntryFormatVersion()`.
- The persisted agent audit-log wrapper currently uses `format="Vergil.AgentAuditLog"` with `version=1` in `Saved/Vergil/AgentAuditTrail.json`.
- `Vergil::DescribeGraphDocument(...)`, `Vergil::DescribeDiagnostics(...)`, `Vergil::DescribeCommandPlan(...)`, and `Vergil::DescribeCompileResult(...)` return stable human-readable inspection strings intended for tooling, logs, and previews.
- `Vergil::SerializeGraphDocument(...)`, `Vergil::SerializeDiagnostics(...)`, and `Vergil::SerializeCompileResult(...)` return deterministic JSON inspection payloads. `Vergil::SerializeCompileResult(...)` nests the current diagnostics and command-plan payloads for the inspected result.
- `UVergilEditorSubsystem` exposes `DescribeCommandPlan()`, `SerializeCommandPlan()`, `DescribeDocument()`, `SerializeDocument()`, `DescribeDiagnostics()`, `SerializeDiagnostics()`, `DescribeCompileResult()`, and `SerializeCompileResult()` as the supported editor-facing inspection surface.
- `UVergilAgentSubsystem` mirrors those read-only helpers through `DescribeCommandPlan()`, `InspectCommandPlanAsJson()`, `DescribeDocument()`, `InspectDocumentAsJson()`, `DescribeDiagnostics()`, `InspectDiagnosticsAsJson()`, `DescribeCompileResult()`, and `InspectCompileResultAsJson()`.
- `Vergil::DescribeAgentRequest(...)` / `SerializeAgentRequest(...)`, `Vergil::DescribeAgentResponse(...)` / `SerializeAgentResponse(...)`, and `Vergil::DescribeAgentAuditEntry(...)` / `SerializeAgentAuditEntry(...)` provide the stable inspection surface for the real agent-layer request/response/audit contracts.
- `UVergilAgentSubsystem` mirrors those agent-contract helpers through `DescribeAgentRequest()`, `InspectAgentRequestAsJson()`, `DescribeAgentResponse()`, `InspectAgentResponseAsJson()`, `DescribeAgentAuditEntry()`, and `InspectAgentAuditEntryAsJson()`.

## Agent request/response contracts

- `EVergilAgentOperation` currently supports `PlanDocument` and `ApplyCommandPlan`.
- `FVergilAgentRequest` wraps `FVergilAgentRequestContext` plus exactly one typed payload selected by `Operation`.
- `FVergilAgentRequestContext` currently carries `RequestId`, `Summary`, `InputText`, and `Tags`.
- `FVergilAgentPlanPayload` currently carries `TargetBlueprintPath`, `Document`, `TargetGraphName`, `bAutoLayout`, and `bGenerateComments`.
- `FVergilAgentApplyPayload` currently carries `TargetBlueprintPath`, `Commands`, and `ExpectedCommandPlanFingerprint`.
- `FVergilAgentRequest::IsWriteRequest()` returns `true` only for `ApplyCommandPlan`, which is the current contract boundary for future permission-gating work.
- `FVergilAgentResponse` currently carries `RequestId`, `Operation`, `State`, `Message`, and `Result`. `Result` stays the existing `FVergilCompileResult` so agent orchestration reuses the same compile/apply diagnostics, plan statistics, and normalized command-plan payload already documented elsewhere.
- `FVergilAgentAuditEntry` currently carries `Request`, `Response`, and `TimestampUtc`.
- `UVergilAgentSubsystem::MakeApplyRequestFromPlan(...)` is the supported helper for the explicit phase handoff. It copies the reviewed normalized plan, carries the target Blueprint path forward from the plan request, and stamps the expected normalized command-plan fingerprint onto the apply request.
- `UVergilAgentSubsystem::ExecuteRequest(...)` now normalizes missing plan target paths from `Document.BlueprintPath`, defaults missing plan graph names to `EventGraph`, normalizes apply command ordering before fingerprint checks, and records the normalized request in the audit trail.
- `PlanDocument` execution runs `UVergilEditorSubsystem::MakeCompileRequest(...)` plus `CompileRequest(..., false)` and never mutates the Blueprint. `ApplyCommandPlan` execution runs `ExecuteCommandPlan(...)` only after the provided expected fingerprint matches the normalized explicit plan.
- Missing or mismatched `ExpectedCommandPlanFingerprint` rejects apply requests before mutation with explicit diagnostics, so plan review and apply replay stay separate.
- `UVergilAgentSubsystem::RecordAuditEntry(...)` now normalizes missing response request ids, missing response operations, and missing `TimestampUtc` before appending the audit trail and immediately persisting it.
- `UVergilAgentSubsystem::GetAuditTrailPersistencePath()`, `FlushAuditTrailToDisk()`, and `ReloadAuditTrailFromDisk()` expose the supported persistence surface for the current audit trail.
- `UVergilAgentSubsystem::ClearAuditTrail()` clears both the in-memory audit trail and the persisted on-disk audit log.

## Versioning policy contracts

- Vergil uses semantic versioning for the plugin release surface and separate integer versioning for document schema, serialized command plans, the graph-document/diagnostics/compile-result inspection payloads, the agent request/response/audit inspection payloads, the persisted agent audit-log wrapper, and the supported-contract inspection manifest.
- The current plugin semantic version is sourced from `Vergil::GetSemanticVersionString()`. `Vergil.uplugin` `VersionName` is expected to stay aligned with that helper.
- The current plugin descriptor version is sourced from `Vergil::PluginDescriptorVersion`. `Vergil.uplugin` `Version` is expected to stay aligned with that constant.
- `Vergil::GetSupportedSchemaMigrationPaths()` exposes the exact supported forward schema-migration steps from the model layer, and the supported-contract manifest mirrors that list for agent/tool inspection.
- Additive `Vergil.ContractManifest` fields do not require a manifest-version bump. Incompatible JSON shape changes do.
- See [VERSIONING.md](VERSIONING.md) for the full semantic-versioning, migration, and release-update checklist.

## Compile result contracts

- `FVergilCompileResult.Statistics.TargetGraphName` records the requested compile target for compiler-produced results. Direct `ExecuteCommandPlan(...)` and serialized replay infer a graph name only when every command targets the same graph; otherwise the field remains unset.
- `FVergilCompileResult.Statistics.RequestedSchemaVersion` reflects the authored request/document schema, while `EffectiveSchemaVersion` reflects the document version actually consumed after any schema-migration pass work.
- `FVergilCompileResult.Statistics.bAutoLayoutRequested` and `bGenerateCommentsRequested` reflect the compile request flags. `bApplyRequested` indicates whether the caller asked for command execution, and `bExecutionAttempted` indicates whether execution actually ran.
- `FVergilCompileResult.Statistics.CommandPlanFingerprint` is a stable fingerprint of the normalized returned command plan. `PlanningInvocationCount` records compiler planning passes invoked for that result, and `ApplyInvocationCount` records editor execution attempts.
- `FVergilCompileResult.Statistics.bExecutionUsedReturnedCommandPlan` is only set when execution consumed the `Commands` array already present on that result. `CompileDocument(..., bApplyCommands=true)` and direct `ExecuteCommandPlan(...)` both execute the returned normalized plan directly.
- `FVergilCompileResult.Statistics` always tracks target-graph node/edge counts plus deterministic command counts for blueprint-definition, graph-structure, connection, finalize, explicit-compile, and post-blueprint-compile phases. `PlannedCommandCount` is the normalized command-plan length.
- `FVergilCompileResult.Statistics.bCommandPlanNormalized` is set when the returned `Commands` array has already been phase-normalized. Compiler output and direct command execution both normalize before returning.
- `FVergilCompileResult.Statistics.CompletedPassNames`, `LastCompletedPassName`, and `FailedPassName` describe compiler-pipeline progress. Successful compiles leave `FailedPassName` unset. Direct command execution does not populate compiler-pass progress because no compiler pipeline ran.
- `FVergilCompileResult.PassRecords` are emitted in attempted-pass order for compiler-produced results only. Each record captures the pass name, whether that pass returned success, the cumulative diagnostic/error counts after the pass, and the planned-command count visible at that point in the pipeline.
- Dry-run compile and compile+apply share the same document-planning helper path in `UVergilEditorSubsystem`. For the same request flags and an equivalent target Blueprint context, they must return identical normalized command plans.
- Persisted asset roundtrip coverage now also verifies that the current supported whole-asset authoring surface survives package save/reload and that dry-run planning against the reloaded Blueprint preserves the normalized command-plan fingerprints before a native Blueprint compile.

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
- Under `UE_5.7`, `K2.Select` currently supports only `IndexPinCategory=bool`, `int`, or `enum`; unsupported authored index categories now fail during semantic validation and direct command-plan preflight instead of deferring to late execution.

## Symbol resolution contracts

- `FVergilSymbolResolutionPass` now runs after semantic validation and before later lowering and command-planning passes.
- The symbol pass resolves callable/member references for `K2.Event.*`, `K2.Call.*`, `K2.VarGet.*`, `K2.VarSet.*`, `K2.BindDelegate.*`, `K2.RemoveDelegate.*`, `K2.ClearDelegate.*`, `K2.CallDelegate.*`, `K2.CreateDelegate.*`, custom-event delegate-signature metadata, and `K2.ForLoop`, `K2.DoOnce`, and `K2.FlipFlop` macro references.
- Explicit `OwnerClassPath` and `DelegateOwnerClassPath` metadata is authoritative. If authored, the symbol pass resolves only against that owner path and fails explicitly when the owner or member cannot be found.
- Without explicit owner metadata, variable and delegate symbols resolve in this order: document-authored members first, then existing Blueprint-local members, then inherited/native members.
- Without explicit owner metadata, `K2.Call.*` now resolves in this order: document-authored function definitions first, then existing Blueprint-local functions, then inherited/native functions. Self-owned resolutions keep an empty `OwnerClassPath`, while inherited/native resolutions are normalized back into `OwnerClassPath` on the compiler working document so planned commands become explicit.
- `K2.CustomEvent.*` may optionally declare `DelegatePropertyName` plus `DelegateOwnerClassPath` metadata for delegate signatures. The symbol pass resolves those signatures before planning, and external-owner resolutions are normalized back into `DelegateOwnerClassPath`.
- `K2.CreateDelegate.*` currently resolves against target-graph custom events, document-authored function definitions, and existing Blueprint or parent-class functions. Ambiguous local matches fail explicitly.
- `K2.ForLoop`, `K2.DoOnce`, and `K2.FlipFlop` macro references now resolve during the symbol pass, and omitted `MacroBlueprintPath` / `MacroGraphName` metadata is normalized to the matching engine `StandardMacros` graph before planning.

## Type resolution contracts

- `FVergilTypeResolutionPass` now runs after symbol resolution and before node lowering, connection legality validation, post-compile finalize lowering, comment post-pass lowering, layout post-pass lowering, and final command planning.
- The type pass normalizes authored type metadata across variable definitions, function signatures, macro signatures, dispatcher parameters, component class paths, interface class paths, and explicit typed-node metadata on the active graph surface.
- Supported logical type categories remain `bool`, `int`, `float`, `double`, `string`, `name`, `text`, `enum`, `object`, `class`, and `struct`.
- `enum`, `object`, `class`, and `struct` references now resolve to canonical object paths before planning, so planned commands stop depending on raw authored whitespace or alternate path spellings.
- The type pass currently resolves explicit type metadata for `K2.Cast`, `K2.SpawnActor`, `K2.Select`, `K2.SwitchEnum`, `K2.MakeStruct`, `K2.BreakStruct`, `K2.MakeArray`, `K2.MakeSet`, and `K2.MakeMap`.
- Failed type resolution stops compilation before node lowering, connection legality validation, or command planning, so invalid authored type references now return zero planned commands.

## Node lowering contracts

- `FVergilNodeLoweringPass` now runs after type resolution and before connection legality, post-compile finalize lowering, comment post-pass lowering, layout post-pass lowering, and final command planning.
- The node-lowering pass resolves handlers against the normalized working document and emits node-scoped `AddNode` and `SetNodeMetadata` commands into the compiler context before the final plan is assembled.
- Comment and generic metadata-lowering commands are emitted in deterministic key order.
- Comment nodes are now reserved for the explicit comment post-pass and do not lower through the core node-lowering stage.
- Failed node lowering stops compilation before later connection legality validation, post-compile finalize lowering, comment post-pass lowering, layout post-pass lowering, or final command planning, so handler failures now return zero planned commands.

## Connection legality contracts

- `FVergilConnectionLegalityPass` now runs after node lowering and before final command planning.
- The connection pass validates target-graph edges against the lowered `AddNode` output, not just raw authored node pins, so later passes only assemble connections that still exist on the lowered pin surface.
- Source pins must lower as output pins, target pins must lower as input pins, exec pins may only connect to exec pins, and data pins may only connect to data pins.
- Input pins may only have one incoming authored edge during compile-time legality validation.
- Lowered source and target pins must stay on the compile target graph, and both sides of one connection must lower into the same graph.
- Failed connection legality validation stops compilation before post-compile finalize lowering, comment post-pass lowering, layout post-pass lowering, or final command planning, so invalid authored edges now return zero planned commands.

## Post-compile finalize contracts

- `FVergilPostCompileFinalizePass` now runs after connection legality validation and before final command planning.
- The post-compile finalize pass emits deferred `FinalizeNode` commands into a dedicated compiler-context buffer instead of piggybacking on node lowering.
- `K2.CreateDelegate.*` currently lowers its finalize payload through this pass, preserving the resolved function name and normalized authored metadata for the later executor finalize step.
- Failed post-compile finalize lowering stops compilation before comment post-pass lowering, layout post-pass lowering, or final command planning, so invalid finalize payloads now return zero planned commands.

## Comment post-pass contracts

- `FVergilCommentPostPass` now runs after post-compile finalize lowering and before layout post-pass lowering plus final command planning.
- Authored `EVergilNodeKind::Comment` nodes now lower through this dedicated optional post-pass instead of the core node-lowering stage.
- `FVergilCompileRequest.bGenerateComments` controls whether comment-node commands are emitted at all. When it is `false`, authored comment nodes are omitted from the returned command plan.
- `FVergilCompileRequest.CommentGeneration` currently exposes `DefaultWidth`, `DefaultHeight`, `DefaultFontSize`, `DefaultColor`, `bShowBubbleWhenZoomed`, `bColorBubble`, and `MoveMode`.
- When authored comment metadata omits any of those style fields, the comment post-pass now emits deterministic default `SetNodeMetadata` commands using the request settings so comment authoring stays explicit and does not inherit per-user graph-editor defaults.
- The comment post-pass reuses the existing `Vergil.Comment` command surface and deterministic node-metadata ordering.

## Layout post-pass contracts

- `FVergilLayoutPostPass` now runs after the comment post-pass and before final command planning.
- `FVergilCompileRequest.bAutoLayout` now gates deterministic `MoveNode` planning through this boundary, and `FVergilCompileRequest.AutoLayout` currently exposes `Origin`, `HorizontalSpacing`, `VerticalSpacing`, and `CommentPadding`.
- Primary non-comment nodes are currently laid out into deterministic dependency columns and rows. When comment generation is enabled, authored comment nodes are stacked into a deterministic left-side band using `CommentPadding` plus authored width or height metadata when present.
- Because the layout pass remains isolated from core lowering and planning, future layout work can extend the same `MoveNode`-based surface without changing earlier compiler stages.

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
- `FinalizeNode` commands are currently emitted by the dedicated post-compile finalize compiler pass, not by node lowering or final command planning.
- Comment-node `AddNode` / `SetNodeMetadata` work is currently emitted by the dedicated comment post-pass when `bGenerateComments` is enabled.
- Supported comment-node metadata keys now include `CommentText`, `Title`, `CommentWidth`, `NodeWidth`, `CommentHeight`, `NodeHeight`, `FontSize`, `Color`, `CommentColor`, `ShowBubbleWhenZoomed`, `ColorBubble`, and `MoveMode`.
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
| `K2.Call.<FunctionName>` | `Call` | none | Optional `OwnerClassPath` constrains function resolution. When omitted, the scaffold resolves document-authored functions first, then existing Blueprint-local functions, then inherited/native functions. Self-owned resolutions keep an empty owner path; inherited/native resolutions normalize that owner path into the planned command. Headless `UE_5.7` coverage now explicitly re-verifies timer-by-function-name helpers plus handle-based timer pause/query helpers through this generic call path. |
| `K2.VarGet.<VariableName>` | `VariableGet` | none | Optional `OwnerClassPath` constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members. `UE_5.7` getter variants now support pure reads, bool branch getters, and validated object/class/soft-reference getters. Impure getter shapes must use `Execute` input plus `Then` and `Else` exec outputs. |
| `K2.VarSet.<VariableName>` | `VariableSet` | none | Optional `OwnerClassPath` constrains property lookup. Without it, the symbol pass resolves document-authored members first, then existing Blueprint members, then inherited members. |
| `K2.Self` | any | none | Creates a self node. |
| `K2.Branch` | any | none | Standard branch node. |
| `K2.Sequence` | any | none | Output exec pins named like `Then_0`, `Then_1`, and so on determine sequence width. |
| `K2.ForLoop` | any | none | Optional `MacroBlueprintPath` and `MacroGraphName`. Defaults resolve to the engine `ForLoop` macro in `StandardMacros`, and the symbol pass validates the selected macro graph before planning. |
| `K2.DoOnce` | any | none | Optional `MacroBlueprintPath` and `MacroGraphName`. Defaults resolve to the engine `DoOnce` macro in `StandardMacros`, and the symbol pass validates the selected macro graph before planning. The bool input pin follows the engine name `Start Closed`. |
| `K2.FlipFlop` | any | none | Optional `MacroBlueprintPath` and `MacroGraphName`. Defaults resolve to the engine `FlipFlop` macro in `StandardMacros`, and the symbol pass validates the selected macro graph before planning. The single input exec pin maps to the engine macro's unnamed entry exec pin, while outputs remain `A`, `B`, and `IsA`. |
| `K2.SpawnActor` | any | `ActorClassPath` | `ActorClassPath` must resolve to an `AActor`-derived class during type resolution and is normalized before planning. Under `UE_5.7` this lowers to `UK2Node_SpawnActorFromClass`, exposing the deterministic `SpawnTransform`, `CollisionHandlingOverride`, `TransformScaleMethod`, `Owner`, `ReturnValue`, and class-specific `ExposeOnSpawn` property pins. `SpawnTransform` must be authored and connected because the `UE_5.7` node expands into by-reference transform calls. The dynamic `Class` and `WorldContextObject` pins are intentionally outside the authored contract. |
| `K2.Delay` | any | none | Lowers to `UKismetSystemLibrary::Delay`. |
| `K2.Cast` | any | `TargetClassPath` | Target class path must resolve to a class during type resolution and is normalized before planning. |
| `K2.Reroute` | any | none | Creates a knot node. |
| `K2.Select` | any | `IndexPinCategory`, `ValuePinCategory` | `IndexPinCategory` currently supports only `bool`, `int`, or `enum` in `UE_5.7`. Optional `IndexObjectPath`, `ValueObjectPath`, and `NumOptions` refine the wildcard shape. Enum index selects use `IndexObjectPath` for the enum, explicit type metadata is resolved before planning, and unsupported index/value connections now fail with explicit apply-time diagnostics. |
| `K2.SwitchInt` | any | none | Planned exec output pins define the case labels and must parse as integers. Unsupported selection-pin type combinations now fail with explicit apply-time diagnostics. |
| `K2.SwitchString` | any | none | Planned exec output pins define the case labels. Optional `CaseSensitive` metadata configures comparison behavior, and unsupported selection-pin type combinations now fail with explicit apply-time diagnostics. |
| `K2.SwitchEnum` | any | `EnumPath` | Enum path must resolve to a `UEnum` during type resolution and is normalized before planning. Unsupported selection-pin type combinations now fail with explicit apply-time diagnostics. |
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
