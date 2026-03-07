[CmdletBinding()]
param(
	[string]$RepoRoot,
	[switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
}

$ResolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$ActiveRunFilePath = Join-Path $ResolvedRepoRoot "Saved/CodexAutomation/RoadmapLoop/active-run.json"
if (-not (Test-Path $ActiveRunFilePath)) {
	throw "No active Codex roadmap loop file was found at '$ActiveRunFilePath'."
}

$ActiveRun = Get-Content -Path $ActiveRunFilePath -Raw | ConvertFrom-Json
$Process = Get-Process -Id ([int]$ActiveRun.pid) -ErrorAction SilentlyContinue
if ($null -ne $Process) {
	Stop-Process -Id $Process.Id -Force:$Force
}

Remove-Item -Path $ActiveRunFilePath -Force

[pscustomobject]@{
	stoppedPid = [int]$ActiveRun.pid
	sessionRoot = [string]$ActiveRun.sessionRoot
	stateFilePath = [string]$ActiveRun.stateFilePath
}
