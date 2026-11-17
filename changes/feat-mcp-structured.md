---
whats_new: **AI tools read the scene as data**: an AI agent connected to SculptForge now gets every result as structured numbers (triangles, bounds, Parts, undo depth, time) and can read the scene, its Parts and the undo history without a tool call.
highlight: AI agents read the scene as data
added: **Structured MCP results and scene resources** (`sculptforge-cli --mcp`): every `tools/call` result carries `structuredContent` (`ok`, `message`, `ms`, the active Part's `triangles`, `vertices` and `bounds`, `parts`, `activePart`, `undoDepth`, `canRedo`, scene totals) next to the text line, and `tools/list` gives every tool an `outputSchema`. Resources `sculptforge://scene`, `sculptforge://parts`, `sculptforge://part/{index}` (levels, micro level, materials) and `sculptforge://history` are read-only (no undo step), listed by `resources/list` and `resources/templates/list`. A studio policy that hides "info" tools hides the numbers and resources too, and reads go into the audit log.
testers:
  - With an MCP client on `sculptforge-cli --mcp` (Claude Code, Inspector): call new_sphere then mesh_info; the result's structured data shows the same triangle count as the text.
  - Read sculptforge://parts and sculptforge://part/0 after a subdivide; the levels list matches subdiv_info, and reading adds no undo step.
---
Stdio only: no network transport, port or IPC added. Live mode (`--attach`) forwards the app's own tools and is unchanged.
