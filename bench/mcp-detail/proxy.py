#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""MCP benchmark proxy: stands between an AI agent and `sculptforge-cli --mcp --attach`.

  - connects to the window first (live_connect; the benchmark's UI script clicks Approve);
  - logs every tools/call (name, arguments, result text) to calls.jsonl, images saved beside it as step-NNN.png;
  - caps the run: after --cap tool calls every call answers "budget used up";
  - --finish (a second connection once the agent is gone): bakes a micro level down (if any, so the OBJ the UI script saves holds the detail), takes
    the final pictures (front, three_quarter, close-up) and disconnects.
Usage: proxy.py --cli PATH --out DIR [--cap 80]   (the agent's MCP server command)
       proxy.py --cli PATH --out DIR --finish < /dev/null
"""
import argparse
import base64
import json
import os
import queue
import subprocess
import sys
import threading
import time

ap = argparse.ArgumentParser()
ap.add_argument("--cli", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--cap", type=int, default=80)
ap.add_argument("--finish", action="store_true", help="no agent: only the final pictures (stdin /dev/null)")
opt = ap.parse_args()
os.makedirs(opt.out, exist_ok=True)

child = subprocess.Popen([opt.cli, "--mcp", "--attach"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
mine = queue.Queue()  # replies to the proxy's own requests (ids "bench-...")
pending = {}          # agent request id -> (name, args, t0)
log = open(os.path.join(opt.out, "calls.jsonl"), "a", encoding="utf-8")
lock = threading.Lock()
step = [0]
calls = [0]
t_start = time.time()


def to_child(msg):
    child.stdin.write((json.dumps(msg) + "\n").encode())
    child.stdin.flush()


def to_agent(line):
    sys.stdout.buffer.write(line if line.endswith(b"\n") else line + b"\n")
    sys.stdout.buffer.flush()


def record(name, args, result, t0, tag="agent", save_as=None):
    texts, images = [], []
    for c in (result or {}).get("content", []):
        if c.get("type") == "text":
            texts.append(c.get("text", ""))
        elif c.get("type") == "image":
            with lock:
                step[0] += 1
                fn = save_as or "step-%03d.png" % step[0]
            with open(os.path.join(opt.out, fn), "wb") as f:
                f.write(base64.b64decode(c.get("data", "")))
            images.append(fn)
    with lock:
        log.write(json.dumps({"t": round(t0 - t_start, 1), "ms": int((time.time() - t0) * 1000), "by": tag, "tool": name,
                              "args": args, "error": bool((result or {}).get("isError")), "text": "\n".join(texts)[:2000],
                              "images": images}) + "\n")
        log.flush()


def reader():
    for line in iter(child.stdout.readline, b""):
        try:
            msg = json.loads(line)
        except ValueError:
            to_agent(line)
            continue
        mid = msg.get("id")
        if isinstance(mid, str) and mid.startswith("bench-"):
            mine.put(msg)
            continue
        key = json.dumps(mid)
        if key in pending:
            name, args, t0 = pending.pop(key)
            record(name, args, msg.get("result"), t0)
        to_agent(line)
    mine.put(None)


threading.Thread(target=reader, daemon=True).start()
seq = [0]


def call(name, args, timeout=600, save_as=None):
    seq[0] += 1
    mid = "bench-%d" % seq[0]
    t0 = time.time()
    to_child({"jsonrpc": "2.0", "id": mid, "method": "tools/call", "params": {"name": name, "arguments": args}})
    while True:
        msg = mine.get(timeout=timeout)
        if msg is None:
            return None
        if msg.get("id") == mid:
            record(name, args, msg.get("result"), t0, tag="bench", save_as=save_as)
            return msg.get("result")


def text(result):
    return "\n".join(c.get("text", "") for c in (result or {}).get("content", []) if c.get("type") == "text")


# Connect before the agent starts, so its first tools/list already has the app's tools.
to_child({"jsonrpc": "2.0", "id": "bench-init", "method": "initialize",
          "params": {"protocolVersion": "2025-06-18", "clientInfo": {"name": "mcp-detail bench"}, "capabilities": {}}})
mine.get(timeout=30)
for _ in range(3):
    if text(call("live_connect", {})).startswith(("Connected", "Already")):
        break

for raw in iter(sys.stdin.buffer.readline, b""):
    try:
        msg = json.loads(raw)
    except ValueError:
        continue
    if msg.get("method") == "tools/call":
        p = msg.get("params", {})
        name, args = p.get("name", ""), p.get("arguments", {}) or {}
        calls[0] += 1
        if calls[0] > opt.cap and "id" in msg:
            stop = {"content": [{"type": "text", "text": "This benchmark's tool-call budget is used up: stop now and "
                                 "say what you made."}], "isError": True}
            record(name, args, stop, time.time(), tag="capped")
            to_agent(json.dumps({"jsonrpc": "2.0", "id": msg["id"], "result": stop}).encode())
            continue
        if "id" in msg:
            pending[json.dumps(msg["id"])] = (name, args, time.time())
    to_child(msg)

# The agent is done. With --finish (a second connection, after the agent's client has closed the first): the final
# pictures, at one size and the same views for every run.
if not opt.finish:
    with open(os.path.join(opt.out, "summary.json"), "w") as f:
        json.dump({"tool_calls": calls[0], "capped": calls[0] > opt.cap, "seconds": round(time.time() - t_start)}, f)
    call("live_disconnect", {}, timeout=30)
    child.stdin.close()
    child.wait(timeout=30)
    sys.exit(0)
info = text(call("micro_info", {}))
if "level" in info.lower() and "no micro" not in info.lower():
    call("micro_bake", {"level": 2, "replace": True})
for shot, view in (("front", {"preset": "front", "frame": "all"}), ("three_quarter", {"preset": "three_quarter", "frame": "all"}),
                   ("closeup", {"preset": "three_quarter", "frame": "all", "zoom": 2.2})):
    call("viewport_image", dict(view, size=1024), save_as="final-%s.png" % shot)
call("scene_overview", {})
call("live_disconnect", {})
child.stdin.close()
child.wait(timeout=30)
