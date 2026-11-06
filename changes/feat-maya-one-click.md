---
whats_new: **Maya bridge, fewer clicks**: what Maya sends loads by itself, **Send to Maya** is one click in the top bar, and the Maya shelf's new **Auto** loads what SculptForge sends. Thanks @tristratos.
changed: Maya bridge (#42, thanks @tristratos): **Get automatically** is on once an exchange folder is chosen; a
  **Send to Maya** button sits in the top bar while the bridge is on; the Maya shelf gets **Auto** (on by default),
  which loads what SculptForge sends by itself. Both sides check only the other side's manifest (time and size),
  once a second; still no ports or connections. Reinstall the shelf (`install_shelf()`) to get the Auto button.
testers:
  - Maya 2022+: copy the new sculptforge_maya.py, run install_shelf() again, same folder in both. Send from Maya: SculptForge loads it within a second, no click.
  - Click Send to Maya in SculptForge's top bar: Maya loads it within a second (Auto), as one undo step.
  - Click Auto on the shelf (it says Manual): a send from SculptForge waits for Get.
---
Follow-ups in REPORT.md.
