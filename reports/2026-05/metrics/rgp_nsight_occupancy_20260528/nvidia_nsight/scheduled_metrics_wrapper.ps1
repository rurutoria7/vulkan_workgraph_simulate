$ErrorActionPreference = "Continue"
Set-Location -LiteralPath 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run'
"START=$([DateTime]::Now.ToString('o')) SESSION=$([System.Diagnostics.Process]::GetCurrentProcess().SessionId) USER=$([Environment]::UserDomainName)\$([Environment]::UserName)" | Set-Content -Encoding UTF8 -LiteralPath 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight\scheduled_metrics_wrapper.stdout.log'
try {
	& 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\scripts\run_occupancy_metrics_20260528.ps1' -RepoRoot 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run' -OutDir 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight' -Machine 'nvidia' -GpuIndex 0 -Frames 150 -WarmupSeconds 2 -Repeats 3 -RunTimeoutSeconds 180  1>> 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight\scheduled_metrics_wrapper.stdout.log' 2>> 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight\scheduled_metrics_wrapper.stderr.log'
	$code = $LASTEXITCODE
	"EXIT_CODE=$code
END=$([DateTime]::Now.ToString('o'))" | Set-Content -Encoding UTF8 -LiteralPath 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight\scheduled_metrics_done.txt'
} catch {
	"EXCEPTION=$($_.Exception.Message)
END=$([DateTime]::Now.ToString('o'))" | Set-Content -Encoding UTF8 -LiteralPath 'C:\Users\Public\CodexWorkgraph\workgraph_vulkan_poc_run\reports\2026-05\metrics\rgp_nsight_occupancy_20260528\nvidia_nsight\scheduled_metrics_done.txt'
}
