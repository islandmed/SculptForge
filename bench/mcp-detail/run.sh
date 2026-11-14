#!/usr/bin/env bash
# One mcp-detail benchmark run: run.sh <label> <claude model> <build dir>   (writes ~/Work/.sf/mcp-detail/runs/<label>/)
# The app runs offscreen with a UI script that approves the proxy's live-mode connection; the agent (Claude Code,
# print mode, only the sculptforge tools) gets prompt.txt, at most CAP tool calls and MINUTES minutes.
set -u
here=$(cd "$(dirname "$0")" && pwd)
label=$1 model=$2 bin=$(cd "$3" && pwd)
CAP=${CAP:-80} MINUTES=${MINUTES:-20}
out=$HOME/Work/.sf/mcp-detail/runs/$label
rm -rf "$out"; mkdir -p "$out/tmp"
sed "s#@OUT@#$out#" "$here/live.sfui.in" > "$out/live.sfui"
export SDL_VIDEO_DRIVER=offscreen TMPDIR=$out/tmp
env -u CLAUDECODE "$bin/sculptforge" --ui-test "$out/live.sfui" --ui-out "$out" > "$out/app.log" 2>&1 &
app=$!
for _ in $(seq 100); do d=$(ls -d "$out"/tmp/sculptforge-ui-test-* 2>/dev/null | head -1); [ -n "$d" ] && break; sleep 0.2; done
sleep 4
cat > "$out/mcp.json" <<J
{"mcpServers":{"sculptforge":{"command":"env","args":["SF_AI_LIVE_DIR=$d","python3","-I","$here/proxy.py","--cli","$bin/sculptforge-cli","--out","$out","--cap","$CAP"]}}}
J
cd "$out" && timeout $((MINUTES*60)) env -u CLAUDECODE claude -p "$(cat "$here/prompt.txt")" --model "$model" \
  --mcp-config "$out/mcp.json" --strict-mcp-config --tools "" --allowedTools mcp__sculptforge \
  --max-turns $((CAP+20)) --output-format stream-json --verbose > "$out/agent.jsonl" 2> "$out/agent.err"
echo "agent rc=$?" >> "$out/app.log"
sleep 3
SF_AI_LIVE_DIR=$d python3 -I "$here/proxy.py" --cli "$bin/sculptforge-cli" --out "$out" --finish < /dev/null
wait $app; echo "app rc=$?" >> "$out/app.log"
