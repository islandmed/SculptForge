# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
# Checks the signing checks without SignPath and without any secret: signs a copy of a small Windows exe with a throwaway
# self-signed "SculptForge Test" certificate (the same subject SignPath's test policy uses) and runs
# tools/ci/verify_signed.ps1 and check_pe_metadata.ps1 on it, expecting each to accept what it must and refuse the rest.
# Run by .github/workflows/signing-selftest.yml (a maintainer's dispatch).
$ErrorActionPreference = 'Stop'
$work = Join-Path $env:RUNNER_TEMP 'signing-selftest'
New-Item -ItemType Directory $work -Force | Out-Null
$src = Join-Path $env:SystemRoot 'System32\whoami.exe'
$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=SculptForge Test' -CertStoreLocation Cert:\CurrentUser\My
$failed = 0
function Expect([string]$what, [bool]$shouldPass, [scriptblock]$run) {
    $passed = $true
    try { & $run | Out-Host } catch { $passed = $false; Write-Host "  (refused: $($_.Exception.Message))" }
    if ($passed -eq $shouldPass) { Write-Host "ok   $what" }
    else { Write-Host "::error::FAIL $what (wanted $(if ($shouldPass) { 'accepted' } else { 'refused' }))"; $script:failed++ }
}

$good = Join-Path $work 'good.exe'
Copy-Item $src $good
# System32 files are signed through Windows catalogs (any copy still reads as Microsoft-signed): extra bytes at the end
# break the catalog's hash, so the copy is a plain unsigned file that takes our signature.
$fs = [IO.File]::Open($good, 'Append'); $fs.Write([byte[]](1..16), 0, 16); $fs.Close()
$sig = Set-AuthenticodeSignature -FilePath $good -Certificate $cert -HashAlgorithm SHA256
Write-Host "signed copy: $($sig.Status)"

$tampered = Join-Path $work 'tampered.exe'
Copy-Item $good $tampered
$bytes = [IO.File]::ReadAllBytes($tampered)
$bytes[1024] = $bytes[1024] -bxor 0xFF # inside the signed body
[IO.File]::WriteAllBytes($tampered, $bytes)

$foreign = Join-Path $work 'foreign.exe' # still carries Microsoft's own signature: a valid one, but not ours
Copy-Item $src $foreign

Expect 'test mode accepts our test-signed exe' $true { ./tools/ci/verify_signed.ps1 -Test -Path $good }
Expect 'test mode refuses a tampered exe' $false { ./tools/ci/verify_signed.ps1 -Test -Path $tampered }
Expect 'test mode refuses another signer' $false { ./tools/ci/verify_signed.ps1 -Test -Path $foreign }
Expect 'release mode refuses a self-signed exe (untrusted root, no timestamp)' $false { ./tools/ci/verify_signed.ps1 -Path $good }
Expect 'a wildcard that matches nothing is an error' $false { ./tools/ci/verify_signed.ps1 -Test -Path (Join-Path $work 'none*.exe') }
Expect 'the metadata check refuses an exe that is not SculptForge' $false { ./tools/ci/check_pe_metadata.ps1 -Path $good -Version '1.2.3' }

if ($failed -gt 0) { throw "$failed signing self-test check(s) failed" }
Write-Host 'signing self-test: all checks behaved'
