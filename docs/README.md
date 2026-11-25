# docs

Documentation for people who use, build or work on SculptForge. The top-level [README](../README.md) is the front
door; this folder holds what it links to. For **using** SculptForge, start with the [user guide](guide/index.md): it is
also built into the app (Help > User Guide, Shift+F1) and published at [sculptforge.app/guide](https://sculptforge.app/guide/).

## Using it

| Page | What |
|---|---|
| [features.md](features.md), [controls.md](controls.md) | What it does, and the controls |
| [pipelines.md](pipelines.md), [studios.md](studios.md) | Automation (CLI, Python, MCP) and what studios get: policy, offline builds, audit log |
| [ai-services.md](ai-services.md), [local-ai.md](local-ai.md), [comfyui.md](comfyui.md), [generators.md](generators.md) | What each AI feature sends where, how to run it all on your own computer, your own ComfyUI, the image-to-3D sources |
| [unreal.md](unreal.md), [blender.md](blender.md), [phone-link.md](phone-link.md), [tablet.md](tablet.md) | Unreal, the Blender bridge, the phone, pens and tablets |
| [brushes.md](brushes.md), [pack-format.md](pack-format.md) | Sharing brushes; the `.sfpack` format for brushes, macros and plugins |
| [performance.md](performance.md), [benchmark.md](benchmark.md) | Measured figures and the benchmark that makes them |

## Building and contributing

| Page | What |
|---|---|
| [building.md](building.md), [build-speed.md](build-speed.md) | Build from source; keep the edit-build loop short |
| [project-layout.md](project-layout.md), [architecture-attributes.md](architecture-attributes.md) | Where things are; the mesh attributes the engine carries |
| [project-format.md](project-format.md), [bridge-format.md](bridge-format.md) | The `.sfproj` project file and the Bridge exchange file |
| [testing-flow.md](testing-flow.md) | The test tiers and how a change finds the tests it can break |
| [ui-design.md](ui-design.md) | The interface spec |
| [releasing.md](releasing.md), [merge-queue.md](merge-queue.md) | How a release is built, signed and published; how changes land on `main` |
| [starter-meshes.md](starter-meshes.md), [brand.md](brand.md) | Where the starter meshes come from; which logo goes where |

Contributing starts at [CONTRIBUTING.md](../CONTRIBUTING.md). Code orientation: [src/README.md](../src/README.md).

## Folders

| Folder | What |
|---|---|
| [guide](guide) | The user guide, one Markdown file per chapter (`index.md` is the contents). Write only the subset described at the top of `src/core/Guide.h`. Built into the app and the website by [tools/guide](../tools/guide/README.md); `images/` are its screenshots |
| [design](design) | Design notes for features, research and larger changes: why it is built the way it is, limits and threat models (for example [plugins](design/plugins.md)). Read the one for a feature before changing it |
| [legal](legal) | The licence exception and the trademark policy: how SculptForge names things. Read before you name a feature, a brush or a file |
| [images](images) | Pictures for the README and these pages |

## Writing docs

- Say what is true of the current code and check every claim against it. Link to the file instead of copying it.
- Use SculptForge's own names for everything; [legal/trademarks.md](legal/trademarks.md) explains the rule and
  `tools/check_marks.py` enforces it (it runs in CI).
- A new image or video needs `tools/check_marks_media.py` run and `tools/marks_media_cache.json` committed.
- Changing something users see? Add or update the guide chapter in the same pull request, and add a release note
  fragment, `changes/<branch>.md` ([format](../changes/README.md)).
