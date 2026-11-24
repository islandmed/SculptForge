# src/ai

The optional AI features and, because it is the only place allowed to talk to the network, the HTTPS client for
everything else that does (the community brush gallery, the one-click update). It builds as `sculptforge_ai`, on top
of `sculptforge_core`. The core never links networking; this library registers its own commands (`AiCommands`) on top
of the core registry, and the app, the CLI and the MCP server add it.

What is sent where, and why nothing is uploaded without you: [docs/ai-services.md](../../docs/ai-services.md).
Running it all on your own computer: [docs/local-ai.md](../../docs/local-ai.md).

## Files

| Group | Files |
|---|---|
| Transport | `Http` (blocking HTTPS: libcurl on Linux and macOS, WinHTTP on Windows; call it from a worker thread), `GradioClient` (Hugging Face Spaces); the socket wrapper it shares with the Unreal link is `src/link/Socket` |
| Switches and consent | `AiPolicy` (the AI on/off switch, `SF_AI`, `--no-ai`, studio lock), `Consent` (the first-use notice per third-party host), `Providers` (the Brush Lab provider presets) |
| Secrets | `Secrets` (API keys live in the OS keychain through `core/Keychain`, never in settings files), `Oidc` (studio sign-in, device flow) |
| Brush Lab | `BrushAssistant`: a description in, a `BrushRecipe` out. The model only ever returns recipe data, which is checked before use |
| Generate 3D | `Generator3D` (your own command), `ImageTo3DService` (hosted models), `Hyper3D`, `Tripo`, `GlbFetch` (fetch and verify the returned GLB), `ComfyUI` (your own ComfyUI), `SpaceHealth` |
| Other | `BrushGalleryClient` (community brushes, cached for offline), `RenderBridge` (File > Render with...), `SplatTrainer` (splat training), `VideoEncoder` (frames to MP4 through ffmpeg), `Process` (a child process without a shell) |

## Rules for this folder

- **Nothing leaves the machine unless the user asked.** A new request to a host that is not this computer goes through
  `Consent`, checks `AiPolicy` and the studio's `NetworkPolicy`, and sends only what the docs table says it sends.
  Update [docs/ai-services.md](../../docs/ai-services.md) in the same change.
- **Keys stay in the keychain.** Read them through `Secrets`; never log, print or save one, and never put one in a
  command's arguments (the audit log hides arguments named like a key, but do not rely on it).
- **Treat every reply as hostile data.** Cap sizes, follow redirects only to the provider's own hosts, check a returned
  model is a well-formed GLB before importing it.
- **No shell.** External tools (ffmpeg, the splat trainer, a render helper) start through `Process` with an argument
  list, so paths need no quoting and a stranger's file name cannot become a command.
- Studio mode (`core/NetworkPolicy.h`) must keep working: an `--offline` build opens no connection at all.

## Test it

The unit cases are `tests/test_ai_consent.cpp`, `test_ai_switch.cpp`, `test_ai_hf_spaces.cpp`, `test_comfyui.cpp`,
`test_generate_job.cpp`, `test_splat_train.cpp` and `test_connect_ai.cpp`; they stand in for outside programs with
`tests/tools/fake_*` and never call the real services. See [tests/README.md](../../tests/README.md) for how to run
them. A real-service check of a ComfyUI workflow is `tools/comfyui-smoke.sh` ([tools](../../tools/README.md)).
