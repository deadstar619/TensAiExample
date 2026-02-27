# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TensAiExample is an Unreal Engine 5.7 project containing the **TensAi** plugin — an AI-powered editor assistant that translates natural language into deterministic, validated engine operations. Supports multiple AI providers (Anthropic Claude, OpenAI-compatible endpoints). Core capabilities: blueprint graph construction via declarative IR, level prototyping, procedural mesh/texture/material creation, and Python script execution.

## Build Commands

This is a standard UE 5.7 C++ project. Build from the `.sln` or via Unreal Build Tool:

```bash
# Build from command line (adjust engine path as needed)
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" TensAiExampleEditor Win64 Development "C:/Unreal 5.7 Projects/TensAiExample/TensAiExample.uproject" -waitmutex

# Generate project files
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/GenerateProjectFiles.bat" "C:/Unreal 5.7 Projects/TensAiExample/TensAiExample.uproject" -game
```

Build targets: `TensAiExampleTarget` (Game) and `TensAiExampleEditorTarget` (Editor). Both use `BuildSettingsVersion.V6` and `IncludeOrderVersion.Unreal5_7`.

## Architecture

### Modules

| Module | Type | Location |
|---|---|---|
| `TensAiExample` | Runtime (game) | `Source/TensAiExample/` |
| `TensAi` | Runtime (plugin, editor-aware) | `Plugins/TensAi/Source/TensAi/` |
| `TensAiWebChat` | Editor-only (chat UI) | `Plugins/TensAi/Source/TensAiWebChat/` |

The `TensAi` module conditionally includes editor dependencies behind `Target.bBuildEditor` guards in its Build.cs. Code inside the module must use `#if WITH_EDITOR` for editor-only functionality.

### Plugin Core Systems

**Agent Subsystem** (`TensAiAgentSubsystem`) — Engine subsystem that manages the AI conversation loop. Registers all agent actions (tools), handles message I/O via delegates (`OnMessage`, `OnBusyChanged`, `OnToolInvoked`, `OnToolResult`, `OnError`, `OnTokenUsageUpdated`).

**API Service** (`TensAiAPIService`) — HTTP client with provider abstraction. Supports Anthropic Claude (Opus 4.6, Sonnet 4.5, Haiku 4.5 with extended thinking) and any OpenAI-compatible endpoint (GPT-4o, Ollama, LM Studio, Azure). Handles text, image (base64), and document content types.

**Conversation Manager** (`TensAiConversation`) — Multi-turn agentic conversation with automatic tool invocation loop. Wraps operations in transactions for undo support. Caps at 5 agent iterations per message by default.

**Blueprint Function Library** (`TensAiEditorLibrary`) — 85+ `UFUNCTION`s exposed to Blueprints/Python for graph manipulation, flow control, custom functions, event dispatchers, introspection, input system, UMG widget creation, and component manipulation.

### Agent Actions (AI Tools)

Each action extends `UTensAiAgentAction` and implements `GetToolName()`, `GetToolDescription()`, `GetInputSchema()`, and `Execute()`. Located in `Plugins/TensAi/Source/TensAi/Private/Agent/AgentActions/` (28 action classes, 80+ tools):

- **BlueprintAssistAction** — `analyze_blueprint`, `list_blueprint_assets`, `search_classes`, `get_log_errors`, `execute_console_command`
- **BlueprintIRAction** — `compile_blueprint_ir`, `snapshot_blueprint`, `preview_blueprint_changes`
- **GraphIntrospectionAction** — `inspect_graph_schema`, `enumerate_node_types`, `inspect_node_template`, `validate_connection`, `get_pin_context_actions`, `get_ir_schema`
- **GraphSemanticAction** — `analyze_graph_semantics`
- **CompilationDiagnosticsAction** — `get_compilation_diagnostics`
- **LevelPrototypeAction** — `spawn_actor`, `delete_actor`, `modify_actor`, `list_actors`, `get_actor_details`
- **SceneOperationsAction** — `duplicate_actor`, `batch_modify_actors`, `manipulate_component`
- **MeshGenerationAction** — `generate_procedural_mesh`, `generate_primitive_mesh`
- **TextureGenerationAction** — `generate_texture`, `create_material`
- **MaterialAction** — `get_material_info`, `set_material_parameters`, `build_material_graph`
- **PythonScriptAction** — `execute_python`, `get_python_api_help`
- **EditorContextAction** — `get_editor_context`, `get_selected_actors`, `get_graph_selection`, `select_actors`, `get_actor_properties`, `set_actor_property`, `import_asset`, `list_project_assets`, `take_viewport_screenshot`, `focus_viewport`, `read_file_contents`, `write_file`, `list_directory`, `open_asset_editor`, `undo_redo`, `get_recent_logs`, `focus_graph_node`, `save_asset`, `compile_blueprint`, `notify_user`, `get_viewport_info`
- **KnowledgeBaseAction** — `query_knowledge_base`
- **ReflectionAction** — `reflect_type`, `search_functions`
- **ResearchAction** — `research_subsystem`
- **CodeModeAction** — `read_engine_source`
- **CodeSchemaAction** — `get_api_schema`, `regenerate_api_stubs`
- **BatchAssetAction** — `batch_execute`
- **BindingGeneratorAction** — `analyze_api_gap`, `generate_cpp_binding`
- **NativizeAction** — `nativize_blueprint`, `scaffold_module`
- **ReparentAction** — `reparent_blueprint`
- **PIEControlAction** — `pie_control`, `query_pie_state`
- **LiveCodingAction** — `live_coding`
- **WidgetTreeAction** — `widget_tree`
- **AssetReferenceAction** — `analyze_asset_references`
- **SelfTestAction** — `self_test`
- **ToolDiscoveryAction** — `get_available_tools`

### Python Layer

`Plugins/TensAi/Content/Python/tensai_helpers.py` (4K lines, 140+ functions) provides high-level abstractions over the UE Python API. Auto-loaded by `init_unreal.py` when the editor starts. Covers blueprints, components, properties, variables, actors, transforms, physics, graph manipulation, flow control, input, UMG, and introspection.

### Knowledge Base

`Plugins/TensAi/Resources/Knowledge/` contains AI-consumable reference docs queried by the `KnowledgeBaseAction` tool at runtime: `PythonAPIReference.md`, `Recipes_FlowControl.md`, `Recipes_Functions.md`, `Recipes_Input.md`, `Recipes_UI.md`, `Recipes_Materials.md`, `Recipes_MaterialGraph.md`, `Recipes_Transforms.md`, `Recipes_LevelBuilding.md`.

### Project Context

`TensAiProjectContextAnalyzer` gathers runtime snapshots (project name, engine version, current level, loaded plugins, actor summary, recent errors) and injects them into the AI system prompt.

## Configuration

- **API key:** Stored in `Config/DefaultTensAi.ini` under `[/Script/TensAi.TensAiSettings]`
- **Rendering:** Ray tracing, Substrate, and dynamic GI are enabled in `DefaultEngine.ini`
- **Default map:** `/Game/Maps/TensAI_DemoMap`

## Development Conventions

- Agent actions follow a consistent pattern: inherit `UTensAiAgentAction`, register via the subsystem, return JSON results from `Execute()`
- Python helpers use class resolution aliases (e.g., `"StaticMesh"` → `"StaticMeshComponent"`) and always include error handling with safe defaults
- Editor-only code in the runtime `TensAi` module must be guarded with `#if WITH_EDITOR`
- All Python execution is wrapped in UE transactions for undo/redo support
- Blueprint IR is the preferred method for graph construction (deterministic, validated, diffable)
- Cross-module headers must be in `Public/` directories; use `TENSAI_API` for exported symbols

## MCP Tool Usage Rules

When calling TensAi MCP tools (especially `execute_python`), **never speculate on APIs**:

- **Only call methods/properties that have been verified** — via `get_python_api_help`, `query_knowledge_base`, or a prior successful call in the same session
- If unsure whether a method exists, **introspect first** (`get_python_api_help` with `dir` or `help`) before writing code that uses it
- Do not add speculative debug prints or exploratory calls after the main operation — if the task is done, stop
- A failed `execute_python` call surfaces as an error in the Unreal Editor log, even if the actual work succeeded before the failing line

## Roadmap

See `Plugins/TensAi/ROADMAP.md`.
