[CmdletBinding()]
param(
	[string]$RepoRoot,
	[string]$SessionRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
}

$ResolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$LoopRoot = Join-Path $ResolvedRepoRoot "Saved/CodexAutomation/RoadmapLoop"

if (-not [string]::IsNullOrWhiteSpace($SessionRoot)) {
	$StateFilePath = Join-Path $SessionRoot "state.json"
	if (-not (Test-Path $StateFilePath)) {
		throw "No state file was found at '$StateFilePath'."
	}

	return Get-Content -Path $StateFilePath -Raw | ConvertFrom-Json
}

$ActiveRunFilePath = Join-Path $LoopRoot "active-run.json"
if (-not (Test-Path $ActiveRunFilePath)) {
	return [pscustomobject]@{
		state = "inactive"
		loopRoot = $LoopRoot
	}
}

$ActiveRun = Get-Content -Path $ActiveRunFilePath -Raw | ConvertFrom-Json
$State = $null
if ($null -ne $ActiveRun.stateFilePath -and (Test-Path ([string]$ActiveRun.stateFilePath))) {
	$State = Get-Content -Path ([string]$ActiveRun.stateFilePath) -Raw | ConvertFrom-Json
}

[pscustomobject]@{
	state = if ($null -ne $State) { [string]$State.state } else { "starting" }
	pid = [int]$ActiveRun.pid
	processRunning = $null -ne (Get-Process -Id ([int]$ActiveRun.pid) -ErrorAction SilentlyContinue)
	repoRoot = [string]$ActiveRun.repoRoot
	sessionRoot = [string]$ActiveRun.sessionRoot
	stateFilePath = [string]$ActiveRun.stateFilePath
	launcherStdoutPath = [string]$ActiveRun.launcherStdoutPath
	launcherStderrPath = [string]$ActiveRun.launcherStderrPath
	updatedAtUtc = if ($null -ne $State -and $null -ne $State.updatedAtUtc) { [string]$State.updatedAtUtc } else { $null }
	iteration = if ($null -ne $State -and $null -ne $State.iteration) { [int]$State.iteration } else { $null }
	maxTickets = if ($null -ne $State -and $null -ne $State.maxTickets) { [int]$State.maxTickets } else { $null }
	lastMessage = if ($null -ne $State -and $null -ne $State.lastMessage) { [string]$State.lastMessage } else { $null }
}
