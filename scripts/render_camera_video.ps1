param(
    [ValidateSet("Tilt", "Pan", "Roll")]
    [string]$Axis = "Tilt",
    [string]$CompId = "",
    [string]$Output = "",
    [int]$Start = 0,
    [int]$End = 60,
    [string]$BuildDir = "build_video_vcpkg"
)

$ErrorActionPreference = "Stop"

$toolDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $toolDir
Set-Location $root

$vcpkgRoot = "C:\vcpkg"
$vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$vcpkgInstalled = Join-Path $vcpkgRoot "installed"
$x264Dll = Join-Path $vcpkgInstalled "x64-windows\bin\libx264-164.dll"

if (-not (Test-Path $vcpkgToolchain)) {
    throw "vcpkg toolchain not found at $vcpkgToolchain"
}
if (-not (Test-Path $vcpkgInstalled)) {
    throw "vcpkg installed tree not found at $vcpkgInstalled"
}

if (-not (Test-Path $x264Dll)) {
    Write-Host "x264 not found in the active vcpkg tree; installing ffmpeg[x264]..." -ForegroundColor Yellow
    & (Join-Path $vcpkgRoot "vcpkg.exe") install --classic ffmpeg[x264]:x64-windows --x-install-root $vcpkgInstalled --no-print-usage
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ([string]::IsNullOrWhiteSpace($CompId)) {
    $CompId = "CameraTiltImage${Axis}Clip"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
    $lowerAxis = $Axis.ToLowerInvariant()
    $Output = "output\camera_${lowerAxis}_video.mp4"
}

cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain `
    -DVCPKG_MANIFEST_MODE=ON `
    -DVCPKG_MANIFEST_FEATURES=video `
    -DVCPKG_INSTALLED_DIR=$vcpkgInstalled `
    -DVCPKG_TARGET_TRIPLET=x64-windows `
    -DCHRONON3D_ENABLE_VIDEO=ON `
    -DCHRONON3D_BUILD_TESTS=OFF `
    -DCHRONON3D_BUILD_CLI=ON
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build $BuildDir --config Debug --target chronon3d_cli -j 16
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$cliDir = Join-Path $BuildDir "apps\chronon3d_cli\Debug"
$env:PATH = "$vcpkgRoot\installed\x64-windows\bin;$cliDir;$env:PATH"

& (Join-Path $cliDir "chronon3d_cli.exe") video $CompId -o $Output --start $Start --end $End
exit $LASTEXITCODE
