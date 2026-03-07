param(
	[string]$EngineRoot = '',
	[string]$UnrealBuildToolPath = '',
	[string]$ProjectPath = '',
	[string]$Target = 'TensAiExampleEditor',
	[string]$Platform = 'Win64',
	[string]$Configuration = 'Development',
	[string]$LogPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VergilCICommon.ps1')

$projectRoot = Get-VergilProjectRoot
$resolvedProjectPath = Resolve-VergilProjectPath -ProjectPath $ProjectPath
$resolvedUnrealBuildToolPath = Resolve-VergilUnrealBuildToolPath -UnrealBuildToolPath $UnrealBuildToolPath -EngineRoot $EngineRoot
$outputDirectory = Get-VergilCILaneOutputDirectory -Lane 'Build' -ProjectRoot $projectRoot

if ([string]::IsNullOrWhiteSpace($LogPath)) {
	$LogPath = Join-Path $outputDirectory 'UnrealBuildTool.log'
}

$logDirectory = Split-Path -Parent $LogPath
if (-not (Test-Path -LiteralPath $logDirectory)) {
	New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
}

if (Test-Path -LiteralPath $LogPath) {
	Remove-Item -LiteralPath $LogPath -Force
}

$arguments = @(
	$Target
	$Platform
	$Configuration
	("-Project={0}" -f $resolvedProjectPath)
	'-WaitMutex'
	'-NoHotReloadFromIDE'
	'-NoUBTMakefiles'
)

Write-Output 'Running Vergil project build:'
Write-Output ('"{0}" {1}' -f $resolvedUnrealBuildToolPath, ($arguments -join ' '))
Write-Output ("Build log: $LogPath")

& $resolvedUnrealBuildToolPath @arguments *>&1 | Tee-Object -FilePath $LogPath
$exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }

if ($exitCode -ne 0) {
	Write-Error ("Vergil project build failed. See {0}" -f $LogPath)
}

exit $exitCode
