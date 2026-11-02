# Studio pipeline strategy

Status: design, 2026-11-01. Research and design only; nothing here is built yet unless the matrix says so.
This is an internal planning document. The public pages (`docs/studios.md`, `docs/guide/studios.md`,
`website/studios.html`) state only what ships.

**Goal:** SculptForge fits every tier of studio, from large feature-animation and VFX studios with in-house
animation systems down to individual artists. The two pillars we compete on are **security** (an app a
studio's IT can approve in one review) and **precision** (deterministic, scriptable, standards-exact output).
We are USD-first: OpenUSD is the contract, and every other format comes from the same data model.

---

## 1. State of the art, November 2026

### OpenUSD

- **Current release: 26.11.1 (21 Oct 2026).** The three before it were 26.08 (20 Jul), 26.05 (24 Apr) and
  26.03 (24 Feb). [1] The release-branch CHANGELOG's newest section is 26.08 [2], so the notes below run to 26.08.
- **UsdSkel** is stable and is still the interchange schema for skinned meshes and blend shapes. 2026 brought
  fixes, not schema changes: in 26.08, a blend-shape-weight regression (#4126) and a crash with `numJoints == 0`
  (the joint-less skeleton is exactly what we write for blend-shape-only rigs, so pin to 26.08+ when we check
  our output). 26.05 fixed defer-skinned prototypes in point instancers, and 26.03 added deferred,
  vertex-shader skinning in Storm (`HD_ENABLE_DEFERRED_SKINNING=1`). [2][3]
- **Rigging (OpenExec, new).** OpenExec is Pixar's computation engine inside USD. It arrived in 25.08 and gained
  *invertible rigging* in 26.03, 26.05 and 26.08: `IrFkController`, `IrJointScope` and `IrSwitchController`,
  which are rig controls that can be driven forwards and solved backwards. 26.08 still labels it "not yet ready
  for production use". [2][3] **Implication:** for the next 12 to 18 months, UsdSkel (joints, bind
  transforms, skinning primvars, blend shapes) remains the safe interchange for rigs. OpenExec is the thing
  to watch for a later "live rig" phase.
- **UsdShade and MaterialX.** 26.08 defaults to MaterialX 1.39.5 and improves `UsdPrimvarReader` handling
  for MaterialX in Storm. 26.05 added a `UsdShade` *EncapsulationMaterialValidator* and renamed Sdr fields
  (`family` → `function`, `sourceType` → `shadingSystem`). [2] A document using MaterialX 1.39 should state its
  version with `MaterialXConfigAPI`. [4]
- **Variants, payloads and composition.** 26.08 added the `matches_regex` variable-expression function, and
  `UsdUtils` dependency functions now evaluate expression variables in asset paths. It also made
  deep reference/inherit chains much faster (one rig went from 8 min to 98 s). 26.05 fixed layer offsets
  being applied twice on internal reference and payload arcs. 26.03 stopped allowing direct edits to
  `Sdf.Payload` attributes. [2]
- **Asset resolution (Ar 2.0).** 26.05 fixed a deadlock in `ArResolver` initialisation and now strips
  file-format arguments when resolving and localising asset paths. [2] Studios plug their asset systems in
  through Ar resolver plug-ins, and increasingly through OpenAssetIO (below).
- **Python.** 26.08 deprecates Python 3.9 and 3.10. The `usd-core` wheels dropped 3.8 and added 3.14 (26.05).
  [2] Our module targets 3.13 (VFX-RP), which every current build supports.
- **Hydra.** Hydra 2.0 (the scene index) is now the default. 26.03 made `USDIMAGINGGL_ENGINE_ENABLE_SCENE_INDEX`
  default to 1, and 26.08 deprecated the Hydra 1.0 classes and removed `HD_ENABLE_SCENE_INDEX_EMULATION`. Old
  render delegates still run through `HdRenderDelegateAdapterRenderer`. [2][5] Studio renderers (RenderMan,
  Arnold, Karma, V-Ray, Redshift, Cycles) all ship Hydra delegates. A sculpting app doesn't *need* Hydra; it
  needs to write USD that those delegates render without surprises.

### ASWF projects and the VFX Reference Platform

| Project | Latest (Nov 2026) | What it means for us |
|---|---|---|
| OpenAssetIO | 1.0.2 (Python 3.13 wheels); 1.0.0 stable earlier [6] | A stable API for "resolve and publish through the studio's asset manager". The USD AR2 bridge (`usdOpenAssetIOResolver`) has no tagged releases and is read-only. [7] |
| MaterialX | 1.39.5 (22 May 2026): OpenPBR 1.1.1, Slang, updated UsdPreviewSurface [8] | Our material export target, alongside UsdPreviewSurface. |
| OpenColorIO | 2.6.0 (1 Oct): the CY2027 base, CLF ST2136-1, built-in ACES 2.0 configs. 2.5.2 fixed CVE-2026-42450 (a LUT parsing overflow) [9] | Viewport colour management and baked texture colour spaces. Parse LUTs only from trusted configs. |
| OpenEXR | 3.5.3 (30 Oct): 15 security fixes; 3.5.0 added ZSTD and `colorInteropID` [10] | Baked maps (displacement and vector displacement must be float). Keep current; it is attack surface. |
| OpenVDB | 14.0.0 (29 Oct): ABI 14, Blosc vendored [11] | Optional: VDB volume import for remeshing, and VDB SDF export. |
| OpenSubdiv | 3.8.0 (Oct) [12] | Our multires levels should match OpenSubdiv Catmull-Clark limit surfaces, so `subdivisionScheme = "catmullClark"` renders identically downstream. |
| OpenTimelineIO | 0.18.x beta: Python 3.13, per-clip colour [13] | Not a sculpting concern. Ignore except for turntable review clips. |
| rez | 3.4.0 (31 May 2026): fixes shell injection in `rez-build` (#1979) [14] | We already ship `packaging/rez/package.py`. Document rez ≥ 3.4. |

**VFX Reference Platform.** CY2027 has been **Final** since 30 Aug 2026 and takes effect on 1 Jan 2027: GCC 14.2,
**glibc 2.34 (EL9)**, C++20, Python 3.13, Qt/PySide 6.8, NumPy 2.4, OpenEXR 3.5, OCIO 2.6, OpenSubdiv 3.8,
OpenVDB 14, Ptex 2.5, Alembic 1.8, FBX 2020.2+, Boost 1.91, oneTBB 2023. CY2026 has GCC 14.2, glibc 2.28,
OpenEXR 3.4, OCIO 2.5, OpenSubdiv 3.7 and OpenVDB 13. [15] Neither table lists OpenUSD or MaterialX; studios
pin those themselves.

### How large studios integrate sculpting tools

Large feature-animation studios run **closed, in-house animation and rigging systems** that no vendor can
plug into directly. A sculpting tool reaches them in four ways, and all four have to work without a vendor on site:

1. **USD layers.** The asset's model or sculpt layer is published as a USD layer (often a payload under a
   model or variant set) and composed into the asset by the studio's own tools. Prim paths must stay stable
   across re-publishes, or overrides authored downstream (shading, rigging, layout) break silently.
2. **Alembic caches.** Alembic is still used for baked geometry and point caches, and as the fallback
   when an in-house system predates USD.
3. **The asset system.** Path resolution, versioning and publish go through the studio's own asset manager,
   through an Ar resolver or OpenAssetIO. A tool that hard-codes file paths gets wrapped in launcher scripts,
   and that is fragile.
4. **Headless batch.** Retopology, bakes, LODs, validation and turntables run on the farm under the job
   scheduler, inside a rez-resolved environment, with no display and no network.

Sculpted deformation goes back into the rig as **corrective blend shapes**, keyed to a pose. That is why
"pose for sculpting" (§6) matters more to large studios than "export a rig": their rig lives elsewhere, and
they want to sculpt fixes *on* a pose and send back a clean delta in the bind space.

---

## 2. What we already have (verified in the tree, 0.10.7)

| Area | Today | Where |
|---|---|---|
| USD export | `.usda`/`.usdz` written without the SDK: one `Mesh` per Part under a root Xform, UVs as face-varying `primvars:st`, `displayColor`, UsdPreviewSurface (clay/painted/textured), sculpt layers as UsdSkel blend shapes on a joint-less Skeleton with a `SkelAnimation` (time-sampled face takes), Gaussian splats as `ParticleField3DGaussianSplat`. Checked with `usdchecker` 26.08. | `src/io/UsdIO.*` |
| USD import | `.usda` and text-root `.usdz`: meshes, xform ops, Z-up, UVs, blend shapes, splats. **No `.usdc`, no references or payloads, no variants.** | `src/io/UsdIO.*` |
| Other formats | FBX read (ufbx, any version) and write (7.4 binary, blend shapes, LOD groups); glTF/GLB (morph targets, `KHR_gaussian_splatting`, Khronos-validated); OBJ, PLY, STL (mm, Z-up), .GoZ | `src/io/` |
| VFX-RP builds | CY2026 (Rocky 8, glibc 2.28) and CY2027 (Rocky 9, glibc 2.34) tarballs, x86_64 and ARM64; CI checks glibc symbols; dependencies static (SDL3, ImGui, GLM, ufbx) | CI, `docs/studios.md` |
| rez | `packaging/rez/package.py`: `PATH`, `PYTHONPATH`, `SCULPTFORGE_ROOT`, studio brush share | `packaging/rez/` |
| Python | `import sculptforge`: `Document`, `run`, `run_script`, `commands()`, NumPy vertices and triangles, layers and deltas; releases the GIL | `src/python/module.cpp` |
| CLI | `sculptforge-cli` runs every editor command headless; `sculptforge --render --turntable` renders offscreen; `--mcp` (stdio only) | `src/cli/` |
| Bridges | Blender (folder + manifest, .GoZ; no sockets), Maya 2022+ (folder + manifest, OBJ; no sockets), Unreal 5 (Python remote execution, static or skeletal mesh with morph targets), ZBrush (GoZ-compatible, plug-in runs inside ZBrush) | `src/io/*Bridge*`, `src/link/UnrealLink.*` |
| Determinism | Seeded remesh, QRemesh, stroke jitter and scatter; deterministic deformers | `src/core/` |
| Posing | Masked transforms with soft falloff at a joint ("mask the body, rotate the arm"). **No skeleton.** | `src/core/MeshTransform.h` |
| Network | No telemetry, no account. **Automatic:** the daily update check, and the brush gallery index when its panel opens. **Remote by default when used:** the AI Brush assistant (Pollinations) and Generate 3D (Hugging Face). Listeners: Phone link (LAN, only while open), and a temporary Unreal call-back. Full audit in §7. | `src/ai/Http.cpp`, `src/link/Socket.cpp` |
| Signing | macOS: **ad-hoc** unless `SCULPTFORGE_CODESIGN_IDENTITY` is set; Windows: unsigned; `SHA256SUMS.txt` per release. **No SBOM, no provenance attestation.** | `cmake/MacSign.cmake`, `release.yml` |

---

## 3. Tiers: what each needs, and where we stand

### Large (feature animation and VFX with in-house systems)

| Need | Status | Gap |
|---|---|---|
| USD in and out with **stable prim paths** | Out ✓ (paths derived from Part names via `usdIdentifier`) | Paths change when a Part is renamed. Need a persistent per-Part `primPath` attribute, kept across renames and reimports. |
| USD **variants** | ✗ | Write `variantSet "sculpt"` (for example `base`/`hero`/`damaged`) and `lod`; read variant selections. |
| **Payloads and references** | ✗ (read) | Read: follow references and payloads in text layers. Write: an asset interface layer plus a payload layer. |
| **`.usdc`** | ✗ | A Crate reader and writer (large). Interim: document `usdcat`. |
| **UsdSkel** with joints | Blend shapes ✓; joints ✗ | §6 |
| **MaterialX** | ✗ (UsdPreviewSurface only) | Add `outputs:mtlx:surface` with `ND_open_pbr_surface_surfaceshader` (or `ND_UsdPreviewSurface_surfaceshader`) beside the preview surface, and `MaterialXConfigAPI` 1.39. |
| **OpenAssetIO / Ar hooks** | ✗ | Resolve `entity://` style references and publish through a manager (§4, phase 2). Opt-in Python plug-in, loaded only from paths the studio configures. |
| rez / VFX-RP | ✓ | Add CY2027 OCIO 2.6 when we adopt OCIO. |
| **Headless batch and Python** | ✓ | Add `--strict` (fail on any warning) and machine-readable JSON results for every command. |
| **Farm-friendly CLI** | ✓ mostly | Exit codes documented, `--threads N`, no writes outside `--out` and `$TMPDIR`, no config writes in batch mode. |
| **Deterministic output** | Algorithms ✓; files unverified | Byte-identical USD/FBX/glTF for the same input and seed on the same build, with a CI test. Fixed float formatting, sorted maps, no timestamps in files. |
| **Offline, no telemetry** | ✓ except the default update check | Studio builds (VFX-RP tarballs, rez) ship with the update check **off**, plus a single `SF_OFFLINE=1` that disables every network path. |
| **Licence for legal review** | GPL-3.0-or-later | §8 FAQ, an SBOM and a stated Python-API position. |
| Alembic | ✗ | Alembic 1.8 (VFX-RP) mesh export, then point-cache import. |
| OpenSubdiv-exact limit surface | Unverified | A test that our subdivision level 1 matches OpenSubdiv Catmull-Clark. |

### Medium (tracker-driven studios, DCC mix)

| Need | Status | Gap |
|---|---|---|
| **Flow Production Tracking** (ShotGrid) | ✗ | Toolkit engine is heavy. Ship a `tk-multi-publish2` collector/publisher hook pair that runs `sculptforge-cli` to export and registers PublishedFiles, plus a launcher entry. Flow's Python and REST APIs are customer-side, so there is no service of ours. [16] |
| **ftrack** | ✗ | The same shape: an action plus a publish script using `ftrack_api`, run by the studio. |
| **Perforce / Git LFS** | Partly (project files are binary) | A `.gitattributes` and `.p4typemap` snippet in the docs; sidecar exports in text USD diff cleanly. |
| **Houdini Solaris round trip** | Out ✓ (usda) | Reading Solaris output needs `.usdc` plus references and payloads. Add a tested recipe: SOP Import → USD ROP → SculptForge → sublayer back. |
| **Maya** | Bridge ✓ (early) | Blend-shape and skin round trip once §6 lands; Maya USD plug-in path via USD files. |
| **Unreal** | ✓ | Skeletal mesh with real joints once §6 phase 2 lands; Interchange-friendly USD. |

### Small (indie animation and games)

| Need | Status | Gap |
|---|---|---|
| **Kitsu** | ✗ | A small `gazu` publish script (CLI export + `publish_preview` of a turntable). [17] |
| **Blender** | ✓ add-on | Armature round trip after §6. |
| **Unreal** | ✓ | As above. |
| **Git LFS** | Partly | Docs snippet. |

### Individuals

| Need | Status | Gap |
|---|---|---|
| Blender, Unreal | ✓ | — |
| **Unity** | Via FBX/glTF | A documented recipe: glTF with morph targets in Unity 6 (glTFast), FBX for skinned meshes. |
| **Sketchfab** | ✗ (manual upload) | Upload is an account API (attack surface and token storage). **Needs the maintainer's OK.** Until then, document the GLB export preset. |
| **3D printing** | STL ✓ (mm, Z-up) | 3MF export (units, colour), wall-thickness and manifold check, hollowing with drain holes. |

---

## 4. Capability matrix

✓ ships · ◐ partial · ✗ missing · P1/P2/P3 = roadmap phase

| Need | Large | Medium | Small | Individual | Status | Phase |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| USD mesh out (usda/usdz) | ● | ● | ○ | ○ | ✓ | — |
| Stable prim paths | ● | ● | | | ◐ | P1 |
| Deterministic, byte-identical export | ● | ○ | | | ◐ | P1 |
| Studio mode: every network path off by policy, proven in CI | ● | ● | ○ | | ◐ | P1 |
| SBOM + build provenance attestation | ● | ● | | | ✗ | P1 |
| Pose for sculpting (UsdSkel skeleton in) | ● | ● | ● | ○ | ✗ | P1 |
| Corrective blend shapes in bind space | ● | ● | ○ | | ✗ | P1 |
| USD variants (write) | ● | ● | | | ✗ | P1 |
| MaterialX (OpenPBR) beside UsdPreviewSurface | ● | ● | ○ | | ✗ | P1 |
| CLI `--strict`, JSON results, documented exit codes | ● | ● | | | ◐ | P1 |
| USD references/payloads (read and write) | ● | ● | | | ✗ | P2 |
| `.usdc` read/write | ● | ● | ○ | | ✗ | P2 |
| Rig export (UsdSkel, glTF skin, FBX skin) | ○ | ● | ● | ● | ✗ | P2 |
| Alembic export | ● | ○ | | | ✗ | P2 |
| OpenAssetIO resolve and publish | ● | ○ | | | ✗ | P2 |
| Flow PT / ftrack publish hooks | | ● | | | ✗ | P2 |
| Kitsu publish script | | | ● | | ✗ | P2 |
| Signed Windows/macOS builds (paid certificates) | ● | ● | ○ | ○ | ✗ | P2 (maintainer) |
| Solaris round-trip recipe, tested | ○ | ● | | | ◐ | P2 |
| OCIO 2.6 viewport and texture colour | ● | ○ | | | ✗ | P3 |
| OpenSubdiv-exact limit surface test | ● | ○ | | | ✗ | P3 |
| VDB import/export | ○ | ○ | | | ✗ | P3 |
| 3MF + print checks | | | | ● | ◐ | P3 |
| Sketchfab upload | | | | ○ | ✗ | P3 (maintainer) |
| OpenExec invertible rigs (watch) | ○ | | | | ✗ | P3 |

● must-have for the tier · ○ nice to have

---

## 5. Roadmap

Effort is for one focused agent; "w" = working weeks, including tests and docs.

### Phase 1: trust and pose (about 5 to 6 w; ships across 0.11.x)

1. **Studio mode** (0.75 w, on top of the "AI tools" consent gate from gen3d-default, whose form is pending the maintainer). One policy (build flag, site
   policy file, `SF_STUDIO=1`, Preferences) that forces AI tools off or local, enforced as a backstop in
   `ai::httpRequest*` and `Socket::listen`/`openMulticast`, new `SCULPTFORGE_*` build flags and a CI no-network proof. Design in §7.
2. **SBOM and provenance** (0.5 w). CycloneDX SBOM per release (our sources plus vendored `third_party/`),
   GitHub artifact attestations (Sigstore, free) for every file, and a `verify` section in the docs.
3. **Deterministic export** (1 w). Byte-identical exports for the same input, seed and build; CI hashes the
   golden outputs for USD, glTF, FBX and OBJ. `--threads` must not change results.
4. **Stable prim paths and variants** (1 w). A persistent `usd:primPath` per Part (kept across renames,
   imported from USD, editable), a `sculpt` variant set from named Part states, and `lod` variants from
   `export_lods`.
5. **MaterialX** (0.5 w). OpenPBR surface beside UsdPreviewSurface, `MaterialXConfigAPI` 1.39, checked
   with `usdchecker` 26.11 and the MaterialX validator.
6. **Pose for sculpting** (2 w), §6.1.
7. **CLI for farms** (0.5 w). `--strict`, `--json` results, documented exit codes, no config writes in batch.

### Phase 2: assets and rigs (about 8 to 10 w; 0.12 to 0.13)

1. **USD composition read** (2 w). References, payloads, sublayers and variant selections in text layers;
   `@asset@` paths through a pluggable resolver interface.
2. **`.usdc` Crate reader/writer** (3 w). In-house from the public file-format description, never copied
   code, fuzzed and tested like the other parsers (`test_malformed_files`).
3. **Export a rig** (2 w), §6.2.
4. **Alembic export** (1 w). Meshes and UVs, Ogawa, Alembic 1.8 compatible.
5. **OpenAssetIO** (1 w). A Python-side bridge: `sculptforge.assets.resolve()` and `publish()` that call the
   studio's OpenAssetIO manager when the studio installs one. No network code of ours; the manager is theirs.
6. **Tracker hooks** (1 w). Flow PT `tk-multi-publish2` hooks, an ftrack action and a Kitsu `gazu` script, in
   `integrations/`, each running `sculptforge-cli` only.
7. **Paid code signing** (0.5 w once bought): Apple Developer ID plus notarisation, and Windows Authenticode
   (or Azure Trusted Signing). **Payment, so the maintainer decides.**

### Phase 3: colour, precision and print (about 5 w; 0.14+)

OCIO 2.6 viewport and texture colour spaces (CY2027) (1.5 w); an OpenSubdiv-exact limit-surface test and
crease export (1 w); VDB SDF import and export (1 w); 3MF and print checks (1 w); Sketchfab upload (only with
the maintainer's OK); a watching brief on OpenExec invertible rigs (re-assess when OpenUSD drops the "not
production-ready" label).

---

## 6. Rigging, in USD terms

**Principle:** our rig data model *is* UsdSkel, so USD export is a direct write, and glTF and FBX are
mechanical translations of the same arrays. Animation stays in the artist's DCC; we pose, sculpt and send
back.

### Data model (our names, mapped to UsdSkel)

| SculptForge | UsdSkel | glTF 2.0 | FBX |
|---|---|---|---|
| **Skeleton** (per Part group) | `Skeleton` prim under a `SkelRoot` | `skin` | `FbxSkeleton` node hierarchy + `FbxSkin` |
| **Joint path** `"hips/spine/chest"` (string, ordered) | `uniform token[] joints` (paths encode the hierarchy) | `skin.joints` (node indices) + node children | node names and parents |
| **Bind pose** (world matrices) | `uniform matrix4d[] bindTransforms` | `inverseBindMatrices` (accessor; the inverse) | cluster `TransformLink` matrices |
| **Rest pose** (local matrices) | `uniform matrix4d[] restTransforms` | node TRS at rest | node local transforms |
| **Pose** (local T/R/S per joint) | `SkelAnimation`: `translations`, `rotations` (quatf), `scales` (half3) | animation channels, or node TRS for a single pose | `FbxAnimCurve` on joint TRS, or a bind pose |
| **Weights** (≤ N per vertex, sorted, normalised) | `primvars:skel:jointIndices` and `primvars:skel:jointWeights`, `elementSize = N`, `interpolation = "vertex"` | `JOINTS_0`/`WEIGHTS_0` (4 per set; more sets beyond 4) | `FbxCluster` per joint (indices + weights) |
| **Mesh bind matrix** | `primvars:skel:geomBindTransform` | (baked into the mesh) | cluster `Transform` |
| **Layer** (sculpt layer, already ours) | `BlendShape` (`offsets`, `pointIndices`) + `skel:blendShapes` / `skel:blendShapeTargets` | morph `targets` + `weights` | `FbxBlendShape` / channel |
| **Corrective** (layer keyed to a pose) | `BlendShape` + `inbetweens` where needed; the pose driver is *not* in UsdSkel, so we record it as `customData` `{sculptforge:driverPose}` | morph target + `extras` | blend-shape channel; driver as user property |
| Layer weights over time | `SkelAnimation.blendShapeWeights` (we already write this) | morph weight animation | blend-shape channel curves |

UsdSkel's rules we follow: the `SkelRoot` encloses both the skeleton and the skinned meshes; `skel:skeleton` and
`skel:animationSource` are bound with `SkelBindingAPI`; joint order is fixed per skeleton and every array uses it;
rotations are quaternions; weights are normalised and padded with zero-weight entries to `elementSize`.
OpenUSD 26.08+ is the version we check against (the `numJoints == 0` and blend-shape-weight fixes).

### Phase 1: pose for sculpting

What artists and large studios need: **sculpt on a posed character, and get a clean result in bind space.**

- **Import a skeleton and weights** from USD (UsdSkel), glTF (skin) and FBX (clusters). For the
  in-house-rig case, UsdSkel is enough: studios export a UsdSkel proxy of their rig for a pose.
- **Pose mode:** select joints in the viewport (a joint gizmo reusing the transform gizmo), rotate them in FK,
  and get linear-blend skinning on the GPU (dual quaternion as an option for shoulders and wrists). Mirror
  pose by joint naming (`_L`/`_R`, `left`/`right`). Poses are saved in the project, and a `SkelAnimation` frame
  from a file can be picked as the pose.
- **Sculpt on the pose; store in bind space.** Brush deltas are applied on the posed surface and mapped back
  through the inverse skinning transform per vertex: Δ_bind = (Σ wᵢ Mᵢ)⁻¹ Δ_posed. They are stored on a layer
  marked as a **corrective** for that pose. Turning the pose off shows the bind mesh with the corrective
  applied, so artists see exactly what will ship.
- **Fallback with no skeleton:** today's masked transform ("Pose by mask") stays, and can be *promoted* to a
  one-joint skeleton with the mask as weights. That gives us an auto-skeleton path for individuals.
- **Export:** the sculpt in bind space, plus each corrective as a UsdSkel `BlendShape` (with its driver pose in
  `customData`), plus the imported skeleton and weights written back unchanged (same joint order, so the
  studio's rig re-binds). glTF and FBX get the same.
- **Tests:** round-trip skeleton, weights and blend shapes through USD/glTF/FBX with array-exact
  comparison; a posed stroke reprojects to bind space within 1e-5; `usdchecker` and the Khronos validator are clean.

Effort: about 2 w (import 0.5, skinning and gizmo 0.75, bind-space sculpt 0.5, export and tests 0.25).

### Phase 2: export a rig

For small studios and individuals who have no rig yet (games, Unity/Unreal/Blender, printing poses).

- **Create a skeleton:** place joints on the surface with snap-to-centre-of-volume (a ray through the mesh,
  then the midpoint of entry and exit), chains by click, mirrored with symmetry. Templates: biped, quadruped
  and a face rig with jaw and eyes.
- **Weights:** automatic weights from geodesic or heat diffusion on our mesh, with smoothing, then weight painting with
  the existing brush engine (a **Weight** paint channel like Vertex Paint), normalise and prune to 4 or 8.
- **Export** to USD (UsdSkel as above, with `SkelAnimation` for saved poses), glTF (`skin` + up to 2 sets of
  `JOINTS`/`WEIGHTS`), and FBX (skeleton + clusters, 7.4). Unreal Send uses the skeleton instead of today's
  single root bone, and the Blender and Maya bridges carry armatures and skin clusters.
- **Not in scope:** IK solvers, constraints, controllers and animation curves beyond poses. Those live in the
  DCC. Watch OpenExec invertible rigs for a later "live rig in USD" phase.

Effort: about 2 w (placement 0.5, auto weights 0.5, weight painting 0.5, exporters and bridges 0.5).

---

## 7. Security story (for studio IT and security review)

Write-up for a security reviewer. Every claim must be true of the shipping build; items marked *(P1)* or
*(P2)* are the roadmap, and the public version only lists them once they ship.

**Network.** The audit below (0.10.7 source, 2026-11-01) is the basis for every network claim we make.
Today, **two calls happen without a click** (the update check, and the brush gallery when its panel opens),
and **two defaults send artists' content to third-party services** when they use them (the AI Brush
assistant → Pollinations, and Generate 3D → Hugging Face). So "nothing phones home" and "AI talks only to the
service you choose" are **not exactly true yet**. Studio mode (below) makes them true.

#### Every outbound call and listener (verified in the tree)

All HTTP goes through one function (`ai::httpRequest` / `httpRequestStream` in `src/ai/Http.cpp`, libcurl,
user agent `SculptForge/0.1`). All sockets go through `src/link/Socket.cpp`. Those are the two choke points.

| # | What | Destination | When | Data sent | Default |
|---|---|---|---|---|---|
| 1 | **Update check** | `https://sculptforge.app/latest.json`, then `https://islandmed.github.io/latest.json` if the first fails | **Automatic**: GUI start-up, at most once a day (not CLI or Python) | A GET with no body; the server sees the IP and the fixed user agent | **ON** (off in the AUR build; `SF_NO_UPDATE_CHECK`, Preferences) |
| 2 | **AI Brush assistant, "Free"** | `https://text.pollinations.ai/openai` | User sends a chat message | The whole chat history (the artist's text and earlier replies), the current brush recipe when "refine" is on, our system prompt, `"referrer":"sculptforge"` | **Free is the default provider**, so a remote third party |
| 2b | Free fallback | `http://localhost:11434` (Ollama) | **Automatic** after #2 fails | `/api/tags`, then the same chat | Loopback only |
| 3 | Brush assistant, Anthropic / OpenAI-compatible | `api.anthropic.com` or the endpoint the user sets | User sends a message; "Find models" button | As #2, plus the API key header | Opt-in |
| 4 | **Generate 3D from image** | Hugging Face Spaces (default `microsoft-trellis-2.hf.space`; also Hunyuan3D, Stable Fast 3D) | User clicks Generate; CLI/Python/MCP `image_to_3d` | The reference image (subject cropped, ≤ 1024 px), plus the HF token if set | **`huggingface` is the default service**, so a remote third party |
| 5 | **Generate 3D from text** | HF Spaces (Hunyuan3D 2 / 2 mini Turbo) | User clicks Generate; `text_to_3d` | The prompt text (+ HF token) | Remote by default. **Bug:** `text_to_3d` silently swaps a saved `command`/`inflate` (offline) choice for `huggingface` (`src/ai/AiCommands.cpp`, `text_to_3d`) |
| 6 | fal.ai / Meshy | `queue.fal.run`, `api.meshy.ai` | User action with that service picked | The image or prompt, plus the key; the result GLB is downloaded | Opt-in |
| 7 | ComfyUI | `http://127.0.0.1:8188`, or the server the user sets | "Check server", Generate | The image or prompt, plus the workflow JSON | Loopback by default |
| 8 | **Community brush gallery** | `raw.githubusercontent.com/islandmed/sculptforge-brushes` | **Automatic when the Brush gallery panel opens** (index), previews as they scroll in; install on click | GETs only (IP + user agent); results cached | Fetches on panel open |
| 9 | Splat-training tools | `github.com` releases (Brush 0.3.0, COLMAP 4.2.1) | "Download" button | GET; checked against a pinned SHA-256, **then executed** | Opt-in |
| 10 | Send to Unreal | UDP multicast `239.0.0.1`, plus a **temporary TCP listener** for the editor's call-back | Ctrl+U / menu / `unreal_send` | A ping; then a Python import script and the exported file paths | Loopback, TTL 0; "network" mode binds `0.0.0.0`, TTL 1 (LAN) |
| 11 | **Phone link** | **Listener `0.0.0.0:8765`** (LAN) | Only while its window is open; token in the QR code + 6-digit pairing, 10-min expiry; WebRTC uses host candidates only (no STUN/TURN) | Serves the capture page; receives uploads; sends the active Part as USDZ/GLB to the phone | Off until opened |
| 12 | Browser links | The system browser: site, guide, issues, Stripe, Discord, What's new, licence | Button click | Nothing from the app; the browser opens the URL | — |
| 13 | MCP / CLI / Python | — | The commands above (`brush_ai`, `image_to_3d`, `text_to_3d`, `unreal_send`, `comfyui_check`, gallery install) can be run by scripts and AI agents | As per the row | MCP is stdio only; no listener |

No telemetry, analytics, crash upload, account or licence server exists. Crash logs stay local
(`src/app/CrashLog.cpp`). Hardware calibration never uses the network (`src/core/HardwareProfile.cpp`).
Everything except rows 1 to 11 works air-gapped.

#### Recommendation: Studio mode on top of the "AI tools" switch

**Layer 1, the "AI tools" gate (being built by the gen3d-default task; the exact form is waiting on the
maintainer).** Two variants are on the table: (a) one switch, **off by default**; or (b) AI features visible by
default, behind a **consent card** shown before the first AI request, plus a show/hide switch. Studio mode works
with either, because it depends on only one property: **no AI code makes any request, local or remote, until the
artist has consented** (turned the switch on, or accepted the card). The gate covers rows 2 to 7 and the AI
commands in row 13. Until consent, the CLI, Python and MCP fail `brush_ai`, `image_to_3d`, `text_to_3d` and
`comfyui_check` with a clear message. The consent step must name what goes where: "the Brush assistant sends your
chat to text.pollinations.ai; Generate 3D uploads your image or prompt to Hugging Face Spaces; pick a local model
to keep everything on this machine". That one step replaces the per-service consent proposed earlier. Once the
gate ships, a default install makes no AI request without consent, and only rows 1 (update check) and 8
(gallery) remain as calls the artist didn't explicitly start.

The contract Studio mode needs from layer 1 (agreed with gen3d-default): one predicate, `aiToolsEnabled()`,
checked at every AI entry point (UI and command registry); a setting in the user config;
`SF_AI_TOOLS=0|1` in the environment for the CLI, Python and farm; and a way for a higher-level policy to
**force it off or limit it to local**, which the artist can't override.

**Layer 2, Studio mode: the whole network, set by IT.** `NetworkPolicy::{Standard, Studio}`, resolved once at
start-up. The strictest source wins, and a user can't loosen a policy set by an admin or the build:

1. build flag `SCULPTFORGE_STUDIO_DEFAULT=ON` (VFX-RP tarballs, rez, `.rpm`): starts in Studio mode;
2. a site policy file, read-only to users: `$SCULPTFORGE_ROOT/policy.json`, `/etc/sculptforge/policy.json`,
   `%ProgramData%\SculptForge\policy.json`, `/Library/Application Support/SculptForge/policy.json`;
3. `SF_STUDIO=1` (alias `SF_OFFLINE=1`) in the environment (rez package, launcher, farm);
4. Preferences > Privacy > **Studio mode** (the artist can turn it *on*, never off when 1 to 3 set it).

```json
{ "studio": true,
  "ai_tools": "off",                         // "off" | "local" (loopback + allow list only) | "user"
  "allow": ["comfy.studio.internal", "ollama.farm:11434"],
  "allow_lan_listeners": false,
  "update_check": false }
```

**What Studio mode does.**
- **AI tools:** forced to the policy's `ai_tools` value, `off` by default. With `local`, the artist can turn AI
  tools on, but the hosted services are hidden, and only loopback and allow-listed hosts (on-prem ComfyUI,
  Ollama, LM Studio, a `command` generator) can be picked.
- **Choke points, as the backstop:** `ai::httpRequest*` refuses any host that isn't loopback or on `allow` ("Blocked
  by studio policy (host)"), so rows 1, 8 and 9 stop too, and anything a future feature adds is blocked even if
  it forgets to check the policy. `Socket::listen` and `openMulticast` refuse non-loopback addresses: Phone link
  and Unreal network mode stop, while local Unreal (loopback, TTL 0) keeps working.
  `"allow_lan_listeners": true` re-enables them for studios that want phone capture.
- The update check is off. Browser links show the URL with a Copy button instead of opening the browser.
- The status bar shows a "Studio mode" badge. `sculptforge-cli --network-report` prints the effective policy, the
  AI tools state and every row above as allowed or blocked, for IT to paste into its review.

**Standard mode, the remaining fixes** (so the public claims hold without Studio mode):
- The brush gallery fetches only on a **Browse** click, not when its panel opens.
- Fix the `text_to_3d` silent switch (row 5): with an offline generator chosen, fail and say so.
- The update check stays on by default for individuals (one GET, documented, one switch); Studio mode turns it off.

**Build flags** (CMake, all compiled out, not merely hidden, so `strings` and the SBOM prove it):

| Flag | Effect |
|---|---|
| `SCULPTFORGE_UPDATE_CHECK=OFF` | Exists. No update code or URL in the binary. |
| `SCULPTFORGE_HOSTED_AI=OFF` (new) | Removes the Pollinations, Hugging Face, fal, Meshy and Anthropic clients and their URLs. The AI tools switch then offers only local AI (Ollama, LM Studio, ComfyUI, command). |
| `SCULPTFORGE_AI_TOOLS=OFF` (new) | Removes AI tools entirely (no switch, no commands). |
| `SCULPTFORGE_PHONE_LINK=OFF` (new) | Removes the LAN HTTP server and WebRTC. |
| `SCULPTFORGE_GALLERY=OFF` (new) | Removes the online brush gallery (studio brush shares still work). |
| `SCULPTFORGE_NETWORK=OFF` (new) | All of the above, plus no libcurl at all, the splat tool download and Unreal LAN mode removed; loopback Unreal kept. The "air-gap build". |
| `SCULPTFORGE_STUDIO_DEFAULT=ON` (new) | Starts in Studio mode (policy level 1); set for the VFX-RP tarballs, rez and `.rpm`. |

**Proof (CI, so it can't regress):**
- run ctest and the UI smoke test inside `unshare -rn` (no network namespace) with `SF_STUDIO=1` (and separately with a default config, to prove that no AI request happens before consent); any
  non-loopback `connect()`/`bind()` seen by an `strace -f -e trace=network` wrapper fails the job;
- a unit test that every row's entry point returns "Blocked by studio policy" under Studio mode;
- for `SCULPTFORGE_NETWORK=OFF` builds: `nm`/`ldd` show no libcurl, and `strings` shows none of the hosted URLs;
- a `tests/network_inventory` check: a grep for `httpRequest`, `listen(` and `openMulticast(` call sites must match
  the table above, so a new network call can't land without updating this audit.

Effort: about 0.75 w once AI tools ships (policy and choke points 0.4, UI/report 0.1, flags and CI proof 0.25).

#### Public claims to correct now (they are inexact today)

| Where | Today | Exact until Studio mode ships |
|---|---|---|
| `website/index.html` ("Nothing phones home"), `website/studios.html:395`, `docs/studios.md:4`, `docs/guide/studios.md:4` | "nothing phones home" / "works fully offline" | "No telemetry and no account. The only automatic request is a once-a-day update check (one switch turns it off). Everything works offline." |
| `README.md:326`, `docs/guide/studios.md:143`, `docs/guide/troubleshooting.md:243`, `website/studios.html:396` | "AI features talk only to the service you choose" | Once the AI gate ships: "AI tools ask before they send anything. When on, the Brush assistant uses Pollinations and Generate 3D uses Hugging Face, unless you pick a local model." |
| `website/index.html:542`, `docs/guide/generate-3d.md:259`, `README.md:326` | "no servers of its own" | "no servers of its own besides the static website the update check reads". |

These go out with the next release's lockstep, alongside the guide and site.

**Files and code.**
- File parsers (OBJ, FBX, glTF, PLY, USD/USDZ, images, .GoZ, brushes, packs) are tested against hostile and
  malformed files, including fuzzer-found cases (`tests/test_malformed_files.cpp`). *(P1)* Continuous fuzzing in CI. Bridges exchange files through one folder, checked strictly (no paths outside it, no
  links, size limits), and nothing in the folder is ever executed.
- No plug-ins from the network. Packs are reviewed and installed by the user, never by an AI agent. Studio
  brush libraries load **read-only** from a share.
- API keys (only if AI features are used) are stored with owner-only permissions and sent only to their
  own service.

**Builds and supply chain.**
- Built in public CI from tagged sources; every release has `SHA256SUMS.txt`. Few dependencies, all linked
  statically (SDL3, Dear ImGui, GLM, ufbx) and listed in `THIRD_PARTY_NOTICES.md`. Studios can rebuild from
  source with the VFX-RP toolchain and get the same binary they reviewed.
- *(P1)* CycloneDX SBOM and Sigstore build-provenance attestations for every artifact
  (`gh attestation verify <file> --repo islandmed/SculptForge`).
- *(P2, paid)* Apple Developer ID notarisation and Windows Authenticode. Today macOS builds are ad-hoc signed
  and Windows builds are unsigned; studios that need signed binaries can sign their own rebuild.
- Private vulnerability reporting through GitHub, with acknowledgement within 7 days (SECURITY.md).

**Deployment.** rez package, VFX-RP tarballs (EL8/EL9), `.rpm`, `.deb`, Flatpak, AppImage. Configuration
goes through environment variables, nothing is written outside the user's config and data folders, and batch mode
writes only where it's told *(P1)*.

---

## 8. GPL and licensing FAQ for studios

Plain answers, not legal advice. The key fact: **the GPL is triggered by distribution, not by use.** The FSF's
own FAQ says copies made and used inside one organisation are not distribution, but copies given to
contractors for off-site use are. [18]

- **Can we use SculptForge commercially on any number of machines?** Yes. No seats, no fees, no licence server.
- **Who owns the models we make?** You do. The GPL covers the program, not its output. Exported USD, FBX,
  glTF, textures and project files carry no licence from us.
- **Can we modify it for in-house use without publishing our changes?** Yes. Internal use, across your
  facilities and farm, is not distribution.
- **What triggers source obligations?** Giving a modified build to someone *outside* your organisation: a
  vendor, an outsourcing partner, a freelancer on their own machine, or a client. Then you must offer them the
  source of your modifications under the GPL. Freelancers working on your machines and network are usually
  treated as internal; confirm with counsel.
- **Our Python tools call `import sculptforge`. Do they become GPL?** Keeping them internal never triggers
  anything. If you *distribute* a script together with SculptForge, the conservative reading is that code
  linked into the same process may form a combined work. Scripts that run `sculptforge-cli` as a separate
  process and exchange files are clearly separate programs. *(Decision for the maintainer and an IP lawyer: add an
  explicit GPL exception, like a "plug-in and script exception", for code that uses only the public Python API
  and CLI. That would remove the question entirely for vendors and tool sellers. Recommended.)*
- **Can we link SculptForge into our closed in-house asset system?** Internally, yes. If that system is ever
  distributed outside your organisation, the combined program must be GPL. Talking to it through files, USD or
  the CLI avoids the question.
- **Does the GPL affect our pipeline when we load USD written by SculptForge?** No. Output files aren't the
  program.
- **Third-party licences?** All bundled dependencies are permissive (zlib, MIT, BSD) and listed in
  `THIRD_PARTY_NOTICES.md`. *(P1)* An SBOM lists them in machine-readable form.
- **Is there a commercial licence or support contract?** Studio memberships fund priority on pipeline bugs and
  custom studio builds. They don't change the licence. *(Offering a non-GPL commercial licence would need every
  contributor's agreement; we don't run a CLA, so this stays GPL.)*

---

## 9. Website draft: studios.html "Pipeline" section (NOT PUBLISHED)

Replaces the current `#pipeline` section once Phase 1 items ship. Lines marked ⏳ go live only after their
feature does. The structure is two pillars, then the integration grid, which already exists on the page.

```html
<section class="section alt" id="pipeline" aria-labelledby="pipeline-title">
  <div class="container">
    <p class="eyebrow">Pipeline</p>
    <h2 id="pipeline-title">Secure enough for IT. Precise enough for the pipeline.</h2>
    <p class="lede">SculptForge is built to pass a studio security review in one pass and to write files your
      pipeline can trust, from a single artist to a facility with its own animation system.</p>

    <div class="pillars">
      <article>
        <h3>Security</h3>
        <ul>
          <li><strong>Offline by design.</strong> No accounts, no licence server, no telemetry. Everything works
            air-gapped.</li>
          <li>⏳ <strong>AI asks first.</strong> No AI feature sends anything anywhere until you say yes,
            and IT can switch it off for everyone.</li>
          <li>⏳ <strong>Studio mode.</strong> One switch (or a site policy file IT controls) blocks every network
            call and listener. AI runs on your own machines, and CI proves it with no network at all.</li>
          <li><strong>Nothing listens by default.</strong> No open ports until you open Phone capture. Bridges exchange files through one folder,
            checked strictly, and nothing in it is ever run.</li>
          <li>⏳ <strong>Verifiable builds.</strong> Built in public CI, with checksums, an SBOM and signed build
            provenance for every file. Rebuild from source with the VFX Reference Platform toolchain.</li>
          <li><strong>Hardened file readers.</strong> Every importer is tested against hostile and malformed files.</li>
          <li><strong>Plain licensing.</strong> GPL-3.0-or-later: use it on any number of machines, own your art,
            modify it in-house. <a href="guide/studios.html#licensing">Licensing FAQ</a></li>
        </ul>
      </article>
      <article>
        <h3>Precision</h3>
        <ul>
          <li><strong>USD-first.</strong> OpenUSD meshes, UVs and materials, with sculpt layers as UsdSkel blend
            shapes, checked with OpenUSD's own <code>usdchecker</code>.</li>
          <li>⏳ <strong>Stable prim paths and variants</strong>, so downstream overrides survive every
            re-publish.</li>
          <li>⏳ <strong>Deterministic output.</strong> The same input and seed give byte-identical files,
            tested in CI.</li>
          <li>⏳ <strong>Pose, sculpt, ship in bind space.</strong> Load your rig's skeleton from USD, glTF or
            FBX, sculpt on the pose, and export correctives as UsdSkel blend shapes your rig re-binds.</li>
          <li><strong>Headless and scriptable.</strong> <code>import sculptforge</code> and
            <code>sculptforge-cli</code> run every operation on the farm, with offscreen turntables.</li>
          <li><strong>Built to the VFX Reference Platform.</strong> CY2026 and CY2027 builds (Rocky 8 and 9),
            a rez package, and static dependencies that never clash with your other tools.</li>
        </ul>
      </article>
    </div>

    <!-- existing integration grid: Blender, Maya, ZBrush Bridge (GoZ-compatible), Unreal, formats,
         Linux/Windows/macOS, Linux pipelines; add ⏳ Houdini Solaris, Flow Production Tracking / ftrack /
         Kitsu publish hooks, Alembic, MaterialX when they ship -->

    <p class="fine">Pipeline TDs: see <a href="guide/studios.html">SculptForge for studios</a> for rez,
      environment variables, headless use and the security notes for your IT review.</p>
  </div>
</section>
```

Keep the existing no-affiliation footer on the page (it names ZBrush for compatibility).

---

## 10. Decisions for the maintainer

1. **GPL exception for scripts and plug-ins using the public API/CLI** (legal): recommended; check with the IP lawyer.
2. **Paid signing certificates** (Apple Developer Program, Windows Authenticode/Trusted Signing): payment.
3. **Sketchfab upload** (an account token and outbound API, so attack surface): later, opt-in.

Everything else in Phases 1 to 3 can go ahead without the maintainer.

---

## Sources

1. OpenUSD releases: https://github.com/PixarAnimationStudios/OpenUSD/releases
2. OpenUSD CHANGELOG (release branch): https://github.com/PixarAnimationStudios/OpenUSD/blob/release/CHANGELOG.md
3. AOUSD, OpenUSD v26.03 and v26.05: https://aousd.org/blog/openusd-v26-03/ ,
   https://aousd.org/blog/announcing-openusd-v26-05-key-features-and-improvements/
4. AOUSD, OpenUSD v25.05 (MaterialX 1.39, MaterialXConfigAPI): https://aousd.org/?p=2131
5. Hydra 2.0 getting started: https://openusd.org/release/api/_page__hydra__getting__started__guide.html
6. OpenAssetIO releases: https://github.com/OpenAssetIO/OpenAssetIO/releases
7. usdOpenAssetIOResolver: https://github.com/OpenAssetIO/usdOpenAssetIOResolver
8. MaterialX releases: https://github.com/AcademySoftwareFoundation/MaterialX/releases
9. OpenColorIO releases: https://github.com/AcademySoftwareFoundation/OpenColorIO/releases
10. OpenEXR releases: https://github.com/AcademySoftwareFoundation/openexr/releases
11. OpenVDB releases: https://github.com/AcademySoftwareFoundation/openvdb/releases
12. OpenSubdiv releases: https://github.com/PixarAnimationStudios/OpenSubdiv/releases
13. OpenTimelineIO releases: https://github.com/AcademySoftwareFoundation/OpenTimelineIO/releases
14. rez releases: https://github.com/AcademySoftwareFoundation/rez/releases
15. VFX Reference Platform: https://vfxplatform.com/
16. Flow Production Tracking Publisher API: https://developers.shotgridsoftware.com/tk-multi-publish2
17. Kitsu / gazu: https://www.cg-wire.com/shotgrid-alternative
18. GNU GPL FAQ, internal distribution: https://www.gnu.org/licenses/gpl-faq.html#InternalDistribution
