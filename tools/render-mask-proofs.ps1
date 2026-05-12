$ErrorActionPreference = "Stop"
if (-not (Test-Path "output")) { New-Item -ItemType Directory -Path "output" | Out-Null }
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { throw "xmake not found in PATH." }

Write-Host "[Mask Proofs] Building..." -ForegroundColor Cyan
xmake f -m debug --profiling=false; xmake -y
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

$proofs = @(
    @{ Name = "MaskRectProof";           Frame = 0;  Out = "output/mask_rect.png" },
    @{ Name = "MaskRoundedRectProof";    Frame = 0;  Out = "output/mask_rounded_rect.png" },
    @{ Name = "MaskCircleProof";         Frame = 0;  Out = "output/mask_circle.png" },
    @{ Name = "MaskTextRevealProof";     Frame = 0;  Out = "output/mask_text_reveal.png" },
    @{ Name = "AnimatedMaskRevealProof"; Frame = 0;  Out = "output/mask_reveal_0000.png" },
    @{ Name = "AnimatedMaskRevealProof"; Frame = 30; Out = "output/mask_reveal_0030.png" },
    @{ Name = "AnimatedMaskRevealProof"; Frame = 60; Out = "output/mask_reveal_0060.png" },
    @{ Name = "MaskLayerTransformProof"; Frame = 0;  Out = "output/mask_layer_transform.png" },
    @{ Name = "MaskCamera25DProof";      Frame = 0;  Out = "output/mask_camera25d_0000.png" },
    @{ Name = "MaskCamera25DProof";      Frame = 30; Out = "output/mask_camera25d_0030.png" },
    @{ Name = "MaskCamera25DProof";      Frame = 60; Out = "output/mask_camera25d_0060.png" }
)
foreach ($p in $proofs) {
    Write-Host "[Mask Proofs] $($p.Name) frame $($p.Frame) -> $($p.Out)" -ForegroundColor Cyan
    xmake run -w . chronon3d_cli render $p.Name --frame $p.Frame -o $p.Out
    if ($LASTEXITCODE -ne 0) { throw "Render failed: $($p.Name)" }
    if (!(Test-Path $p.Out) -or (Get-Item $p.Out).Length -le 0) { throw "PNG empty: $($p.Out)" }
}
Write-Host "[Mask Proofs] Done." -ForegroundColor Green
Get-Item output/mask_*.png | Select-Object Name, Length
