# tools/test-xmake.ps1
$ErrorActionPreference = "Stop"

Write-Host "Starting xmake smoke test..." -ForegroundColor Cyan

# 1. Clean and Configure
Write-Host "`n[1/7] Cleaning and configuring..." -ForegroundColor Cyan
if (Get-Command xmake -ErrorAction SilentlyContinue) {
    xmake clean -a
    xmake f -m debug --profiling=false
    xmake -y
} else {
    Write-Error "xmake command not found in PATH."
    exit 1
}

# 2. Help Test
Write-Host "`n[2/7] Testing CLI help..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli --help
if ($LASTEXITCODE -ne 0) { throw "help failed" }

# 3. List Test
Write-Host "`n[3/7] Testing composition list..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli list
if ($LASTEXITCODE -ne 0) { throw "list failed" }

# 4. Info Test
Write-Host "`n[4/7] Testing composition info (RoundedRectProof)..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli info RoundedRectProof
if ($LASTEXITCODE -ne 0) { throw "info failed" }

# 5. Render Test (Single Frame)
Write-Host "`n[5/7] Testing single frame render..." -ForegroundColor Cyan
if (-not (Test-Path "output")) { New-Item -ItemType Directory -Path "output" | Out-Null }
xmake run -w . chronon3d_cli render RoundedRectProof --frame 0 -o output/xmake_smoke.png
if ($LASTEXITCODE -ne 0) { throw "render failed" }

if (!(Test-Path "output/xmake_smoke.png")) {
    throw "PNG was not created"
}

$size = (Get-Item "output/xmake_smoke.png").Length
if ($size -le 0) {
    throw "PNG is empty"
}

# 6. Render Test (Animation)
Write-Host "`n[6/7] Testing animation frames (AnimatedVisualQualityProof)..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli render AnimatedVisualQualityProof --frame 0 -o output/xmake_avq_0000.png
xmake run -w . chronon3d_cli render AnimatedVisualQualityProof --frame 30 -o output/xmake_avq_0030.png
xmake run -w . chronon3d_cli render AnimatedVisualQualityProof --frame 60 -o output/xmake_avq_0060.png
if ($LASTEXITCODE -ne 0) { throw "animation render failed" }

# 7. Proof Tests
Write-Host "`n[7/7] Testing main proofs..." -ForegroundColor Cyan
$proofs = @("BasicShapes", "BlendingShowcase", "ShadowProof", "GlowProof", "VisualStackOrderProof")
foreach ($proof in $proofs) {
    Write-Host "Rendering $proof..." -ForegroundColor Gray
    xmake run -w . chronon3d_cli render $proof --frame 0 -o "output/xmake_$($proof).png"
    if ($LASTEXITCODE -ne 0) { throw "$proof render failed" }
}

Write-Host "`n[SUCCESS] xmake smoke test passed!" -ForegroundColor Green
