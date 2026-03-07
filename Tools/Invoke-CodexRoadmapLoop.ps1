[CmdletBinding()]
param(
	[string]$RepoRoot,
	[string]$RoadmapPath = "Plugins/Vergil/ROADMAP.md",
	[int]$MaxTickets = 100,
	[string]$CodexCommand = "codex",
	[string]$Model,
	[string]$Profile,
	[string]$SessionRoot,
	[string]$StateFilePath,
	[string]$ActiveRunFilePath,
	[switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
}

function Write-LoopState {
	param(
		[string]$Path,
		[hashtable]$State
	)

	if ([string]::IsNullOrWhiteSpace($Path)) {
		return
	}

	$StateJson = $State | ConvertTo-Json -Depth 8
	Set-Content -Path $Path -Value $StateJson -Encoding utf8
}

function Get-GitText {
	param(
		[string]$WorkingDirectory,
		[string[]]$Arguments
	)

	$Output = git -C $WorkingDirectory @Arguments 2>$null
	if ($LASTEXITCODE -ne 0) {
		throw "git $($Arguments -join ' ') failed."
	}

	return ($Output | Out-String).Trim()
}

function Get-UpstreamDivergence {
	param(
		[string]$WorkingDirectory
	)

	$Raw = git -C $WorkingDirectory rev-list --left-right --count HEAD...@{upstream} 2>$null
	if ($LASTEXITCODE -ne 0) {
		throw "git rev-list --left-right --count HEAD...@{upstream} failed."
	}

	$Parts = ($Raw | Out-String).Trim() -split "\s+"
	if ($Parts.Length -lt 2) {
		throw "Unable to parse upstream divergence output '$Raw'."
	}

	return @{
		Ahead = [int]$Parts[0]
		Behind = [int]$Parts[1]
	}
}

function Remove-ActiveRunFileIfOwned {
	param(
		[string]$Path,
		[string]$OwnedSessionRoot
	)

	if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
		return
	}

	try {
		$Active = Get-Content -Path $Path -Raw | ConvertFrom-Json
		if (($null -ne $Active.sessionRoot -and [string]$Active.sessionRoot -eq $OwnedSessionRoot) -or ($null -ne $Active.pid -and [int]$Active.pid -eq $PID)) {
			Remove-Item -Path $Path -Force
		}
	} catch {
		# Leave malformed ownership files alone so a human can inspect them.
	}
}

$ResolvedRepoRoot = (Resolve-Path $RepoRoot).Path
if ([string]::IsNullOrWhiteSpace($SessionRoot)) {
	$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
	$SessionRoot = Join-Path $ResolvedRepoRoot "Saved/CodexAutomation/RoadmapLoop/$Timestamp"
}

New-Item -ItemType Directory -Path $SessionRoot -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($StateFilePath)) {
	$StateFilePath = Join-Path $SessionRoot "state.json"
}

$ResolvedRoadmapPath = Join-Path $ResolvedRepoRoot $RoadmapPath
if (-not (Test-Path $ResolvedRoadmapPath)) {
	throw "Roadmap file '$ResolvedRoadmapPath' was not found."
}

$Prompt = @"
Continue with the next unfinished roadmap ticket for the Vergil plugin by inspecting $RoadmapPath.
Follow AGENTS.md and all repository instructions in this workspace.
Complete exactly one unfinished roadmap ticket end-to-end in this run: implement, verify, commit, and push to origin/Experimental.
If a prerequisite ticket is immediately required to complete the target ticket correctly, complete that prerequisite and then the blocked ticket in the same run.
Ignore unrelated untracked scratch files such as .codex_tmp_supported_docs_test.txt and tmp_supported_table.txt unless the active ticket truly requires them.
After the push succeeds, stop immediately and summarize the completed work.
If no unfinished roadmap tickets remain, respond with exactly NO_OPEN_TICKETS and make no changes.
Do not wait for another prompt.
"@

$BaseState = @{
	pid = $PID
	repoRoot = $ResolvedRepoRoot
	roadmapPath = $RoadmapPath
	sessionRoot = $SessionRoot
	state = "starting"
	startedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
	maxTickets = $MaxTickets
}
Write-LoopState -Path $StateFilePath -State $BaseState

if ($DryRun) {
	$DryRunState = $BaseState.Clone()
	$DryRunState.state = "dry_run"
	$DryRunState.prompt = $Prompt
	Write-LoopState -Path $StateFilePath -State $DryRunState
	Write-Output "Dry run only."
	Write-Output "Repo root: $ResolvedRepoRoot"
	Write-Output "Roadmap path: $RoadmapPath"
	Write-Output "Session root: $SessionRoot"
	Write-Output ""
	Write-Output $Prompt
	return
}

$LastCompletedHead = Get-GitText -WorkingDirectory $ResolvedRepoRoot -Arguments @("rev-parse", "HEAD")

try {
	for ($Iteration = 1; $Iteration -le $MaxTickets; $Iteration++) {
		$IterationRoot = Join-Path $SessionRoot ("iteration-{0:D3}" -f $Iteration)
		New-Item -ItemType Directory -Path $IterationRoot -Force | Out-Null

		$PromptPath = Join-Path $IterationRoot "prompt.txt"
		$StdoutPath = Join-Path $IterationRoot "codex.stdout.log"
		$StderrPath = Join-Path $IterationRoot "codex.stderr.log"
		$LastMessagePath = Join-Path $IterationRoot "last-message.txt"
		Set-Content -Path $PromptPath -Value $Prompt -Encoding utf8

		$BeforeHead = Get-GitText -WorkingDirectory $ResolvedRepoRoot -Arguments @("rev-parse", "HEAD")
		$BeforeStatus = Get-GitText -WorkingDirectory $ResolvedRepoRoot -Arguments @("status", "--short")

		$RunningState = @{
			pid = $PID
			repoRoot = $ResolvedRepoRoot
			roadmapPath = $RoadmapPath
			sessionRoot = $SessionRoot
			state = "running"
			startedAtUtc = $BaseState.startedAtUtc
			updatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
			iteration = $Iteration
			maxTickets = $MaxTickets
			beforeHead = $BeforeHead
			beforeStatus = $BeforeStatus
			iterationRoot = $IterationRoot
			lastMessagePath = $LastMessagePath
			stdoutPath = $StdoutPath
			stderrPath = $StderrPath
		}
		Write-LoopState -Path $StateFilePath -State $RunningState

		$CodexArgs = @(
			"exec",
			"--color", "never",
			"--dangerously-bypass-approvals-and-sandbox",
			"-C", $ResolvedRepoRoot,
			"-o", $LastMessagePath
		)
		if (-not [string]::IsNullOrWhiteSpace($Profile)) {
			$CodexArgs += @("--profile", $Profile)
		}
		if (-not [string]::IsNullOrWhiteSpace($Model)) {
			$CodexArgs += @("--model", $Model)
		}
		$CodexArgs += $Prompt

		& $CodexCommand @CodexArgs 1> $StdoutPath 2> $StderrPath
		$ExitCode = $LASTEXITCODE

		$LastMessage = if (Test-Path $LastMessagePath) {
			(Get-Content -Path $LastMessagePath -Raw).Trim()
		} else {
			""
		}

		$AfterHead = Get-GitText -WorkingDirectory $ResolvedRepoRoot -Arguments @("rev-parse", "HEAD")
		$AfterStatus = Get-GitText -WorkingDirectory $ResolvedRepoRoot -Arguments @("status", "--short")

		if ($ExitCode -ne 0) {
			$FailedState = @{
				pid = $PID
				repoRoot = $ResolvedRepoRoot
				roadmapPath = $RoadmapPath
				sessionRoot = $SessionRoot
				state = "failed"
				startedAtUtc = $BaseState.startedAtUtc
				finishedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
				iteration = $Iteration
				exitCode = $ExitCode
				beforeHead = $BeforeHead
				afterHead = $AfterHead
				afterStatus = $AfterStatus
				lastMessage = $LastMessage
				iterationRoot = $IterationRoot
				stdoutPath = $StdoutPath
				stderrPath = $StderrPath
			}
			Write-LoopState -Path $StateFilePath -State $FailedState
			throw "codex exec failed in iteration $Iteration with exit code $ExitCode."
		}

		if ($LastMessage -eq "NO_OPEN_TICKETS") {
			$CompletedState = @{
				pid = $PID
				repoRoot = $ResolvedRepoRoot
				roadmapPath = $RoadmapPath
				sessionRoot = $SessionRoot
				state = "no_open_tickets"
				startedAtUtc = $BaseState.startedAtUtc
				finishedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
				iteration = $Iteration
				beforeHead = $BeforeHead
				afterHead = $AfterHead
				afterStatus = $AfterStatus
				lastMessage = $LastMessage
				iterationRoot = $IterationRoot
			}
			Write-LoopState -Path $StateFilePath -State $CompletedState
			return
		}

		if ($AfterHead -eq $BeforeHead) {
			$FailedState = @{
				pid = $PID
				repoRoot = $ResolvedRepoRoot
				roadmapPath = $RoadmapPath
				sessionRoot = $SessionRoot
				state = "failed_no_head_change"
				startedAtUtc = $BaseState.startedAtUtc
				finishedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
				iteration = $Iteration
				beforeHead = $BeforeHead
				afterHead = $AfterHead
				afterStatus = $AfterStatus
				lastMessage = $LastMessage
				iterationRoot = $IterationRoot
			}
			Write-LoopState -Path $StateFilePath -State $FailedState
			throw "codex exec completed without changing HEAD in iteration $Iteration."
		}

		$Divergence = Get-UpstreamDivergence -WorkingDirectory $ResolvedRepoRoot
		if ($Divergence.Ahead -ne 0) {
			$FailedState = @{
				pid = $PID
				repoRoot = $ResolvedRepoRoot
				roadmapPath = $RoadmapPath
				sessionRoot = $SessionRoot
				state = "failed_not_pushed"
				startedAtUtc = $BaseState.startedAtUtc
				finishedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
				iteration = $Iteration
				beforeHead = $BeforeHead
				afterHead = $AfterHead
				afterStatus = $AfterStatus
				ahead = $Divergence.Ahead
				behind = $Divergence.Behind
				lastMessage = $LastMessage
				iterationRoot = $IterationRoot
			}
			Write-LoopState -Path $StateFilePath -State $FailedState
			throw "HEAD advanced to $AfterHead but the branch is still ahead of upstream by $($Divergence.Ahead) commit(s)."
		}

		$SucceededState = @{
			pid = $PID
			repoRoot = $ResolvedRepoRoot
			roadmapPath = $RoadmapPath
			sessionRoot = $SessionRoot
			state = "ticket_completed"
			startedAtUtc = $BaseState.startedAtUtc
			updatedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
			iteration = $Iteration
			beforeHead = $BeforeHead
			afterHead = $AfterHead
			afterStatus = $AfterStatus
			ahead = $Divergence.Ahead
			behind = $Divergence.Behind
			lastMessage = $LastMessage
			iterationRoot = $IterationRoot
		}
		Write-LoopState -Path $StateFilePath -State $SucceededState
		$LastCompletedHead = $AfterHead
	}

	$MaxedState = @{
		pid = $PID
		repoRoot = $ResolvedRepoRoot
		roadmapPath = $RoadmapPath
		sessionRoot = $SessionRoot
		state = "max_tickets_reached"
		startedAtUtc = $BaseState.startedAtUtc
		finishedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
		maxTickets = $MaxTickets
		lastCompletedHead = $LastCompletedHead
	}
	Write-LoopState -Path $StateFilePath -State $MaxedState
} finally {
	Remove-ActiveRunFileIfOwned -Path $ActiveRunFilePath -OwnedSessionRoot $SessionRoot
}
