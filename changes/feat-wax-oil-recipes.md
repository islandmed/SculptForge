---
whats_new: **Wax and Oil Stain Material Recipes**: in **Tool > Paint Layers > Material Recipe**, Wax lays semi-glossy candle wax with a soft build-up and glossier edges; Oil Stain lays darker, more saturated patches with a soft edge, glossy in the middle and rougher toward the rim.
highlight: Wax and Oil Stain Material Recipes
added: **Wax and Oil Stain Material Recipes** (Tool > Paint Layers > Material Recipe): Wax is a warm, semi-glossy body whose roughness varies, with a soft uneven build-up (height) and glossier edges; SculptForge renders no translucency, so thin parts and edges get a lighter, warmer tint as a stand-in. Oil Stain lays patches of darker, more saturated amber with a soft edge, glossy in the middle and rougher toward the rim. Both are Fill layers you can edit, and they work with `material_recipe name=wax` / `name=oil_stain`.
member_request: true
testers:
  - Sculpt a bust, Tool > Texture Image > Auto UV, New Texture; Tool > Paint Layers > Material Recipe > Wax; Render > Materials > Viewport Shading > PBR. Good: warm semi-glossy wax, brighter rims and thin parts, no dark or noisy artefacts.
  - Same with Oil Stain on top of another recipe or a plain colour: patches of dark amber, soft at the rim, sharp highlight in the middle; Undo removes it.
---
Tests: tests/test_fill_layers.cpp (material_recipes_are_data_and_wear_the_model, wax_and_oil_recipes_use_real_channels_and_say_so).
