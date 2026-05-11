param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Get-LatestStamp {
    $paths = @(
        "include\chronon",
        "scenes",
        "src",
        "CMakeLists.txt",
        "CMakePresets.json",
        "vcpkg.json"
    )

    $latest = [DateTime]::MinValue
    foreach ($path in $paths) {
        if (Test-Path $path -PathType Container) {
            foreach ($item in Get-ChildItem $path -Recurse -File) {
                if ($item.LastWriteTimeUtc -gt $latest) {
                    $latest = $item.LastWriteTimeUtc
                }
            }
        } elseif (Test-Path $path -PathType Leaf) {
            $item = Get-Item $path
            if ($item.LastWriteTimeUtc -gt $latest) {
                $latest = $item.LastWriteTimeUtc
            }
        }
    }

    return $latest
}

function Invoke-ChrononBuild {
    param([switch]$Bootstrap)

    if ($Bootstrap) {
        & powershell -ExecutionPolicy Bypass -File "$root\chronon-win.ps1" -Configuration $Configuration
    } else {
        & powershell -ExecutionPolicy Bypass -File "$root\chronon-win.ps1" -Configuration $Configuration -SkipInstall
    }
}

Invoke-ChrononBuild -Bootstrap

$lastStamp = Get-LatestStamp
Write-Host "Chronon watch running. Watching include\chronon, scenes, src, and build files."

while ($true) {
    Start-Sleep -Milliseconds 500
    $currentStamp = Get-LatestStamp
    if ($currentStamp -gt $lastStamp) {
        $lastStamp = $currentStamp
        Write-Host "Change detected, rebuilding ChrononTemplate..."
        Invoke-ChrononBuild
    }
}
