# Garments: sewing patterns at real size

A garment in SculptForge is made the way clothes are made: flat pattern panels at their real size in millimetres,
sewn edge to edge. The same pattern prints on your home printer to cut real fabric, and drapes on your model.
Open **Tool > Garments** (the **Pattern** window shows the panels flat): **Templates** starts a garment at your
sizes, **Print** writes the pages. Everything here is also a `garment_*` command for **Scripting** and the AI
assistant ("make me trousers for a 1000 mm hip").

## Your size chart

Templates are drawn from a size chart saved in the project: type your measurements (or your model's) once and every
template uses them. In **Templates > Size chart (mm)**, type a value and press Enter (hover a name to see how to
measure it); **Default sizes** puts them all back. From a script, `garment_size` lists the chart and takes any
measurement in millimetres:

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

Pick one in **Templates** (or `garment_new template=<name>`): it replaces the scene's pattern with that garment at
your sizes. Tick **Add beside the pattern** (`add=true`) to put it next to what is there, so one pattern can hold a
whole outfit. Measurements given here go into the chart too, and
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

## Drape it on a body

A sewn pattern drapes on any body in the scene (the **Full Body** starter mesh is a good first one) and becomes a
Part to sculpt. You put each panel where it goes; SculptForge sews and settles it.

1. Start a garment (**Templates > T-shirt**). Under **Drape on the body** press **Place panels**.
2. **Click the body once for each panel**: the front of the chest for a front panel. The panel appears a little in front of the click, facing you, its top up
   the screen. The panel list ticks it off and asks for the next one. **Place each panel slightly in front of the
   body at its widest point**, from the view that faces it: the front from the front, the back from behind
   (turn the view round), each sleeve from its side.
3. Adjust the chosen panel if it needs it: **Turn** spins it in the view (a sleeve's top toward the shoulder),
   **Wrap** curls it round the body (a panel sewn to itself, like a sleeve or a trouser leg, starts at 300 degrees, a
   tube), **Gap** moves it out from the click. Click a panel in the list to place it again.
4. **Drape** sews the seams shut and lets the garment settle on the visible Parts, live. **Pause**, **Stop** (keep
   it as it is now) or **Reset** (back to the placed panels).
5. **Convert to Part** keeps it: the Part **Garment**, seams welded, UVs from the flat pattern (each panel where it
   is on the sheet), the pattern's thickness. One undo step; the body stays the active Part.

**Units per metre** says how big the body is in real life: 100 for a body modelled in centimetres (the starter
meshes; set for you when the body is over 20 units tall), 1 for metres. The pattern is at real size, so a wrong
value makes the garment a doll's or a giant's.

Cloth dropped from above settles *behind* overhangs: a panel placed above a chin or a beard ends up tucked behind
it. That is the physics, not a bug: place the panel in front of the body instead. From a script:

```
new_starter_mesh name=full-body
garment_new template=tshirt
garment_place panel=Front point=0,135,12 facing=0,0,1
garment_place panel=Back point=0,135,-12 facing=0,0,-1
garment_place panel="Left sleeve" point=20,140,0 facing=1,0,0
garment_place panel="Right sleeve" point=-20,140,0 facing=-1,0,0
garment_drape
```

## Print it at home

**Print** shows how many pages the pattern takes on **A4** or **Letter** (portrait or **Landscape**); **Save PDF...**
writes them all in one PDF, **Save SVG pages...** one SVG per page. From a script: `garment_export
path=trousers.pdf paper=letter landscape=true`.

- **Print at 100%** ("actual size", never "fit to page"), then measure the **50 mm square** on page A1.
- Each page has a name: rows A, B, C down, columns 1, 2, 3 across. Pages with nothing on them are left out.
- Trim each page on its line at the left and the top, lay it over its neighbours' lines and match the black
  triangles (each page prints half of each; the neighbour's name sits next to it). Tape, then cut.
- A panel that is another's mirror image (the other leg, the other sleeve) prints once, named for example
  "Left front: cut 2, 1 reversed (Right front)": fold the fabric right sides together and cut through both layers.
- Solid lines are the sewing lines, dashed lines the cutting lines (seam allowance: 10 mm, 5 mm on gloves).
  Ticks across the allowance are notches; the arrow is the grain line, kept parallel to the fabric's selvedge.

For a print shop or a plotter, **Export SVG** writes the whole pattern as one real-size sheet; **Export DXF** writes
the outlines for a cutting machine.
