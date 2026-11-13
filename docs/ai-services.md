# AI services: privacy and model licences

Every AI feature in SculptForge is **optional and off until you use it**. SculptForge has no
telemetry, no account and no servers of its own. When you use an AI feature, SculptForge talks
**directly** to the service you picked; nothing goes anywhere else.

**Your art is never uploaded or used for training. AI features run only when you use them and can be turned off.** Your sculpts stay on your computer; only what you hand an AI feature (a brush description,
or the picture or text you give Generate 3D) goes to the service you picked.

## Turning AI off

**Preferences > AI > Enable AI features** (also asked in the first-run welcome). Off: Brush Lab and
Generate 3D are hidden, their commands (`brush_ai`, `image_to_3d`, `text_to_3d`, `comfyui_check`)
are not registered in the app, `sculptforge-cli`, Python or the MCP tool list, and no AI request is
made. The setting is `"enabled"` in `ai.json`. `SF_AI=off` in the environment (or `--no-ai`) turns
it off and locks it, for studios; `SF_AI=on` turns it on whatever was saved. A studio's policy file locks it either
way (see [IT deployment](guide/studios.md#it-deployment)).

## What is sent where

| Feature | Service (your choice) | What is sent | Key |
|---|---|---|---|
| Brush Lab | Free (default): Pollinations.ai's hosted open models, falling back to a running local Ollama. Or Anthropic (Claude), or any OpenAI-compatible endpoint such as a local Ollama / LM Studio | Your brush description and the current brush settings (text only) | None for Free and local models; your API key otherwise |
| Generate 3D (image) | Hugging Face Spaces (free), fal.ai, Meshy, Hyper3D, Tripo, your own ComfyUI, or your own command | The image you chose, cropped and resized to at most 1024 px | Optional HF token; your fal.ai / Meshy / Hyper3D / Tripo key; none for ComfyUI |
| Generate 3D (text) | Hugging Face Spaces (free), fal.ai, Meshy, Hyper3D, Tripo, your own ComfyUI | Your text description | As above |
| Send to Unreal | Your own Unreal Editor (local network) | The exported FBX | None |

Before the first request to a service on someone else's computer (Pollinations, each Hugging Face Space family,
fal.ai, Meshy, Hyper3D, Tripo, a remote OpenAI-compatible server or ComfyUI), the panel shows what is sent, to which host and
who pays; nothing is sent until **Allow**, which is remembered per service (`sendAllowed` in `ai.json`) and can be
revoked in **Preferences > AI > Online AI services**. Scripts, `sculptforge-cli` and AI tools get the same refusal
until it is allowed in the app. Servers on this computer never ask. A studio policy's `ai.providers` / `ai.endpoints`
pre-allow what they list and forbid the rest.

Keys are stored in your operating system's keychain (macOS Keychain, Windows Credential Manager, or the Secret
Service on Linux), never in a settings file, or read from environment variables (`ANTHROPIC_API_KEY`,
`OPENAI_API_KEY`, `OPENROUTER_API_KEY`, `HF_TOKEN`, `FAL_KEY`, `MESHY_API_KEY`; the Hyper3D and Tripo keys are keychain only, there is no environment variable for them). Keys saved by older versions in
`ai.json` move into the keychain at the first start.

**Preferences > AI > Provider** has presets for Anthropic, OpenAI, Ollama, LM Studio, vLLM, llama.cpp server,
a LiteLLM proxy (a studio's own gateway, which can front Amazon Bedrock, Google Vertex AI and Azure OpenAI) and
OpenRouter. Studios can limit which providers and addresses artists may use, require their single sign-on, and keep
an audit log of every AI action: see [IT deployment](guide/studios.md#it-deployment).

## The free Brush Lab model

The **Free** provider sends your brush request to [Pollinations.ai](https://pollinations.ai)'s
anonymous, OpenAI-compatible text endpoint (currently GPT-OSS 20B, an Apache-2.0 open model). There's
no sign-up and no key, and it only ever receives text: your description and the brush values. The
anonymous tier allows about one request every few seconds, which is plenty for a chat. If the service
can't be reached and Ollama is running on your machine, SculptForge uses your first local model
instead. For fully private use, pick **OpenAI-compatible** and point it at Ollama or LM Studio:
see [Run AI on your own computer](local-ai.md) for step-by-step setup on Windows, macOS and Linux.

## The free image-to-3D and text-to-3D models

The free option runs each model author's **own public demo** on Hugging Face's shared ZeroGPU
hardware. SculptForge isn't affiliated with Hugging Face or any of the model authors, and doesn't
host, bundle or redistribute any model. GPU time is rationed per user per day; a free Hugging Face
token raises the allowance.

**Each model has its own licence, and you're responsible for following it:**

| Model | Author / provider | Licence | Key terms (summary, not legal advice) |
|---|---|---|---|
| TRELLIS.2 | Microsoft (Space by Microsoft) | MIT | Permissive. |
| Hunyuan3D 2.1, Hunyuan3D 2, Hunyuan3D 2 mini Turbo | Tencent (Spaces by Tencent) | [Tencent Hunyuan Community License](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE) | **Not licensed for use in the European Union, the United Kingdom or South Korea.** Products over 1 million monthly active users need a separate licence from Tencent. Tencent claims no rights in your outputs. Outputs may not be used to improve other AI models. |
| Stable Fast 3D | Stability AI (Space by Stability AI) | [Stability AI Community License](https://huggingface.co/stabilityai/stable-fast-3d/blob/main/LICENSE.md) | Free, including commercial use, for individuals and organisations under US $1M annual revenue; above that, an enterprise licence is required. You own your outputs. "Powered by Stability AI". |

These summaries are for convenience only. Read the licences linked above before using outputs
commercially, and check where you are. If you're in the EU, UK or South Korea, choose TRELLIS.2,
Stable Fast 3D or a paid service instead of Hunyuan3D.

## Your own ComfyUI

With **ComfyUI - your server**, the image or description goes only to the ComfyUI address you
enter: your own computer, or a GPU server at your studio. It runs there with whatever models that
server has, and nothing is sent to anyone else. The picture is saved in ComfyUI's `input` folder and
the result in its `output` folder, as ComfyUI does with any job. The licences of the models in your
workflow apply as usual; the Hunyuan3D terms above apply to the bundled Hunyuan3D workflows too.
See [Generate 3D with your own ComfyUI](comfyui.md).

## Paid services

fal.ai, Meshy, Hyper3D and Tripo are paid services with their own terms of service and pricing; generations are
billed to your account with them. Model availability and names can change on their side.

Hyper3D Rodin 2.5 can run two ways:

- **fal.ai > Rodin 2.5**: with your fal.ai key. The image or description goes to fal.ai, which runs Rodin.
- **Hyper3D Rodin (direct)**: with your own Hyper3D key, from the API dashboard at hyper3d.ai (a Creator
  plan or higher; Basic API access covers the Extreme Low and Low tiers, the others need Full access). The
  image or description goes to Hyper3D (`api.hyper3d.com`) and nowhere else; each generation is billed to
  your Hyper3D account, and Hyper3D keeps results as its data retention policy says.

  What SculptForge does to keep this narrow:
  - It is off until you enter your own key. The key is kept in the system keychain, never in `ai.json`,
    never in a log, and there is no environment variable, shared or trial key.
  - The API address is fixed in the app: https only, certificates checked, no redirects followed. The
    key is sent only there. The model file is accepted only over https from Hyper3D's own domains
    (`hyper3d.com`, `hyper3d.ai`, `deemos.com`), up to 200 MB, and only when it is a well-formed GLB; it is
    saved in a private temporary folder, read by the usual glTF importer and deleted after import.
  - It waits politely: the first status check after 5 s, then slower, up to every 30 s, for 20 minutes at most.
  - With **Enable AI features** off nothing is sent. Studios: the provider id is `hyper3d` in
    `ai.providers` (and `https://api.hyper3d.com/` in `ai.endpoints` when that list is used); each
    generation writes one audit log line (tier, image or text, prompt length; never the key or the image).

### Tripo (direct)

One more direct provider, built the same way as Hyper3D above: off until you enter your own key (kept in the
system keychain, never in `ai.json`, never in a log, never in a project; no environment variable, no shared or trial
key), one fixed API address, https with certificates checked and no redirects followed, the key sent only there.

- **Tripo** (`openapi.tripo3d.com`, provider id `tripo`): image or text to 3D with Tripo v3.1 (or v3.0), asked for the
  shape only (no texture, the cheaper tier: about $0.20 a picture, $0.10 a description, billed to your Tripo account;
  prices are Tripo's list prices and can change). Get a key at platform.tripo3d.ai. The model file is accepted only over
  https from Tripo's own domains (`tripo3d.com`, `tripo3d.ai`), up to 200 MB, and only when it is a well-formed GLB; it
  is saved in a private temporary folder and deleted after import. If a file would come from anywhere else it is not
  downloaded, and the message gives the task id: the model stays in your Tripo account. Tripo's terms apply to your
  key and your results (paid accounts own their outputs; keys may not be shared).

With a studio policy the provider id is `tripo` in `ai.providers` (and the hosts in `ai.endpoints`);
each generation writes one audit log line (model, input kind, prompt length; never the key or the image).

## When a service is down

Every call has a timeout, the wait for a model is capped, and the progress line says what is happening. When a paid
service can't be reached, times out or is busy, SculptForge tries the free Hugging Face models **only if you have
already allowed them** (their first-use notice), and says so in the result; it never moves to another paid service,
so nothing is billed to an account you didn't choose. A rejected key, no credits or a refused picture is reported as it
is, because any other provider would say the same.

The free Spaces are run by other people and switch off, change or queue. The update check you already have (the
`latest.json` feed, once a day) carries a short health note for each Space (up, slow to start, switched off, with a
reason), written by a script that asks Hugging Face for each Space's public status; the app makes no extra request for
it and sends nothing. A Space that is switched off is greyed out in the model list with the reason, and a run moves to
the first Space that is up instead of waiting on a dead one.
