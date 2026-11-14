# Image-to-3D generators

**Image to 3D → Full 3D model** offers seven sources:

| Service | What it is | Setup |
|---|---|---|
| **Hugging Face** (default, free) | The official public demos of Hunyuan3D 2.1 / 2 / 2 mini Turbo (Tencent), TRELLIS.2 (Microsoft) and Stable Fast 3D (Stability AI), on Hugging Face's shared ZeroGPU hardware | Nothing. A free token from huggingface.co/settings/tokens (read access), or `HF_TOKEN`, raises the daily GPU allowance; TRELLIS.2 needs one |
| **fal.ai** | Hosted reconstruction models, billed per generation: TRELLIS.2 (Microsoft), Hunyuan3D 3.1 Pro/Rapid (Tencent), Meshy 6, Hyper3D Rodin 2.5 (image or text) - or any other fal model id | Key from fal.ai/dashboard/keys, or `FAL_KEY` |
| **Meshy** | Meshy's own API (`meshy-7.1` by default) | Key from meshy.ai, or `MESHY_API_KEY` |
| **Hyper3D Rodin (direct)** | Hyper3D's own API: Rodin Gen-2.5, image or text, in five tiers (Extreme Low to Extreme High), billed to your Hyper3D account | Your key from hyper3d.ai (API dashboard), saved in the system keychain; no environment variable. See [ai-services.md](ai-services.md#paid-services) |
| **Tripo (direct)** | Tripo's own API: v3.1 / v3.0, image or text, the shape only (about $0.20 a picture, $0.10 a text), billed to your Tripo account | Your key from developers.tripo3d.ai, saved in the system keychain; no environment variable. See [ai-services.md](ai-services.md#tripo-and-stability-ai-direct) |
| **ComfyUI** | Any workflow on your own ComfyUI, on this computer or a studio GPU server: Hunyuan3D, TRELLIS, your own graph. See [comfyui.md](comfyui.md) | The server address and an API-format workflow (bundled ones included) |
| **My generator command** | Anything you run locally (see below) | A command line |
| **Offline blockout** | Inflates the outline into a rounded blob. Fine for simple shapes, not characters | Nothing |

**About the free option:** these are the model authors' own demo apps. They're free,
but GPU time is rationed per user per day, there can be a queue at busy times,
and an app can be paused or changed by its owner. SculptForge shows queue
position and progress, and tells you when the allowance runs out. Typical times
are about 6 s (Turbo, Stable Fast 3D) and 20 s (Hunyuan3D 2.1). For guaranteed
capacity, use fal.ai or Meshy, or run a model on your own GPU.

For the hosted services, SculptForge crops the subject out of a transparent
background, centres it on a square and scales it to at most 1024 px, then sends
it to the service (with your key or token, if any). It polls until the model is ready,
downloads the GLB, and by default voxel-remeshes it into a watertight surface
you can sculpt straight away. Nothing leaves your machine unless you pick a
hosted service and press **Generate**. Keys are stored in your local `ai.json`
(owner-only permissions) or read from the environment.

Tips for good results: one character or object per image, a plain or
transparent background, the whole subject in frame, and a front or
three-quarter view. A clean concept-art turnaround pose works better than a
busy illustration.

From the command line or an MCP agent:

```sh
sculptforge-cli -c 'image_to_3d path=hero.png; export_glb path=hero.glb'                    # free (Hunyuan3D 2.1)
HF_TOKEN=... sculptforge-cli -c 'image_to_3d path=hero.png model=trellis-2; export_glb path=hero.glb'
FAL_KEY=... sculptforge-cli -c 'image_to_3d path=hero.png service=fal model=fal-ai/trellis-2; export_glb path=hero.glb'
FAL_KEY=... sculptforge-cli -c 'text_to_3d prompt="a dragon head" service=fal model=fal-ai/hyper3d/rodin/v2.5; export_glb path=dragon.glb'
MESHY_API_KEY=... sculptforge-cli -c 'image_to_3d path=hero.png service=meshy remesh=false; export_usd path=hero.usdz'
```

## Your own generator

The **My generator command** option runs a plain command line, so it works
with local open-source models on your own GPU, a studio's internal service, or
a script that calls any cloud API.

## The contract

- `{input}` is replaced with the image path, `{output}` with the path the
  generator must write. Both are shell-quoted for you, so don't add quotes.
- Write a `.glb` (preferred) or `.obj` to `{output}` and exit with status 0.
- SculptForge imports the result, welds UV seams so it can be sculpted, and
  turns any glTF morph targets into sculpt layers.

The pre-filled command runs SculptForge's own silhouette inflation, which is a
quick way to check the pipeline works:

```
sculptforge-cli --set input={input} --set output={output} -c 'image_inflate path=$input subdivisions=2; export_glb path=$output'
```

## Wiring up a model

Open-source image-to-3D models such as TRELLIS.2, Hunyuan3D, TripoSR and
Stable Fast 3D ship their own inference scripts (they need a CUDA GPU). Wrap whichever you use in a
small script that takes an input image and an output path, then point
SculptForge at it:

```
python3 /path/to/my_generator.py {input} {output}
```

```python
# my_generator.py - adapt to the model you use
import sys
image_path, output_path = sys.argv[1], sys.argv[2]
# 1. load your model (cache it between runs if startup is slow)
# 2. run it on image_path
# 3. export the mesh to output_path as .glb
```

Tips:

- Generated meshes are often dense, uneven and full of small holes. **Mesh →
  Voxel Remesh** turns them into one clean watertight surface.
- For cloud services, keep API keys in environment variables your script
  reads. Don't put them in the command line.
