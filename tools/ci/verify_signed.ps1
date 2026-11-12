# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
# Fails unless every file given (paths or wildcards) has a valid Authenticode signature with a timestamp, and prints
# each signer. packages.yml's Windows job runs it on everything SignPath signed (docs/releasing.md "Windows code signing").
param([Parameter(Mandatory)][string[]]$Path)
$ErrorActionPreference = 'Stop'
$files = @(Get-ChildItem -Path $Path -File)
if ($files.Count -eq 0) { throw "verify_signed: nothing matches $Path" }
$bad = 0
foreach ($f in $files) {
    $s = Get-AuthenticodeSignature -FilePath $f.FullName
    if ($s.Status -ne 'Valid' -or -not $s.TimeStamperCertificate) {
        Write-Host "::error::$($f.Name): signature $($s.Status), timestamp $(if ($s.TimeStamperCertificate) { 'yes' } else { 'none' }) ($($s.StatusMessage))"
        $bad++
    } else {
        Write-Host "$($f.Name): Valid, $($s.SignerCertificate.Subject)"
    }
}
if ($bad -gt 0) { throw "$bad of $($files.Count) file(s) are not validly signed" }
