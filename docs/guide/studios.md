# SculptForge for studios

SculptForge is built to drop into animation, VFX and game studios that run their own pipelines: native on
Linux, scriptable, standards-based, with no licence server and nothing that phones home. This chapter is for
pipeline TDs and IT: the VFX Reference Platform builds, Rez deployment, shared brush libraries, environment
variables, headless use, licensing and [IT deployment](#it-deployment) (a managed policy, the AI audit log, single
sign-on, AI gateways and air-gapped use).

## VFX Reference Platform builds

SculptForge tracks the [VFX Reference Platform](https://vfxplatform.com). Every commit is built and tested in
the platform's reference environments:

| | CY2026 | CY2027 | SculptForge |
|---|---|---|---|
| Linux compiler | GCC 14.2 | GCC 14.2 | Built and tested with `gcc-toolset-14` |
| glibc | 2.28 (EL8) | 2.34 (EL9) | Rocky Linux 8 and 9 builds; CI checks that no newer glibc symbols are used |
| C++ | C++20 | C++20 | C++20 |
| Python | 3.13 | 3.13 | The `sculptforge` module is built and tested against 3.13 |
| Windows | VS 2022 17.6+ | VS build tools 14.44+ | Visual Studio 2022 |
| macOS minimum | 14.0 | 14.0 | Universal binary with deployment target 11.0 |
| FBX | 2020.x | 2020.x | Writes FBX 7.4 binary, which FBX SDK 2020 reads |

SculptForge doesn't depend on Qt, OpenEXR, Boost, TBB or other platform libraries, so it can't conflict with
the versions your other tools load. Its few dependencies (SDL3, Dear ImGui, GLM, ufbx) are linked into the
binaries.

Each release includes `SculptForge-<version>-linux-<arch>-vfx2026.tar.gz` (Rocky 8, glibc 2.28) and
`...-vfx2027.tar.gz` (Rocky 9, glibc 2.34), for x86_64 and ARM64, with the Python module under
`lib/sculptforge/python`.

## Deploy with Rez

`packaging/rez/package.py` in the source tree builds and installs a Rez package:

```sh
cd packaging/rez
SCULPTFORGE_PYTHON=$(which python3.13) rez-build --install     # or --release for your release repository
rez-env sculptforge -- sculptforge                              # the app
rez-env sculptforge -- sculptforge-cli --list                   # every pipeline command
rez-env sculptforge python-3.13 -- python -c "import sculptforge"
```

The package puts `bin/` on `PATH` and the Python module on `PYTHONPATH`, and sets `SCULPTFORGE_ROOT`. The
Python module is built for the first `python3` found unless `SCULPTFORGE_PYTHON` names an interpreter.

## Studio brush libraries

Each artist's own brushes live in their user brush library, one `.sfbrush` file per brush (see
[Brush Lab and sharing brushes](brush-lab.md)). To give a whole team the same brushes, put `.sfbrush` files on
a share and list it in `SCULPTFORGE_BRUSH_PATH`:

```sh
export SCULPTFORGE_BRUSH_PATH=/studio/brushes:/show/troll/brushes    # ';' between folders on Windows
sculptforge-cli -c 'brush_library'      # every brush, where it comes from and its file, plus the folders
```

- The app, `sculptforge-cli` and the Python module load these folders at start-up, **read-only**: SculptForge
  never writes into them, so the share can be mounted read-only.
- Studio brushes show a `studio` tag in the Custom brushes list.
- When brushes share a name, the artist's own wins over a studio one, which wins over a built-in. Among studio
  folders, the first one listed wins.
- Refining a studio brush in Brush Lab saves the artist's own version (tagged `yours`) without touching the
  share. Deleting that version brings the studio brush back.
- To publish a brush, copy its `.sfbrush` file into the share: right-click it in the Custom brushes list >
  **Save .sfbrush...**, or take it from an artist's user library.

With Rez, set the share once for everyone: when `SCULPTFORGE_STUDIO_BRUSHES` is set as the package resolves
(in a site package, your rezconfig or the launcher), it is appended to `SCULPTFORGE_BRUSH_PATH`.

```sh
export SCULPTFORGE_STUDIO_BRUSHES=/studio/brushes
rez-env sculptforge -- sculptforge
```

## Environment variables

| Variable | Effect |
|---|---|
| `SF_AI` | `off`: every AI feature off and locked (app, `sculptforge-cli`, MCP, Python); `on`: on whatever was saved. `--no-ai` does the same as `off` for one start |
| `SF_STUDIO_PROFILE` | A studio profile (`studio.json`) to start in studio mode with, like `--studio` (see [Your studio build](#your-studio-build)) |
| `SF_OFFLINE` | `1`: studio mode with nothing allowed, like `--offline` |
| `SF_POLICY` | A policy file, when there is no system-wide one (see [The policy file](#the-policy-file)) |
| `SF_KEYCHAIN` | `none`: don't use the OS keychain (render nodes, CI); keys last for the session or come from the environment |
| `SCULPTFORGE_BRUSH_PATH` | Read-only studio brush folders (`:`-separated, `;` on Windows) |
| `SCULPTFORGE_STUDIO_BRUSHES` | With the Rez package: appended to `SCULPTFORGE_BRUSH_PATH` |
| `SCULPTFORGE_BRUSH_GALLERY` | Use another community brush gallery index (a URL or a folder), for example an in-house mirror |
| `HF_TOKEN` (or `HUGGING_FACE_HUB_TOKEN`) | Hugging Face token for the free image-to-3D models |
| `FAL_KEY`, `MESHY_API_KEY` | Keys for the paid generation services |
| `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`, `OPENROUTER_API_KEY` | Keys for Brush Lab providers |
| `XDG_DATA_HOME` (Linux), `APPDATA` (Windows) | Move the settings folder (see [where settings live](troubleshooting.md#where-settings-live)) |

Keys from the environment are used when none is saved in the app (keys saved in the app live in the OS keychain), so
a launcher can supply them without storing them for each artist.

## Headless and farm use

- `sculptforge-cli` runs every editing operation without a display: imports, remeshing, decimation, LOD
  chains, FBX, USD and glTF export, splat conversion and AI generation. See
  [Scripting and automation](scripting.md#sculptforge-cli).
- `sculptforge --render out.png --turntable 8` renders review images and turntables offscreen. It needs an
  OpenGL 3.3 context; on a headless Linux node, run it under Xvfb.
- `sculptforge-cli --mcp` exposes the same operations to AI agents through the Model Context Protocol.
- `import sculptforge` gives Python the same commands plus NumPy arrays of the mesh, for your own tools.

A typical farm job turns incoming scans into game-ready assets:

```sh
sculptforge-cli --set in=/ingest/scan_042.ply --set usd=/assets/scan_042.usda --set glb=/assets/scan_042.glb -c '
  open path=$in
  splat_transform rotate_x=180 fit=true
  splat_clean
  splat_to_mesh detail=320 preset=game color=texture
  export_usd path=$usd
  export_lods path=$glb levels=4 ratio=0.5
'
```

A `--set` variable replaces a whole value, so pass complete paths (with their extensions) as variables.

## Formats in a pipeline

| Format | Read | Write | Notes |
|---|---|---|---|
| OpenUSD (`.usda`, `.usdz`) | Yes | Yes | Meshes, xform ops, Z-up stages, UsdSkel blend shapes, ParticleField Gaussian splats, textures; checked with OpenUSD's validators |
| FBX | Yes (any version) | Yes (7.4 binary) | LOD groups, blend shapes, centimetre units, textures; checked with ufbx's strict mode |
| glTF / GLB | Yes | Yes | Morph targets, vertex colours, `KHR_gaussian_splatting`, embedded textures; checked with the Khronos validator |
| OBJ | Yes | Yes | Objects as Parts, groups as face groups, vertex colours, textures through `.mtl` |
| PLY | Yes | Yes | Meshes with vertex colours, Gaussian splats |
| STL | Yes (binary and ASCII) | Yes (binary) | |
| Bridge | Yes | Yes | Face Groups, mask, vertex paint, UVs |
| SculptForge project (`.sfproj`) | Yes | Yes | Everything; a documented ZIP of JSON and binary arrays |

Binary `.usdc` files and USD references or payloads aren't read yet; convert with `usdcat in.usd -o out.usda`.
The project format is described in `docs/project-format.md` in the source tree, so pipeline tools can read it
without SculptForge.

## Licensing

SculptForge is free software under the GNU General Public License, version 3 or later. In practice:

- **Using it is unrestricted**, commercially or otherwise, on any number of machines. There are no seats,
  licence servers or accounts.
- **Your artwork belongs to you.** The GPL covers the program, not the models you make with it.
- **You can modify it in-house.** The GPL only requires you to share source if you *distribute* the modified
  program outside your organisation. Internal use, including across your own facilities and farm, is generally
  not considered distribution. (This is the common reading of the GPL, not legal advice; check with your
  counsel.)
- SculptForge has no telemetry and no servers of its own. AI features are optional and talk only to the
  service each artist chooses, or to your own ComfyUI server.

## Studio membership

SculptForge is free for any number of artists, and nothing in the app is ever paywalled. A Studio membership is
$300 a month for up to 10 artists, plus $25 a month for each extra artist. The first 10 studios become Founding
Studios at $150 a month, locked for as long as they stay subscribed. Each artist gets everything in Member, and the
studio gets:

- **Its own build** with its brushes, alphas, Lit Spheres, keymaps, macros, preferences, paths and logo built in.
- **Isolation:** a private release channel, and no outside connections by default (see
  [Offline deployment](#offline-deployment)).
- **A pinned version:** you choose when to move. Security fixes are backported to the current release and one
  long-term support (LTS) line, and your studio is always on one of those two.
- **An offline signed installer** with checksums for IT.
- **Support:** a first response within 2 business days (a response, not a fix), up to 3 custom items a month
  (bigger features are quoted separately), your requests first on the roadmap, and a private Discord channel.

Every release stays free for everyone; bug-fix and security releases go out to everyone the same day. Join at
[sculptforge.app/studios.html](https://sculptforge.app/studios.html#membership) or write to hello@sculptforge.app.

**What stays private, and what doesn't.** SculptForge is GPL-3.0, and so is your studio build: its source comes
with it and is yours to share. There are no exclusive or secret features. What stays private is yours: the
brushes, alphas, settings and files you send are never put in public builds or packs, and your build never reaches
out of your network.

## Your studio build

Your build is SculptForge with a **studio profile** baked in: one `studio.json` and a folder beside it.

```json
{
  "format": "sculptforge-studio", "version": 1,
  "id": "acme", "name": "Acme Animation",
  "updates": {"feed": "https://sw.acme.internal/sculptforge/latest.json", "mirror": "/mnt/sw/sculptforge/latest.json"},
  "allow": [],
  "localNetwork": false,
  "ai": false,
  "packs": ["packs/acme.sfpack"],
  "brushPaths": ["/studio/brushes"],
  "preferences": "ui.json",
  "branding": {"logo": "logo.png", "title": "Acme"}
}
```

| Key | What it does |
|---|---|
| `updates.feed`, `updates.mirror` | Where the build looks for updates: your private channel, then your mirror (a URL or a file on a share) |
| `allow` | More URLs the build may reach, such as your own ComfyUI server (empty by default) |
| `localNetwork` | `true` lets the Unreal link and phone capture use your studio network |
| `ai` | `true` keeps the AI features; they can still reach only the URLs above |
| `packs` | Your brushes, alphas, Lit Spheres, keymaps and macros as `.sfpack` files, installed at first start |
| `brushPaths` | Read-only brush shares, as `SCULPTFORGE_BRUSH_PATH` |
| `preferences` | The first-start interface settings: layout, shortcuts and UI scale (**Edit > Preferences > Interface** exports them) |
| `branding` | Your logo in **Help > About** and a word in the title bar |

Paths are relative to the profile's folder, and the profile is data only: nothing in it runs. An unknown key is an
error, so a typo never quietly drops a setting.

To request your build, send in one email or a message in your team channel:

- **What to bake in:** your exported keymap and layout, brush presets and packs (`.sfbrush`, `.sfpack`), and the
  pipeline defaults you want.
- **References for custom content:** images or scans you own, with a line on what each item should do.
- **Where it runs:** platforms and, for Linux, the distribution or VFX Reference Platform year.
- **Where updates come from:** your channel or mirror URL, or a folder on a share.
- **Who gets access:** the GitHub usernames for your private release channel.

Files are treated as data only: scripts and executables you send are never run. Your build is made by the same
pipeline and tests as public builds and published in your private channel, signed, with its checksums and source.

To try a profile on a public build first, start it with `sculptforge --studio /path/to/studio.json` (or set
`SF_STUDIO_PROFILE`); `sculptforge-cli` and the Python module take the same profile.

## Offline deployment

A studio build is in **studio mode**, whatever happens to its files:

- **No outside connections.** No update check against the public website, no AI services, no community brush
  gallery, no Discord presence; SculptForge has no telemetry. Every request goes through one gate that allows only
  your update channel, your mirror and your `allow` list, refuses everything else before it is sent, and never
  follows a redirect. Network sockets open only on this computer (loopback) unless `localNetwork` is `true`.
- **Fails closed.** If the profile is missing or broken, a studio build still starts offline, with nothing allowed.
- **Updates on your schedule.** The build reads its feed only from your channel or mirror. Copy a new release's
  `latest.json` to the mirror when you're ready to move; until then nobody is offered it.

To install on machines without internet access:

1. Download the release from your private channel: the installer or archive for each platform,
   `SHA256SUMS.txt` and `SHA256SUMS.txt.sig`.
2. Check the files: `sha256sum -c SHA256SUMS.txt --ignore-missing` (Windows:
   `Get-FileHash <file> -Algorithm SHA256`), and the signature with the public key we send you:
   `openssl pkeyutl -verify -pubin -inkey sculptforge-release.pem -rawin -in SHA256SUMS.txt -sigfile SHA256SUMS.txt.sig`.
3. Deploy as usual: the Windows installer or portable zip, the `.rpm`/`.deb`, the tar.gz or the Rez package.
4. To test studio mode on any machine, `sculptforge --offline` turns it on with nothing allowed; `SF_OFFLINE=1`
   does the same for the CLI and Python.

## IT deployment

Everything an IT team needs to roll SculptForge out under its own rules: a policy file that locks settings, an
audit log of what AI did, the studio's own AI gateway and single sign-on, and offline use. Nothing here needs a
studio build; it works on the public one. For a security review, see the [security overview](security.md).

### The policy file

Put a `policy.json` where only administrators can write it:

| OS | Path |
|---|---|
| Linux | `/etc/sculptforge/policy.json` |
| macOS | `/Library/Application Support/SculptForge/policy.json` |
| Windows | `C:\ProgramData\SculptForge\policy.json` (deploy it with Group Policy, Intune, SCCM or your usual tool) |

SculptForge reads it at every start (the app, `sculptforge-cli`, MCP and the Python module). Each setting it locks
is greyed out in **Edit > Preferences** with **Managed by your studio** next to it. Without a system file,
`SF_POLICY=/path/policy.json` names one, for testing a policy or setting it from a launcher or a Rez package; while
the system file exists, `SF_POLICY` is ignored, so an artist can't loosen it.

```json
{
  "format": "sculptforge-policy", "version": 1,
  "organization": "Acme Animation",
  "ai": {"enabled": true, "providers": ["litellm", "comfyui"],
         "endpoints": ["https://llm.acme.internal/", "http://comfy.acme.internal:8188/"], "requireSignIn": true},
  "network": {"enabled": false, "allow": ["https://sw.acme.internal/"], "localNetwork": false},
  "mcp": {"enabled": true, "live": "never", "categories": ["info", "edit", "files"]},
  "audit": {"path": "//fs01/audit/sculptforge/{user}@{host}.jsonl", "maxMB": 20, "keep": 10, "required": true},
  "signIn": {"issuer": "https://acme.okta.com/oauth2/default", "clientId": "0oa1example",
             "useFor": ["https://llm.acme.internal/"]},
  "telemetry": "none",
  "updates": {"check": true, "channel": "stable", "feed": "https://sw.acme.internal/sculptforge/latest.json"}
}
```

Every key is optional: what the policy leaves out stays the artist's choice. An unknown key or a wrong value is an
error, so a typo never quietly drops a lock, and **a broken or unreadable policy fails closed**: AI, network
features and AI tools stay off until it is fixed. `sculptforge-cli --policy` shows what the policy manages (exit
code 2 when it is broken); use it to check a file before you deploy it.

| Key | What it does |
|---|---|
| `organization` | Shown as "Managed by Acme Animation" |
| `ai.enabled` | `false`: every AI feature off and locked (as `SF_AI=off`). `true`: on and locked. Left out: the artist's choice |
| `ai.providers` | The only AI providers artists can pick: `free`, `anthropic`, `openai`, `ollama`, `lmstudio`, `vllm`, `llamacpp`, `litellm`, `openrouter`, `openai-compatible` (Brush Lab) and `huggingface`, `fal`, `meshy`, `comfyui`, `command` (Generate 3D) |
| `ai.endpoints` | The only addresses AI requests may go to (URL prefixes, matched at a `/`) |
| `ai.requireSignIn` | AI works only while the artist is signed in with the studio account (needs `signIn`) |
| `network.enabled` | `false`: studio mode (see [Offline deployment](#offline-deployment)): no outside connection except `network.allow`, `ai.endpoints`, the identity provider and your update feed |
| `network.allow` | More URL prefixes that stay reachable while the network is off |
| `network.localNetwork` | `true`: the Unreal link and phone capture may use the studio network while the network is off |
| `mcp.enabled` | `false`: no AI tool can connect (`sculptforge-cli --mcp` refuses to start, and **Help > Connect an AI tool** is gone) |
| `mcp.live` | `"never"`: AI tools work only on their own scene and can never control an open SculptForge window (the window never listens for them; **Let AI tools control this window** is locked off) |
| `mcp.categories` | The kinds of command AI tools may use: `info` (read the scene), `edit` (change it in memory), `files` (open, save, export, bridges), `scripts` (macros), `network` (brush gallery, Unreal), `ai` (AI generation). Left out: all |
| `audit.path`, `audit.maxMB`, `audit.keep` | Where the [audit log](#audit-log) goes, when it rotates (10 MB) and how many old files it keeps (5) |
| `audit.required` | `true`: an AI action whose audit line can't be written (the share is down) is refused |
| `signIn.issuer`, `signIn.clientId` | Your identity provider and the app registration (see [Single sign-on](#single-sign-on)) |
| `signIn.clientSecret` | Only for providers that issue one to installed apps and require it (Google); it isn't secret there, and artists can read the policy |
| `signIn.scopes`, `signIn.audience` | Scopes to ask for (default `openid profile email offline_access`); an audience, if your provider needs one (Auth0) |
| `signIn.useFor` | Addresses that get the sign-in as a bearer token (default: `ai.endpoints`) |
| `telemetry` | Only `"none"`: SculptForge has no telemetry. The key is there so your checklist can state it |
| `updates.check` | `false`: no update check, and the artist can't turn it on |
| `updates.channel` | `"stable"` or `"preview"`, locked |
| `updates.feed`, `updates.mirror` | Your own update feed (a URL) and mirror (a URL or a file on a share) instead of the public one |

The policy works alongside a [studio profile](#your-studio-build): a profile bakes in content and branding, the
policy locks settings. When both turn the network off, both allow lists apply.

### Audit log

Every AI action is one line of JSON in a local file: each command an AI tool (MCP) runs, refused ones included (`"tool":
"mcp"`, or `"mcp-live"` when it controls an open window), and every Brush Lab and Generate 3D request (`"ai"`), from
the app, `sculptforge-cli` and Python.

```json
{"time":"2026-11-05T14:03:22.120Z","user":"ana","host":"ws042","client":"mcp:claude-code 2.1.0","tool":"mcp",
 "command":"export_glb","args":{"path":"/show/troll/assets/rock.glb"},"result":"ok","message":"Exported","ms":930}
```

- **What is never written:** artwork (meshes, images, textures), keys and tokens (any argument named like a key,
  token, password or secret is `<redacted>`), and long text (strings are cut to 120 characters; a long prompt is
  recorded as its length). File paths are kept: they say what was read or written.
- **Where:** `audit.path` from the policy, with `{user}`, `{host}` and `{date}` filled in, so each machine can write
  its own file on a share. Without a policy, `audit/ai-audit.jsonl` in the [settings
  folder](troubleshooting.md#where-settings-live). **Edit > Preferences > AI** shows the path;
  `sculptforge-cli --audit-log` prints it.
- **Rotation:** past `audit.maxMB` the file becomes `.1`, `.1` becomes `.2`, and so on up to `audit.keep`.
- Nothing is sent anywhere: collect the files with your own tools (any log shipper reads JSON lines).

### AI providers

**Edit > Preferences > AI > Provider** has presets that fill in the address and a model, so artists pick a name
instead of typing URLs:

| Preset | Address | Key |
|---|---|---|
| Anthropic (Claude) | `https://api.anthropic.com/v1/messages` | The artist's or the studio's API key |
| OpenAI | `https://api.openai.com/v1/chat/completions` | API key |
| Ollama | `http://localhost:11434/v1/chat/completions` | None |
| LM Studio | `http://localhost:1234/v1/chat/completions` | None |
| vLLM server | `http://localhost:8000/v1/chat/completions` (or your GPU server) | Optional |
| llama.cpp server | `http://localhost:8080/v1/chat/completions` (or your server) | None |
| LiteLLM proxy (studio gateway) | `http://localhost:4000/v1/chat/completions` (change it to your gateway) | A virtual key, or the studio sign-in |
| OpenRouter | `https://openrouter.ai/api/v1/chat/completions` | API key |

**Amazon Bedrock, Google Vertex AI and Azure OpenAI** go through a [LiteLLM proxy](https://docs.litellm.ai/docs/simple_proxy)
your studio runs: the proxy holds the cloud credentials, applies your budgets and model list, and SculptForge talks
only to it. A minimal proxy configuration:

```yaml
model_list:
  - model_name: claude
    litellm_params: {model: "bedrock/<Bedrock model id>", aws_region_name: us-west-2}
  - model_name: gemini
    litellm_params: {model: "vertex_ai/<Vertex model id>", vertex_project: acme-render, vertex_location: us-central1}
  - model_name: gpt
    litellm_params: {model: "azure/<your deployment>", api_base: "https://acme.openai.azure.com", api_version: "<API version>"}
```

Point the preset at `https://llm.acme.internal/v1/chat/completions`, list that prefix in `ai.endpoints`, allow only
`litellm` in `ai.providers`, and artists can reach no other AI service. To validate the studio sign-in at the
gateway, use LiteLLM's JWT authentication with your identity provider's keys.

**Keys are kept in the operating system's keychain**: macOS Keychain, Windows Credential Manager, or the Secret
Service on Linux (GNOME Keyring, KWallet, KeePassXC). They are never written to a settings file; keys from older
versions move into the keychain at the first start. Where there is no keychain (`SF_KEYCHAIN=none`, a render node),
a key typed in lasts until SculptForge quits; the environment variables `ANTHROPIC_API_KEY`, `OPENAI_API_KEY`,
`OPENROUTER_API_KEY`, `HF_TOKEN`, `FAL_KEY` and `MESHY_API_KEY` work everywhere.

### Single sign-on

With `signIn` in the policy, **Edit > Preferences > AI > Studio sign-in** signs the artist in with the studio
account (Okta, Microsoft Entra ID, Google Workspace, Auth0, Keycloak or any OpenID Connect provider). It uses the
**device authorization grant** only: SculptForge shows a short code and opens your provider's page, and the artist
signs in there, in their browser, with your usual login and MFA. Nothing on the artist's computer listens for the
answer: no local port, no redirect, no embedded browser. `sculptforge-cli --sign-in` does the same in a terminal.

The tokens are kept in the keychain and refreshed when they expire. The access token is sent as
`Authorization: Bearer` only to the addresses in `signIn.useFor` (default: `ai.endpoints`), such as your AI gateway;
never to anyone else. SculptForge reads the ID token's name and email only to show who is signed in: your
services verify the access token.

To set it up, register a **public client** (no client secret) with the device flow turned on:

| Provider | Where | Issuer |
|---|---|---|
| Okta | Applications > Create App Integration > OIDC > Native Application; Grant type: Device Authorization | `https://<org>.okta.com/oauth2/default` |
| Microsoft Entra ID | App registrations > New registration; Authentication > Allow public client flows: Yes | `https://login.microsoftonline.com/<tenant id>/v2.0` |
| Google Workspace | Google Cloud console > Credentials > OAuth client ID > TVs and Limited Input devices | `https://accounts.google.com` (list `https://oauth2.googleapis.com/` in `network.allow` when the network is off) |
| Auth0 | Applications > Native; Advanced settings > Grant types: Device Code | `https://<tenant>.auth0.com/` (set `signIn.audience` to your API) |
| Keycloak | Clients > Create; Client authentication: Off; OAuth 2.0 Device Authorization Grant: On | `https://<host>/realms/<realm>` |

Put the client id in `signIn.clientId`. Google's device flow also wants the client's secret, which Google documents
as not confidential for this client type: put it in `signIn.clientSecret`. Google's access tokens are meant for
Google's own APIs, so with Google Workspace have your gateway validate the ID token instead.

### Offline and air-gapped use

- `"network": {"enabled": false}` in the policy, or `sculptforge --offline` / `SF_OFFLINE=1`, turns on studio
  mode: every request goes through one gate that refuses anything not allowed before it is sent, follows no redirect,
  and sockets open only on loopback (see [Offline deployment](#offline-deployment)).
- AI can stay on in an air-gapped facility with local models: allow `ollama`, `lmstudio`, `vllm`, `llamacpp` or
  `comfyui` and list their addresses in `ai.endpoints`.
- Updates come from `updates.feed` / `updates.mirror` only; copy a release's `latest.json` there when you are ready.
- Install from the signed checksums as described under [Offline deployment](#offline-deployment).

## Not yet

Planned pipeline features, roughly in order: OpenColorIO viewport display, Alembic export, fuller USD
authoring (layers, variants, references, `.usdc`), UVs and texture baking, multiresolution sculpting. Pipeline
requests are welcome as issues on the project's GitHub page.

## See also

- [Scripting and automation](scripting.md)
- [Exporting, Unreal and Bridge](exporting.md)
- [Brush Lab and sharing brushes](brush-lab.md)
- [Image and text to 3D](generate-3d.md): running models on your own ComfyUI server
- [Troubleshooting](troubleshooting.md)
- [Security overview for IT reviews](security.md)
