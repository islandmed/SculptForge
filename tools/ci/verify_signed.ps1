# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
# Fails unless every file given (paths or wildcards) has a valid Authenticode signature with a timestamp, and prints
# each signer. packages.yml's Windows job runs it on everything SignPath signed (docs/releasing.md "Windows code signing").
# -Test (SignPath's test policy, a self-signed "SculptForge Test" certificate): the signature must be present, intact and
# the signer "SculptForge Test"; an untrusted root is expected there and a timestamp is not required. signtool verify /pa
# is printed for the record (its failure on the untrusted root is expected and does not fail the job).
param([Parameter(Mandatory)][string[]]$Path, [switch]$Test)
$ErrorActionPreference = 'Stop'
$files = @(Get-ChildItem -Path $Path -File)
if ($files.Count -eq 0) { throw "verify_signed: nothing matches $Path" }
$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -Last 1
$bad = 0
foreach ($f in $files) {
    $s = Get-AuthenticodeSignature -FilePath $f.FullName
    $subject = if ($s.SignerCertificate) { $s.SignerCertificate.Subject } else { 'no signer' }
    if ($Test) {
        $ok = $s.SignerCertificate -and $s.Status -in 'Valid', 'NotTrusted', 'UnknownError' -and $subject -like '*SculptForge Test*'
    } else {
        $ok = $s.Status -eq 'Valid' -and $s.TimeStamperCertificate
    }
    if (-not $ok) {
        Write-Host "::error::$($f.Name): signature $($s.Status), signer $subject, timestamp $(if ($s.TimeStamperCertificate) { 'yes' } else { 'none' }) ($($s.StatusMessage))"
        $bad++
    } else {
        Write-Host "$($f.Name): $($s.Status), $subject"
    }
    if ($Test -and $signtool) {
        & $signtool.FullName verify /pa /v $f.FullName 2>&1 | Select-Object -First 12 | ForEach-Object { Write-Host "  signtool: $_" }
        $global:LASTEXITCODE = 0
    }
}
if ($bad -gt 0) { throw "$bad of $($files.Count) file(s) are not validly signed" }
