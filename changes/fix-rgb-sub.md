---
whats_new: **Rgb** now lays colour on Sub strokes too, for every brush and stroke type.
fixed: With **Rgb** on, a Sub stroke (Alt, or Sub toggled) laid no colour: it painted back towards white instead, so only Add strokes coloured the Part. Sub now lays the colour like Add, with the same alpha and Colour intensity, for every brush and stroke type. The Paint brush itself still paints back to white on Alt.
testers:
  - Any OS: draw palette > Rgb on, pick a red, hold Alt and stroke over the Part (try Dots, Spray, and an alpha): the stroke colours it red where it digs in, as an Add stroke does.
  - Any OS: Paint brush (P) with Alt still paints back towards white.
---
