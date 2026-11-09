$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio installer was not found."
}

$install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $install) {
    throw "Visual Studio with the C++ toolset was not found."
}

$cmake = Join-Path $install "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    throw "CMake was not found in the Visual Studio installation."
}

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    # Free the Debug exe so MSVC can overwrite it (common F5 / lingering-process failure).
    Get-Process -Name SatEngine -ErrorAction SilentlyContinue | Stop-Process -Force

    & $cmake --preset windows
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & $cmake --build --preset windows-debug
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
