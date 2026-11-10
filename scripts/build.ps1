param(
    [ValidateSet("Editor", "GamePlayer")][string]$Target = "Editor",
    [ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
    [string]$ProjectFolder = ""
)

$ErrorActionPreference = "Stop"

# These stage markers are also consumed by the editor's live build status.
Write-Output "[SatBuild] 1/4 Preparing build tools..."

$root = Split-Path -Parent $PSScriptRoot
if ($Target -eq "GamePlayer") {
    if ([string]::IsNullOrWhiteSpace($ProjectFolder)) {
        $ProjectFolder = Join-Path $root "Examples\LuaCube"
    }
    # Resolve user-relative paths before changing to the engine checkout.
    $projectPath = (Resolve-Path -LiteralPath $ProjectFolder).ProviderPath
    if (-not (Test-Path -LiteralPath (Join-Path $projectPath "SatEngineProject.sat") -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $projectPath "Scene.sat") -PathType Leaf)) {
        throw "Project must contain SatEngineProject.sat and Scene.sat. Save the scene in the editor first."
    }
    $outputPath = Join-Path $projectPath "Build"
    $exportBuild = Join-Path $outputPath ".satengine-build"
}

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

Push-Location $root
try {
    if ($Target -eq "GamePlayer") {
        Write-Output "[SatBuild] 2/4 Configuring project and fetching dependencies..."
        & $cmake --preset windows-gameplayer -B $exportBuild "-DSAT_GAME_PROJECT=$projectPath"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Output "[SatBuild] 3/4 Compiling engine and embedded Lua game..."
        & $cmake --build $exportBuild --config $Configuration --target GamePlayer
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Output "[SatBuild] 4/4 Packaging standalone executable..."
        & $cmake --install $exportBuild --config $Configuration --prefix $outputPath
        exit $LASTEXITCODE
    }

    # Free the running editor so MSVC can overwrite it.
    Get-Process -Name SatEngine -ErrorAction SilentlyContinue | Stop-Process -Force
    Write-Output "[SatBuild] 2/4 Configuring editor..."
    & $cmake --preset windows
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $buildPreset = "windows-$($Configuration.ToLowerInvariant())"
    Write-Output "[SatBuild] 3/4 Compiling editor..."
    & $cmake --build --preset $buildPreset --target Editor
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
