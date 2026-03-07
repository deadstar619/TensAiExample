# Codex Roadmap Loop

This repo includes a detached Codex driver that can keep working roadmap tickets without another chat message after each push.

## What it does

- launches a background PowerShell process
- runs `codex exec` against this repo in a loop
- asks Codex to complete exactly one unfinished Vergil roadmap ticket per iteration
- expects each iteration to implement, verify, commit, and push
- stops the iteration if Codex reports `NO_OPEN_TICKETS`

The loop currently targets [Plugins/Vergil/ROADMAP.md](../Plugins/Vergil/ROADMAP.md).

## Start

From the repo root:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Start-CodexRoadmapLoop.ps1
```

Useful options:

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Start-CodexRoadmapLoop.ps1 -ForceRestart
powershell -ExecutionPolicy Bypass -File .\Tools\Start-CodexRoadmapLoop.ps1 -MaxTickets 3
powershell -ExecutionPolicy Bypass -File .\Tools\Start-CodexRoadmapLoop.ps1 -DryRun
```

## Check status

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Get-CodexRoadmapLoopStatus.ps1
```

Important fields:

- `state`: current loop state such as `running`, `ticket_completed`, `failed`, or `no_open_tickets`
- `pid`: detached PowerShell PID
- `sessionRoot`: folder for logs and per-iteration artifacts
- `iteration`: current loop iteration

## Stop

```powershell
powershell -ExecutionPolicy Bypass -File .\Tools\Stop-CodexRoadmapLoop.ps1 -Force
```

Use this before making manual commits on the same branch. The loop records the starting `HEAD` for each iteration and expects to be the only writer for that ticket run.

## Logs and state

Each run creates a session folder under:

```text
Saved/CodexAutomation/RoadmapLoop/<timestamp>/
```

Useful files:

- `active-run.json`: current detached run metadata
- `state.json`: latest loop state
- `launcher.stdout.log` / `launcher.stderr.log`: detached PowerShell output
- `iteration-###/prompt.txt`: exact prompt sent to Codex for that ticket
- `iteration-###/codex.stdout.log`: Codex exec output
- `iteration-###/last-message.txt`: Codex final message for that iteration

## Operating rules

- Start the loop from a clean tracked worktree. Untracked scratch files are fine if they are unrelated.
- Stop the loop before manual commits, rebases, or branch switches.
- If the loop reports `failed_not_pushed` or `failed_no_head_change`, inspect the iteration logs before restarting.
- The scripts prefer `codex.cmd` rather than the PowerShell shim because detached PowerShell runs handled the shim poorly.

## Typical workflow

1. Start the loop.
2. Check status occasionally.
3. Read the latest session logs if a run fails.
4. Stop the loop before doing unrelated manual git work.
5. Restart with `-ForceRestart` after you resolve the issue.
