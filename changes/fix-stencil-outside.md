---
whats_new: **Stencil**: a stroke past the image's edge now sculpts and paints as if there were no Stencil; **Outside the image: Block** keeps the old behaviour.
fixed: **Stencil** (Sculpt > Alpha): strokes outside the Stencil's image did nothing, which read as a broken brush. Now they work as if there were no Stencil, and the image weighs only where it covers the model. New option **Outside the image: Sculpt | Block** (Sculpt is the default, in Classic too); Block keeps the old behaviour. Tiled images are unchanged. Reported by a member.
testers:
  - Any OS: Sculpt > Alpha > Stencil, load an image, make it small with Size, stroke across its edge and well past it. Both sides sculpt; set Outside the image to Block and only the part under the image does.
---
Reporter's consent to be named not confirmed: "a member". Status-line hint now says when a stroke starts off the image with Block.
