# Bridge setup

SculptForge's **Bridge** (**File > Bridge**) exchanges Parts with ZBrush through ZBrush's own exchange folder, and
the export plug-in in this folder converts a ZBrush library once. This page is the one place that names the other
application, because setting it up needs its folder and menu names.

## The bridge folder

The Bridge uses the shared folder ZBrush's exchange plug-in uses: `C:\Users\Public\Pixologic` on Windows and
`/Users/Shared/Pixologic` on macOS. Nothing to install: **File > Bridge > Send** writes there and starts ZBrush or
brings it forward; send back from ZBrush with its own exchange button, and SculptForge receives it. ZBrush doesn't
run on Linux, so there set a **Custom bridge folder** (for example inside a Wine prefix), or move Bridge files by
hand with **Import / Export Bridge file**.

## Converting a library once

ZBrush's `.ZTL` files are proprietary and undocumented, so SculptForge can't read them. `SculptForgeBridgeExport.txt`
is a ZScript plug-in that runs inside ZBrush and writes every SubTool of a ZTool, at its highest subdivision level,
next to the `.ZTL` as a Bridge file (`Dragon.ZTL` -> `Dragon.sf001_Body.GoZ`, `Dragon.sf002_Horns.GoZ`, ...), with
face groups, vertex paint, the mask and UVs. Opening `Dragon.ZTL` in SculptForge then opens those files as Parts in
the same order and with the same names (a folder of numbered Bridge files, such as `Dragon.sculptforge/`, opens the
same way).

Install: copy `SculptForgeBridgeExport.txt` into ZBrush's `ZStartup/ZPlugs64` folder and restart ZBrush (it
compiles the script to a `.zsc`), or load it once with **ZScript > Load**. The buttons are in
**ZPlugin > SculptForge**:

- **Convert ZTL**: pick a `.ZTL`; it is loaded and converted.
- **Save for SculptForge**: saves the open ZTool as a `.ZTL` where you choose, and converts it.

Not carried over: lower subdivision levels, layers, materials and textures. Status: not yet tested in every ZBrush
version; please report problems with yours. Licence: GPL-3.0-or-later, like SculptForge. Releases before 0.10.11
shipped the same file as `SculptForgeZToolExport.txt`.

ZBrush is a trademark of Maxon. SculptForge is not affiliated with or endorsed by Maxon.
