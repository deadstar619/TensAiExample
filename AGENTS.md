# Repository Instructions

## Ticket Workflow

- When working roadmap tickets for this repository, default to completing the current ticket end-to-end in one pass: implement, verify, commit, and push, unless the user explicitly asks to pause before commit or push.
- After a ticket has been committed and pushed successfully, immediately look up the next roadmap ticket and start it without waiting for another prompt, unless the user has redirected the work.
- Keep each commit scoped to the ticket that was just completed. Do not bundle unrelated dirty files into the commit.
- For unattended chaining outside an active chat turn, use `Tools/Start-CodexRoadmapLoop.ps1` to launch the detached Codex roadmap loop, `Tools/Get-CodexRoadmapLoopStatus.ps1` to inspect it, and `Tools/Stop-CodexRoadmapLoop.ps1` to stop it.
- User-facing usage notes for that detached loop live in `Tools/CodexRoadmapLoop.md`.

## Vergil / TensAi Constraints

- Do not use TensAi tools or utilities except `execute_python` and bridge/editor-management tools such as build, launch, close, and wait.
- If a missing capability is required to complete a ticket correctly and immediately, add that prerequisite as a ticket first, complete it, and then resume the blocked ticket.
- Run Vergil scaffold automation serially, not in parallel, because the documented automation runner reuses shared log paths.
