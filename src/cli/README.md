# src/cli

`sculptforge-cli`: the headless command-line tool, and the MCP (Model Context Protocol) server inside it. It links
`sculptforge_core`, `sculptforge_ai` and the `sculptforge_mcp` library built from this folder plus `src/link/AiLink*`
and `src/link/MayaLink`. No window, no GPU.

## sculptforge-cli

```sh
sculptforge-cli -c 'open_obj path=rock.obj; export_lods path=out/rock.glb levels=4 ratio=0.5'
sculptforge-cli -f job.txt --set NAME=rock     # a script file; $NAME is replaced
sculptforge-cli --list                         # every command with its parameters
sculptforge-cli --schema                       # the same as LLM tool definitions (JSON)
sculptforge-cli --help                         # every option, benchmark flags included
```

The command language is `src/core/Script.h`; the commands are the registry in `src/core/Commands.cpp`, the same ones
the app, Python and the MCP server use. `main.cpp` parses the options; `--no-ai`, `--no-packs`, `--studio FILE` and
`--offline` give the safe and locked-down modes studios use ([docs/studios.md](../../docs/studios.md)).

## The MCP server

`sculptforge-cli --mcp` speaks newline-delimited JSON-RPC 2.0 on **stdin/stdout only**. The AI tool starts it as a
child process; it opens no port and no socket.

```sh
claude mcp add sculptforge -- sculptforge-cli --mcp     # Claude Code; Help > Connect an AI tool... in the app shows the command for any client
```

| File | Role |
|---|---|
| `McpServer` | The protocol: `initialize`, `tools/list`, `tools/call`; every registry command is one tool, with a JSON schema made from its typed parameters |
| `McpTools` | Tools and MCP prompts beyond the registry: `viewport_image` (the headless server draws the visible Parts on the CPU), `view_closeup`, and the recipes (`prompts/list`) that walk an agent through a whole job |
| `McpBatch` | Transactions: `batch` (several commands as one undo step, all rolled back if one fails) and `checkpoint_save`, `checkpoint_restore`, `checkpoint_list` |
| `McpScene` | What an agent reads without parsing text: `structuredContent` (triangles, vertices, bounds, Parts, undo depth, time) on every result, and read-only resources for the scene, Parts and undo history |
| `McpAttach` | `--mcp --attach`: the same tool set, but driving the open app (live mode) through `src/link/AiLink` instead of a scene of its own |

### Safety model

- **Same commands, same undo.** Agents get no private door: each call goes through the command registry as a user's
  would, and is one undo step (or part of a `batch`).
- **Not everything is exposed.** `pack_install`, `pack_remove`, `pack_enable`, `pack_disable` and every `plugin_*`
  command are hidden from agents (`hiddenFromAgents` in `McpServer.cpp`): installing or running code is the user's
  decision. Python scripts cannot be started by an AI tool either (see the security guide below).
- **Policy.** A studio's policy file ([ManagedPolicy](../core/ManagedPolicy.h)) can allow only some kinds of tool
  (read, edit, files, scripts, network, AI), forbid live mode (`"mcp": {"live": "never"}`) or MCP altogether. Tools it
  forbids are neither listed nor run. `--no-ai` leaves the AI commands out.
- **Audit log.** Every call, refused ones too, is a line in the local audit log ([AuditLog](../core/AuditLog.h)): tool,
  command, arguments, result, time. Never artwork, never secrets, long text is cut. With `audit.required` a call that
  cannot be logged is refused before it runs.
- **Live mode** is asked for in the app (Preferences > AI), and the app asks again before anything outside the project
  folder, overwrites, scripts or network. The channel is a Unix socket (mode 0600, in a folder only the user can enter)
  or a Windows named pipe with a current-user-only ACL, and the peer must be the same user: [src/link](../link/README.md).

See also [docs/guide/security.md](../../docs/guide/security.md) and [docs/guide/scripting.md](../../docs/guide/scripting.md).

## Adding a tool

Add a command to the registry (see [src/core](../core/README.md)); it shows up in the CLI, the MCP list, Python
and the schema without more work. Only edit `Mcp*.cpp` for something that is not a command (a prompt, a resource, a
view). Give every parameter a description: an agent chooses by reading it.

## Test it

The MCP tests are `tests/test_mcp_batch.cpp`, `test_mcp_progress.cpp`, `test_mcp_structured.cpp`, `test_ai_link.cpp`
(live mode's channel) and `test_studio_policy.cpp`; the live-mode flow is `tests/ui/ai_live.sfui`. Run them by name:
see [tests/README.md](../../tests/README.md). `bench/mcp-detail` measures how far an agent gets with fine detail over MCP.
