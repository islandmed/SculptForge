# MCP detail benchmark

Does an AI agent driving SculptForge over MCP reach fine detail (pores, wrinkles, creases, scales), or stop at
blocking? One fixed prompt (`prompt.txt`, an orc head), run through `sculptforge-cli --mcp --attach` against the app
running offscreen, judged by pictures and by numbers.

- `run.sh <label> <model> <build dir>`: one run. The app starts with `live.sfui.in`, which turns live mode on and
  clicks Approve. The agent (Claude Code in print mode, sculptforge tools only) gets the prompt, at most `CAP` tool
  calls (80) and `MINUTES` minutes (20). After it, a second connection takes the same three final pictures
  (front, three-quarter, a 2.2x close-up) and the UI script saves the mesh as `final.obj`. Output goes to
  `~/Work/.sf/mcp-detail/runs/<label>/`.
- `proxy.py`: sits between the agent and the CLI. It logs every tool call (`calls.jsonl`: arguments, result text,
  pictures as files) and enforces the cap.
- `metric.py`: the geometric detail of a mesh, scale-free. `hf` is high-frequency energy: the RMS distance along the
  normal between the surface and a Taubin-smoothed copy, as a fraction of the size. `curv_var` is the variance of
  the mean curvature. A blockout scores near 0 on both; pores, wrinkles and creases raise both.
- `report.py`: contact sheets, the metric per run and what each agent did, written to `results/`.

Our own runs only. Agents: Claude Code with Sonnet and with Haiku (Haiku stands in for a smaller open model).
Codex wasn't logged in on the bench machine and no local open model was installed, so neither ran.

## Results (11-12, Claude Code: Sonnet and Haiku, 2 runs each, before and after the detail tools)

`results/before.jpg` and `results/after.jpg`: front, three-quarter and close-up of each run's final model.
`results/results.md`: the numbers. `results/runs/`: every tool call of every run.

Before: all four runs used up the 80 calls in about three minutes. They added detail at 0.1-1M triangles, one
hand-placed stroke per call, and never looked closer than the whole model. The forms read well, the surfaces are soft.

After: all four runs looked with view_closeup (3-8 calls each) and three used detail_pass (4-11 calls; Sonnet's
first run none). Sonnet's second run shows pores, skin
texture and forehead wrinkles over the whole head. Haiku's runs show forehead and cheek lines; after-haiku-1 also lost the scar lines its before-run had. Sonnet's first run spent
its calls rebuilding the block-out and never got to detail, which is why the instructions now say to keep a third of
the steps for detail.

The numbers back the pictures only in part. `hf` and `curv_var` rise with real detail, but also with faceting and the
mesh resolution: before-haiku-1 scores high from low-resolution faceting, and before-haiku-2's micro noise was baked
at 4M vertices. Judge by the close-ups first. A metric at a fixed world scale (resampled) would separate these.
