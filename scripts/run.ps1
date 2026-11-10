param(
    [ValidateSet("Editor", "GamePlayer")][string]$Target = "Editor",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [string]$ProjectFolder = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ($Target -eq "GamePlayer") {
    if ([string]::IsNullOrWhiteSpace($ProjectFolder)) {
        $ProjectFolder = Join-Path $root "Examples\LuaCube"
    }
    $projectPath = (Resolve-Path -LiteralPath $ProjectFolder).ProviderPath
    $projectName = (Get-Item -LiteralPath $projectPath).Name
    $exeDir = Join-Path $projectPath "Build"
    $exe = Join-Path $exeDir "$projectName.exe"
} else {
    $exeDir = Join-Path $root "build\bin\$Configuration"
    $exe = Join-Path $exeDir "SatEngine.exe"
}

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Executable was not found at $exe. Build first."
}
Start-Process -FilePath $exe -WorkingDirectory $exeDir
