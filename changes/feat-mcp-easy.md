---
whats_new: **Help > Connect an AI tool**: use SculptForge from Claude, ChatGPT (Codex CLI), Gemini (Antigravity CLI),
  Grok (Grok Build), Cursor, VS Code and more. Pick your subscription; the window shows the exact command or settings
  for your install, with Copy buttons. Claude Desktop installs it from a bundle on each release.
highlight: Connect your AI tool in one window
added: **Help > Connect an AI tool** (while AI features are on): an "I have a subscription" picker (Claude, ChatGPT,
  Gemini, Grok, other tools) and, for this install, the exact command or configuration for Claude Desktop, Claude Code,
  Codex CLI, Antigravity CLI, Gemini CLI, Grok Build, Cursor, VS Code, Visual Studio, JetBrains AI Assistant, Zed,
  Windsurf (Devin Local), GitHub Copilot CLI, Cline, Continue, Kiro, Amazon Q Developer CLI, Warp, Goose, opencode,
  Rovo Dev CLI, LM Studio, Jan and the OpenAI Agents SDK, each checked against the vendor's documentation, with Copy
  buttons. Remote-only apps (the ChatGPT and Grok apps, Rovo on the web) are named as such: SculptForge runs no network
  server.
added: A Claude Desktop bundle (`SculptForge-*.mcpb`) for Windows, macOS and Linux on every release, and `server.json`
  for the official MCP registry.
added: The MCP server (`sculptforge-cli --mcp`) gains `viewport_image` (a clay-shaded picture of the scene from any
  angle, to check each step) and recipes as MCP prompts: a creature head, a stylised bust from a reference image, a
  game-ready asset (retopology, UVs, bake, LODs), rock variations and a step-by-step lesson. Its server info carries a
  title and clearer instructions.
added: AppImage: `SculptForge.AppImage --cli ...` runs the command-line tool inside it (so MCP clients can start it).
added: Guide (Scripting > AI agents): every client's setup, your own agent, and plain function calling with
  `sculptforge-cli --schema` for the OpenAI, Gemini and xAI APIs.
testers:
  - Any OS: Help > Connect an AI tool. Each subscription shows its tools; the path under "This install" is your
    sculptforge-cli. Copy the Claude Code (or Codex) line, run it, ask the agent for "a sphere remeshed at 128,
    exported to ~/ball.glb", open ball.glb in SculptForge.
  - Windows: copy the Cursor or Claude Desktop JSON; the path has doubled backslashes and the client starts SculptForge.
  - Preferences > AI off: Help no longer lists Connect an AI tool.
  - Claude Desktop: install SculptForge-*.mcpb from the release (Settings > Extensions > Advanced settings); its tools
    and the recipes appear.
---
Reviewers: stdio only, no new listener. The window only reads SDL_GetBasePath / FLATPAK_ID / APPIMAGE to build paths.
