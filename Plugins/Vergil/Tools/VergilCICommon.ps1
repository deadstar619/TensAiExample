Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-VergilProjectRoot {
	return (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
}

function Resolve-VergilProjectPath {
	param(
		[string]$ProjectPath
	)

	$projectRoot = Get-VergilProjectRoot
	if ([string]::IsNullOrWhiteSpace($ProjectPath)) {
		$ProjectPath = Join-Path $projectRoot 'TensAiExample.uproject'
	}

	return (Resolve-Path -LiteralPath $ProjectPath).Path
}

function Resolve-VergilEngineRoot {
	param(
		[string]$EngineRoot
	)

	$candidates = New-Object System.Collections.Generic.List[string]
	if (-not [string]::IsNullOrWhiteSpace($EngineRoot)) {
		$candidates.Add($EngineRoot)
	}

	if (-not [string]::IsNullOrWhiteSpace($env:UE_5_7_ROOT)) {
		$candidates.Add($env:UE_5_7_ROOT)
	}

	if (-not [string]::IsNullOrWhiteSpace($env:UE_ROOT)) {
		$candidates.Add($env:UE_ROOT)
	}

	$candidates.Add('C:\Program Files\Epic Games\UE_5.7')

	foreach ($candidate in $candidates) {
		if ([string]::IsNullOrWhiteSpace($candidate)) {
			continue
		}

		if (Test-Path -LiteralPath $candidate) {
			return (Resolve-Path -LiteralPath $candidate).Path
		}
	}

	throw "Unable to resolve the Unreal Engine root. Pass -EngineRoot explicitly or set UE_5_7_ROOT."
}

function Resolve-VergilEditorCmdPath {
	param(
		[string]$EditorCmdPath,
		[string]$EngineRoot
	)

	if ([string]::IsNullOrWhiteSpace($EditorCmdPath)) {
		$resolvedEngineRoot = Resolve-VergilEngineRoot -EngineRoot $EngineRoot
		$EditorCmdPath = Join-Path $resolvedEngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
	}

	return (Resolve-Path -LiteralPath $EditorCmdPath).Path
}

function Resolve-VergilUnrealBuildToolPath {
	param(
		[string]$UnrealBuildToolPath,
		[string]$EngineRoot
	)

	if ([string]::IsNullOrWhiteSpace($UnrealBuildToolPath)) {
		$resolvedEngineRoot = Resolve-VergilEngineRoot -EngineRoot $EngineRoot
		$UnrealBuildToolPath = Join-Path $resolvedEngineRoot 'Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe'
	}

	return (Resolve-Path -LiteralPath $UnrealBuildToolPath).Path
}

function Get-VergilCILogRoot {
	param(
		[string]$ProjectRoot
	)

	return Join-Path $ProjectRoot 'Saved\Logs\VergilCI'
}

function Get-VergilCILaneOutputDirectory {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Lane,

		[Parameter(Mandatory = $true)]
		[string]$ProjectRoot
	)

	return Join-Path (Get-VergilCILogRoot -ProjectRoot $ProjectRoot) $Lane
}

function Reset-VergilDirectory {
	param(
		[Parameter(Mandatory = $true)]
		[string]$Path
	)

	if (Test-Path -LiteralPath $Path) {
		Remove-Item -LiteralPath $Path -Recurse -Force
	}

	New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

function Invoke-VergilPowerShellFile {
	param(
		[Parameter(Mandatory = $true)]
		[string]$ScriptPath,

		[string[]]$Arguments = @(),

		[string]$WorkingDirectory = ''
	)

	$resolvedScriptPath = (Resolve-Path -LiteralPath $ScriptPath).Path
	$invocation = @(
		'-NoProfile'
		'-ExecutionPolicy'
		'Bypass'
		'-File'
		$resolvedScriptPath
	) + $Arguments

	$resolvedWorkingDirectory = $WorkingDirectory
	if ([string]::IsNullOrWhiteSpace($resolvedWorkingDirectory)) {
		$resolvedWorkingDirectory = Split-Path -Parent $resolvedScriptPath
	}

	Push-Location $resolvedWorkingDirectory
	try {
		& powershell.exe @invocation
		$exitCode = if ($null -ne $LASTEXITCODE) { $LASTEXITCODE } else { 0 }
		if ($exitCode -ne 0) {
			throw ("PowerShell script failed with exit code {0}: {1}" -f $exitCode, $resolvedScriptPath)
		}
	}
	finally {
		Pop-Location
	}
}
