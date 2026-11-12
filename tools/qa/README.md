# tools/qa

- `KeyDrive.cs` + `keydrive.sh`: real-input key check on the Windows PC. It injects keys with user32 `SendInput` into the
  real SculptForge window (the Win32 -> SDL path the UI tests' `key_down` skips) and writes a shot per case.
  `win.sh fetch VERSION` first, then `tools/qa/keydrive.sh VERSION [case...]` (cases: tap, together, vfirst, rolling,
  slowroll, slowv, held, latcombo, latcombo35, latplain; shots and keydrive.txt land in `./keydrive-out/`, or `$KEYDRIVE_OUT`).
  It builds the driver on the PC with csc.exe as `C:\sf\keydrv\sculptforge.exe` so `win.sh run` carries it through its queue
  and the desktop session; the app under test is the release in `C:\sf\VERSION`.
- `artist_pass.py`: the artist QA pass.
