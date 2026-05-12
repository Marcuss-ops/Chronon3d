$ErrorActionPreference = "Stop"

if (-not (Test-Path "output")) {
    New-Item -ItemType Directory -Path "output" | Out-Null
}

if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) {
    throw "xmake not found in PATH."
}

Write-Host "[Camera2.5D Proofs] Building..." -ForegroundColor Cyan
xmake f -m debug --profiling=false
xmake -y

if ($LASTEXITCODE -ne 0) { throw "xmake build failed" }

$proofs = @(
    @{ Name = "Camera25DDepthScaleProof";    Frame = 0;  Out = "output/camera25d_depth_scale.png" },

    @{ Name = "Camera25DParallaxProof";      Frame = 0;  Out = "output/camera25d_parallax_0000.png" },
    @{ Name = "Camera25DParallaxProof";      Frame = 30; Out = "output/camera25d_parallax_0030.png" },
    @{ Name = "Camera25DParallaxProof";      Frame = 60; Out = "output/camera25d_parallax_0060.png" },

    @{ Name = "Camera25DZOrderProof";        Frame = 0;  Out = "output/camera25d_z_order.png" },

    @{ Name = "Camera25DCameraPushProof";    Frame = 0;  Out = "output/camera25d_push_0000.png" },
    @{ Name = "Camera25DCameraPushProof";    Frame = 30; Out = "output/camera25d_push_0030.png" },
    @{ Name = "Camera25DCameraPushProof";    Frame = 60; Out = "output/camera25d_push_0060.png" },

    @{ Name = "Camera25DMixed2D3DProof";     Frame = 0;  Out = "output/camera25d_mixed_0000.png" },
    @{ Name = "Camera25DMixed2D3DProof";     Frame = 59; Out = "output/camera25d_mixed_0059.png" },

    @{ Name = "Camera25DImageParallaxProof"; Frame = 0;  Out = "output/camera25d_image_parallax_0000.png" },
    @{ Name = "Camera25DImageParallaxProof"; Frame = 30; Out = "output/camera25d_image_parallax_0030.png" },
    @{ Name = "Camera25DImageParallaxProof"; Frame = 60; Out = "output/camera25d_image_parallax_0060.png" }
)

foreach ($p in $proofs) {
    Write-Host "[Camera2.5D Proofs] Rendering $($p.Name) frame $($p.Frame) -> $($p.Out)" -ForegroundColor Cyan
    xmake run -w . chronon3d_cli render $p.Name --frame $p.Frame -o $p.Out
    if ($LASTEXITCODE -ne 0) { throw "Render failed for $($p.Name) frame $($p.Frame)" }
    if (!(Test-Path $p.Out)) { throw "PNG not created: $($p.Out)" }
    if ((Get-Item $p.Out).Length -le 0) { throw "PNG is empty: $($p.Out)" }
}

Write-Host "[Camera2.5D Proofs] Done." -ForegroundColor Green
Get-Item output/camera25d_*.png | Select-Object Name, Length
