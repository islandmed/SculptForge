# Garment templates: our proportions

src/core/GarmentTemplates.cpp makes every template from the size chart (garment_size, saved in the project as
projectSettings["garment_sizes"]). These numbers are SculptForge's own, chosen for a dartless fit on an average body
and checked by sewing and welding every template at the smallest, largest, default and random charts
(tests/test_garment_templates.cpp). No commercial or published pattern block was copied or traced. All in mm; a
"quarter" is one quarter of a girth plus its ease. Keep this note and the code in step.

## Shared rules

- Front and back pieces are drawn as seen from outside; the back is the front's mirror where the shapes differ
  (mirroredPanel: edge i of the copy is edge mirrorEdge(n, i) of the original, run backwards, so they sew unflipped).
- Seams join opposite directions (the sewing rule in core/Garment.h). Every template's seams match in length by
  construction (the two sides share a run, or one is measured from the other), so nothing is eased by accident.
- Templates are sewn the way the garment is made because the user picked the template; nothing is sewn from where
  panels lie, nothing is placed from the body (docs/design/garments.md, "Do not build").
- Layout: panels in rows 60 mm apart, a new row past 2.4 m (the T-shirt keeps its own layout).

## T-shirt

Unchanged from part 1 (core/Garment.cpp tshirtPattern): quarter = (chest + 80) / 4; length from the shoulder.

## Trousers

- Ease: hip + 40; rise + 15. Hip line 0.4 of the rise above the crotch line.
- Front quarter at the hip = hip/4 - 10, back = hip/4 + 10. Crotch extension: front 0.06 x hip, back 0.11 x hip.
- Hem: front 0.2 x hip (at least 120); the back hem is wider by as much as the back is wider at the hip plus crotch,
  so the inseams of front and back have the same run (and length). The outseam is the same line on both.
- Waist: (waist + 20) / 2 per leg, split so the front and back side points coincide; centre front 10 in from the hip
  line, centre back 25 in and 20 higher (seat room).
- Crotch curve: a cubic from the centre waist, straight down to 0.3 of the rise above the crotch line, out to the
  extension point.
- length_mm replaces the inseam (shorts).

## Skirts

- Waist quarter (waist + 20) / 4, hip quarter (hip + 40) / 4 at 190 below the waist (0.45 of the length if shorter);
  the hip curve is one cubic.
- Straight: hem = hip. A-line: hem quarter = hip quarter + 0.25 x (length - hip depth).
- Waistband: front + back waist long, 80 wide (folded: 40), its ends joined at the side.

## Hoodie

- Body quarter (chest + 120) / 4, straight to the hem; length = back length + 230.
- Neck: half width 0.24 x neck; front depth 1.05 x that, back 0.25 x. Shoulder half = shoulder / 2 + 10 (dropped).
- Armhole depth 0.3 x chest + 10. Sleeve cap = armhole + 1.5% ease; sleeve = arm + 30; upper sleeve half
  (upper arm + 90) / 2; cuff half (wrist + 70) / 2.
- Hood half: depth 0.42 x head + 30, height 0.62 x head + 20; its neck edge is half the neckline (front part, then
  back part, split by their lengths).
- Kangaroo pocket: 1.2 x the body quarter wide (capped), 0.3 of the length high, hand openings scooped from 35% up.

## Dress

- Bodice quarter (chest + 60) / 4 tapering to the waist quarter (waist + 50) / 4; length back length + 15.
- Neck half 0.22 x neck; front depth 1.3 x, back 0.3 x; shoulder half = shoulder / 2 - 5; armhole depth 0.24 x chest.
- Skirt: the A-line skirt with the bodice's waist (so the waist seam matches exactly); default length waist to
  knee + 50.

## Cloak

- A half circle: neck radius (neck + 40) / pi, so each quarter's neck arc is half the neck plus ease; outer radius
  = neck radius + length (0.6 x height by default). Quarter arcs are single cubics (control 0.5523 x radius).
- Hood as the hoodie's, its neck edge the cape's neck arc.

## Gloves

- Panel width hand / 2 + 12, cuff 50 below the wrist; wrist half (wrist / 4 + 12), capped at half the width.
- Knuckle line 0.55 x hand length above the cuff; fingers a quarter of the width each, 18% narrower at the tip,
  lengths (of the hand length) index 0.40, middle 0.45, ring 0.42, little 0.33; round tips.
- Thumb from 0.2 to 0.45 of the hand length up the side, pointing out at about 29 degrees from vertical, 0.36 of the
  hand long, its tip square to it.
- Back and palm are mirrors, sewn all round except the wrist; seam allowance 5; particle distance hand-width / 20
  (3 to 6), so a finger is about five particles across and its two layers stay apart.
