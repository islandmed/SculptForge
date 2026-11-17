---
whats_new: **Brush thumbnails show the alpha**: a brush with an alpha shows it pressed into the ball in the brush's own stroke type (Dots, Drag Stamp, Drag Dot, Spray), in the shelf slot, the picker and the Library; right-click > **Regenerate thumbnail** makes it again.
added: **Alpha on the brush thumbnail** (#208): a brush's thumbnail in the shelf slots, the brush picker, the Library and the Ctrl-held tile shows the brush's alpha pressed into the clay ball in its stroke type (Dots and Lazy Mouse an S across the ball, Drag Stamp one stamp, Drag Dot one dab, Spray a scatter), with its Angle and Tile. Brushes that differ only by alpha tell apart at a glance. It follows the brush's settings and the alpha file (an edited image makes a new thumbnail), is made once and cached, and right-click > **Regenerate thumbnail** (shelf slot, picker, Library) makes it again. Thanks @nagdav853.
testers:
  - Any OS: pick a brush, load an alpha (Library > Alpha) and set Stroke > Drag Stamp; its slot and picker tile show the alpha pressed in. Pick another alpha: the thumbnail changes.
  - Any OS: edit the alpha image on disk, right-click the brush's tile > Regenerate thumbnail; it shows the new image.
---
Brush preview code: app/BrushPreview.cpp (AlphaLook), cache keys in app/Thumbnails.cpp, settings lookup App::brushThumbnail.
