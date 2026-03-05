# BP_NodeTypeTest Postmortem (IR Node Type Automation)

Date: 2026-03-05
Blueprint: `/Game/Tests/BP_NodeTypeTest`

## What Went Wrong During Implementation

1. `patch_blueprint` contract mismatch
- Expected IR-style top-level payload (`blueprint_path`, `variables`, `functions`, `event_graph`).
- Actual tool required `operations` array with atomic ops (`create_blueprint`, `add_function`, `add_node`, `connect`, `set_default`, etc.).
- Impact: initial full patch failed immediately (`Missing required field: 'operations'`).

2. Function graph completion behavior
- In this patch mode, function `result` node was not reliably addressable by reserved name in `connect` ops.
- Many functions were valid/executable without explicit `... -> result.execute` wiring, but this is non-obvious and differs from IR expectations.
- Impact: early wiring attempts failed (`target node 'result' not found`).

3. Switch/struct pin-name instability before instantiation
- `switch_enum` template inspection did not expose actual enum case exec pins until node was materialized.
- `make_struct`/`break_struct` required exact explicit pin names (`ReturnValue` -> `InVec`) in this patch path.
- Impact: connection failures until runtime node pin introspection was used.

4. `select` wildcard default assignment failure
- Setting defaults on `K2Node_Select` option pins failed under wildcard resolution.
- Impact: unable to drive `select` output value in this build path; node kept for compile-coverage only.

5. `format_text` placeholder pin default failure
- Setting `Status` placeholder pin default failed in this patcher variant.
- Workaround was to use fixed format string (`[TEST_FORMAT_TEXT] OK`) while still validating `format_text + reroute` node types.

6. Dispatcher unbind compile error
- `unbind_dispatcher` required delegate pin wiring; missing connection caused compile error.
- Fix: explicitly connected `create_delegate.OutputDelegate` -> `unbind_dispatcher.Delegate`.

7. Snapshot/ID drift and remove-node targeting friction
- Node IDs seen from logs/introspection were not always valid in later `remove_node` ops.
- Semantic IDs from `snapshot_blueprint` were more reliable than transient generated names.

8. Log filtering ambiguity during PIE validation
- Pattern-only log queries mixed editor setup logs and runtime logs.
- Reliable validation needed tight filters and recent-window checks around active PIE runs.

## Bugs / Gaps Identified

1. Tooling documentation gap
- `get_ir_schema` describes top-level IR payload that does not match active `patch_blueprint` tool contract in this environment.
- Recommendation: expose versioned schema and explicit mode (`ir_full` vs `ops_atomic`) at runtime.

2. `inspect_node_template` usability gap for enum/templated nodes
- For nodes like `switch_enum`, pre-flight inspection did not provide case pins required for deterministic wiring.
- Recommendation: allow template inspection with concrete enum binding to return full pin map.

3. Weak wildcard coercion feedback
- `select`/wildcard pin default failures occur without rich remediation guidance.
- Recommendation: return expected concrete type and suggested fix (e.g., set node value type first).

4. Inconsistent graph lifecycle behavior across combined operations
- Combined remove/add/connect transactions occasionally failed due to graph initialization timing.
- Recommendation: internal op batching should stabilize graph creation before connection phase.

5. `unbind_dispatcher` ergonomics
- Node requires explicit delegate pin even when bind path already exists.
- Recommendation: optional auto-bind delegate reuse for matching dispatcher/handler pairs.

6. Result-node wiring discoverability
- Function graph terminal semantics are unclear in atomic patch mode.
- Recommendation: provide reserved `entry`/`result` aliases that always resolve.

## Residual Gaps In The Current Test Blueprint

1. `TestCollections` currently validates node presence/compilation, not runtime `select` data flow.
- Root cause: wildcard/default assignment issues in this patch path.

2. `TestFormatText` validates `format_text + reroute` execution, but not dynamic placeholder argument assignment.
- Root cause: placeholder pin default assignment failure for named argument pin.

3. `TestCastAndSelf` includes a valid but redundant cast (`self` already `Actor` lineage).
- Compiler note only; behavior is still correct for node-type coverage.

## Final Status

- PIE runtime tags passed for all expected `[TEST_*]` outputs.
- Blueprint compiles with no errors.
- Remaining issues are mostly tooling/schema/ergonomics gaps rather than runtime test failure.

## 2026-03-05 Update: Pure Getter Validation

### What changed

1. Updated `TestPureCall` to use a pure engine getter call path only:
- Pure call: `GetActorScale3D`
- Pure conversion: `Conv_VectorToString`
- Marker print: `[TEST_PURE_CALL] Getter`
- Value print: vector string from the getter (expected default actor scale: `X=1.000 Y=1.000 Z=1.000`)

2. Removed the temporary custom getter function from this test pass.

### Focused validation instructions (pure function only)

1. Save asset:
- `save_asset` for `/Game/Tests/BP_NodeTypeTest`

2. Run PIE briefly:
- `pie_control` start
- wait ~3 seconds
- `get_recent_logs` with:
  - `category=LogBlueprintUserMessages`
  - `pattern=\\[TEST_PURE_CALL\\]`
- `pie_control` stop

3. Pass criteria:
- Log contains `[TEST_PURE_CALL] Getter`
- Companion value print is present from the pure getter conversion path (for default scale: `X=1.000 Y=1.000 Z=1.000`)
