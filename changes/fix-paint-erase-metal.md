---
fixed: Texture painting: Alt/Option-erase on the Base now also resets metallic, roughness and the other material channels, so no metal stays under the white. A plain colour picked after a material swatch (Gold, Bare metal...) is plain paint again instead of metal, and the metal swatches' tooltip says metal looks dark in a dark environment. Erasing no longer leaves a faint ghost outline at the rim of a soft stroke.
---
Reviewers: tests/test_texture_channels.cpp (alt_erase_*), tests/ui/paint_erase_metal.sfui; before/after clips in the shots dir.
