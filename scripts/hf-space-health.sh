#!/usr/bin/env bash
# Daily probe of the free Hugging Face Spaces that Generate 3D uses (no Claude, no GPU quota: it only reads each
# Space's public /config; whether a Space has switched Text to 3D off is NOT in /config - its Text Prompt tab stays
# visible - so that is only learned by a real job, and the app retries on use). Prints nothing while every Space is as last time; prints the differences when one goes
# down, loses its endpoint, or changes the shape of its inputs/outputs. Run by sf-hfhealth.timer.
#   scripts/hf-space-health.sh [state-dir]      exit 0 always; output = what changed
set -u
state=${1:-${XDG_STATE_HOME:-$HOME/.local/state}/sculptforge}; mkdir -p "$state"
spaces="tencent-hunyuan3d-2mini-turbo tencent-hunyuan3d-2 tencent-hunyuan3d-2-1 microsoft-trellis-2"
now=$(for sp in $spaces; do
  body=$(curl -fsS -m 45 "https://$sp.hf.space/config" 2>/dev/null) || { echo "$sp DOWN (no /config)"; continue; }
  printf '%s' "$body" | python3 -I -c '
import json, sys
sp = sys.argv[1]
try: c = json.load(sys.stdin)
except Exception: print(sp, "DOWN (not JSON)"); sys.exit()
comp = {x["id"]: x["type"] for x in c.get("components", [])}
for d in c.get("dependencies", []):
    if d.get("api_name") in ("shape_generation", "preprocess_image", "image_to_3d", "extract_glb", "requires_bg_remove", "run_button"):
        print(sp, d["api_name"], "in=" + ",".join(comp.get(i, "?") for i in d["inputs"]), "out=" + ",".join(comp.get(i, "?") for i in d["outputs"]))
' "$sp"
done)
last="$state/hfhealth-last.txt"
[ -f "$last" ] && [ "$now" = "$(cat "$last")" ] && exit 0
if [ -f "$last" ]; then diff <(cat "$last") <(printf '%s\n' "$now") | grep '^[<>]'; else printf '%s\n' "$now"; fi
printf '%s\n' "$now" > "$last"
exit 0
