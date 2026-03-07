# Extending Vergil Node Handlers

This guide documents the supported workflow for adding a new custom node handler to Vergil.

The supported extension seam is the compiler-side `IVergilNodeHandler` interface plus `FVergilNodeRegistry`. New handler work is not complete until the compiler path, executor path, support manifest, docs, and automation all agree on the same family contract.

## Decide the handling path first

Every new node family should fit one of the existing support-matrix categories:

- `GenericNodeSpawner`: use this when the target still materializes as a normal `UK2Node` and the remaining work is deterministic validation plus setup of an existing engine node class.
- `SpecializedHandler`: use this when the family still ends in a `UK2Node`, but the engine surface needs family-specific resolution or setup that the shared generic path cannot infer on its own, such as dedicated async-task factories.
- `DirectLowering`: use this when the family is not a good fit for the generic spawner at all, such as events, interface messages, call nodes, macro instances, variable helpers, or delegate/finalize flows.
- `Unsupported`: use this intentionally when the family is known but not yet safe to expose. Unsupported families should still be named in the manifest so diagnostics stay explicit.

Do not start by adding executor branches. Pick the handling path first, then make the manifest and docs reflect that choice.

## Supported extension seam

The public extension contracts are:

- `Source/VergilBlueprintCompiler/Public/VergilCompilerTypes.h`
  `IVergilNodeHandler` defines `GetDescriptor()`, `CanHandle(...)`, and `BuildCommands(...)`.
- `Source/VergilBlueprintCompiler/Public/VergilNodeRegistry.h`
  `FVergilNodeRegistry::RegisterHandler(...)` is for one exact descriptor.
  `FVergilNodeRegistry::RegisterFallbackHandler(...)` is for prefix or predicate-driven families that override `CanHandle(...)`.

Planning stays pure. A handler may only inspect the normalized compiler context and emit deterministic command payloads or diagnostics. It must not mutate the editor directly.

## Implementation checklist

### 1. Define the authored contract

Add or update the descriptor row in `Source/VergilBlueprintCompiler/Private/VergilContractInfo.cpp`:

- `BuildSupportedDescriptorContracts()`
- `BuildNodeSupportMatrix()`

This keeps three surfaces aligned:

- the markdown descriptor table
- the code-backed manifest exposed through inspection APIs
- unsupported-family diagnostics that now read from the manifest

If the family is intentionally unsupported for now, stop here and add the unsupported matrix row plus diagnostics instead of landing a half-supported executor path.

### 2. Add any earlier-pass normalization the family needs

Before writing the handler, decide whether the node needs work in:

- schema migration
- semantic validation
- symbol resolution
- type resolution

Handlers should consume normalized metadata, class paths, and planned pins whenever possible. Do not push avoidable normalization into late executor code.

### 3. Implement the compiler handler

Add an `IVergilNodeHandler` implementation in `Source/VergilBlueprintCompiler/Private/VergilCompilerPasses.cpp`.

Typical rules:

- `GetDescriptor()` should return the emitted command name or the exact descriptor anchor for the handler.
- Override `CanHandle(...)` only when the family uses descriptor prefixes or extra predicate checks.
- `BuildCommands(...)` should emit deterministic `FVergilCompilerCommand` payloads through the compiler context.
- Use `CopyPlannedPins(...)` when the family depends on authored pin intent.
- Use `Node.Metadata` only as authored input. If earlier passes normalize data into metadata or command fields, prefer the normalized values.
- Add explicit diagnostics for missing suffixes or required metadata before returning `false`.

Most node families currently emit `AddNode` commands. Families that require a second-phase setup, such as `K2.CreateDelegate.*`, should reserve that work for the dedicated finalize pass instead of mutating the `AddNode` phase into an ad hoc multi-stage executor.

### 4. Register the handler deliberately

Register the handler through `EnsureGenericFallbackHandler()` in `Source/VergilBlueprintCompiler/Private/VergilCompilerPasses.cpp`, unless the family needs a different bootstrap point.

Prefer:

- `RegisterHandler(...)` for one exact descriptor
- `RegisterFallbackHandler(...)` for prefix families such as `K2.Call.*` or other `CanHandle(...)`-driven families

The registry lookup order matters. Exact registrations should win over broad fallbacks, and broad fallbacks should stay narrow enough that unsupported descriptors still fail explicitly.

### 5. Extend the executor path that matches the handling choice

#### Generic node spawner

If the handler belongs in the shared `UK2Node` spawner path, update `Source/VergilEditor/Private/VergilCommandExecutor.cpp`:

- `FindGenericK2NodeSpawnerCommand(...)`
- `ValidateGenericK2NodeSpawnerCommand(...)`
- `ExecuteGenericK2NodeSpawnerCommand(...)`

Use this path when the executor only needs deterministic validation and family-specific setup around a real engine node class.

#### Specialized handler

If the family still ends in the shared spawner but needs extra engine-specific resolution, add the smallest specialized branch needed for that family. Current dedicated async-task families are the reference shape: the compiler uses a specialized handler, while the executor still lands in the generic spawner with a specialized kind.

#### Direct lowering

If the family is not a good fit for the generic spawner, extend the explicit executor branches instead:

- `ExecuteAddNode(...)`
- `ExecuteFinalizeNode(...)`

Use this for event entrypoints, interface messages, macro instances, variable helpers, delegate helpers, and other families where the engine behavior is defined by bespoke lowering rather than generic node instantiation.

### 6. Keep diagnostics explicit

Unsupported or deferred families should report through the manifest-backed diagnostics helpers instead of generic fallback text. If a new unsupported family is introduced, add or update the matching node-support matrix row so the failure message names the exact gap.

### 7. Add automation before calling it done

At minimum, new handler work should update or add:

- compile-time validation coverage for new required metadata or type/symbol resolution
- execution coverage for the handler family
- `Vergil.Scaffold.SupportedNodeContractDocs` if `SUPPORTED_DESCRIPTOR_CONTRACTS.md` changed

Use PIE coverage when the family's real value is runtime behavior rather than graph shape alone.

## Non-negotiable rules

- The manifest is the source of truth for supported versus unsupported families.
- Generic fallback planning is not blanket support.
- Hidden engine pins such as `WorldContextObject` remain outside the authored contract unless Vergil explicitly documents them.
- Deterministic command ordering still matters. Handler changes must preserve stable plan output.
- New handler work is not complete until the README, supported-contract docs, roadmap, and automation reflect it.

## Release checklist for a new handler family

1. Add the descriptor contract row.
2. Add or update the node-support matrix row.
3. Add any earlier-pass validation or normalization.
4. Implement and register the compiler handler.
5. Extend the matching executor path.
6. Add explicit diagnostics for unsupported or invalid cases.
7. Add automation coverage.
8. Update `README.md`, `SUPPORTED_DESCRIPTOR_CONTRACTS.md`, and `ROADMAP.md`.
