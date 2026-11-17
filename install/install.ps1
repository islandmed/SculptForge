# Installs the latest SculptForge release on Windows: downloads the installer, checks it against the
# release's SHA256SUMS.txt and runs it.
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   irm https://raw.githubusercontent.com/islandmed/SculptForge/main/install/install.ps1 | iex
#   & ([scriptblock]::Create((irm .../install.ps1))) -Version v0.3.0     a specific release
#   & ([scriptblock]::Create((irm .../install.ps1))) -Portable          the .zip into %LOCALAPPDATA%\SculptForge
param([string]$Version = '', [switch]$Portable)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is far faster without the progress bar
$repo = 'islandmed/SculptForge'

if (-not $Version) { $Version = (Invoke-RestMethod "https://api.github.com/repos/$repo/releases/latest").tag_name }
if ($Version -notmatch '^v\d') { throw "SculptForge: could not find the latest release (got '$Version')" }
$v = $Version.Substring(1)
$url = "https://github.com/$repo/releases/download/$Version"
$tmp = Join-Path ([IO.Path]::GetTempPath()) "sculptforge-$v"
New-Item -ItemType Directory -Force $tmp | Out-Null

function Get-Asset([string]$name) {
  Write-Host "SculptForge: downloading $name"
  $file = Join-Path $tmp $name
  Invoke-WebRequest "$url/$name" -OutFile $file -UseBasicParsing
  $sums = Invoke-WebRequest "$url/SHA256SUMS.txt" -UseBasicParsing
  $line = ($sums.Content -split "`n") | Where-Object { $_ -match " $([regex]::Escape($name))\s*$" } | Select-Object -First 1
  if (-not $line) { throw "SculptForge: $name is not in SHA256SUMS.txt" }
  $want = ($line -split ' ')[0].ToUpper()
  if ((Get-FileHash $file -Algorithm SHA256).Hash -ne $want) { throw "SculptForge: $name checksum mismatch, not installing" }
  return $file
}

if ($Portable) {
  $zip = Get-Asset "SculptForge-$v-windows-x86_64.zip"
  $dest = Join-Path $env:LOCALAPPDATA 'SculptForge'
  if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
  Expand-Archive $zip -DestinationPath $tmp -Force
  Move-Item (Join-Path $tmp "SculptForge-$v-windows-x86_64") $dest
  # This is an install, not a stick: settings stay per-user, as with the installer (portable.txt would move them into the folder).
  Remove-Item -LiteralPath (Join-Path $dest 'bin\portable.txt') -Force -ErrorAction SilentlyContinue
  $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut(
    (Join-Path ([Environment]::GetFolderPath('Programs')) 'SculptForge.lnk'))
  $shortcut.TargetPath = Join-Path $dest 'bin\sculptforge.exe'
  $shortcut.Save()
  Write-Host "SculptForge: installed $Version in $dest (Start menu: SculptForge)"
} else {
  $setup = Get-Asset "SculptForge-$v-windows-x86_64.exe"
  Write-Host "SculptForge: starting the installer"
  Start-Process $setup -Wait
  Write-Host "SculptForge: done"
}
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
