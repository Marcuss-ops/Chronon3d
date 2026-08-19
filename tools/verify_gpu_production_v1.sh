#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CLI=${CHRONON_CLI:-"$ROOT/.tmp/chronon-builds/native-verify/apps/chronon3d_cli/chronon3d_cli"}
PLAN=${CHRONON_GOLDEN_PLAN:-"$ROOT/examples/render_plan_text_smoke.json"}
ASSETS=${CHRONON_ASSETS_ROOT:-"$ROOT/test_renders/test1-hello-chronon"}
CERT_DIR=${CHRONON_CERT_DIR:-"$(mktemp -d /tmp/chronon-gpu-cert.XXXXXX)"}
STRESS_COUNT=${CHRONON_STRESS_COUNT:-100}
mkdir -p "$CERT_DIR/matrix" "$CERT_DIR/stress"
# Runtime receipts intentionally carry the short commit identity exposed by
# TelemetryManager. Keep the certification artifact and its assertion in the
# same identity domain; a full SHA here would reject an otherwise matching
# binary on every clean build.
SHA=${CHRONON_CERT_SHA:-$(git -C "$ROOT" rev-parse --short HEAD)}
dirty=$(git -C "$ROOT" status --porcelain | grep -vE '^[ M?]{1,3}tools/cuda_nvdec_nvenc_overlay_bench\.cpp$' || true)
test -z "$dirty" || { echo "working tree must be clean outside the CUDA benchmark" >&2; exit 2; }

render_one() {
  local plan=$1 out=$2 log=$3 assets=${4:-$ASSETS}
  "$CLI" render --plan "$plan" --assets-root "$assets" --output "$out" \
    --backend vulkan --codec h264 --hardware nvenc --encoder-backend native \
    --ffmpeg-mode pipe --profile production --log-level error >"$log" 2>&1
  python3 - "$out" <<'PY'
import json, subprocess, sys
out=sys.argv[1]
r=json.load(open(out+'.receipt.json')); t=json.load(open(out+'.timing.json'))
assert r['copy_eligible'] is True
assert r['render']['backend']=='vulkan'
assert r['identity']['git_sha'] == open('/tmp/chronon-gpu-cert-head-sha').read().strip()
assert r['media']['codec']=='h264' and r['media']['pixel_format']=='yuv420p'
assert r['media']['frame_count']==r['render']['frames']
g=t['job']['gpu']
assert g['effective_backend']=='vulkan' and g['software_fallback_nodes']==0
assert g['gpu_readback_bytes']==0 and g['gpu_readback_ms']==0.0
assert g['cpu_pixel_readback_count']==0 and g['cpu_pixel_readback_bytes']==0
assert g['gpu_native_encode_frames']==r['render']['frames']
assert g['video_pipe_fallback_frames']==0 and g['video_native_fallback_frames']==0
s=t.get('job',{}).get('gpu',{})
assert s.get('gpu_encode_failures',0)==0 and s.get('gpu_surface_create_failures',0)==0
s=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_name,nb_frames,pix_fmt','-of','json',out],text=True))['streams'][0]
assert s['codec_name']=='h264' and s['pix_fmt']=='yuv420p' and int(s['nb_frames'])==r['render']['frames']
PY
}

printf '%s\n' "$SHA" >/tmp/chronon-gpu-cert-head-sha
render_one "$PLAN" "$CERT_DIR/golden.mp4" "$CERT_DIR/golden.log"

python3 - "$ROOT/examples/render_plan_text_smoke.json" "$CERT_DIR/matrix" <<'PY'
import json, pathlib, sys
base=json.load(open(sys.argv[1])); out=pathlib.Path(sys.argv[2])
ids='name_glow_typewriter name_glow_slide name_glow_pop image_fast_fade image_slide_left image_slide_right modern_rounded_pop bottom_card_rise fast_fade_through clean_slide_up slide_lateral phrase_word_reveal undertext_pop snap_scale'.split()
image_ids={'image_fast_fade','image_slide_left','image_slide_right','modern_rounded_pop','bottom_card_rise'}
for p in ids:
    plan=json.loads(json.dumps(base)); plan['job_id']='gpu-v1-'+p
    layer=plan['layers'][1]; layer['preset']=p; layer['animation']={'preset':'fade_in'}; layer['font']='assets/fonts/Poppins-Bold.ttf'
    if p in image_ids:
        layer.clear(); layer.update({'id':'image-'+p,'type':'image','preset':p,'asset':'assets/test_image.png','position':[320,180],'box_width':260,'box_height':260,'start_frame':0,'duration_frames':90})
    (out/(p+'.json')).write_text(json.dumps(plan))
PY
mapfile -t matrix_plans < <(find "$CERT_DIR/matrix" -name '*.json' -print | sort)
for plan in "${matrix_plans[@]}"; do
  n=$(basename "$plan" .json)
  render_one "$plan" "$CERT_DIR/matrix/$n.mp4" "$CERT_DIR/matrix/$n.log" "$ROOT"
done

for n in $(seq 1 "$STRESS_COUNT"); do
  render_one "$PLAN" "$CERT_DIR/stress/$n.mp4" "$CERT_DIR/stress/$n.log"
done

CERT_OUT="$ROOT/output/gpu-certification/$SHA"
mkdir -p "$CERT_OUT"
python3 - "$CERT_DIR" "$CERT_OUT" "$SHA" "$STRESS_COUNT" <<'PY'
import json, pathlib, sys
root=pathlib.Path(sys.argv[1]); out=pathlib.Path(sys.argv[2]); sha=sys.argv[3]; count=int(sys.argv[4])
gold=json.load(open(root/'golden.mp4.receipt.json'))
rs=[json.load(open(p)) for p in sorted((root/'stress').glob('*.mp4.receipt.json'))]
assert len(rs)==count and all(r['copy_eligible'] and r['render']['backend']=='vulkan' for r in rs)
assert len({r['output']['sha256'] for r in rs})==1
import subprocess
def command(*args):
    try: return subprocess.check_output(args,text=True,stderr=subprocess.DEVNULL).strip()
    except Exception: return 'unavailable'
manifest={'schema':'chronon.gpu-production-v1-cert.v1','git_sha':sha,'gpu_name':command('nvidia-smi','--query-gpu=name','--format=csv,noheader'),'gpu_driver':command('nvidia-smi','--query-gpu=driver_version','--format=csv,noheader'),'ffmpeg_version':command('ffmpeg','-version').splitlines()[0],'golden_sha256':gold['output']['sha256'],'stress_jobs':count,'stress_sha256':rs[0]['output']['sha256'],'matrix_jobs':len(list((root/'matrix').glob('*.mp4.receipt.json'))),'same_sha':all(r['identity']['git_sha']==gold['identity']['git_sha'] for r in rs)}
assert manifest['same_sha']; (root/'CERTIFICATION.json').write_text(json.dumps(manifest,indent=2)+'\n'); (out/'certification.json').write_text(json.dumps(manifest,indent=2)+'\n'); print(json.dumps(manifest,indent=2))
PY
echo "GPU Production V1 certification passed: $CERT_DIR"
