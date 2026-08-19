#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CLI=${CHRONON_CLI:-"$ROOT/.tmp/chronon-builds/native-verify/apps/chronon3d_cli/chronon3d_cli"}
OUT=${PRESET_SHOWCASE_OUT:-"$ROOT/output/preset-showcase-v1"}
ASSETS=${CHRONON_ASSETS_ROOT:-"$ROOT"}
mkdir -p "$OUT/plans" "$OUT/names" "$OUT/images" "$OUT/important-phrases" "$OUT/receipts"
test -x "$CLI" || { echo "missing CLI: $CLI" >&2; exit 2; }

python3 - "$OUT/plans" <<'PY'
import json, pathlib, sys
out = pathlib.Path(sys.argv[1])
families = {
    "names": ["name_glow_typewriter", "name_glow_slide", "name_glow_pop"],
    "images": ["image_fast_fade", "image_slide_left", "image_slide_right", "modern_rounded_pop", "bottom_card_rise"],
    "important-phrases": ["fast_fade_through", "clean_slide_up", "slide_lateral", "phrase_word_reveal", "undertext_pop"],
}
for family, presets in families.items():
    for preset in presets:
        if family == "images":
            layer = {"id":"overlay", "type":"image", "preset":preset, "asset":"assets/test_image.png", "position":[640,360], "box_width":360, "box_height":360, "fit":"contain", "start_frame":15, "duration_frames":60}
        else:
            text = "Tim Cook" if family == "names" else "QUESTO CAMBIA TUTTO"
            layer = {"id":"overlay", "type":"text", "text":text, "preset":preset, "font":"assets/fonts/Poppins-Bold.ttf", "position":[640,360], "start_frame":15, "duration_frames":60}
        plan = {"schema":"chronon.render-plan", "version":1, "job_id":f"showcase-{family}-{preset}", "canvas":{"width":1920,"height":1080,"fps":30,"duration_frames":150}, "layers":[{"id":"background","type":"color","color":[0.0,0.0,0.0,1.0]}, layer], "output":{"path":"result.mp4","format":"mp4","codec":"h264"}}
        (out / f"{family}__{preset}.json").write_text(json.dumps(plan, indent=2)+"\n")
PY

mapfile -t plans < <(find "$OUT/plans" -name '*.json' -print | sort)
for plan in "${plans[@]}"; do
  name=$(basename "$plan" .json)
  family=${name%%__*}
  out="$OUT/$family/$name.mp4"
  plan_arg=${plan#"$ROOT/"}
  if [ "$plan_arg" = "$plan" ]; then
    plan_arg="$plan"
  fi
  echo "RENDER $name"
  (cd "$ROOT" && "$CLI" render "--plan=$plan_arg" --assets-root "$ASSETS" --output "$out" --backend vulkan --codec h264 --hardware nvenc --encoder-backend native --ffmpeg-mode pipe --profile production --log-level error)
  cp "$out.receipt.json" "$OUT/receipts/$name.receipt.json"
  cp "$out.timing.json" "$OUT/receipts/$name.timing.json"
  python3 - "$out" <<'PY'
import json, subprocess, sys
out=sys.argv[1]
r=json.load(open(out+'.receipt.json')); t=json.load(open(out+'.timing.json')); g=t['job']['gpu']
assert r['copy_eligible'] is True and r['render']['backend']=='vulkan'
assert r['media']['codec']=='h264' and g['effective_backend']=='vulkan'
assert g['software_fallback_nodes']==0 and g['cpu_pixel_readback_count']==0 and g['cpu_pixel_readback_bytes']==0
assert g['gpu_native_encode_frames']==r['render']['frames']
assert g['video_pipe_fallback_frames']==0 and g['video_native_fallback_frames']==0
subprocess.check_call(['ffprobe','-v','error',out], stdout=subprocess.DEVNULL)
PY
done

python3 - "$OUT" <<'PY'
import hashlib, json, pathlib, sys
root=pathlib.Path(sys.argv[1]); files=[]
for p in sorted(root.rglob('*')):
    if p.is_file() and p.name not in {'MANIFEST.json','MANIFEST.sha256'}:
        files.append({'path':str(p.relative_to(root)), 'bytes':p.stat().st_size, 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
manifest={'schema':'chronon.preset-showcase.v1','canvas':{'width':1920,'height':1080,'fps':30,'frames':150},'families':{'names':3,'images':5,'important-phrases':5},'files':files}
(root/'MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n')
(root/'MANIFEST.sha256').write_text(hashlib.sha256((root/'MANIFEST.json').read_bytes()).hexdigest()+'\n')
print(f"SHOWCASE_PASS files={len(files)} manifest_sha256={hashlib.sha256((root/'MANIFEST.json').read_bytes()).hexdigest()}")
PY
