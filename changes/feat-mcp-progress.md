---
whats_new: **AI agents see progress and can cancel**: long steps (remesh, subdivide, decimate, LOD export, 3D generation) report how far they are, and an agent can stop one; the scene stays as it was.
added: **MCP progress and cancel** (stdio server): a call with `_meta.progressToken` gets `notifications/progress` (at most 10 a second) from remesh, subdivide, decimate, export_lods, image_to_3d and text_to_3d, and from each command of a batch; `notifications/cancelled` stops the request in flight at its next cancel point, leaving the scene as before (no undo step; a cancelled batch rolls back whole), and the server keeps reading while a call runs, so `ping` is answered meanwhile. micro_divide and subdiv_divide have no cancel point and say so in their descriptions.
testers:
  - Any OS: ask an AI tool (Claude Desktop, Claude Code) to remesh a dense model at a high resolution, then stop it; the client shows progress and the model is unchanged.
---
Reviewers: src/core/OpControl.h (ambient progress + cancel per thread), runMcpServer in src/cli/McpServer.cpp (reader thread + one worker,
replies under one lock), tests/test_mcp_progress.cpp.
