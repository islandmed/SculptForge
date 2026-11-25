---
whats_new: AI clients now see the batch and checkpoint tools only when connected to the headless server.
fixed: Live MCP tool discovery now omits `batch`, `checkpoint_save`, `checkpoint_list`, and `checkpoint_restore`
  before and after pairing. The headless MCP server continues to list all four. Fix by @surrealbydesign.
testers:
  - Request `tools/list` with `sculptforge-cli --mcp --attach` before and after pairing. Both lists should include
    `part_list` and omit the batch and checkpoint tools.
  - Request `tools/list` with `sculptforge-cli --mcp`. The batch and checkpoint tools should all be present.
---
