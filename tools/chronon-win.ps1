# tools/chronon-win.ps1
# Build script for Chronon3d on Windows.
# Dependencies are managed via vcpkg manifest mode (vcpkg.json).

$ErrorActionPreference = "Stop"

# Get the directory of the script
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path "$ScriptDir\.."
Set-Location $ProjectDir

Write-Host "Chronon3d Windows Build Tool" -ForegroundColor Cyan
Write-Host "Project Root: $ProjectDir"

# ── vcpkg ─────────────────────────────────────────────────────────────────────
if (-not $env:VCPKG_ROOT) {
    $env:VCPKG_ROOT = "$env:USERPROFILE\vcpkg"
}

if (-not (Test-Path "$env:VCPKG_ROOT\vcpkg.exe")) {
    Write-Host "vcpkg not found at $env:VCPKG_ROOT" -ForegroundColor Yellow
    Write-Host "Cloning and bootstrapping vcpkg..."
    git clone https://github.com/microsoft/vcpkg "$env:VCPKG_ROOT"
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg" }
    
    & "$env:VCPKG_ROOT\bootstrap-vcpkg.bat" -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "Failed to bootstrap vcpkg" }
}

# Fetch full history so the baseline commit in vcpkg.json is reachable.
$vcpkgGitDir = Join-Path $env:VCPKG_ROOT ".git"
if (Test-Path $vcpkgGitDir) {
    $isShallow = git -C "$env:VCPKG_ROOT" rev-parse --is-shallow-repository 2>$null
    if ($isShallow -eq "true") {
        Write-Host "Fetching full vcpkg history for baseline..."
        git -C "$env:VCPKG_ROOT" fetch --unshallow
    }
}

# ── Compiler cache (optional) ─────────────────────────────────────────────────
if (Get-Command sccache -ErrorAction SilentlyContinue) {
    $env:CMAKE_C_COMPILER_LAUNCHER = "sccache"
    $env:CMAKE_CXX_COMPILER_LAUNCHER = "sccache"
    Write-Host "Using sccache for build acceleration." -ForegroundColor Green
}

# ── Configure + Build ─────────────────────────────────────────────────────────
$Preset = if ($env:CHRONON_PRESET) { $env:CHRONON_PRESET } else { "win-release" }
$Jobs = if ($env:CHRONON_JOBS) { $env:CHRONON_JOBS } else { $env:NUMBER_OF_PROCESSORS }

Write-Host "`n[1/2] Configuring with preset: $Preset" -ForegroundColor Cyan
cmake --preset "$Preset"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }

Write-Host "`n[2/2] Building..." -ForegroundColor Cyan
# Binary dir follows the pattern in CMakePresets.json: build/chronon/${presetName}
# Note: win-release-video and others might have different paths, but we try to infer it.
$BuildDir = "build/chronon/$Preset"
if ($Preset -like "*-video") {
    $BuildDir = "build/chronon/vs/$Preset"
}

cmake --build "$BuildDir" -j $Jobs --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host "`nBuild complete!" -ForegroundColor Green
Write-Host "Binaries are located in: $BuildDir"
Write-Host "  chronon3d_cli   — CLI for rendering and video export"
Write-Host "  test targets    — available via ctest (e.g., ctest --preset win-test)"
