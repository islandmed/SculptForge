# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
# Fails unless every file given has the product name "SculptForge" and the product version of this build in its version
# resource. SignPath Foundation requires both on every signed file and enforces them in the artifact configuration
# (product-name / product-version, docs/releasing.md "Windows code signing"); checking here fails the build before a
# signing request is made, with a message that names the file. Prints the version for the signing steps (SF_VERSION).
param([Parameter(Mandatory)][string[]]$Path, [string]$Version = '')
$ErrorActionPreference = 'Stop'
if (-not $Version) {
    $line = Select-String -Path build\CMakeCache.txt -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$' | Select-Object -First 1
    if (-not $line) { throw 'check_pe_metadata: no CMAKE_PROJECT_VERSION in build\CMakeCache.txt' }
    $Version = $line.Matches[0].Groups[1].Value.Trim()
}
$files = @(Get-ChildItem -Path $Path -File)
if ($files.Count -eq 0) { throw "check_pe_metadata: nothing matches $Path" }
$bad = 0
foreach ($f in $files) {
    $v = $f.VersionInfo
    if ($v.ProductName -ne 'SculptForge' -or $v.ProductVersion -ne $Version) {
        Write-Host "::error::$($f.Name): product name '$($v.ProductName)', version '$($v.ProductVersion)' (want 'SculptForge' $Version)"
        $bad++
    } else {
        Write-Host "$($f.Name): SculptForge $Version"
    }
}
if ($bad -gt 0) { throw "$bad of $($files.Count) file(s) lack the product name / version SignPath enforces" }
if ($env:GITHUB_ENV) { "SF_VERSION=$Version" >> $env:GITHUB_ENV }
