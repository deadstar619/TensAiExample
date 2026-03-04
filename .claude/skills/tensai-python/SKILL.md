# tensai-python

Guidance for writing Python code executed via the TensAi `execute_python` MCP tool.

## Activation

Auto-activate when:
- Using `execute_python` to manipulate blueprints, actors, or assets
- Spawning or manipulating actors/blueprints via Python
- The user asks to run Python in the Unreal Editor

## Rules

### Always use tensai_helpers first

```python
import tensai_helpers as ch
```

Before using any raw `unreal` module call, check if `tensai_helpers` has a function for it. The helpers are validation-safe, have error handling, and bypass the pre-execution validator that blocks raw calls like `unreal.get_editor_subsystem`.

**Discovery pattern:**
```python
print([m for m in dir(ch) if 'keyword' in m.lower()])
```

Only use raw `unreal` when no helper exists.

### No speculative code

Only call APIs that have been verified via:
- `get_python_api_help` (with `dir` or `help`)
- `query_knowledge_base`
- A prior successful call in the same session

Never guess at method/property names. If unsure, introspect first.

### Key function signatures

See [references/common-helpers.md](references/common-helpers.md) for the most-used functions.

**Critical gotchas:**
- `ch.create_blueprint(name, path, parent_class)` — name is just the asset name, path is the directory. Never pass a full path as name.
- `ch.spawn_blueprint_actor(blueprint_path, label, location)` — preferred for spawning BP actors in the level
- Do NOT use `unreal.get_editor_subsystem(unreal.EditorActorSubsystem)` directly — blocked by pre-execution validator
- Do NOT use deprecated `EditorLevelLibrary`
- A failed line in `execute_python` surfaces as an error in the editor log even if prior lines succeeded — keep scripts minimal
