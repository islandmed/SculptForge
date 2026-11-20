---
whats_new: **Voxel Remesh no longer refuses when memory is tight**: if a remesh needs more than the free memory but the computer can hold it, it says so and lets you **Proceed anyway** (it may use swap and be slow). One that can't fit at all says which Resolution would.
changed: **High-resolution Voxel Remesh with little free memory** (#182, thanks @TheLycanFenrir): a remesh that needs more than the
  free memory used to refuse. Now (at any Resolution, one memory estimate and one dialog), when it fits in RAM plus the most swap the system can grow to (90% of that), a
  dialog says "This needs about X GB; Y GB is free. It may use swap and be slow." with Proceed anyway or Cancel (Cancel
  is the default; nothing is remembered, it asks every time). Proceed saves the recovery copy first, then remeshes with
  the usual progress and Cancel. The dialog shows the limit it used. The swap figure is the
  maximum, not today's size: Linux the swap size; Windows the page file maximum you set (system managed: the volume's free space, at most 3x RAM,
  so a page file that grows 52 -> 84 GB counts as 84); macOS the startup volume's free space less 10 GB. One that is past RAM plus swap is still refused, now with the numbers and the highest
  Resolution that fits. Running out of memory midway is caught: the Part stays as it was and a message says so.
testers:
  - Linux/macOS: with many apps open (free memory low) set Tool > Remesh > Resolution to 2000+ on a dense Part; the dialog says it may use swap; Cancel leaves the Part; Proceed anyway remeshes and Undo returns the Part.
  - Windows: same, with a page file whose maximum is bigger than its current size; the dialog's "Limit used" line shows RAM + the page file maximum (System Properties > Advanced > Performance > Virtual memory).
  - A Resolution too high for the computer (4096 on a 8 GB laptop with a big Part): no dialog, the status line gives the numbers and a Resolution that fits.
---
Estimate tiers (fits / may swap / too big) are core/MemoryGuard; the dialog is the High resolution remesh one in
PanelRetopo.cpp with a swap variant. Windows/mac queries can't run on this machine; the maths for each OS is unit-tested with injected values (parsePagingFiles, windowsSwapCeiling, macSwapCeiling).
