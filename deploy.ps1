# Build a self-contained folder, a zip and (with -Installer) a Windows installer.
#   .\deploy.ps1 [-Installer] [-Prefix <conda env>] [-RepoUrl <url>]
# Outputs in dist\: ChemFox\ChemFox.exe, ChemFox-<version>-windows-x64.zip, ChemFox-<version>-setup.exe
param([switch]$NoZip, [switch]$Installer, [string]$Prefix = "", [string]$RepoUrl = "")
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if (-not $Prefix) { $Prefix = if ($env:CONDA_PREFIX) { $env:CONDA_PREFIX } else { Join-Path $root "..\.env" } }
$lib = Join-Path (Resolve-Path $Prefix) "Library"
$bin = Join-Path $lib "bin"
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\(ChemFox VERSION (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value

# Build first; this also loads the MSVC environment (for dumpbin) into this process.
& (Join-Path $root "build.ps1") -Prefix $Prefix -RepoUrl $RepoUrl
if ($LASTEXITCODE) { exit $LASTEXITCODE }
# Native tools write progress to stderr; rely on exit codes instead.
$ErrorActionPreference = "Continue"

$dist = Join-Path $root "dist\ChemFox"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item (Join-Path $root "build\bin\ChemFox.exe") $dist

# Qt libraries and plugins. The compiler runtime is copied below from the env, matching what Qt was built with.
$env:PATH = "$bin;$env:PATH"
& (Join-Path $bin "windeployqt6.exe") --qtpaths (Join-Path $bin "qtpaths6.exe") --release --no-translations --no-compiler-runtime --no-opengl-sw `
    --no-system-d3d-compiler --no-quick-import --dir $dist (Join-Path $dist "ChemFox.exe") | Out-Null
if ($LASTEXITCODE) { throw "windeployqt failed ($LASTEXITCODE)" }

# Everything else (RDKit, Boost, freetype, ICU, the MSVC runtime, ...): follow imports
# recursively and copy whatever resolves to the env's bin folder.
$have = @{}
Get-ChildItem $dist -Recurse -Include *.dll, *.exe | ForEach-Object { $have[$_.Name.ToLower()] = $true }
$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem $dist -Recurse -Include *.dll, *.exe | ForEach-Object { $queue.Enqueue($_.FullName) }
$copied = 0
while ($queue.Count) {
    $file = $queue.Dequeue()
    $deps = dumpbin /nologo /dependents $file | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $matches[1] }
    foreach ($d in $deps) {
        $key = $d.ToLower()
        if ($have.ContainsKey($key) -or $key.StartsWith("api-ms-win-")) { continue }
        $src = Join-Path $bin $d
        if (-not (Test-Path $src)) { continue }   # a Windows system DLL
        Copy-Item $src $dist
        $have[$key] = $true
        $copied++
        $queue.Enqueue((Join-Path $dist $d))
    }
}
# Runtime pieces loaded dynamically rather than imported.
foreach ($d in "vcruntime140_1.dll", "msvcp140_1.dll", "msvcp140_2.dll", "msvcp140_atomic_wait.dll") {
    if (-not $have.ContainsKey($d) -and (Test-Path (Join-Path $bin $d))) { Copy-Item (Join-Path $bin $d) $dist }
}
Copy-Item (Join-Path $root "samples") (Join-Path $dist "samples") -Recurse
foreach ($f in "README.md", "LICENSE", "NOTICE", "CHANGELOG.md") { if (Test-Path (Join-Path $root $f)) { Copy-Item (Join-Path $root $f) $dist } }
& (Join-Path $root "packaging\collect-licenses.ps1") -Prefix $Prefix -Dest (Join-Path $dist "licenses")
if (-not (Test-Path (Join-Path $dist "licenses\THIRD-PARTY.txt"))) { throw "Third-party licences were not collected; refusing to package without them" }

$size = (Get-ChildItem $dist -Recurse | Measure-Object Length -Sum).Sum / 1MB
"Deployed to $dist ($copied non-Qt DLLs, {0:N0} MB)" -f $size

if (-not $NoZip) {
    $zip = Join-Path $root "dist\ChemFox-$version-windows-x64.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path $dist -DestinationPath $zip
    "Zipped to $zip ({0:N0} MB)" -f ((Get-Item $zip).Length / 1MB)
}

if ($Installer) {
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { throw "Inno Setup 6 not found (winget install JRSoftware.InnoSetup)" }
    & $iscc /Q "/DAppVersion=$version" "/DSourceDir=$dist" "/DOutputDir=$(Join-Path $root 'dist')" (Join-Path $root "packaging\windows\chemfox.iss")
    if ($LASTEXITCODE) { throw "Inno Setup failed ($LASTEXITCODE)" }
    "Installer: " + (Join-Path $root "dist\ChemFox-$version-setup.exe")
}
