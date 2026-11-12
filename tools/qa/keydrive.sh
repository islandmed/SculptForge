#!/bin/bash
# Real-input key check on the Windows PC (tools/qa/KeyDrive.cs): keydrive.sh VERSION [case...]
# VERSION is a C:\sf\VERSION folder (win.sh fetch / put). Builds the driver on the PC as C:\sf\keydrv\sculptforge.exe
# (so win.sh run's queue and desktop session carry it), runs it, and copies the shots + keydrive.txt to ./keydrive-out/.
set -e; v=$1; shift
here=$(cd "$(dirname "$0")" && pwd); out=${KEYDRIVE_OUT:-keydrive-out}; mkdir -p "$out"
ssh -o BatchMode=yes sfwin 'powershell -NoProfile -Command "New-Item -ItemType Directory -Force C:\sf\keydrv | Out-Null"'
scp -q "$here/KeyDrive.cs" sfwin:C:/sf/keydrv/KeyDrive.cs
~/Work/.sf/win.sh ps 'cd C:\sf\keydrv; & C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /r:System.Drawing.dll /out:C:\sf\keydrv\sculptforge.exe KeyDrive.cs' || true   # (ps ends in grep: no output = status 1)
real=$(~/Work/.sf/win.sh ps "(Get-ChildItem C:\\sf\\$v -Recurse -Filter sculptforge.exe | Select -First 1).FullName" | tr -d '\r')
SF_PC_PRIO=${SF_PC_PRIO:-1} ~/Work/.sf/win.sh run keydrv "$real" "C:\\sf\\keydrv\\out-$v" "$@"
scp -q -r "sfwin:C:/sf/keydrv/out-$v/*.png" "sfwin:C:/sf/keydrv/out-$v/*.txt" "$out/"
cat "$out/keydrive.txt"
