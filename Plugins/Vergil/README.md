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
- Document-authored member variables are now part of that supported contract surface, including type/flag/metadata/default authoring.
- Generic fallback planning is not a guarantee that execution exists. The contract document is the source of truth for what the current scaffold actually supports.

## Current baseline

- Milestone 0 is complete.
- `VGR-1002` is complete.
- Document-authored member variables now have structural validation, deterministic command planning, editor execution, and headless automation coverage.
- `Vergil.Scaffold.*` currently passes headlessly with zero Vergil, Blueprint, or automation warnings.

## Planning

- See `ROADMAP.md` for the tracked implementation roadmap and milestone breakdown.
