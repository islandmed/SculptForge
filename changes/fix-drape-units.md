---
fixed: **Drape** on a figure modelled in centimetres or millimetres (the Full Body starter, most scans): the Units per metre slider now starts from the size of the scene (centimetres above 20 units tall, millimetres above 600) instead of 1, so the cloth no longer simulates a 166 m figure that never comes to rest. Move the slider to set it yourself; the `cloth_drape` command still defaults to 1.
---
The guess is sceneUnitsPerMetre (core/Units.h), shared with Garments > Drape.
