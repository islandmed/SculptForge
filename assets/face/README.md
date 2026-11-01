# Face template

`ict-arkit.sfface` is the template head Generate Facial Shapes (Tool > Facial Shapes, `face_shapes_generate`,
src/core/Face.h) fits to a sculpt. It is embedded into the program at build time.

| File | Source | Licence |
|---|---|---|
| `ict-arkit.sfface` | The "ICT Face Model Light" of [ICT-FaceKit](https://github.com/ICT-VGL/ICT-FaceKit) by the USC Institute for Creative Technologies Vision and Graphics Lab: `FaceXModel/generic_neutral_mesh.obj` (vertices 0-14061: face, head and neck, mouth socket, eye sockets) and the expression OBJs, converted by `tools/face/build_ict_template.py` | [MIT](https://github.com/ICT-VGL/ICT-FaceKit/blob/master/LICENSE), Copyright (c) 2020 USC Institute for Creative Technologies (the full licence text is in THIRD_PARTY_NOTICES.md, ICT-FaceKit section) |

What changed from ICT's data: the 52 shapes carry Apple ARKit's names (ICT's `_L` / `_R` become `Left` / `Right`,
the character's own sides), ICT's separate `browInnerUp_L/_R` and `cheekPuff_L/_R` are summed into ARKit's single
`browInnerUp` and `cheekPuff`, `cheekRaiser_L/_R` and `PupilDilate_L/_R` (not ARKit shapes) are left out, and
`tongueOut`, which ICT doesn't have, is a quarter of `jawOpen` (a tongue SubTool gets the tongue's motion).
Offsets under 0.02 mm are dropped and the rest quantised to 16 bits; the teeth are kept only as the lower jaw's
rigid motion per shape. ICT's 100 identity (PCA) shapes are not used. The separately licensed full ICT Face Model
is not used either.

To rebuild: download ICT-FaceKit's `FaceXModel` folder, then
`python3 tools/face/build_ict_template.py <ICT-FaceKit>/FaceXModel assets/face/ict-arkit.sfface`.
