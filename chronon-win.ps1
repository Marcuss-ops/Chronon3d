param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipInstall,
    [switch]$SkipCacheInstall
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$vcpkg = if (Test-Path "C:\vcpkg\vcpkg.exe") {
    "C:\vcpkg\vcpkg.exe"
} else {
    throw "vcpkg.exe not found at C:\vcpkg\vcpkg.exe"
}

$vsDevCmd = $null
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    # Try Community v18 first
    $vsDevCmd = & $vsWhere -version "[18,19)" -products Microsoft.VisualStudio.Product.Community -find "Common7\Tools\VsDevCmd.bat" | Select-Object -First 1
    if (-not $vsDevCmd) {
        # Fallback to any latest
        $vsDevCmd = & $vsWhere -latest -products * -requires Microsoft.Component.MSBuild -find "Common7\Tools\VsDevCmd.bat" | Select-Object -First 1
    }
}
if (-not $vsDevCmd) {
    $vsDevCmdCandidates = @(
        "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    )
    $vsDevCmd = $vsDevCmdCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $vsDevCmd) {
    throw "VsDevCmd.bat not found. Install Visual Studio Build Tools or Community with the C++ workload."
}

Write-Host "Using VsDevCmd: $vsDevCmd"

$sccache = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter sccache.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $sccache -and -not $SkipCacheInstall) {
    $winget = Get-Command winget -ErrorAction SilentlyContinue
    if ($winget) {
        winget install --id Mozilla.sccache -e --accept-package-agreements --accept-source-agreements | Out-Host
        $sccache = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter sccache.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
    }
}

if ($sccache) {
    $env:CHRONON_COMPILER_LAUNCHER = $sccache
}

$packages = @(
    "sdl2", "spdlog", "fmt", "entt", "meshoptimizer", "stb",
    "nlohmann-json", "magic-enum", "ffmpeg", "tracy", "mimalloc",
    "taskflow", "concurrentqueue", "abseil", "glm", "simdjson",
    "xxhash", "highway", "doctest"
)

$preset = if ($Configuration -eq "Debug") { "win-debug" } else { "win-release" }
$buildPreset = if ($Configuration -eq "Debug") { "win-debug" } else { "win" }

$packages_str = $packages -join " "

$cmd = @(
    "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul",
    "if /i `"%SkipInstall%`" == `"False`" ( `"$vcpkg`" install --classic --triplet x64-windows --x-install-root `"$root\vcpkg_installed`" --no-print-usage $packages_str )",
    "cmake --preset $preset",
    "cmake --build --preset $buildPreset"
) -join " && "

$env:SkipInstall = if ($SkipInstall) { "True" } else { "False" }
& cmd /c $cmd
