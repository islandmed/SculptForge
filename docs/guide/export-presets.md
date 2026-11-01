# Export presets

**Export Maps** writes a texture's channels ([Material painting](pbr-painting.md)) as the image files a game engine
or DCC expects: which files, what they are called and how channels are packed into them come from an **export
preset**. SculptForge has presets for the common targets, and you can write your own as a small JSON file.

## Export maps

1. Open **Tool > Texture Map**. Under **Material Channels**, choose a preset in the list next to **Export
   Maps...** (hover a preset for what it writes).
2. Click **Export Maps...** and pick a folder. The files are named after the Part.

From a script or the console: `export_maps preset=unreal folder=/path/to/maps name=Rock`.
`export_maps list=true` lists the presets and their maps.

Channels you haven't turned on export their default value (roughness 0.65, metallic 0, flat normal, AO 1), so a
preset always writes a complete set. Maps marked optional below are written only when one of their channels is on.

## Built-in presets

| Preset | Files (for a Part called Rock) | Normal |
| --- | --- | --- |
| `unreal` | `T_Rock_BC` (base colour; opacity in alpha when painted), `T_Rock_N`, `T_Rock_ORM` (R occlusion, G roughness, B metallic); optional `T_Rock_E` (emissive), `T_Rock_H` (16-bit height) | DirectX |
| `unity_urp` | `Rock_BaseMap`, `Rock_Normal`, `Rock_MetallicSmoothness` (R metallic, A smoothness = 1 - roughness); optional `Rock_Occlusion`, `Rock_Emission`, `Rock_Height` | OpenGL |
| `unity_hdrp` | `Rock_BaseMap`, `Rock_Normal`, `Rock_MaskMap` (R metallic, G occlusion, B detail mask = white, A smoothness); optional `Rock_Emission`, `Rock_Height` | OpenGL |
| `gltf` | `Rock_baseColor`, `Rock_normal`, `Rock_occlusionRoughnessMetallic` (R, G, B as glTF expects); optional `Rock_emissive` | OpenGL |
| `godot` | `Rock_albedo`, `Rock_normal`, `Rock_orm`; optional `Rock_emission`, `Rock_height` | OpenGL |
| `blender` | One file per channel: `Rock_BaseColor`, `Rock_Roughness`, `Rock_Metallic`, `Rock_Normal`; optional `Rock_Height` (16-bit), `Rock_AO`, `Rock_Emission`, `Rock_Alpha` | OpenGL |
| `usd` | One file per UsdPreviewSurface input: `Rock_diffuseColor`, `Rock_roughness`, `Rock_metallic`, `Rock_normal`; optional `Rock_occlusion`, `Rock_emissiveColor`, `Rock_opacity`, `Rock_displacement` | OpenGL |

All files are PNG at the texture's size: 8-bit, except height maps, which are 16-bit greyscale. The normal map is
the effective normal: the Normal channel with the height's normal combined in. DirectX presets flip green.

**Send to Unreal** ([Exporting](exporting.md#send-to-unreal)) uses the `unreal` preset and then builds a material
`M_<Name>` in Unreal from the maps: BC to Base Color, ORM's R / G / B to Ambient Occlusion, Roughness and Metallic,
N to Normal, E to Emissive. It is an ordinary Unreal material you can edit.

## Material slots in model files

You don't need Export Maps to get the material into a model file: when the texture has material channels,
exporting a model writes the maps beside it (or inside it) and wires them into its material.

| Format | What the material gets |
| --- | --- |
| glTF / GLB | Base colour (alpha from opacity, `alphaMode` BLEND), `metallicRoughnessTexture` and `occlusionTexture` from one packed map, `normalTexture`, `emissiveTexture` |
| USD / USDZ | A UsdPreviewSurface whose `roughness`, `metallic`, `normal`, `occlusion`, `emissiveColor` and `opacity` inputs each read their own map (raw colour space; the normal with scale 2, bias -1) |
| OBJ | The MTL's PBR keys: `map_Pr` (roughness), `map_Pm` (metallic), `norm` / `map_Bump` (normal), `map_Ke` (emission), `map_d` (opacity) |
| FBX | Textures on the material's NormalMap, ShininessExponent (roughness), ReflectionFactor (metallic), EmissiveColor and TransparencyFactor, as Blender reads them |

A texture without material channels exports exactly as before: just the colour.

## Your own presets

A preset is a JSON file. Point `export_maps preset=` at it:

```json
{
  "label": "Studio packed",
  "file": "{name}_{map}",
  "normal": "directx",
  "maps": [
    {"map": "albedo", "channels": ["base_color.r", "base_color.g", "base_color.b", "opacity?"]},
    {"map": "rma", "channels": ["roughness", "metallic", "ao"]},
    {"map": "nrm", "channels": ["normal.r", "normal.g", "normal.b"]},
    {"map": "disp", "channels": ["height"], "bits": 16, "optional": true}
  ]
}
```

- **file**: the file name pattern; `{name}` is the object name, `{map}` the map's name (required). `.png` is added.
- **normal**: `opengl` (green up, the default) or `directx` (green down).
- **maps**: 1 to 32 maps. Each has a **map** name (letters, digits, `-` and `_`) and 1 (grey), 3 (RGB) or 4
  (RGBA) **channels**.
- A channel source is a channel name (`roughness`, `metallic`, `ao`, `height`, `opacity`), a component
  (`base_color.r`, `normal.g`, `emissive.b`), `smoothness` (1 - roughness), a constant (`0`, `1`, `0.5`) or
  `1-` in front of any of them to invert it.
- A `?` after a source drops that component when its channel isn't on (an RGBA map becomes RGB).
- **optional**: write the map only when one of its channels is on. **bits**: 8 (default) or 16.

A preset with a mistake is refused with a message saying what is wrong.
