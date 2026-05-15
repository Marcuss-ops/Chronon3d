param(
    [ValidateSet("Tilt", "Pan", "Roll")]
    [string]$Axis = "Tilt",
    [string]$CompId = "",
    [string]$Output = "",
    [int]$Start = 0,
    [int]$End = 60,
    [string]$Preset = "win-debug-video"
)

$ErrorActionPreference = "Stop"

$toolDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $toolDir
Set-Location $root

$vcpkgRoot = "C:\vcpkg"
$vcpkgInstalled = Join-Path $vcpkgRoot "installed"

if ([string]::IsNullOrWhiteSpace($CompId)) {
    $CompId = "CameraTiltImage${Axis}Clip"
}

if ([string]::IsNullOrWhiteSpace($Output)) {
    $lowerAxis = $Axis.ToLowerInvariant()
    $Output = "output\camera_${lowerAxis}_video.mp4"
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$binaryDir = Join-Path $root "build\chronon\vs\$Preset"
$cli = Get-ChildItem $binaryDir -Recurse -Filter chronon3d_cli.exe | Select-Object -First 1 -ExpandProperty FullName
if (-not $cli) {
    throw "chronon3d_cli.exe not found under $binaryDir"
}

$cliDir = Split-Path -Parent $cli
$env:PATH = "$vcpkgRoot\installed\x64-windows\bin;$cliDir;$env:PATH"

& $cli video camera --axis $Axis -o $Output --start $Start --end $End
exit $LASTEXITCODE
