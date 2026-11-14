---
whats_new: **Pick colour (C)** now picks from any Part under the pointer, not only the selected one, and says why when it can't.
highlight: C picks colour from any Part
fixed: **Pick colour (C)** picked only from the selected Part, so over any other Part it did nothing or took the wrong colour. It now picks the colour of the nearest visible Part under the pointer (its texture or Vertex Paint; a Part without paint gives the colour it is shown in), and the status bar says why when there is nothing to pick (off the model, over a hidden Part). Thanks to phoenix for the report.
testers:
  - macOS: paint one Part, select another, point at the painted one and press C; the colour (and its Part's name) shows in the status bar and the Colour swatch.
  - Press C with the pointer off the model: the status bar says "Nothing to pick".
---
