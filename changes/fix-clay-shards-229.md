---
fixed: With **View falloff** on (the Brush palette), Clay, Clay Block, Flatten, Trim and the Brush Lab's plane brushes tore
  long shards into a model: one stroke on a new Sphere left two bands of spikes. The brush reaches through the model as
  a cylinder, and these brushes took their plane from all of it, the front and the far side together, then pulled the far
  side through to that plane. The plane now comes from the surface under the cursor, and they leave what lies a brush
  radius or more behind it, so a stroke over a thin part (an ear, a horn) still sculpts both of its faces and the far
  side of a head or a sphere stays put. A brush that ever computes an invalid value (not a number) now skips that dab
  and says so in the log, instead of moving the mesh (Fixes #229, reported by @Velisariy).
testers:
  - Any OS: new Sphere, Brush palette > View falloff on, X symmetry on, Clay Block and then Clay: strokes down the
    sides of the sphere build clean slabs; no spikes or shards, the back of the sphere unchanged (turn it around).
  - The same with Flatten and Trim.
  - A head with ears, View falloff on, Clay over an ear seen from the side: both faces of the ear change, the
    other ear does not.
  - View falloff off: Clay, Clay Block, Flatten and Trim feel exactly as before.
---
