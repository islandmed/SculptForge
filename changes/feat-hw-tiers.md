---
whats_new: **Hardware tiers and Turbo**: SculptForge sorts your computer into a tier (Essential, Standard, High, Extreme) and switches on what it can carry: more Focus Detail memory, 16K textures. A card on first launch says what it found; the tier sits in the top bar. The new bolt (**View > Turbo**, Ctrl+Alt+T) trades antialiasing and detail while you sculpt and orbit for speed, and puts everything back the moment you stop.
highlight: Hardware tiers and a Turbo button
added: **Hardware tiers** (Edit > Preferences > Performance): the detected tier and why ("RTX 3060, 12 GB VRAM, 32 GB
  RAM: High"), what it turns on, Auto or any tier by hand. The Essential tier works exactly as before; Standard and up
  keep more Focus Detail in memory, and High and up allow 16K paint and bake textures. A small card on first launch
  (and after a hardware change) shows the tier once; the chip in the top bar opens the panel. A memory guard steps
  the tier down a level when free memory runs low, and back up when there is room.
  **Turbo** (the bolt in the top bar, View > Turbo, Ctrl+Alt+T): antialiasing off and a lighter view while you
  sculpt and orbit, the detected tier's top budgets; full quality when the view stops, and Off changes nothing you
  set. Remembered per computer.
  **Graphics card** picker on computers with two (applies on the next start, through the system's own setting; a
  card that fails to start falls back to the system's choice). **Compute: Auto / CPU / GPU**, with each task's path
  shown (all CPU for now). SF_HW_SIMULATE=floor|standard|high|extreme, SF_TIER_<NAME>, SF_COMPUTE=cpu|gpu, SF_TURBO=1
  for benches.
testers:
  - Any OS, fresh settings folder: the tier card appears in the 3D view's corner with your GPU, memory and tier; Got
    it hides it; it stays hidden after a restart.
  - Windows laptop with two GPUs: Preferences > Performance > Graphics card lists both; pick the integrated one,
    restart, the panel says "Drawing now on" it; Auto, restart, back on the discrete one.
  - Click the bolt: it turns orange and the status bar says what changed; orbit a big sculpt; click again: AA and
    everything as before.
---
Reviewers: core/HardwareTier.h is the contract focusdetail, core200m, microlod and billion-ideas read
(currentTierConfig().budgets). Floor values are today's constants (tests/test_hardware_tier.cpp pins them).
