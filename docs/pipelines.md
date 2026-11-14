# Pipelines and automation

Back to the [README](../README.md).

```sh
# LOD chain for a game asset
sculptforge-cli -c 'open path=rock.obj; export_lods path=out/rock.glb levels=4 ratio=0.5'

# Concept art or a description -> sculptable 3D model (free, via Hugging Face)
sculptforge-cli -c 'image_to_3d path=hero.png; export_usd path=hero.usdz'
sculptforge-cli -c 'text_to_3d prompt="a stylised dragon head"; export_glb path=dragon.glb'

# Sculpt -> Unreal (static mesh with 4 LODs, 2 m tall)
sculptforge-cli -c 'open path=statue.obj; unreal_send destination=/Game/Props height_cm=200 lods=4'

# Phone capture -> watertight base mesh
sculptforge-cli -c 'open path=scan.ply; splat_transform rotate_x=180 fit=true; splat_clean; splat_to_mesh resolution=256; export_glb path=base.glb'

# Design a brush (free, no key) and save it for the team
sculptforge-cli -c 'brush_ai prompt="weathered stone edges"; brush_save path=stone.sfbrush'

# ...or with a private local model
sculptforge-cli -c 'brush_ai prompt="weathered stone edges" provider=openai-compatible model=qwen3:4b-instruct'

# Review-sheet turntable (8 frames)
sculptforge model.glb --render review/model.png --turntable 8 --size 1024x1024
```

Run `sculptforge-cli --list` for every command.

### Use with AI tools

SculptForge works from Claude, ChatGPT (Codex CLI), Gemini, Grok, Cursor, VS Code, JetBrains, Zed and other MCP clients. The AI tool starts `sculptforge-cli --mcp` on your computer; nothing listens on the network.

In the app, **Help > Connect an AI tool...** shows the exact command for your install, with Copy buttons. Claude Desktop installs it from a bundle (`SculptForge-*.mcpb` on each [release](https://github.com/islandmed/SculptForge/releases/latest)).

```sh
claude mcp add --scope user sculptforge -- /path/to/sculptforge-cli --mcp      # Claude Code
codex mcp add sculptforge -- /path/to/sculptforge-cli --mcp                     # Codex CLI (sign in with ChatGPT)
```

Every command is a tool, plus `viewport_image` (a picture of the scene) and ready-made recipes (MCP prompts). Every
client, your own agent and plain function calling (`sculptforge-cli --schema`):
[the guide](guide/scripting.md#ai-agents-mcp).
