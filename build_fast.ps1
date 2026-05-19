# Chronon3D Fast Build Script

$sccachePath = "C:\Users\pater\AppData\Local\Microsoft\WinGet\Packages\Mozilla.sccache_Microsoft.Winget.Source_8wekyb3d8bbwe\sccache-v0.15.0-x86_64-pc-windows-msvc\sccache.exe"
$cores = $env:NUMBER_OF_PROCESSORS

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "         Chronon3D Fast Compiler" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "Cores detected: $cores"
Write-Host "Using compiler cache: $sccachePath"

# Start sccache server if not running
& $sccachePath --start-server 2>$null

# Reconfigure CMake to use compiler launcher if not already done
if (!(Test-Path "build_vs\CMakeCache.txt")) {
    Write-Host "Configuring CMake with sccache..." -ForegroundColor Yellow
    cmake -B build_vs -S . -DCMAKE_CXX_COMPILER_LAUNCHER=$sccachePath
}

Write-Host "Building in parallel using $cores threads..." -ForegroundColor Yellow
$startTime = [System.DateTime]::Now

# Run MSBuild in parallel
cmake --build build_vs --config Release --parallel $cores

$endTime = [System.DateTime]::Now
$duration = ($endTime - $startTime).TotalSeconds

Write-Host "-----------------------------------------" -ForegroundColor Cyan
Write-Host "Build finished in ($([Math]::Round($duration, 2))) seconds." -ForegroundColor Green
Write-Host "=========================================" -ForegroundColor Cyan
