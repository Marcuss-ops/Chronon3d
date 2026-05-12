param(
    [string]$Mode = "debug",
    [switch]$Profiling
)

$ErrorActionPreference = "Stop"

# Ensure output directory exists
if (-not (Test-Path "output")) {
    New-Item -ItemType Directory -Path "output" | Out-Null
}

Write-Host "`n[Chronon3d Dev] Configuring xmake ($Mode, Profiling=$Profiling)..." -ForegroundColor Cyan

$profiling_val = if ($Profiling) { "true" } else { "false" }
xmake f -m $Mode --profiling=$profiling_val

Write-Host "[Chronon3d Dev] Building..." -ForegroundColor Cyan
xmake -y

Write-Host "[Chronon3d Dev] Verifying List Command..." -ForegroundColor Cyan
xmake run chronon3d_cli list

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: chronon3d_cli list failed (Exit Code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[Chronon3d Dev] Running Smoke Render (RoundedRectProof)..." -ForegroundColor Cyan
xmake run chronon3d_cli render RoundedRectProof --frame 0 -o output/xmake_smoke.png

if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: chronon3d_cli render failed (Exit Code: $LASTEXITCODE)" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "`n[Chronon3d Dev] Success! Build and smoke test passed." -ForegroundColor Green
