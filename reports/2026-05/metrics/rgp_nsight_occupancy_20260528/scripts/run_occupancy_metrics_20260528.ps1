param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..\..")).Path,
	[string]$OutDir = (Resolve-Path (Join-Path $PSScriptRoot "..\amd_rgp")).Path,
	[string]$Machine = "amd",
	[int]$GpuIndex = 0,
	[int]$Frames = 150,
	[int]$WarmupSeconds = 2,
	[int]$Repeats = 3,
	[int]$RunTimeoutSeconds = 0,
	[switch]$CounterRuns
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$manifest = Join-Path $OutDir "run_manifest.csv"
$summary = Join-Path $OutDir "metrics_summary.csv"
$progress = Join-Path $OutDir "run_progress.log"
Remove-Item -LiteralPath $manifest, $summary, $progress -ErrorAction SilentlyContinue

function ConvertTo-SafeName {
	param([string]$Value)
	return ($Value -replace "[^A-Za-z0-9_.-]", "_")
}

function Get-Percentile {
	param(
		[double[]]$Values,
		[double]$Percentile
	)
	if ($Values.Count -eq 0) {
		return 0.0
	}
	$sorted = @($Values | Sort-Object)
	$index = [int][math]::Floor(($sorted.Count - 1) * $Percentile)
	return $sorted[$index]
}

function Read-WgMetricsSummary {
	param(
		[string]$MetricsFile,
		[string]$Case,
		[string]$RunType,
		[int]$Repeat
	)

	$rows = @(Get-Content -LiteralPath $MetricsFile | Where-Object { $_ -like "WG_METRICS *" } | ForEach-Object {
		$values = $_.Substring(11).Split(",")
		[pscustomobject]@{
			frame = [int]$values[0]
			compute_ms = [double]$values[6]
			edges = [uint32]$values[11]
			vertices = [uint32]$values[12]
		}
	})

	$compute = @($rows | ForEach-Object { $_.compute_ms })
	[pscustomobject]@{
		machine = $Machine
		case = $Case
		run_type = $RunType
		repeat = $Repeat
		frames = $rows.Count
		compute_median_ms = Get-Percentile -Values $compute -Percentile 0.50
		compute_p10_ms = Get-Percentile -Values $compute -Percentile 0.10
		compute_p90_ms = Get-Percentile -Values $compute -Percentile 0.90
		compute_min_ms = Get-Percentile -Values $compute -Percentile 0.00
		compute_max_ms = Get-Percentile -Values $compute -Percentile 1.00
		edges_min = if ($rows.Count -gt 0) { ($rows | Measure-Object -Property edges -Minimum).Minimum } else { 0 }
		vertices_min = if ($rows.Count -gt 0) { ($rows | Measure-Object -Property vertices -Minimum).Minimum } else { 0 }
	}
}

function Invoke-WgRun {
	param(
		[string]$Case,
		[string]$RunType,
		[int]$Repeat,
		[string[]]$Flags
	)

	$exe = Join-Path $RepoRoot "build\bin\Release\workgraph_poc.exe"
	if (-not (Test-Path -LiteralPath $exe)) {
		throw "Missing executable: $exe"
	}

	$safeName = ConvertTo-SafeName "${Machine}_${Case}_r${Repeat}_${RunType}"
	$metricsFile = Join-Path $OutDir "raw_${safeName}.csv"
	$stdoutFile = Join-Path $OutDir "raw_${safeName}.stdout.log"
	$stderrFile = Join-Path $OutDir "raw_${safeName}.stderr.log"
	$modeFlags = if ($RunType -eq "timing") { @("--wg-timestamps-only") } else { @("--wg-metrics") }
	$runArgs = @(
		"--resourcepath", $RepoRoot,
		"--gpu", "$GpuIndex",
		"--benchmark", "--benchwarmup", "$WarmupSeconds", "--benchmarkframes", "$Frames",
		"--width", "640", "--height", "480"
	) + $modeFlags + @(
		"--wg-metrics-file", $metricsFile,
		"--wg-metrics-interval", "1"
	) + $Flags

	Add-Content -LiteralPath $progress -Value ("{0:s} START {1} repeat={2} type={3}" -f (Get-Date), $Case, $Repeat, $RunType)
	$timer = [System.Diagnostics.Stopwatch]::StartNew()
	$process = Start-Process -FilePath $exe -ArgumentList $runArgs -WorkingDirectory $RepoRoot -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
	$timedOut = $false
	if ($RunTimeoutSeconds -gt 0) {
		$deadline = (Get-Date).AddSeconds($RunTimeoutSeconds)
		while (-not $process.HasExited -and (Get-Date) -lt $deadline) {
			Start-Sleep -Milliseconds 250
			$process.Refresh()
		}
		if (-not $process.HasExited) {
			$timedOut = $true
			Stop-Process -Id $process.Id -Force
			[void]$process.WaitForExit(5000)
			for ($i = 0; $i -lt 20; $i++) {
				try {
					Add-Content -LiteralPath $stderrFile -Value "RUN_TIMEOUT_SECONDS=$RunTimeoutSeconds"
					break
				} catch {
					Start-Sleep -Milliseconds 250
				}
			}
		}
	} else {
		$process.WaitForExit()
	}
	$process.Refresh()
	$timer.Stop()

	for ($i = 0; $i -lt 20 -and -not (Test-Path -LiteralPath $metricsFile); $i++) {
		Start-Sleep -Milliseconds 250
	}

	$hasMetrics = (Test-Path -LiteralPath $metricsFile) -and ((Get-Item -LiteralPath $metricsFile).Length -gt 0)
	$status = if ($timedOut) { "timed_out" } elseif ($hasMetrics) { "ok" } else { "failed" }
	[pscustomobject]@{
		machine = $Machine
		case = $Case
		run_type = $RunType
		repeat = $Repeat
		gpu_index = $GpuIndex
		frames_requested = $Frames
		warmup_seconds = $WarmupSeconds
		metrics_file = $metricsFile
		stdout_file = $stdoutFile
		stderr_file = $stderrFile
		workdir = $RepoRoot
		args = ($runArgs -join " ")
		status = $status
		exit_code = $process.ExitCode
		duration_sec = [math]::Round($timer.Elapsed.TotalSeconds, 3)
	} | Export-Csv -LiteralPath $manifest -NoTypeInformation -Append

	if ($status -eq "ok") {
		Read-WgMetricsSummary -MetricsFile $metricsFile -Case $Case -RunType $RunType -Repeat $Repeat |
			Export-Csv -LiteralPath $summary -NoTypeInformation -Append
	}

	Add-Content -LiteralPath $progress -Value ("{0:s} END   {1} repeat={2} type={3} status={4} sec={5:N1}" -f (Get-Date), $Case, $Repeat, $RunType, $status, $timer.Elapsed.TotalSeconds)
}

$cases = @(
	[pscustomobject]@{
		name = "problem_reproduction"
		flags = @("--wg-queue-shards", "1", "--wg-q2-shards", "1", "--wg-node-c-start", "72", "--wg-no-q1-lane-pop", "--wg-no-q2-deq-batch")
	},
	[pscustomobject]@{
		name = "optimized_main_path"
		flags = @("--wg-queue-shards", "256", "--wg-q2-shards", "256", "--wg-node-c-start", "72", "--wg-q1-lane-pop", "--wg-no-q2-deq-batch")
	}
)

$runTypes = @("timing")
if ($CounterRuns) {
	$runTypes += "counter"
}

foreach ($repeat in 1..$Repeats) {
	foreach ($caseDef in $cases) {
		foreach ($runType in $runTypes) {
			Invoke-WgRun -Case $caseDef.name -RunType $runType -Repeat $repeat -Flags $caseDef.flags
		}
	}
}

Add-Content -LiteralPath $progress -Value ("{0:s} DONE" -f (Get-Date))
