#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"
CLI="${CHRONON3D_CLI:-$ROOT/Chronon3d/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli}"
if [[ ! -x "$CLI" && -x "$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli" ]]; then
  CLI="$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
fi
PLAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/plans" && pwd)"
OUT="${CHRONON3D_STRESS_OUT:-$ROOT/ChrononTemplate/out/text_3d_stress_suite_v1}"
mkdir -p "$OUT"
[[ -x "$CLI" ]] || { echo "Missing built CLI: $CLI" >&2; exit 2; }

for plan in "$PLAN_DIR"/*.plan.json; do
  name="$(basename "$plan" .plan.json)"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --dry-run --backend software --profile draft >"$OUT/$name.validate.json"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --output "$OUT/$name.mp4" --backend software --profile draft --report >"$OUT/$name.log" 2>&1
  python3 - "$OUT/$name.mp4" "$name" <<'PY'
import json, subprocess, sys
path, name = sys.argv[1:]
probe = subprocess.run([
    "ffprobe", "-v", "error", "-select_streams", "v:0", "-show_streams",
    "-show_format", "-of", "json", path,
], check=True, capture_output=True, text=True)
data = json.loads(probe.stdout)
if not data.get("streams"):
    raise SystemExit(f"FAIL {name}: no video stream")
stream = data["streams"][0]
checks = {
    "codec": stream.get("codec_name") == "h264",
    "width": stream.get("width") == 960,
    "height": stream.get("height") == 540,
    "fps": stream.get("avg_frame_rate") == "24/1",
    "frames": int(stream.get("nb_frames", 0)) == 72,
}
failed = [key for key, ok in checks.items() if not ok]
if failed:
    raise SystemExit(f"FAIL {name}: {', '.join(failed)} validation failed: {stream}")
subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "null", "-"],
               check=True, stdout=subprocess.DEVNULL)
print(f"PASS {name}: H.264 960x540 @ 24fps, 72 frames, full decode")
PY
done

python3 - "$OUT" "$(dirname "$PLAN_DIR")/coverage.json" <<'PY'
import glob, json, os, sys
root=sys.argv[1]
coverage={item["id"]:item for item in json.load(open(sys.argv[2]))}
items=[]
for path in sorted(glob.glob(os.path.join(root, '*.mp4.timing.json'))):
    data=json.load(open(path)); summary=data.get('summary', {})
    name=os.path.basename(path[:-len('.mp4.timing.json')])
    if name not in coverage: continue
    items.append({**coverage[name], 'frames':data.get('frames_total'),
                  'avg_frame_ms':summary.get('avg_frame_ms'),
                  'measured_fps':summary.get('measured_fps'),
                  'render_loop_fps':summary.get('render_loop_fps'),
                  'export':'PASS' if data.get('frames_total') == 72 else 'FAIL'})
json.dump({'schema':'chronon.text3d-stress-results.v1','render_backend':'software',
           'resolution':'960x540','fps':24,'duration_frames':72,
           'summary':{'plan_validation':'PASS 10/10','software_exports':f'PASS {sum(x["export"]=="PASS" for x in items)}/10',
                      'full_feature_coverage':'PARTIAL; see individual cases'},
           'cases':items}, open(os.path.join(root,'results.json'),'w'), indent=2)
print(f"Wrote results.json for {len(items)} cases")
PY
