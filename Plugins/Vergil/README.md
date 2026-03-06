# Vergil

Vergil is a clean-room plugin scaffold for deterministic Blueprint authoring.

## Module layout

- `VergilCore`: logging, versioning, diagnostics.
- `VergilBlueprintModel`: canonical graph document and serializable node/pin/edge data.
- `VergilBlueprintCompiler`: registry-backed compiler surface and validation pipeline entry point.
- `VergilEditor`: editor subsystem and developer settings.
- `VergilAgent`: agent-facing orchestration and audit trail.
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

## Current supported contracts

- The scaffold only supports the document fields and descriptor families documented in [SUPPORTED_DESCRIPTOR_CONTRACTS.md](SUPPORTED_DESCRIPTOR_CONTRACTS.md).
- Document-authored Blueprint metadata is now part of that supported contract surface for `BlueprintDisplayName`, `BlueprintDescription`, `BlueprintCategory`, and `HideCategories`.
- Document-authored member variables are now part of that supported contract surface, including type/flag/metadata/default authoring.
- Document-authored function definitions now lower into Blueprint function graph/signature authoring for function name, purity, access, and typed inputs/outputs. Function body authoring remains future work.
- Document-authored macro definitions now lower into Blueprint macro graph/signature authoring for exec/data inputs and outputs. Macro body authoring remains future work.
- Document-authored component definitions now lower into Blueprint component hierarchy authoring for component creation, parent attachment, attach sockets, template properties, and relative transforms.
- Document-authored implemented interfaces now lower into Blueprint interface application for authored interface class paths.
- Document-authored class defaults now lower into post-compile Blueprint class default writes for authored property names and serialized values.
- Document-authored construction script definitions now lower into construction-script graph authoring when the compile target graph is `UserConstructionScript`.
- Legacy document schemas now have explicit model-level forward-migration helpers.
- Structural validation now also rejects unsupported Blueprint metadata keys, empty variable metadata keys, whitespace-only typed object/class paths, invalid dispatcher parameter type shapes, and graph edges that reference pins outside their declared source/target nodes.
- Direct command plans are now preflight-validated before any editor transaction starts, so malformed plans fail with diagnostics and execute zero commands.
- Compiler-emitted and direct command plans are now normalized into deterministic execution-phase order before apply/logging.
- Command plans now have stable debug-print strings plus versioned JSON serialization/deserialization for inspection and replay.
- Direct command-plan execution now supports explicit Blueprint metadata writes, function/macro graph creation, component creation/attachment/property mutation, interface application, class default writes, member renames, node removal/movement, and explicit Blueprint compile commands.
- Generic fallback planning is not a guarantee that execution exists. The contract document is the source of truth for what the current scaffold actually supports.

## Current baseline

- Milestone 0 is complete.
- `VGR-1001`, `VGR-1002`, `VGR-1003`, `VGR-1004`, `VGR-1005`, `VGR-1006`, `VGR-1007`, `VGR-1008`, `VGR-1009`, `VGR-1010`, `VGR-2001`, `VGR-2002`, `VGR-2003`, `VGR-2004`, `VGR-3001`, `VGR-3002`, `VGR-4001`, `VGR-4002`, `VGR-4003`, `VGR-4004`, `VGR-4005`, `VGR-4006`, `VGR-4007`, and `VGR-4008` are complete.
- Document-authored Blueprint metadata now has structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored member variables now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored function and macro definitions now have structural validation plus deterministic command planning and editor execution for graph/signature creation and updates.
- Document-authored component definitions now have structural validation plus deterministic command planning and editor execution for component creation, attachment, attach sockets, template properties, and relative transforms.
- Document-authored implemented interfaces now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored class defaults now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- Document-authored construction script definitions now have structural validation, deterministic command planning, editor execution, and headless automation coverage when targeting `UserConstructionScript`.
- Schema migration helpers now exist for older documents, and the compiler now runs schema migration as its first pass so supported legacy documents upgrade automatically before validation and planning.
- The compiler now runs a dedicated semantic validation pass before planning, rejecting unsupported compile targets plus invalid known-node descriptor/kind/metadata combinations before command generation.
- Structural validation now checks dispatcher parameter type shapes, variable metadata keys, trimmed object/class paths, and graph-edge pin ownership across both graph surfaces.
- Command execution now validates command-plan shape and intra-plan references before opening a transaction, preventing partial mutation from malformed plans.
- Command plans now normalize into deterministic execution-phase order before they are returned or applied, so compile output and direct `ExecuteCommandPlan` replay share the same visible ordering.
- Command plans now also have stable debug printing plus JSON serialization/deserialization, and the editor subsystem can replay serialized command plans directly.
- The explicit editor command surface now covers Blueprint metadata, function graphs, macro graphs, components, interfaces, class defaults, member renames, node moves/removals, and explicit compile commands. Document lowering now exists for Blueprint metadata, variables, dispatchers, function definitions, macro definitions, component hierarchy data, component template properties, class defaults, implemented interfaces, and construction-script graphs; the remaining asset-model slices are still future work.
- `Vergil.Scaffold.*` currently passes headlessly with zero Vergil, Blueprint, or automation warnings.

## Planning

- See `ROADMAP.md` for the tracked implementation roadmap and milestone breakdown.
