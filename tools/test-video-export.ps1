$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg not found in PATH. Install ffmpeg or add it to PATH."
}

Write-Host "[Video Export Test] Building..." -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File "$PSScriptRoot\chronon-win.ps1" -Configuration Debug

if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed"
}

$cli = "build\chronon\win-debug\chronon3d_cli.exe"

# 1. Range render test
$rangeDir = "output/video_range_test"
if (Test-Path $rangeDir) {
    Remove-Item $rangeDir -Recurse -Force
}
New-Item -ItemType Directory -Path $rangeDir | Out-Null

Write-Host "[Video Export Test] Testing frame range 0-10 (expecting 10 frames)..." -ForegroundColor Cyan
& $cli render AnimatedImageProof --start 0 --end 10 -o "$rangeDir/frame.png"

if ($LASTEXITCODE -ne 0) {
    throw "range render failed"
}

$count = (Get-ChildItem $rangeDir -Filter "*.png").Count
if ($count -ne 10) {
    throw "Expected 10 frames (0-9), got $count"
}

if (!(Test-Path "$rangeDir/frame_0000.png")) {
    throw "Missing frame_0000.png"
}

if (!(Test-Path "$rangeDir/frame_0009.png")) {
    throw "Missing frame_0009.png"
}

if (Test-Path "$rangeDir/frame_0010.png") {
    throw "Unexpected frame_0010.png; end should be exclusive"
}

# 2. Boundary render test
$boundaryDir = "output/video_range_boundary"
if (Test-Path $boundaryDir) {
    Remove-Item $boundaryDir -Recurse -Force
}
New-Item -ItemType Directory -Path $boundaryDir | Out-Null

Write-Host "[Video Export Test] Testing frame range 30-35 (expecting 5 frames)..." -ForegroundColor Cyan
& $cli render AnimatedImageProof --start 30 --end 35 -o "$boundaryDir/frame.png"

if ($LASTEXITCODE -ne 0) {
    throw "boundary range render failed"
}

$count = (Get-ChildItem $boundaryDir -Filter "*.png").Count
if ($count -ne 5) {
    throw "Expected 5 boundary frames (30-34), got $count"
}

if (!(Test-Path "$boundaryDir/frame_0030.png")) {
    throw "Missing frame_0030.png"
}

if (!(Test-Path "$boundaryDir/frame_0034.png")) {
    throw "Missing frame_0034.png"
}

if (Test-Path "$boundaryDir/frame_0035.png") {
    throw "Unexpected frame_0035.png; end should be exclusive"
}

# 3. MP4 render test
$out = "output/video_export_test.mp4"
if (Test-Path $out) {
    Remove-Item $out -Force
}

Write-Host "[Video Export Test] Testing MP4 export..." -ForegroundColor Cyan
.\tools\render-video.ps1 AnimatedImageProof $out -Start 0 -End 30 -Fps 30

if (!(Test-Path $out)) {
    throw "MP4 was not created"
}

$size = (Get-Item $out).Length
if ($size -le 0) {
    throw "MP4 is empty"
}

Write-Host "[Video Export Test] Done." -ForegroundColor Green
Write-Host "MP4 size: $size bytes"
