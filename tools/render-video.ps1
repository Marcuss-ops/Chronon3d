param(
    [Parameter(Mandatory=$true)]
    [string]$Composition,

    [Parameter(Mandatory=$true)]
    [string]$Output,

    [int]$Start = 0,
    [int]$End = 120,
    [int]$Fps = 30,
    [string]$Mode = "debug"
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    Write-Error "ffmpeg not found in PATH. Please install ffmpeg to use this script."
    exit 1
}

$framesDir = "output/frames/$Composition"

if (Test-Path $framesDir) {
    Remove-Item $framesDir -Recurse -Force
}

New-Item -ItemType Directory -Path $framesDir | Out-Null

Write-Host "[render-video] Build ($Mode)..." -ForegroundColor Cyan
# Add xmake to path if needed (custom for this environment)
$env:PATH += ";C:\Users\pater\xmake"

xmake f -m $Mode --profiling=false
xmake -y

if ($LASTEXITCODE -ne 0) {
    Write-Error "xmake build failed"
    exit 1
}

Write-Host "[render-video] Rendering frames $Start to $End..." -ForegroundColor Cyan
xmake run -w . chronon3d_cli render $Composition --start $Start --end $End -o "$framesDir/frame.png"

if ($LASTEXITCODE -ne 0) {
    Write-Error "chronon3d_cli render failed"
    exit 1
}

# The CLI now automatically appends _#### if #### is missing.
# So we expect frame_0000.png, frame_0001.png, etc.

$firstFrame = Join-Path $framesDir "frame_0000.png"
if (!(Test-Path $firstFrame)) {
    # Try with #### pattern just in case
    $firstFrame = Join-Path $framesDir "frame_0000.png"
    if (!(Test-Path $firstFrame)) {
        Write-Error "Expected first frame not found: $firstFrame"
        exit 1
    }
}

Write-Host "[render-video] Encoding MP4..." -ForegroundColor Cyan

ffmpeg -y `
    -framerate $Fps `
    -i "$framesDir/frame_%04d.png" `
    -c:v libx264 `
    -pix_fmt yuv420p `
    -movflags +faststart `
    $Output

if ($LASTEXITCODE -ne 0) {
    Write-Error "ffmpeg failed"
    exit 1
}

if (!(Test-Path $Output)) {
    Write-Error "Output video not created: $Output"
    exit 1
}

$size = (Get-Item $Output).Length
Write-Host "[render-video] Done: $Output ($($size / 1KB) KB)" -ForegroundColor Green
