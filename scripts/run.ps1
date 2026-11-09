$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$exeDir = Join-Path $root "build\bin\Debug"
$exe = Join-Path $exeDir "SatEngine.exe"

if (-not (Test-Path $exe)) {
    throw "SatEngine.exe was not found at $exe. Build first."
}

Get-Process -Name SatEngine -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Process -FilePath $exe -WorkingDirectory $exeDir
