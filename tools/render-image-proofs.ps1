$ErrorActionPreference = "Stop"

if (-not (Test-Path "output")) {
    New-Item -ItemType Directory -Path "output" | Out-Null
}

Write-Host "[Image Proofs] Building..." -ForegroundColor Cyan
$env:PATH += ";C:\Users\pater\xmake"
xmake f -m debug --profiling=false
xmake -y

if ($LASTEXITCODE -ne 0) {
    throw "xmake build failed"
}

# Ensure checker.png exists
Write-Host "[Image Proofs] Generating checker asset..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli render CheckerGen --frame 0 -o assets/images/checker.png

$proofs = @(
    @{ Name = "ImageProof"; Frame = 0; Out = "output/image_proof.png" },
    @{ Name = "AnimatedImageProof"; Frame = 0; Out = "output/animated_image_0000.png" },
    @{ Name = "AnimatedImageProof"; Frame = 30; Out = "output/animated_image_0030.png" },
    @{ Name = "AnimatedImageProof"; Frame = 60; Out = "output/animated_image_0060.png" },
    @{ Name = "ImageMissingProof"; Frame = 0; Out = "output/image_missing.png" },
    @{ Name = "ImageAlphaProof"; Frame = 0; Out = "output/image_alpha.png" },
    @{ Name = "ImageClippingProof"; Frame = 0; Out = "output/image_clipping.png" },
    @{ Name = "ImageLayerTransformProof"; Frame = 0; Out = "output/image_layer_transform.png" },
    @{ Name = "ImageDrawOrderProof"; Frame = 0; Out = "output/image_draw_order.png" }
)

foreach ($p in $proofs) {
    Write-Host "[Image Proofs] Rendering $($p.Name) frame $($p.Frame) -> $($p.Out)" -ForegroundColor Cyan

    xmake run -w . chronon3d_cli render $p.Name --frame $p.Frame -o $p.Out

    if ($LASTEXITCODE -ne 0) {
        throw "Render failed for $($p.Name) frame $($p.Frame)"
    }

    if (!(Test-Path $p.Out)) {
        throw "PNG not created: $($p.Out)"
    }

    $size = (Get-Item $p.Out).Length
    if ($size -le 0) {
        throw "PNG is empty: $($p.Out)"
    }
}

Write-Host "[Image Proofs] Done." -ForegroundColor Green
Get-Item output/image_*.png, output/animated_image_*.png | Select-Object Name, Length
