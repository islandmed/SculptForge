# SculptForge ZTL Export (ZBrush plug-in)

ZBrush's `.ZTL` files are proprietary and undocumented, so SculptForge can't read them. This ZScript plug-in runs
inside ZBrush and converts a ZTool once: every SubTool, at its highest subdivision level, is written next to the
`.ZTL` as a GoZ file (`Dragon.ZTL` -> `Dragon.sf001_Body.GoZ`, `Dragon.sf002_Horns.GoZ`, ...). The .GoZ
files keep polygroups, polypaint, the mask and UVs. Opening `Dragon.ZTL` in SculptForge then opens those files as Parts in
the same order and with the same names (`convertedZToolFiles` / `loadConvertedZTool` in `src/io/GozIO.cpp`; a
folder of numbered `.GoZ` files, such as `Dragon.sculptforge/`, opens the same way).

Install: copy `SculptForgeZToolExport.txt` into ZBrush's `ZStartup/ZPlugs64` folder and restart ZBrush (it compiles
the script to a `.zsc`), or load it once with **ZScript > Load**. The buttons are in **ZPlugin > SculptForge**:

- **Convert ZTL**: pick a `.ZTL`; it is loaded and converted.
- **Save for SculptForge**: saves the open ZTool as a `.ZTL` where you choose, and converts it.

Not carried over: lower subdivision levels, ZBrush layers, materials and textures. Status: new and not yet tested
in every ZBrush version (ZBrush doesn't run on the Linux machines the project is developed on); please report
problems with your ZBrush version. Licence: GPL-3.0-or-later, like SculptForge.

The file keeps its old name, `SculptForgeZToolExport.txt`, so existing links work. ZBrush, ZScript, ZTool, GoZ, MatCap and other ZBrush feature names are
trademarks of Maxon Computer GmbH. SculptForge is not affiliated with or endorsed by Maxon.
