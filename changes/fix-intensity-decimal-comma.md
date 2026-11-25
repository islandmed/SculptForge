---
fixed: **Intensity** typed in the **U** and **I** pop-ups with a decimal comma (0,5, as Spanish and many European users type it) was read as 0; the comma is now a decimal point, like the point. A second separator is ignored.
testers:
  - Any OS: press U over the model, type 0,5, Enter; the Intensity readout shows 50. U, type ,25, Enter shows 25.
  - Any OS: U, type 0,5 then a point then 7: the second separator is ignored and the readout previews 57.
---
Typed numbers in the picker pop-ups (Intensity, Size) share one parser that accepts '.' or ',' and rejects ambiguous text.
