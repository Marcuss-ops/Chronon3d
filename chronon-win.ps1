param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [switch]$SkipInstall
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

$vcpkg = if (Test-Path "C:\vcpkg\vcpkg.exe") {
    "C:\vcpkg\vcpkg.exe"
} else {
    throw "vcpkg.exe not found at C:\vcpkg\vcpkg.exe"
}

$vsDevCmdCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
)
$vsDevCmd = $vsDevCmdCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vsDevCmd) {
    throw "VsDevCmd.bat not found. Install Visual Studio Build Tools or Community with the C++ workload."
}

$sccache = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter sccache.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $sccache) {
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

if (-not $SkipInstall) {
    $packages = @(
        "sdl2",
        "spdlog",
        "fmt",
        "entt",
        "meshoptimizer",
        "stb",
        "nlohmann-json",
        "magic-enum",
        "ffmpeg",
        "tracy",
        "mimalloc"
    )

    & $vcpkg install --classic --triplet x64-windows --x-install-root "$root\vcpkg_installed" --no-print-usage @packages
}

$preset = if ($Configuration -eq "Debug") { "win-debug" } else { "win-release" }
$buildPreset = if ($Configuration -eq "Debug") { "win-debug" } else { "win" }

$cmd = @(
    "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul",
    "cmake --preset $preset",
    "cmake --build --preset $buildPreset"
) -join " && "

& cmd /c $cmd
