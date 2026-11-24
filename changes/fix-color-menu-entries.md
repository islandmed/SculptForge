---
whats_new: **Paint > Material** lists every material channel as soon as the Part has a texture; tick one there to add it, no trip to **Tool > Texture Image**.
changed: **Paint > Material** (texture painting): the section now appears as soon as paint goes into a texture and lists every
  material channel, the ones the texture does not have yet in grey. Ticking a greyed channel adds it to the texture (the same
  undoable step as Material Channels in Tool > Texture Image) and makes the brush paint it; a material swatch adds Roughness
  and Metallic in one click. Texture Image keeps the same ticks and stays the place to turn a channel off. Requested by a member.
member_request: true
member_credit: Fenix
testers:
  - Any OS: Paint > Paint brush, New Texture, then look at Paint > Material: Roughness, Metallic, Normal, Height, Ambient Occlusion, Emissive and Opacity are listed in grey with "Tick a channel to paint more than colour" under them.
  - Tick Roughness: its grey value squares and slider appear, Tool > Texture Image > Material Channels shows Roughness ticked, Ctrl+Z unticks both.
  - Click the Gold swatch with no channels on: Roughness and Metallic come on together and a stroke paints a gold metal.
---
Reviewer notes: UI-only (PanelPbr.cpp drawBrushMaterialContents); the channel change runs the existing texture_channels command after the
palette is drawn (captureTexture replaces the texture pointer), the same pattern Texture Image uses. Defaults unchanged: no
channel comes on by itself. UI test tests/ui/color_material_entries.sfui (+ 1440/2880 shot variants). Member consent for the
credit: feedback/feature-queue.md row (consent yes).
