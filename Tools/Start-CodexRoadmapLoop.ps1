[CmdletBinding()]
param(
	[string]$RepoRoot,
	[string]$RoadmapPath = "Plugins/Vergil/ROADMAP.md",
	[int]$MaxTickets = 100,
	[string]$Model,
	[string]$Profile,
	[switch]$DryRun,
	[switch]$ForceRestart
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
}

$ResolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$LoopRoot = Join-Path $ResolvedRepoRoot "Saved/CodexAutomation/RoadmapLoop"
New-Item -ItemType Directory -Path $LoopRoot -Force | Out-Null

$ActiveRunFilePath = Join-Path $LoopRoot "active-run.json"
if ((Test-Path $ActiveRunFilePath) -and -not $ForceRestart) {
	$Existing = Get-Content -Path $ActiveRunFilePath -Raw | ConvertFrom-Json
	$ExistingProcess = Get-Process -Id ([int]$Existing.pid) -ErrorAction SilentlyContinue
	if ($null -ne $ExistingProcess) {
		throw "A Codex roadmap loop is already running as PID $($Existing.pid). Use Tools/Get-CodexRoadmapLoopStatus.ps1 to inspect it or Tools/Stop-CodexRoadmapLoop.ps1 to stop it."
	}
}

$Timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$SessionRoot = Join-Path $LoopRoot $Timestamp
New-Item -ItemType Directory -Path $SessionRoot -Force | Out-Null

$StateFilePath = Join-Path $SessionRoot "state.json"
$LauncherStdoutPath = Join-Path $SessionRoot "launcher.stdout.log"
$LauncherStderrPath = Join-Path $SessionRoot "launcher.stderr.log"
$InvokeScriptPath = Join-Path $PSScriptRoot "Invoke-CodexRoadmapLoop.ps1"
$HostProcessPath = (Get-Process -Id $PID).Path

$ArgumentList = @(
	"-NoProfile",
	"-ExecutionPolicy", "Bypass",
	"-File", $InvokeScriptPath,
	"-RepoRoot", $ResolvedRepoRoot,
	"-RoadmapPath", $RoadmapPath,
	"-MaxTickets", [string]$MaxTickets,
	"-SessionRoot", $SessionRoot,
	"-StateFilePath", $StateFilePath,
	"-ActiveRunFilePath", $ActiveRunFilePath
)
if (-not [string]::IsNullOrWhiteSpace($Model)) {
	$ArgumentList += @("-Model", $Model)
}
if (-not [string]::IsNullOrWhiteSpace($Profile)) {
	$ArgumentList += @("-Profile", $Profile)
}

if ($DryRun) {
	$Preview = @{
		hostProcessPath = $HostProcessPath
		repoRoot = $ResolvedRepoRoot
		roadmapPath = $RoadmapPath
		maxTickets = $MaxTickets
		sessionRoot = $SessionRoot
		stateFilePath = $StateFilePath
		activeRunFilePath = $ActiveRunFilePath
		argumentList = $ArgumentList
	}
	$Preview | ConvertTo-Json -Depth 6
	return
}

$Process = Start-Process `
	-FilePath $HostProcessPath `
	-ArgumentList $ArgumentList `
	-WorkingDirectory $ResolvedRepoRoot `
	-RedirectStandardOutput $LauncherStdoutPath `
	-RedirectStandardError $LauncherStderrPath `
	-PassThru

$ActiveRun = @{
	pid = $Process.Id
	repoRoot = $ResolvedRepoRoot
	roadmapPath = $RoadmapPath
	startedAtUtc = (Get-Date).ToUniversalTime().ToString("o")
	sessionRoot = $SessionRoot
	stateFilePath = $StateFilePath
	launcherStdoutPath = $LauncherStdoutPath
	launcherStderrPath = $LauncherStderrPath
}

Set-Content -Path $ActiveRunFilePath -Value ($ActiveRun | ConvertTo-Json -Depth 6) -Encoding utf8

$ActiveRun
