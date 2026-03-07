# Vergil Versioning and Migration

Vergil tracks the plugin release, document schema, serialized command-plan format, document/diagnostic/compile-result inspection formats, agent request/response/audit inspection formats, the persisted agent audit-log format, and inspection-manifest format as separate version surfaces. They are related, but they do not move in lockstep.

## Current version surfaces

- Plugin semantic version: `0.1.0`
  Source of truth: `Vergil::GetSemanticVersionString()` and `Vergil.uplugin` `VersionName`
- Plugin descriptor version: `1`
  Source of truth: `Vergil::PluginDescriptorVersion` and `Vergil.uplugin` `Version`
- Document schema version: `3`
  Source of truth: `Vergil::SchemaVersion`
- Supported document schema migration steps: `1->2`, `2->3`
  Source of truth: `Vergil::GetSupportedSchemaMigrationPaths()`
- Graph-document inspection format: `Vergil.GraphDocument` version `1`
  Source of truth: `Vergil::GetDocumentInspectionFormatName()` and `Vergil::GetDocumentInspectionFormatVersion()`
- Diagnostics inspection format: `Vergil.Diagnostics` version `1`
  Source of truth: `Vergil::GetDiagnosticsInspectionFormatName()` and `Vergil::GetDiagnosticsInspectionFormatVersion()`
- Compile-result inspection format: `Vergil.CompileResult` version `1`
  Source of truth: `Vergil::GetCompileResultInspectionFormatName()` and `Vergil::GetCompileResultInspectionFormatVersion()`
- Agent request inspection format: `Vergil.AgentRequest` version `1`
  Source of truth: `Vergil::GetAgentRequestFormatName()` and `Vergil::GetAgentRequestFormatVersion()`
- Agent response inspection format: `Vergil.AgentResponse` version `1`
  Source of truth: `Vergil::GetAgentResponseFormatName()` and `Vergil::GetAgentResponseFormatVersion()`
- Agent audit-entry inspection format: `Vergil.AgentAuditEntry` version `1`
  Source of truth: `Vergil::GetAgentAuditEntryFormatName()` and `Vergil::GetAgentAuditEntryFormatVersion()`
- Persisted agent audit-log format: `Vergil.AgentAuditLog` version `1`
  Source of truth: `UVergilAgentSubsystem` persisted audit-log serialization in `VergilAgentSubsystem.cpp`
- Command-plan serialization format: `Vergil.CommandPlan` version `1`
  Source of truth: `Vergil::GetCommandPlanFormatName()` and `Vergil::GetCommandPlanFormatVersion()`
- Supported-contract inspection manifest: `Vergil.ContractManifest` version `1`
  Source of truth: `FVergilSupportedContractManifest.ManifestVersion`

## Semantic versioning policy

- `MAJOR` changes when Vergil breaks a previously supported public contract in a way that older callers cannot consume without code or data changes.
- `MINOR` changes when Vergil adds backward-compatible capabilities such as new supported document fields, descriptor families, command types, or inspection data.
- `PATCH` changes when Vergil only fixes behavior, diagnostics, tests, tooling, or documentation without expanding or breaking the supported serialized contract surface.
- Schema, command-plan, inspection, and inspection-manifest version numbers are not replacements for semantic versioning. They only version their own serialized formats.

## Migration policy

- Document schema migration is forward-only.
- The compiler runs schema migration before structural validation, semantic validation, lowering, and command planning.
- Current-schema documents are left unchanged.
- Older documents are upgraded only when every required forward step exists.
- Downgrades are not attempted.
- Newer-than-compiler documents are not rewritten. They remain on their authored schema version and emit the existing future-schema warning.
- Current migrations are additive. `1->2` formalized the expanded whole-asset surface, and `2->3` added Blueprint-level metadata without removing prior authored fields.

## Version bump guidance

- Bump the plugin semantic version for every release-facing change.
- Bump the schema version only when the persisted `FVergilGraphDocument` contract changes.
- Add an explicit migration step and automation coverage whenever a new schema version is introduced and legacy documents should continue working.
- Bump the graph-document inspection format version only for incompatible `Vergil.GraphDocument` JSON shape changes.
- Bump the diagnostics inspection format version only for incompatible `Vergil.Diagnostics` JSON shape changes.
- Bump the compile-result inspection format version only for incompatible `Vergil.CompileResult` JSON shape changes.
- Bump the agent request, response, or audit-entry inspection format version only for incompatible `Vergil.AgentRequest`, `Vergil.AgentResponse`, or `Vergil.AgentAuditEntry` JSON shape changes.
- Bump the persisted agent audit-log format version only for incompatible `Vergil.AgentAuditLog` wrapper changes in the saved on-disk audit trail.
- Bump the command-plan format version only for incompatible serialized command-plan changes.
- Bump the inspection-manifest version only for incompatible `Vergil.ContractManifest` JSON shape changes. Additive fields may remain on the same manifest version.

## Release checklist

- Update `Vergil::SemanticVersionMajor`, `SemanticVersionMinor`, and `SemanticVersionPatch`.
- Keep `Vergil.uplugin` `VersionName` and `Version` aligned with `VergilVersion.h`.
- If the document schema changed, update `Vergil::SchemaVersion`, add the forward migration step, and extend migration automation.
- If graph-document, diagnostics, compile-result, agent request/response/audit inspection JSON, or the persisted agent audit-log wrapper changed incompatibly, update the corresponding version surface and extend inspection or persistence coverage.
- If serialized command plans changed incompatibly, update the command-plan format version and deserializer coverage.
- If the supported-contract manifest changed incompatibly, update its manifest version and inspection coverage.
- Update `README.md`, `SUPPORTED_DESCRIPTOR_CONTRACTS.md`, and `ROADMAP.md` when the public versioning or migration contract changes.
