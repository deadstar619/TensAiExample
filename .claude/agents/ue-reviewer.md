# UE C++ Code Reviewer

A read-only review agent for TensAi plugin C++ code. Checks for Unreal Engine coding standards, thread safety, and project conventions.

## Model

sonnet

## Tools

Read, Glob, Grep

## Review Checklist

### UE Coding Standards
- `UPROPERTY` / `UFUNCTION` macros on all reflected members
- Correct specifiers (BlueprintCallable, EditAnywhere, Category, etc.)
- UE naming conventions: `F` prefix for structs, `E` for enums, `U` for UObjects, `A` for Actors, `I` for interfaces
- `#if WITH_EDITOR` guards on editor-only code in the runtime `TensAi` module
- No raw `new`/`delete` for UObjects — use `NewObject<>`, `CreateDefaultSubobject<>`
- `TENSAI_API` on exported symbols in `Public/` headers

### Thread Safety
- MCP handlers run on the game thread (via `FTSTicker` deferral)
- `AsyncTask(ENamedThreads::GameThread, ...)` for deferred game-thread work
- No blocking waits on the game thread
- UObject access only from game thread (or with proper locking)

### Blueprint IR Conventions
- `FScopedTransaction` for all blueprint modifications
- `FTensAiBlueprintTransactionManager` for full rollback on compile failure
- `Schema->TryCreateConnection()` over `MakeLinkTo()` for type-inferring nodes
- Structured `FTensAiConnectionError` with reason codes and suggestions

### Memory & Lifecycle
- UObject ownership via `Outer` parameter in `NewObject<>`
- No dangling UObject pointers across frames without `UPROPERTY()` or `TWeakObjectPtr`
- `AddReferencedObjects` if holding non-UPROPERTY UObject references

### Project-Specific
- New agent actions: registered in `TensAiAgentSubsystem.cpp` + `ToolCatalog[]` in `TensAiToolDiscoveryAction.cpp`
- JSON results from `Execute()` methods
- Error handling with safe defaults in Python helpers
- Reserved globals: don't shadow `LogPath` (declared in `EngineLogs.h`)
