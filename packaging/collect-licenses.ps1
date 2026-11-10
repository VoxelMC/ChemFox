# Copy the licence texts of every conda package that contributes a file to the deployed app
# (Qt, RDKit, Boost, freetype, ...) into <Dest>\<package>\, plus an index.
#   packaging\collect-licenses.ps1 -Prefix <conda env> -Dest dist\ChemFox\licenses [-Shipped dist\ChemFox]
param([Parameter(Mandatory)][string]$Prefix, [Parameter(Mandatory)][string]$Dest, [string]$Shipped = "")
$ErrorActionPreference = "Stop"
if (-not $Shipped) { $Shipped = Split-Path $Dest -Parent }
$shippedNames = @{}
# Only libraries count: generic names like LICENSE or README.md appear in many packages.
Get-ChildItem $Shipped -Recurse -File | Where-Object { $_.Name -match '\.(dll|dylib|so)(\.[0-9.]+)?$' } |
    ForEach-Object { $shippedNames[$_.Name.ToLower()] = $true }

New-Item -ItemType Directory -Force $Dest | Out-Null
$index = @("Third-party software included in ChemFox", "", "Each folder holds that package's licence as distributed by conda-forge.", "")
foreach ($meta in Get-ChildItem (Join-Path $Prefix "conda-meta") -Filter *.json) {
    $j = Get-Content $meta.FullName -Raw | ConvertFrom-Json
    # Does any file from this package ship with the app?
    $ships = $false
    foreach ($f in $j.files) { if ($shippedNames.ContainsKey(([IO.Path]::GetFileName($f)).ToLower())) { $ships = $true; break } }
    if (-not $ships) { continue }
    $lic = Join-Path $j.extracted_package_dir "info\licenses"
    if (-not (Test-Path $lic)) { $lic = Join-Path $j.extracted_package_dir "info/licenses" }
    $out = Join-Path $Dest $j.name
    if (Test-Path $lic) {
        New-Item -ItemType Directory -Force $out | Out-Null
        Copy-Item "$lic\*" $out -Recurse -Force
    }
    $index += "{0} {1} - {2}" -f $j.name, $j.version, $j.license
}
# Code compiled into RDKit whose licences conda-forge's package doesn't carry (packaging\licenses-extra).
$extra = Join-Path $PSScriptRoot "licenses-extra"
if (Test-Path $extra) {
    $out = Join-Path $Dest "rdkit-bundled"
    New-Item -ItemType Directory -Force $out | Out-Null
    Copy-Item (Join-Path $extra "*.txt") $out -Force
    $index += "", "Compiled into RDKit (licences in rdkit-bundled\):",
        "  InChI 1.06 (IUPAC/InChI Trust) - MIT",
        "  coordgenlibs (Schrodinger) - BSD-3-Clause",
        "  maeparser (Schrodinger) - MIT",
        "  RingDecomposerLib (Rarey lab) - BSD-3-Clause",
        "  Eigen (header-only, unmodified) - MPL-2.0; source at https://gitlab.com/libeigen/eigen"
}
$index += "",
    "Qt is used under the GNU LGPL v3 and linked dynamically: you may replace the Qt libraries with your own builds. The exact source is conda-forge's qt6-main package (https://github.com/conda-forge/qt-main-feedstock, version listed above) and https://download.qt.io/.",
    "FreeType is used under the FreeType License (FTL), not the GPL: portions of this software are copyright (c) The FreeType Project (www.freetype.org). All rights reserved.",
    "This software is based in part on the work of the Independent JPEG Group (libjpeg-turbo)."
Set-Content (Join-Path $Dest "THIRD-PARTY.txt") -Value $index -Encoding utf8
"Collected licences for $($index.Count - 6) packages into $Dest"
