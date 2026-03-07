param(
	[ValidateSet('All', 'Build', 'HeadlessAutomation', 'GoldenTests', 'PIRuntime', 'Benchmarks', 'PerfSmoke')]
	[string]$Lane = 'All',
	[string]$EngineRoot = '',
	[string]$UnrealBuildToolPath = '',
	[string]$EditorCmdPath = '',
	[string]$ProjectPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'VergilCICommon.ps1')

$projectRoot = Get-VergilProjectRoot
$resolvedProjectPath = Resolve-VergilProjectPath -ProjectPath $ProjectPath
$resolvedEditorCmdPath = Resolve-VergilEditorCmdPath -EditorCmdPath $EditorCmdPath -EngineRoot $EngineRoot
$resolvedUnrealBuildToolPath = Resolve-VergilUnrealBuildToolPath -UnrealBuildToolPath $UnrealBuildToolPath -EngineRoot $EngineRoot
$automationScriptPath = Join-Path $PSScriptRoot 'Invoke-VergilScaffoldAutomation.ps1'
$buildScriptPath = Join-Path $PSScriptRoot 'Invoke-VergilProjectBuild.ps1'
$benchmarkScriptPath = Join-Path $PSScriptRoot 'Invoke-VergilLargeGraphBenchmarks.ps1'

function Invoke-VergilAutomationLane {
	param(
		[Parameter(Mandatory = $true)]
		[string]$LaneName,

		[Parameter(Mandatory = $true)]
		[string]$TestFilter,

		[string]$LogFileName = 'Automation.log'
	)

	$outputDirectory = Get-VergilCILaneOutputDirectory -Lane $LaneName -ProjectRoot $projectRoot
	Reset-VergilDirectory -Path $outputDirectory

	Invoke-VergilPowerShellFile `
		-ScriptPath $automationScriptPath `
		-WorkingDirectory $projectRoot `
		-Arguments @(
			'-EditorCmdPath', $resolvedEditorCmdPath,
			'-ProjectPath', $resolvedProjectPath,
			'-TestFilter', $TestFilter,
			'-LogPath', (Join-Path $outputDirectory $LogFileName)
		)
}

switch ($Lane) {
	'All' {
		$buildOutputDirectory = Get-VergilCILaneOutputDirectory -Lane 'Build' -ProjectRoot $projectRoot
		Reset-VergilDirectory -Path $buildOutputDirectory
		Invoke-VergilPowerShellFile `
			-ScriptPath $buildScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-ProjectPath', $resolvedProjectPath,
				'-UnrealBuildToolPath', $resolvedUnrealBuildToolPath,
				'-LogPath', (Join-Path $buildOutputDirectory 'UnrealBuildTool.log')
			)

		Invoke-VergilAutomationLane -LaneName 'HeadlessAutomation' -TestFilter 'Vergil.Scaffold' -LogFileName 'VergilAutomation_HeadlessAutomation.log'
		Invoke-VergilAutomationLane -LaneName 'GoldenTests' -TestFilter 'Vergil.Scaffold.GoldenAssetSnapshot' -LogFileName 'GoldenAssetSnapshot.log'

		$goldenOutputDirectory = Get-VergilCILaneOutputDirectory -Lane 'GoldenTests' -ProjectRoot $projectRoot
		Invoke-VergilPowerShellFile `
			-ScriptPath $automationScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath,
				'-TestFilter', 'Vergil.Scaffold.SourceControlDiff',
				'-LogPath', (Join-Path $goldenOutputDirectory 'SourceControlDiff.log')
			)

		Invoke-VergilAutomationLane -LaneName 'PIRuntime' -TestFilter 'Vergil.Scaffold.PIERuntime' -LogFileName 'PIERuntime.log'
		Invoke-VergilPowerShellFile `
			-ScriptPath $benchmarkScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath
			)
	}
	'Build' {
		$buildOutputDirectory = Get-VergilCILaneOutputDirectory -Lane 'Build' -ProjectRoot $projectRoot
		Reset-VergilDirectory -Path $buildOutputDirectory
		Invoke-VergilPowerShellFile `
			-ScriptPath $buildScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-ProjectPath', $resolvedProjectPath,
				'-UnrealBuildToolPath', $resolvedUnrealBuildToolPath,
				'-LogPath', (Join-Path $buildOutputDirectory 'UnrealBuildTool.log')
			)
	}
	'HeadlessAutomation' {
		Invoke-VergilAutomationLane -LaneName 'HeadlessAutomation' -TestFilter 'Vergil.Scaffold' -LogFileName 'VergilAutomation_HeadlessAutomation.log'
	}
	'GoldenTests' {
		$outputDirectory = Get-VergilCILaneOutputDirectory -Lane 'GoldenTests' -ProjectRoot $projectRoot
		Reset-VergilDirectory -Path $outputDirectory

		Invoke-VergilPowerShellFile `
			-ScriptPath $automationScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath,
				'-TestFilter', 'Vergil.Scaffold.GoldenAssetSnapshot',
				'-LogPath', (Join-Path $outputDirectory 'GoldenAssetSnapshot.log')
			)

		Invoke-VergilPowerShellFile `
			-ScriptPath $automationScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath,
				'-TestFilter', 'Vergil.Scaffold.SourceControlDiff',
				'-LogPath', (Join-Path $outputDirectory 'SourceControlDiff.log')
			)
	}
	'PIRuntime' {
		Invoke-VergilAutomationLane -LaneName 'PIRuntime' -TestFilter 'Vergil.Scaffold.PIERuntime' -LogFileName 'PIERuntime.log'
	}
	'Benchmarks' {
		Invoke-VergilPowerShellFile `
			-ScriptPath $benchmarkScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath
			)
	}
	'PerfSmoke' {
		Write-Warning 'PerfSmoke is deprecated and now forwards to the dedicated large-graph Benchmarks lane.'
		Invoke-VergilPowerShellFile `
			-ScriptPath $benchmarkScriptPath `
			-WorkingDirectory $projectRoot `
			-Arguments @(
				'-EditorCmdPath', $resolvedEditorCmdPath,
				'-ProjectPath', $resolvedProjectPath
			)
	}
}
