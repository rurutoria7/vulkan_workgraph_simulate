param(
	[string]$RepoRoot = "C:\CGV\Projects\D3DWorkgraph\workgraph_vulkan_poc",
	[string]$OutDir = "C:\CGV\Projects\D3DWorkgraph\workgraph_vulkan_poc\reports\2026-05\metrics\request_batching_validation_20260525",
	[int]$Frames = 150
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$manifest = Join-Path $OutDir "run_manifest.csv"
$progress = Join-Path $OutDir "run_progress.log"
Remove-Item -LiteralPath $manifest, $progress -ErrorAction SilentlyContinue

function ConvertTo-SafeName {
	param([string]$Value)
	return ($Value -replace "[^A-Za-z0-9_.-]", "_")
}

function Invoke-WgRun {
	param(
		[string]$Suite,
		[string]$Setting,
		[string]$Case,
		[string]$RunType,
		[int]$Repeat,
		[string]$WorkDir,
		[string[]]$Flags
	)

	$exe = Join-Path $WorkDir "build\bin\Release\workgraph_poc.exe"
	$safeName = ConvertTo-SafeName "${Suite}_${Setting}_${Case}_r${Repeat}_${RunType}"
	$metricsFile = Join-Path $OutDir "raw_${safeName}.csv"
	$stdoutFile = Join-Path $OutDir "raw_${safeName}.stdout.log"
	$stderrFile = Join-Path $OutDir "raw_${safeName}.stderr.log"
	$modeFlags = if ($RunType -eq "timing") { @("--wg-timestamps-only") } else { @("--wg-metrics") }
	$runArgs = @(
		"--benchmark", "--benchwarmup", "0", "--benchmarkframes", "$Frames",
		"--width", "640", "--height", "480"
	) + $modeFlags + @(
		"--wg-metrics-file", $metricsFile,
		"--wg-metrics-interval", "1"
	) + $Flags

	Add-Content -LiteralPath $progress -Value ("{0:s} START {1} {2} {3} repeat={4} type={5}" -f (Get-Date), $Suite, $Setting, $Case, $Repeat, $RunType)
	$timer = [System.Diagnostics.Stopwatch]::StartNew()
	$process = Start-Process -FilePath $exe -ArgumentList $runArgs -WorkingDirectory $WorkDir -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
	$exitCode = $process.ExitCode
	$timer.Stop()

	for ($i = 0; $i -lt 20 -and -not (Test-Path -LiteralPath $metricsFile); $i++) {
		Start-Sleep -Milliseconds 250
	}

	$status = if (($exitCode -eq 0) -and (Test-Path -LiteralPath $metricsFile)) { "ok" } else { "failed" }
	[pscustomobject]@{
		suite = $Suite
		setting = $Setting
		case = $Case
		run_type = $RunType
		repeat = $Repeat
		frames_requested = $Frames
		metrics_file = $metricsFile
		stdout_file = $stdoutFile
		stderr_file = $stderrFile
		workdir = $WorkDir
		flags = ($Flags -join " ")
		status = $status
		exit_code = $exitCode
		duration_sec = [math]::Round($timer.Elapsed.TotalSeconds, 3)
	} | Export-Csv -LiteralPath $manifest -NoTypeInformation -Append

	Add-Content -LiteralPath $progress -Value ("{0:s} END   {1} {2} {3} repeat={4} type={5} status={6} sec={7:N1}" -f (Get-Date), $Suite, $Setting, $Case, $Repeat, $RunType, $status, $timer.Elapsed.TotalSeconds)
	if ($status -ne "ok") {
		Add-Content -LiteralPath $progress -Value ("{0:s} WARN  continuing after failed run: {1} {2} {3} repeat={4} type={5}" -f (Get-Date), $Suite, $Setting, $Case, $Repeat, $RunType)
	}
}

$oldWorkDir = Join-Path $RepoRoot "tmp\worktrees\fix-wave-batched-atomics"
$oldBaseFlags = @("--wg-node-c-start", "72")
$oldCases = @(
	[pscustomobject]@{ setting = "old_unsharded"; case = "scalar_q1_off_q2_off"; flags = $oldBaseFlags + @("--wg-no-q1-deq-batch", "--wg-no-q2-deq-batch") },
	[pscustomobject]@{ setting = "old_unsharded"; case = "q1_on_q2_off"; flags = $oldBaseFlags + @("--wg-q1-deq-batch", "--wg-no-q2-deq-batch") }
)
foreach ($limit in @(1, 2, 4, 8, 16, 32)) {
	$oldCases += [pscustomobject]@{
		setting = "old_unsharded"
		case = "q1_on_q2_batch_l$limit"
		flags = $oldBaseFlags + @("--wg-q1-deq-batch", "--wg-q2-deq-batch", "--wg-q2-deq-batch-limit", "$limit")
	}
}

$mainBaseFlags = @("--wg-q1-lane-pop", "--wg-node-c-start", "72")
$mainSettings = @(
	[pscustomobject]@{ setting = "stress_16_shards"; flags = $mainBaseFlags + @("--wg-queue-shards", "16", "--wg-q2-shards", "16") },
	[pscustomobject]@{ setting = "default_256_shards"; flags = $mainBaseFlags + @("--wg-queue-shards", "256", "--wg-q2-shards", "256") }
)
$mainCases = @([pscustomobject]@{ case = "q2_batch_off"; flags = @("--wg-no-q2-deq-batch") })
foreach ($limit in @(1, 2, 4, 8, 16, 32)) {
	$mainCases += [pscustomobject]@{ case = "q2_batch_l$limit"; flags = @("--wg-q2-deq-batch", "--wg-q2-deq-batch-limit", "$limit") }
}

foreach ($runType in @("timing", "counter")) {
	foreach ($repeat in 1..3) {
		foreach ($caseDef in $oldCases) {
			Invoke-WgRun -Suite "old_repro" -Setting $caseDef.setting -Case $caseDef.case -RunType $runType -Repeat $repeat -WorkDir $oldWorkDir -Flags $caseDef.flags
		}
		foreach ($settingDef in $mainSettings) {
			foreach ($caseDef in $mainCases) {
				Invoke-WgRun -Suite "main_sharded" -Setting $settingDef.setting -Case $caseDef.case -RunType $runType -Repeat $repeat -WorkDir $RepoRoot -Flags ($settingDef.flags + $caseDef.flags)
			}
		}
	}
}

Add-Content -LiteralPath $progress -Value ("{0:s} DONE" -f (Get-Date))
