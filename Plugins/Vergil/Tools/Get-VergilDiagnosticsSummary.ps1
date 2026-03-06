param(
	[Parameter(Mandatory = $true)]
	[string]$LogPath,

	[int]$MaxDetails = 10,

	[switch]$AsJson
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-UniqueLines {
	param(
		[string[]]$Lines
	)

	$seen = @{}
	$result = New-Object System.Collections.Generic.List[string]
	foreach ($line in $Lines) {
		if ([string]::IsNullOrWhiteSpace($line)) {
			continue
		}

		if (-not $seen.ContainsKey($line)) {
			$seen[$line] = $true
			$result.Add($line)
		}
	}

	return $result.ToArray()
}

$resolvedLogPath = (Resolve-Path -LiteralPath $LogPath).Path
$lines = Get-Content -LiteralPath $resolvedLogPath

$testsByPath = @{}
foreach ($line in $lines) {
	if ($line -match 'Test Completed\. Result=\{(?<Result>[^}]+)\} Name=\{(?<Name>[^}]+)\} Path=\{(?<Path>[^}]+)\}') {
		$testsByPath[$Matches.Path] = [PSCustomObject]@{
			Name   = $Matches.Name
			Path   = $Matches.Path
			Result = $Matches.Result
		}
	}
}

$completedTests = @($testsByPath.Values | Sort-Object Path)
$passedTests = @($completedTests | Where-Object { $_.Result -eq 'Success' })
$failedTests = @($completedTests | Where-Object { $_.Result -ne 'Success' })

$vergilDiagnostics = New-Object System.Collections.Generic.List[object]
foreach ($line in $lines) {
	if ($line -match 'Vergil diagnostic \[(?<Severity>[^\]]+)\] (?<Code>[^:]+): (?<Message>.+)$') {
		$vergilDiagnostics.Add([PSCustomObject]@{
			Severity = $Matches.Severity
			Code     = $Matches.Code
			Message  = $Matches.Message.Trim()
			Line     = $line
		})
	}
}

$blueprintWarnings = Get-UniqueLines @($lines | Where-Object { $_ -match 'LogBlueprint: Warning:' })
$blueprintErrors = Get-UniqueLines @($lines | Where-Object { $_ -match 'LogBlueprint: Error:' })
$automationWarnings = Get-UniqueLines @($lines | Where-Object { $_ -match 'LogAutomationController: Warning:' -or $_ -match 'AutomationTestingLog: Warning:' })
$automationErrors = Get-UniqueLines @($lines | Where-Object { $_ -match 'LogAutomationController: Error:' -or $_ -match 'AutomationTestingLog: Error:' })
$vergilFailureLines = Get-UniqueLines @($lines | Where-Object { $_ -match 'Vergil compiler pass ' -or $_ -match 'Vergil compile request failed' -or $_ -match 'Vergil command execution failed' })

$exitCode = $null
foreach ($line in $lines) {
	if ($line -match 'TEST COMPLETE\. EXIT CODE: (?<Code>\d+)') {
		$exitCode = [int]$Matches.Code
	}
}

$vergilErrorCount = @($vergilDiagnostics | Where-Object { $_.Severity -eq 'Error' }).Count
$vergilWarningCount = @($vergilDiagnostics | Where-Object { $_.Severity -eq 'Warning' }).Count

$status = 'Success'
if (@($failedTests).Count -gt 0 -or @($blueprintErrors).Count -gt 0 -or @($automationErrors).Count -gt 0 -or $vergilErrorCount -gt 0 -or (($null -ne $exitCode) -and $exitCode -ne 0)) {
	$status = 'Failed'
}
elseif (@($blueprintWarnings).Count -gt 0 -or @($automationWarnings).Count -gt 0 -or $vergilWarningCount -gt 0) {
	$status = 'Warning'
}

$summary = [ordered]@{}
$summary.LogPath = $resolvedLogPath
$summary.Status = $status
$summary.ExitCode = $exitCode
$summary.TotalTests = @($completedTests).Count
$summary.PassedTests = @($passedTests).Count
$summary.FailedTests = @($failedTests).Count
$summary.VergilErrorCount = $vergilErrorCount
$summary.VergilWarningCount = $vergilWarningCount
$summary.BlueprintWarningCount = @($blueprintWarnings).Count
$summary.BlueprintErrorCount = @($blueprintErrors).Count
$summary.AutomationWarningCount = @($automationWarnings).Count
$summary.AutomationErrorCount = @($automationErrors).Count
$summary.FailedTestDetails = @($failedTests)
$summary.VergilDiagnostics = @($vergilDiagnostics.ToArray())
$summary.BlueprintWarnings = @($blueprintWarnings | Select-Object -First $MaxDetails)
$summary.BlueprintErrors = @($blueprintErrors | Select-Object -First $MaxDetails)
$summary.AutomationWarnings = @($automationWarnings | Select-Object -First $MaxDetails)
$summary.AutomationErrors = @($automationErrors | Select-Object -First $MaxDetails)
$summary.VergilFailureLines = @($vergilFailureLines | Select-Object -First $MaxDetails)

if ($AsJson) {
	$summary | ConvertTo-Json -Depth 6
	return
}

$output = New-Object System.Collections.Generic.List[string]
$output.Add('Vergil Diagnostics Summary')
$output.Add(("Log: {0}" -f $summary.LogPath))
$output.Add(("Status: {0}" -f $summary.Status))
if ($null -ne $summary.ExitCode) {
	$output.Add(("Automation exit code: {0}" -f $summary.ExitCode))
}
$output.Add(("Tests: {0} total, {1} passed, {2} failed" -f $summary.TotalTests, $summary.PassedTests, $summary.FailedTests))
$output.Add(("Vergil diagnostics: {0} errors, {1} warnings" -f $summary.VergilErrorCount, $summary.VergilWarningCount))
$output.Add(("Blueprint diagnostics: {0} errors, {1} warnings" -f $summary.BlueprintErrorCount, $summary.BlueprintWarningCount))
$output.Add(("Automation diagnostics: {0} errors, {1} warnings" -f $summary.AutomationErrorCount, $summary.AutomationWarningCount))

if (@($summary.FailedTestDetails).Count -gt 0) {
	$output.Add('')
	$output.Add('Failed tests:')
	foreach ($test in $summary.FailedTestDetails) {
		$output.Add(("- {0} ({1})" -f $test.Path, $test.Result))
	}
}

if (@($summary.BlueprintWarnings).Count -gt 0) {
	$output.Add('')
	$output.Add('Blueprint warnings:')
	foreach ($line in $summary.BlueprintWarnings) {
		$output.Add(("- {0}" -f $line))
	}
}

if (@($summary.BlueprintErrors).Count -gt 0) {
	$output.Add('')
	$output.Add('Blueprint errors:')
	foreach ($line in $summary.BlueprintErrors) {
		$output.Add(("- {0}" -f $line))
	}
}

if (@($summary.AutomationWarnings).Count -gt 0) {
	$output.Add('')
	$output.Add('Automation warnings:')
	foreach ($line in $summary.AutomationWarnings) {
		$output.Add(("- {0}" -f $line))
	}
}

if (@($summary.AutomationErrors).Count -gt 0) {
	$output.Add('')
	$output.Add('Automation errors:')
	foreach ($line in $summary.AutomationErrors) {
		$output.Add(("- {0}" -f $line))
	}
}

if (@($summary.VergilDiagnostics).Count -gt 0) {
	$output.Add('')
	$output.Add('Vergil diagnostics:')
	foreach ($diag in @($summary.VergilDiagnostics | Select-Object -First $MaxDetails)) {
		$output.Add(("- [{0}] {1}: {2}" -f $diag.Severity, $diag.Code, $diag.Message))
	}
}

if (@($summary.VergilFailureLines).Count -gt 0) {
	$output.Add('')
	$output.Add('Vergil failure lines:')
	foreach ($line in $summary.VergilFailureLines) {
		$output.Add(("- {0}" -f $line))
	}
}

$output
