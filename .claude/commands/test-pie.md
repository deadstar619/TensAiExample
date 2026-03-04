# PIE Test Workflow

Test a blueprint by spawning it in the level and running PIE. Argument: blueprint path (e.g. `/Game/Tests/BP_MyTest`).

## Steps

1. **Save the asset** — use `save_asset` with the blueprint path
2. **Spawn the BP actor** — use `execute_python`:
   ```python
   import tensai_helpers as ch
   ch.spawn_blueprint_actor('$ARGUMENTS', label='PIETestActor')
   ```
   If `ch.spawn_blueprint_actor` is unavailable, fall back to `spawn_actor` MCP tool with `{"blueprint": "$ARGUMENTS", "label": "PIETestActor"}`
3. **Start PIE** — use `pie_control` with `{"action": "start"}`
4. **Wait for output** — wait 3 seconds, then use `get_recent_logs` with `{"count": 30, "filter": "LogBlueprintUserMessages"}` to capture PrintString output
5. **Stop PIE** — use `pie_control` with `{"action": "stop"}`
6. **Report results** — show the user the captured log output. This is the source of truth for verification — do NOT take screenshots.

## Important

- Do NOT use screenshots to verify results — printed output / log messages are the single source of truth
- If PIE fails to start, check `get_compilation_diagnostics` for blueprint compile errors
- Clean up: after stopping PIE, the spawned test actor remains in the level. Mention this to the user.
