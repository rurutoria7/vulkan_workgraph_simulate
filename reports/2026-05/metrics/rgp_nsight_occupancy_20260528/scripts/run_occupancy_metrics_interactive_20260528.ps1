param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..\..")).Path,
	[string]$OutDir = (Resolve-Path (Join-Path $PSScriptRoot "..\nvidia_nsight")).Path,
	[string]$Machine = "nvidia",
	[int]$GpuIndex = 0,
	[int]$SessionId = 2,
	[string]$PsExecPath = (Join-Path $env:USERPROFILE "tools\PSTools\PsExec64.exe"),
	[int]$Frames = 150,
	[int]$WarmupSeconds = 2,
	[int]$Repeats = 3,
	[int]$RunTimeoutSeconds = 180,
	[switch]$UseSystem,
	[switch]$CounterRuns
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $PsExecPath)) {
	throw "Missing PsExec: $PsExecPath"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$wrapper = Join-Path $OutDir "interactive_metrics_wrapper.ps1"
$done = Join-Path $OutDir "interactive_metrics_done.txt"
$launchLog = Join-Path $OutDir "interactive_metrics_launch.log"
$wrapperStdout = Join-Path $OutDir "interactive_metrics_wrapper.stdout.log"
$wrapperStderr = Join-Path $OutDir "interactive_metrics_wrapper.stderr.log"
Remove-Item -LiteralPath $done, $launchLog, $wrapperStdout, $wrapperStderr -ErrorAction SilentlyContinue

$baseScript = Join-Path $PSScriptRoot "run_occupancy_metrics_20260528.ps1"
$counterFlag = if ($CounterRuns) { "-CounterRuns" } else { "" }
$wrapperBody = @"
`$ErrorActionPreference = "Continue"
Set-Location -LiteralPath '$RepoRoot'
"START=`$([DateTime]::Now.ToString('o')) SESSION=`$([System.Diagnostics.Process]::GetCurrentProcess().SessionId) USER=`$([Environment]::UserName)" | Set-Content -Encoding UTF8 -LiteralPath '$wrapperStdout'
try {
	& '$baseScript' -RepoRoot '$RepoRoot' -OutDir '$OutDir' -Machine '$Machine' -GpuIndex $GpuIndex -Frames $Frames -WarmupSeconds $WarmupSeconds -Repeats $Repeats -RunTimeoutSeconds $RunTimeoutSeconds $counterFlag 1>> '$wrapperStdout' 2>> '$wrapperStderr'
	`$code = `$LASTEXITCODE
	"EXIT_CODE=`$code`nEND=`$([DateTime]::Now.ToString('o'))" | Set-Content -Encoding UTF8 -LiteralPath '$done'
} catch {
	"EXCEPTION=`$(`$_.Exception.Message)`nEND=`$([DateTime]::Now.ToString('o'))" | Set-Content -Encoding UTF8 -LiteralPath '$done'
}
"@
Set-Content -Encoding UTF8 -LiteralPath $wrapper -Value $wrapperBody

$psExecArgs = @("-nobanner", "-accepteula")
if ($UseSystem) {
	$psExecArgs += "-s"
}
$psExecArgs += @("-i", "$SessionId", "-d", "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", $wrapper)

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $PsExecPath @psExecArgs *> $launchLog
$psExecExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference
Add-Content -LiteralPath $launchLog -Value "PSEXEC_EXIT_CODE=$psExecExitCode"

$pollTimeoutSeconds = [Math]::Max(60, ($Repeats * 2 * $RunTimeoutSeconds) + 60)
$deadline = (Get-Date).AddSeconds($pollTimeoutSeconds)
while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $done)) {
	Start-Sleep -Seconds 1
}

if (-not (Test-Path -LiteralPath $done)) {
	throw "Interactive metrics run did not finish within $pollTimeoutSeconds seconds. See $OutDir"
}

Get-Content -LiteralPath $done
