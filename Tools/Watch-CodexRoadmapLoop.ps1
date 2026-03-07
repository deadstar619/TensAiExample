[CmdletBinding()]
param(
	[string]$RepoRoot,
	[string]$SessionRoot,
	[ValidateSet("stderr", "stdout", "launcher-stderr", "launcher-stdout")]
	[string]$Stream = "stderr",
	[int]$Tail = 80,
	[switch]$NoWait
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
	$RepoRoot = (Resolve-Path (Join-Path (Split-Path -Parent $PSCommandPath) "..")).Path
}

function Get-OptionalPropertyValue {
	param(
		[object]$InputObject,
		[string]$PropertyName
	)

	if ($null -eq $InputObject) {
		return $null
	}

	$Property = $InputObject.PSObject.Properties[$PropertyName]
	if ($null -eq $Property) {
		return $null
	}

	return $Property.Value
}

$ResolvedRepoRoot = (Resolve-Path $RepoRoot).Path
$LoopRoot = Join-Path $ResolvedRepoRoot "Saved/CodexAutomation/RoadmapLoop"

if ([string]::IsNullOrWhiteSpace($SessionRoot)) {
	$ActiveRunFilePath = Join-Path $LoopRoot "active-run.json"
	if (-not (Test-Path $ActiveRunFilePath)) {
		throw "No active roadmap loop was found. Start one first or pass -SessionRoot."
	}

	$ActiveRun = Get-Content -Path $ActiveRunFilePath -Raw | ConvertFrom-Json
	$SessionRoot = [string]$ActiveRun.sessionRoot
}

$StateFilePath = Join-Path $SessionRoot "state.json"
if (-not (Test-Path $StateFilePath)) {
	throw "No state file was found at '$StateFilePath'."
}

$State = Get-Content -Path $StateFilePath -Raw | ConvertFrom-Json

switch ($Stream) {
	"stderr" {
		$LogPath = [string](Get-OptionalPropertyValue -InputObject $State -PropertyName "stderrPath")
		if ([string]::IsNullOrWhiteSpace($LogPath)) {
			throw "The active state does not expose stderrPath yet."
		}
	}
	"stdout" {
		$LogPath = [string](Get-OptionalPropertyValue -InputObject $State -PropertyName "stdoutPath")
		if ([string]::IsNullOrWhiteSpace($LogPath)) {
			throw "The active state does not expose stdoutPath yet."
		}
	}
	"launcher-stderr" {
		$LogPath = Join-Path $SessionRoot "launcher.stderr.log"
	}
	"launcher-stdout" {
		$LogPath = Join-Path $SessionRoot "launcher.stdout.log"
	}
	default {
		throw "Unsupported stream '$Stream'."
	}
}

if (-not (Test-Path $LogPath)) {
	throw "The selected log file '$LogPath' does not exist yet."
}

Write-Output ("Watching {0}" -f $LogPath)
Write-Output ""

if ($NoWait) {
	Get-Content -Path $LogPath -Tail $Tail -Encoding UTF8
} else {
	Get-Content -Path $LogPath -Tail $Tail -Wait -Encoding UTF8
}
