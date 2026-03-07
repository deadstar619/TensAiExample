param(
	[string]$EngineRoot = '',
	[string]$EditorCmdPath = '',
	[string]$ProjectPath = '',
	[string]$OutputDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VergilCICommon.ps1')

$projectRoot = Get-VergilProjectRoot
$resolvedProjectPath = Resolve-VergilProjectPath -ProjectPath $ProjectPath
$resolvedEditorCmdPath = Resolve-VergilEditorCmdPath -EditorCmdPath $EditorCmdPath -EngineRoot $EngineRoot

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Get-VergilCILaneOutputDirectory -Lane 'Benchmarks' -ProjectRoot $projectRoot
}

Reset-VergilDirectory -Path $OutputDirectory

$automationScriptPath = Join-Path $PSScriptRoot 'Invoke-VergilScaffoldAutomation.ps1'
$summarySourcePath = Join-Path $projectRoot 'Saved\Vergil\Benchmarks\LargeGraphBenchmarkSummary.json'
$summaryDestinationPath = Join-Path $OutputDirectory 'LargeGraphBenchmarkSummary.json'
$benchmarkLogPath = Join-Path $OutputDirectory 'LargeGraphBenchmark.log'

if (Test-Path -LiteralPath $summarySourcePath) {
	Remove-Item -LiteralPath $summarySourcePath -Force
}

Invoke-VergilPowerShellFile `
	-ScriptPath $automationScriptPath `
	-WorkingDirectory $projectRoot `
	-Arguments @(
		'-EditorCmdPath', $resolvedEditorCmdPath,
		'-ProjectPath', $resolvedProjectPath,
		'-TestFilter', 'Vergil.Scaffold.LargeGraphBenchmark',
		'-LogPath', $benchmarkLogPath
	)

if (-not (Test-Path -LiteralPath $summarySourcePath)) {
	throw ("Large-graph benchmark automation did not produce the expected summary file: {0}" -f $summarySourcePath)
}

Copy-Item -LiteralPath $summarySourcePath -Destination $summaryDestinationPath -Force
$summary = Get-Content -LiteralPath $summaryDestinationPath -Raw | ConvertFrom-Json

Write-Output ''
Write-Output 'Vergil large-graph benchmark summary'
Write-Output ("Summary: {0}" -f $summaryDestinationPath)
foreach ($scenario in @($summary.scenarios)) {
	Write-Output (("- {0}: nodes={1} edges={2} comments={3} plan={4:N3}s apply={5:N3}s" -f `
		$scenario.name,
		$scenario.documentNodeCount,
		$scenario.documentEdgeCount,
		$scenario.commentCount,
		$scenario.plan.elapsedSeconds,
		$scenario.apply.elapsedSeconds))
}
