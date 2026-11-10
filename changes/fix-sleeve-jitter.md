---
fixed: **Drape**: sewn seams stay closed where the cloth rests on the body (a T-shirt's waist and armpit seams stay
  within about 3 mm instead of opening to 8 mm), and a drape with one particle flipping between two spots a millimetre
  apart now comes to rest.
testers:
  - Garments: sew a T-shirt on the Full Body starter, Drape it, and nudge the sleeves a few millimetres first: it reaches
    rest and the waist and armpit seams look closed.
---
Solver (src/core/Cloth.*): after the contact push each seam is closed again and both ends are pushed out together along the
contact normal (the pair moves, not one end); the rest window's peak speed is taken over two frames. Fabric thickness for
garments (5 mm closes the hip poke) is set by the garment drape, not here.
