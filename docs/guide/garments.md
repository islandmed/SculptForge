# Garments: sewing patterns at real size

A garment in SculptForge is made the way clothes are made: flat pattern panels at their real size in millimetres,
sewn edge to edge. The same pattern prints on your home printer to cut real fabric, and (with the cloth solver)
drapes on your model. For now you make and print patterns with the `garment_*` commands, from **Scripting** or by
asking the AI assistant ("make me trousers for a 1000 mm hip").

## Your size chart

Templates are drawn from a size chart saved in the project: type your measurements (or your model's) once and every
template uses them. `garment_size` lists the chart; give any measurement in millimetres to change it:

```
garment_size chest_mm=1020 waist_mm=860 hip_mm=1040 inseam_mm=800
```

| Measurement | How to take it | Default |
| --- | --- | --- |
| `chest` | round the fullest part of the chest | 960 |
| `waist` | round the natural waist | 820 |
| `hip` | round the fullest part of the hips | 1000 |
| `neck` | round the base of the neck | 380 |
| `shoulder` | shoulder point to shoulder point, across the back | 440 |
| `arm` | shoulder point to wrist, arm hanging | 600 |
| `upper_arm` | round the fullest part of the upper arm | 320 |
| `wrist` | round the wrist | 170 |
| `back_length` | the bone at the base of the neck down to the waist | 430 |
| `inseam` | crotch to the floor (or to where the trousers should end) | 790 |
| `rise` | sitting on a hard chair: waist down to the seat, at the side | 260 |
| `waist_to_knee` | waist to knee, down the side | 580 |
| `height` | height | 1750 |
| `head` | round the forehead and the back of the head | 570 |
| `hand` | round the knuckles, thumb out | 210 |
| `hand_length` | wrist crease to the tip of the middle finger | 190 |

`garment_size reset=true` puts every value back to the defaults (an adult medium).

## Templates

`garment_new template=<name>` replaces the scene's pattern with a template at your sizes; `add=true` puts it beside
what is there, so one pattern can hold a whole outfit. Measurements given here go into the chart too, and
`length_mm` sets the template's main length for this garment only.

| Template | Panels | `length_mm` is |
| --- | --- | --- |
| `tshirt` | front, back, two short sleeves | shoulder to hem (700) |
| `trousers` | front and back of each leg | the inseam (shorts: 200 or so) |
| `skirt` | straight front and back, waistband | waist to hem (waist to knee) |
| `skirt_aline` | flared front and back, waistband | waist to hem |
| `hoodie` | front, back, long sleeves, two-piece hood, kangaroo pocket | shoulder to hem |
| `dress` | sleeveless bodice, A-line skirt, joined at the waist | waist to hem |
| `cloak` | half-circle cape in two quarters, hood | neck to hem (0.6 x height) |
| `glove` | a pair: back and palm of each hand | (from the hand) |

Every template comes sewn the way that garment is put together, with notches where pieces meet and the seam lengths
matched (`garment_info` lists every seam with both lengths). The hoodie's pocket is the one loose piece: topstitch
it onto the front where you want it. The proportions (how much ease, how deep the armhole, how the crotch curve is
shaped) are SculptForge's own, chosen to fit an average body without darts; they are a starting point to
adjust, not a tailored block.

```
garment_size hip_mm=1050 inseam_mm=780
garment_new template=hoodie
garment_new template=trousers add=true
garment_info
```

## Print it at home

`garment_export path=trousers.pdf` writes the pattern as A4 pages (`paper=letter` for Letter, `landscape=true` to
turn them):

- **Print at 100%** ("actual size", never "fit to page"), then measure the **50 mm square** on page A1.
- Each page has a name: rows A, B, C down, columns 1, 2, 3 across. Pages with nothing on them are left out.
- Trim each page on its line at the left and the top, lay it over its neighbours' lines and match the black
  triangles (each page prints half of each; the neighbour's name sits next to it). Tape, then cut.
- A panel that is another's mirror image (the other leg, the other sleeve) prints once, named for example
  "Left front: cut 2, 1 reversed (Right front)": fold the fabric right sides together and cut through both layers.
- Solid lines are the sewing lines, dashed lines the cutting lines (seam allowance: 10 mm, 5 mm on gloves).
  Ticks across the allowance are notches; the arrow is the grain line, kept parallel to the fabric's selvedge.

`format=svg_pages` writes the same pages as SVG files (`trousers-A1.svg`, ...). For a print shop or a plotter,
`path=trousers.svg` writes the whole pattern as one real-size sheet; `path=trousers.dxf` writes the outlines for a
cutting machine.
