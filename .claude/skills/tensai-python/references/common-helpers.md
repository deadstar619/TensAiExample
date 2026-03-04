# Common tensai_helpers Functions

`import tensai_helpers as ch`

## Blueprint Creation & Management

| Function | Signature | Notes |
|---|---|---|
| `create_blueprint` | `ch.create_blueprint(name, path, parent_class)` | name = asset name only, path = directory. Full paths auto-split. |
| `open_blueprint` | `ch.open_blueprint(blueprint_path)` | Opens BP in editor. Returns the blueprint object. |

## Actor Spawning

| Function | Signature | Notes |
|---|---|---|
| `spawn_blueprint_actor` | `ch.spawn_blueprint_actor(blueprint_path, label=None, location=None)` | Preferred for BP actors. Don't use raw subsystem calls. |
| `spawn_actor` | `ch.spawn_actor(actor_class, label=None, location=None)` | For native classes only. Crashes on BP classes (`__name__` issue). |

## Properties & Variables

| Function | Signature | Notes |
|---|---|---|
| `set_property` | `ch.set_property(obj, prop_name, value)` | Safe property setter with type coercion. |
| `get_property` | `ch.get_property(obj, prop_name)` | Safe property getter. |
| `add_variable` | `ch.add_variable(blueprint, var_name, var_type, default_value=None)` | Add BP variable. |

## Components

| Function | Signature | Notes |
|---|---|---|
| `add_component` | `ch.add_component(blueprint, component_class, name=None)` | Uses class aliases (e.g. "StaticMesh" -> "StaticMeshComponent"). |

## Discovery

When you need a function that isn't listed here, discover it:
```python
# Search by keyword
print([m for m in dir(ch) if 'keyword' in m.lower()])

# Get help on a specific function
help(ch.function_name)
```

Always verify a function exists before calling it. Never guess.
