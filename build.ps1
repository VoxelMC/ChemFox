# Configure and build ChemFox on Windows with MSVC and a conda-forge environment.
#   .\build.ps1 [-Test] [-Run] [-Prefix <conda env>] [-RepoUrl <https://github.com/you/ChemFox>]
# The environment defaults to ..\.env (see environment.yml); CI passes its own.
param([string]$Config = "Release", [switch]$Run, [switch]$Test, [string]$Prefix = "", [string]$RepoUrl = "")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if (-not $Prefix) { $Prefix = if ($env:CONDA_PREFIX) { $env:CONDA_PREFIX } else { Join-Path $root "..\.env" } }
$envPrefix = Resolve-Path $Prefix
$lib = Join-Path $envPrefix "Library"

# Import the MSVC developer environment into this PowerShell session (skipped when already there).
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
    $ErrorActionPreference = "Continue"
    cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
}
$ErrorActionPreference = "Continue"
$env:PATH = "$lib\bin;$envPrefix;$envPrefix\Scripts;$env:PATH"

$build = Join-Path $root "build"
if (-not (Test-Path (Join-Path $build "build.ninja"))) {
    cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_PREFIX_PATH=$lib" "-DRDKIT_ROOT=$lib" "-DCHEMFOX_REPO_URL=$RepoUrl"
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} elseif ($RepoUrl) {
    cmake -S $root -B $build "-DCHEMFOX_REPO_URL=$RepoUrl" | Out-Null
}
cmake --build $build
if ($LASTEXITCODE) { exit $LASTEXITCODE }

if ($Test) { & "$build\bin\chemfox_tests.exe"; if ($LASTEXITCODE) { exit $LASTEXITCODE } }
if ($Run) { Start-Process "$build\bin\ChemFox.exe" }
