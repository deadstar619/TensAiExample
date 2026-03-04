# Full Rebuild Cycle

Close the Unreal Editor, build the project, and relaunch. Use this after C++ changes that require a full UBT build (new UCLASS, Build.cs changes, MCP server changes).

## Steps

1. **Close the editor** — use `close_editor` MCP tool (saves all dirty packages first)
2. **Kill leftover processes** — run `taskkill //F //IM LiveCodingConsole.exe 2>/dev/null` (may not be running, ignore errors)
3. **Build the project** — use `build_project` MCP tool with Development configuration
4. **Check build result** — if build fails, use `get_build_diagnostics` and report errors to the user. Do NOT relaunch on failure.
5. **Relaunch the editor** — use `launch_editor` MCP tool (waits for MCP connection)
6. **Confirm ready** — report build success and editor status to the user

## When to use

- New `UCLASS` added (UHT header regeneration needed)
- `.Build.cs` modified (module dependency changes)
- MCP server code changed (Live Coding corrupts HTTP routes)
- Any change that Live Coding cannot handle

## When NOT to use

- Modifying existing C++ classes — use `live_coding compile` instead (faster, no editor restart)
- Blueprint-only changes — no build needed at all
