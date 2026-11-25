---
fixed: "**AI tools (live mode)**: the tool list a paired AI tool gets no longer offers `batch` and the `checkpoint_*` tools, which only work in the headless server and answered \"Unknown tool\" when called; the list now matches what a live session can call. Reported by SurrealByDesign."
testers:
  - "Any OS: pair an MCP client with the open window (Preferences > AI > Let AI tools control this window); its tool list has part_list and no batch or checkpoint_save, and every tool listed runs."
---
A live session's tools/list came from the headless list, which includes the history tools; the window's dispatcher has no handler for them. mcpAppToolList now skips them (isHistoryTool). Test: tests/test_ai_link.cpp lists before and after pairing and checks the headless list still has all four.
