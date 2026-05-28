param(
	[string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..\..")).Path,
	[string]$OutDir = (Resolve-Path (Join-Path $PSScriptRoot "..\nvidia_scheduled_debug")).Path,
	[string]$RunAsUser = "OFFICE365\111062113",
	[string]$TaskPrefix = "CodexWgDebug",
	[int]$GpuIndex = 0,
	[switch]$KeepProcess
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$launchScript = Join-Path $OutDir "debug_launch_app.ps1"
$shotScript = Join-Path $OutDir "debug_capture_screen.ps1"
$pidFile = Join-Path $OutDir "debug_app_pid.txt"
$png = Join-Path $OutDir "debug_screen.png"
$shotLog = Join-Path $OutDir "debug_capture.log"
$metrics = Join-Path $OutDir "debug_timing.csv"
Remove-Item -LiteralPath $pidFile, $png, $shotLog, $metrics -ErrorAction SilentlyContinue

Stop-Process -Name workgraph_poc -Force -ErrorAction SilentlyContinue

$launchBody = @"
Set-Location -LiteralPath '$RepoRoot'
`$exe = Join-Path '$RepoRoot' 'build\bin\Release\workgraph_poc.exe'
`$args = @('--resourcepath','$RepoRoot','--gpu','$GpuIndex','--benchmark','--benchwarmup','1','--benchmarkframes','5','--width','640','--height','480','--wg-timestamps-only','--wg-metrics-file','$metrics','--wg-metrics-interval','1','--wg-queue-shards','256','--wg-q2-shards','256','--wg-node-c-start','72','--wg-q1-lane-pop','--wg-no-q2-deq-batch')
`$p = Start-Process -FilePath `$exe -ArgumentList `$args -WorkingDirectory '$RepoRoot' -PassThru
"PID=`$(`$p.Id) START=`$([DateTime]::Now.ToString('o')) USER=`$([Environment]::UserDomainName)\`$([Environment]::UserName) SESSION=`$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)" | Set-Content -Encoding UTF8 -LiteralPath '$pidFile'
"@
Set-Content -Encoding UTF8 -LiteralPath $launchScript -Value $launchBody

$shotBody = @"
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
`$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
`$bmp = New-Object System.Drawing.Bitmap `$bounds.Width, `$bounds.Height
`$graphics = [System.Drawing.Graphics]::FromImage(`$bmp)
`$graphics.CopyFromScreen(`$bounds.Location, [System.Drawing.Point]::Empty, `$bounds.Size)
`$bmp.Save('$png', [System.Drawing.Imaging.ImageFormat]::Png)
`$graphics.Dispose()
`$bmp.Dispose()
"CAPTURED=`$([DateTime]::Now.ToString('o')) BOUNDS=`$(`$bounds.Width)x`$(`$bounds.Height) USER=`$([Environment]::UserDomainName)\`$([Environment]::UserName) SESSION=`$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)" | Set-Content -Encoding UTF8 -LiteralPath '$shotLog'
"@
Set-Content -Encoding UTF8 -LiteralPath $shotScript -Value $shotBody

$launchTask = "${TaskPrefix}Launch"
$shotTask = "${TaskPrefix}Shot"
$time = (Get-Date).AddMinutes(1).ToString("HH:mm")
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
schtasks.exe /Delete /TN $launchTask /F *> $null
schtasks.exe /Delete /TN $shotTask /F *> $null
schtasks.exe /Create /TN $launchTask /SC ONCE /ST $time /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$launchScript`"" /RU $RunAsUser /IT /F *> $null
schtasks.exe /Run /TN $launchTask *> $null
Start-Sleep -Seconds 8
schtasks.exe /Create /TN $shotTask /SC ONCE /ST $time /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$shotScript`"" /RU $RunAsUser /IT /F *> $null
schtasks.exe /Run /TN $shotTask *> $null
$ErrorActionPreference = $previousErrorActionPreference

for ($i = 0; $i -lt 20 -and -not (Test-Path -LiteralPath $png); $i++) {
	Start-Sleep -Milliseconds 500
}

if (-not $KeepProcess) {
	Stop-Process -Name workgraph_poc -Force -ErrorAction SilentlyContinue
}

Get-Content -LiteralPath $pidFile -ErrorAction SilentlyContinue
Get-Content -LiteralPath $shotLog -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $png) {
	Get-Item -LiteralPath $png | Select-Object FullName, Length, LastWriteTime
}
