param(
	[string]$EditorCmdPath = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe',

	[string]$ProjectPath = '',

	[string]$TestFilter = 'Vergil.Scaffold',

	[string]$LogPath = '',

	[switch]$SkipSummary,

	[switch]$SummaryAsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
	$ProjectPath = Join-Path $projectRoot 'TensAiExample.uproject'
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
	$LogPath = Join-Path $projectRoot 'Saved\Logs\VergilAutomation_Scaffold.log'
}

$resolvedEditorCmdPath = (Resolve-Path -LiteralPath $EditorCmdPath).Path
$resolvedProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$logDirectory = Split-Path -Parent $LogPath
if (-not (Test-Path -LiteralPath $logDirectory)) {
	New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
}

if (Test-Path -LiteralPath $LogPath) {
	Remove-Item -LiteralPath $LogPath -Force
}

$summaryScriptPath = Join-Path $PSScriptRoot 'Get-VergilDiagnosticsSummary.ps1'
$arguments = @(
	$resolvedProjectPath
	'-NoSplash'
	'-Unattended'
	'-NullRHI'
	'-nop4'
	("-ExecCmds=Automation RunTests {0}; Quit" -f $TestFilter)
	'-TestExit=Automation Test Queue Empty'
	'-LogCmds=LogAutomationController Verbose'
	("-AbsLog={0}" -f $LogPath)
	'-stdout'
	'-FullStdOutLogOutput'
)

Write-Output ('Running Vergil scaffold automation:')
Write-Output ('"{0}" {1}' -f $resolvedEditorCmdPath, ($arguments -join ' '))

& $resolvedEditorCmdPath @arguments
$exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }

if (-not $SkipSummary) {
	Write-Output ''
	if ($SummaryAsJson) {
		& $summaryScriptPath -LogPath $LogPath -AsJson
	}
	else {
		& $summaryScriptPath -LogPath $LogPath
	}
}

exit $exitCode
