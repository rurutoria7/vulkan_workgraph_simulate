param(
	[string]$RepoRoot = "C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run",
	[string]$TracePath,
	[string]$OutDir,
	[string]$RunAsUser = "OFFICE365\111062113",
	[string]$TaskPrefix = "CodexNsightGui",
	[int]$WaitSeconds = 25
)

$ErrorActionPreference = "Stop"

if (-not $TracePath) {
	$TracePath = Join-Path $RepoRoot "reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight_trace_direct_test2\workgraph_poc_2026_05_28_22_42_19.ngfx-gputrace"
}
if (-not $OutDir) {
	$OutDir = Join-Path $RepoRoot "reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight_gui_capture"
}

$ngfx = "C:\Program Files\NVIDIA Corporation\Nsight Graphics 2026.2.0\host\windows-desktop-nomad-x64\ngfx.exe"
if (-not (Test-Path -LiteralPath $ngfx)) {
	throw "ngfx.exe not found: $ngfx"
}
if (-not (Test-Path -LiteralPath $TracePath)) {
	throw "Trace not found: $TracePath"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$launchScript = Join-Path $OutDir "open_nsight_trace.ps1"
$shotScript = Join-Path $OutDir "capture_nsight_screen.ps1"
$png = Join-Path $OutDir "nvidia_nsight_gpu_trace_screen.png"
$log = Join-Path $OutDir "nvidia_nsight_gui_capture.log"
Remove-Item -LiteralPath $png, $log -ErrorAction SilentlyContinue

Stop-Process -Name ngfx, ngfx-ui -Force -ErrorAction SilentlyContinue

$launchBody = @"
`$ErrorActionPreference = 'Continue'
Start-Process -FilePath '$ngfx' -ArgumentList @('--project', '$TracePath') -WorkingDirectory '$OutDir'
"LAUNCHED=`$([DateTime]::Now.ToString('o')) USER=`$([Environment]::UserDomainName)\`$([Environment]::UserName) SESSION=`$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)" | Add-Content -Encoding UTF8 -LiteralPath '$log'
"@
Set-Content -Encoding UTF8 -LiteralPath $launchScript -Value $launchBody

$shotBody = @"
`$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
`$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
`$bmp = New-Object System.Drawing.Bitmap `$bounds.Width, `$bounds.Height
`$graphics = [System.Drawing.Graphics]::FromImage(`$bmp)
`$graphics.CopyFromScreen(`$bounds.Location, [System.Drawing.Point]::Empty, `$bounds.Size)
`$bmp.Save('$png', [System.Drawing.Imaging.ImageFormat]::Png)
`$graphics.Dispose()
`$bmp.Dispose()
"CAPTURED=`$([DateTime]::Now.ToString('o')) BOUNDS=`$(`$bounds.Width)x`$(`$bounds.Height) USER=`$([Environment]::UserDomainName)\`$([Environment]::UserName) SESSION=`$([System.Diagnostics.Process]::GetCurrentProcess().SessionId)" | Add-Content -Encoding UTF8 -LiteralPath '$log'
Get-Process ngfx*, workgraph_poc -ErrorAction SilentlyContinue |
	Select-Object ProcessName, Id, MainWindowTitle |
	Format-Table -AutoSize |
	Out-String |
	Add-Content -Encoding UTF8 -LiteralPath '$log'
"@
Set-Content -Encoding UTF8 -LiteralPath $shotScript -Value $shotBody

$launchTask = "${TaskPrefix}Open"
$shotTask = "${TaskPrefix}Shot"
$time = (Get-Date).AddMinutes(1).ToString("HH:mm")

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
schtasks.exe /Delete /TN $launchTask /F *> $null
schtasks.exe /Delete /TN $shotTask /F *> $null
schtasks.exe /Create /TN $launchTask /SC ONCE /ST $time /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$launchScript`"" /RU $RunAsUser /IT /F *> $null
$createOpen = $LASTEXITCODE
schtasks.exe /Run /TN $launchTask *> $null
$runOpen = $LASTEXITCODE
Start-Sleep -Seconds $WaitSeconds
schtasks.exe /Create /TN $shotTask /SC ONCE /ST $time /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$shotScript`"" /RU $RunAsUser /IT /F *> $null
$createShot = $LASTEXITCODE
schtasks.exe /Run /TN $shotTask *> $null
$runShot = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference

for ($i = 0; $i -lt 20 -and -not (Test-Path -LiteralPath $png); $i++) {
	Start-Sleep -Milliseconds 500
}

@(
	"CREATE_OPEN=$createOpen RUN_OPEN=$runOpen"
	"CREATE_SHOT=$createShot RUN_SHOT=$runShot"
) | Add-Content -Encoding UTF8 -LiteralPath $log

Get-Content -LiteralPath $log -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $png) {
	Get-Item -LiteralPath $png | Select-Object FullName, Length, LastWriteTime
} else {
	throw "Nsight screenshot was not created. See $log"
}
