# Chronon3D Fast Build Script

$sccachePath = "C:\Users\pater\AppData\Local\Microsoft\WinGet\Packages\Mozilla.sccache_Microsoft.Winget.Source_8wekyb3d8bbwe\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe"
$cores = [int]($env:NUMBER_OF_PROCESSORS ?? 16)
$configurePreset = "win-debug-video"
$buildPreset = "win-debug-video"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "         Chronon3D Fast Compiler" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Preset: $buildPreset"
Write-Host "Cores detected: $cores"
Write-Host "Using compiler cache: $sccachePath"

& $sccachePath --start-server 2>$null

if (!(Test-Path "build/chronon/vs/$configurePreset/CMakeCache.txt")) {
    Write-Host "Configuring CMake preset $configurePreset with sccache..." -ForegroundColor Yellow
    cmake --preset $configurePreset -DCMAKE_CXX_COMPILER_LAUNCHER=$sccachePath
}

Write-Host "Building in parallel using $cores threads..." -ForegroundColor Yellow
$startTime = [System.DateTime]::Now

cmake --build --preset $buildPreset --parallel $cores

$endTime = [System.DateTime]::Now
$duration = ($endTime - $startTime).TotalSeconds

Write-Host "-----------------------------------------" -ForegroundColor Cyan
Write-Host "Build finished in ($([Math]::Round($duration, 2))) seconds." -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Cyan
