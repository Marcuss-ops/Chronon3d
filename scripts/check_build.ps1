# check_build.ps1 - sanity check the CMake build graph
# Usage: .\scripts\check_build.ps1

$ErrorActionPreference = "Stop"

$toolDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $toolDir
Set-Location $root

if (-not $env:VCPKG_ROOT) {
    $defaultVcpkg = Join-Path $env:USERPROFILE "vcpkg"
    if (Test-Path $defaultVcpkg) {
        $env:VCPKG_ROOT = $defaultVcpkg
    }
}

$buildDir = "build\chronon\win-release"
$exe = Join-Path $buildDir "chronon3d_cli.exe"
$buildScript = Join-Path $root "tools\chronon-win.ps1"

function Measure-Build {
    param([string]$Label, [int]$LimitSecs)
    $start = Get-Date
    & powershell -ExecutionPolicy Bypass -File $buildScript -Configuration Release -SkipInstall -SkipCacheInstall
    if ($LASTEXITCODE -ne 0) {
        Write-Error "$Label FAILED"
        exit 1
    }
    $secs = [int]((Get-Date) - $start).TotalSeconds
    Write-Host "$Label`: ${secs}s" -ForegroundColor $(if ($secs -le $LimitSecs) { "Green" } else { "Red" })
    if ($secs -gt $LimitSecs) {
        Write-Error "FAIL: $Label took ${secs}s (limit: ${LimitSecs}s)"
        exit 1
    }
    return $secs
}

Write-Host ""
Write-Host "=== CLEAN BUILD (video=n) ===" -ForegroundColor Cyan
& powershell -ExecutionPolicy Bypass -File $buildScript -Configuration Release -SkipInstall -SkipCacheInstall
if ($LASTEXITCODE -ne 0) { Write-Error "Clean build FAILED"; exit 1 }

Write-Host ""
Write-Host "=== INCREMENTAL BUILD ===" -ForegroundColor Cyan
(Get-Item "src\core\pipeline.cpp").LastWriteTime = Get-Date
Measure-Build "Incremental build" 5 | Out-Null

Write-Host ""
Write-Host "=== GUARDRAIL CHECK (video=n must not link FFmpeg in core/graph) ===" -ForegroundColor Cyan
$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($dumpbin -and (Test-Path $exe)) {
    $imports = dumpbin /IMPORTS $exe 2>$null | Select-String "avcodec|avformat|swscale"
    if ($imports) {
        Write-Error "FAIL: FFmpeg linked into the binary even with video=n:`n$imports"
        exit 1
    }
    Write-Host "OK: no FFmpeg imports in the binary (video=n)" -ForegroundColor Green
} else {
    Write-Host "SKIP: dumpbin or chronon3d_cli.exe not found" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "=== ALL OK ===" -ForegroundColor Green
Write-Host "The CMake dev loop is healthy. You can push." -ForegroundColor Green
