---
highlight: Fixed: big Clay strokes on Micro levels could break the surface into shards (since 0.11.10); Micro sculpting is back to 0.11.9's speed
fixed: **Micro levels**: big Clay, Clay Block, Flatten and Trim strokes on a Part with a Micro level could break the
  surface into shards across the whole brush (since 0.11.10), and sculpting there had become much slower on very dense
  meshes. Both are back to how 0.11.9 worked, and so is its one known issue: where several Clay strokes end on the
  same spot of a rough surface, a few short spikes can show. A proper fix for that comes in 0.11.16.
testers:
  - A model with a Micro level (Tool > Geometry > Divide to Micro): five big Clay strokes (Size 240) over the same
    area. The surface stays smooth, with no shards.
---
Revert of f084f9fd (0.11.10), fix/revert-micro-fold 784adebb (core-200m-08). Its grid fallback moved points along
their own normals by up to a quarter of the brush per dab, which shattered big strokes on rough micro surfaces (lion
112M: v0.11.9 smooth, v0.11.12 shattered, revert smooth; ~/Work/.sf/shots/micro-fold/). Floor speed is 0.11.9's own
(the same path). The no-fallback replacement is 0.11.16.
