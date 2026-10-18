#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../" && pwd)"
CLI="$ROOT/Chronon3d/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli"
PLAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/plans" && pwd)"
OUT="$ROOT/ChrononTemplate/out/text_3d_stress_suite_v1"
mkdir -p "$OUT"
[[ -x "$CLI" ]] || { echo "Missing built CLI: $CLI" >&2; exit 2; }

for plan in "$PLAN_DIR"/*.plan.json; do
  name="$(basename "$plan" .plan.json)"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --dry-run --backend software --profile draft >"$OUT/$name.validate.json"
  "$CLI" render --plan "$plan" --assets-root "$ROOT/Chronon3d" --output "$OUT/$name.mp4" --backend software --profile draft --report >"$OUT/$name.log" 2>&1
  frames="$(ffprobe -v error -select_streams v:0 -show_entries stream=nb_frames -of csv=p=0 "$OUT/$name.mp4")"
  [[ "$frames" == 72 ]] || { echo "FAIL $name: expected 72 frames, got $frames" >&2; exit 1; }
  echo "PASS $name: $frames frames"
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
