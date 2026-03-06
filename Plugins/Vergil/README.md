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

## Next implementation steps

  1. Replace deprecated timer usage with Clear and Invalidate Timer by Handle
