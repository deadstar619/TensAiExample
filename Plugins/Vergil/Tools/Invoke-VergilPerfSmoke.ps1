param(
	[string]$EngineRoot = '',
	[string]$EditorCmdPath = '',
	[string]$ProjectPath = '',
	[string]$OutputDirectory = '',
	[int]$MaxTotalSeconds = 0,
	[int]$MaxPerScenarioSeconds = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VergilCICommon.ps1')

$projectRoot = Get-VergilProjectRoot
$resolvedProjectPath = Resolve-VergilProjectPath -ProjectPath $ProjectPath
$resolvedEditorCmdPath = Resolve-VergilEditorCmdPath -EditorCmdPath $EditorCmdPath -EngineRoot $EngineRoot

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
	$OutputDirectory = Get-VergilCILaneOutputDirectory -Lane 'PerfSmoke' -ProjectRoot $projectRoot
}

Reset-VergilDirectory -Path $OutputDirectory

$automationScriptPath = Join-Path $PSScriptRoot 'Invoke-VergilScaffoldAutomation.ps1'
$summaryPath = Join-Path $OutputDirectory 'PerfSmokeSummary.json'
$scenarios = @(
	[PSCustomObject]@{
		Name = 'GoldenAssetSnapshot'
		TestFilter = 'Vergil.Scaffold.GoldenAssetSnapshot'
	},
	[PSCustomObject]@{
		Name = 'SourceControlDiff'
		TestFilter = 'Vergil.Scaffold.SourceControlDiff'
	},
	[PSCustomObject]@{
		Name = 'CompileApplyRecoveryRoundtrip'
		TestFilter = 'Vergil.Scaffold.CompileApplyRecoveryRoundtrip'
	},
	[PSCustomObject]@{
		Name = 'PIERuntime'
		TestFilter = 'Vergil.Scaffold.PIERuntime'
	}
)

$results = New-Object System.Collections.Generic.List[object]
$totalStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

foreach ($scenario in $scenarios) {
	$scenarioLogPath = Join-Path $OutputDirectory ("{0}.log" -f $scenario.Name)
	$scenarioStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

	Write-Output ("Running Vergil perf-smoke scenario: {0} ({1})" -f $scenario.Name, $scenario.TestFilter)
	Invoke-VergilPowerShellFile `
		-ScriptPath $automationScriptPath `
		-WorkingDirectory $projectRoot `
		-Arguments @(
			'-EditorCmdPath', $resolvedEditorCmdPath,
			'-ProjectPath', $resolvedProjectPath,
			'-TestFilter', $scenario.TestFilter,
			'-LogPath', $scenarioLogPath
		)

	$scenarioStopwatch.Stop()
	$elapsedSeconds = [Math]::Round($scenarioStopwatch.Elapsed.TotalSeconds, 2)
	$results.Add([PSCustomObject]@{
		Name = $scenario.Name
		TestFilter = $scenario.TestFilter
		ElapsedSeconds = $elapsedSeconds
		LogPath = $scenarioLogPath
	})

	if ($MaxPerScenarioSeconds -gt 0 -and $scenarioStopwatch.Elapsed.TotalSeconds -gt $MaxPerScenarioSeconds) {
		throw ("Perf-smoke scenario '{0}' exceeded the per-scenario budget of {1} seconds ({2:N2}s)." -f $scenario.Name, $MaxPerScenarioSeconds, $scenarioStopwatch.Elapsed.TotalSeconds)
	}
}

$totalStopwatch.Stop()
$totalElapsedSeconds = [Math]::Round($totalStopwatch.Elapsed.TotalSeconds, 2)

$summary = [ordered]@{
	Format = 'Vergil.PerfSmokeSummary'
	Version = 1
	GeneratedUtc = (Get-Date).ToUniversalTime().ToString('o')
	TotalElapsedSeconds = $totalElapsedSeconds
	MaxTotalSeconds = $MaxTotalSeconds
	MaxPerScenarioSeconds = $MaxPerScenarioSeconds
	Scenarios = @($results.ToArray())
}

$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Output ''
Write-Output 'Vergil perf-smoke summary'
Write-Output ("Summary: {0}" -f $summaryPath)
Write-Output ("Total elapsed: {0:N2}s" -f $totalStopwatch.Elapsed.TotalSeconds)
foreach ($result in $results) {
	Write-Output (("- {0}: {1:N2}s ({2})" -f $result.Name, $result.ElapsedSeconds, $result.TestFilter))
}

if ($MaxTotalSeconds -gt 0 -and $totalStopwatch.Elapsed.TotalSeconds -gt $MaxTotalSeconds) {
	throw ("Perf-smoke total runtime exceeded the budget of {0} seconds ({1:N2}s)." -f $MaxTotalSeconds, $totalStopwatch.Elapsed.TotalSeconds)
}
