#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# tools/verify_z3_equivalence.sh — Z3 GPU-output parity gate
#
# Z3 (TICKET-ZERO-COPY-ZWAVES) removes the per-frame device-to-device copy
# `source → slot encode surface` (`copy_surface_to_video_encode` in
# scene_native_output.cpp) by binding the graph's final composite directly to
# the slot's encode surface.  Removing a GPU pass can change nothing about the
# rendered pixels — but that must be PROVEN, not assumed.
#
# This gate proves output parity between two runs of the SAME scene corpus:
#
#   * rendered-frame parity — every scene is rendered to a PNG frame set on
#     the Vulkan graph lane; the reference and candidate RGBA frame hashes
#     must be byte-identical (the graph output the encoder consumes is the
#     same before and after Z3), and
#   * native-lane identity — the candidate's `*.timing.json` sidecar must
#     report `gpu_surface_copy_frames == 0` (gate 6: no per-frame D2D copy
#     remains) with the effective backend / encoder backend unchanged.
#
# The parity comparison deliberately hashes RENDERED FRAMES, not the encoded
# MP4 bitstream: NVENC output is not guaranteed bit-exact between runs, while
# the render graph output IS deterministic — pixel equality is the contract
# the equivalence gate must hold.
#
# Workflow on the GPU certification host:
#
#   1. Reference (current main, pre-Z3):  bash tools/verify_z3_equivalence.sh --record /tmp/z3-ref --video
#   2. Candidate  (Z3 branch):            bash tools/verify_z3_equivalence.sh --record /tmp/z3-cand --video
#   3. Verdict:                            bash tools/verify_z3_equivalence.sh --compare /tmp/z3-ref /tmp/z3-cand
#
# Usage:
#   bash tools/verify_z3_equivalence.sh --record <dir> [--scenes S1,S2] \
#       [--png-frames N] [--frames N] [--warmup N] [--video] [--cli PATH]
#   bash tools/verify_z3_equivalence.sh --compare <ref-dir> <cand-dir>
#
# Scene default: `DarkGridBackground` — a registered built-in procedural
# composition with no external assets (same default as
# verify_zero_copy_end_to_end.sh; CHRONON3D_BENCH_SCENE overrides).
#
# Exit codes:
#   0 = EQUIVALENCE_PASS
#   1 = EQUIVALENCE_FAIL (frame parity or candidate gate-6 mismatch)
#   2 = BLOCKED (missing CLI/artifacts/tools or malformed manifest)
# ═══════════════════════════════════════════════════════════════════════════

set -uo pipefail

GATE_NAME="verify_z3_equivalence"

# ── Defaults ─────────────────────────────────────────────────────────────
SCENES="${CHRONON3D_BENCH_SCENE:-DarkGridBackground}"
PNG_FRAMES="${CHRONON3D_Z3_PNG_FRAMES:-10}"
FRAMES="${CHRONON3D_BENCH_FRAMES:-30}"
WARMUP="${CHRONON3D_BENCH_WARMUP:-10}"
DO_VIDEO=0
MODE=""
REF_DIR=""
CAND_DIR=""
RECORD_DIR=""
OUTPUT_DIR="/tmp/z3_equivalence"

CLI_BIN="${CHRONON3D_CLI:-}"
if [[ -z "$CLI_BIN" ]]; then
    for candidate in \
        "$PWD/build/chronon/linux-video-release/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-video-test/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-video-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-fast-dev/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/chronon/linux-release/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/fast/apps/chronon3d_cli/chronon3d_cli" \
        "$PWD/build/apps/chronon3d_cli/chronon3d_cli"; do
        if [[ -x "$candidate" ]]; then
            CLI_BIN="$candidate"
            break
        fi
    done
fi

# ── Arg parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --record)       MODE="record"; RECORD_DIR="$2"; shift 2 ;;
        --compare)      MODE="compare"; REF_DIR="$2"; CAND_DIR="$3"; shift 3 ;;
        --scenes)       SCENES="$2"; shift 2 ;;
        --png-frames)   PNG_FRAMES="$2"; shift 2 ;;
        --frames)       FRAMES="$2"; shift 2 ;;
        --warmup)       WARMUP="$2"; shift 2 ;;
        --video)        DO_VIDEO=1; shift ;;
        --cli)          CLI_BIN="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,40p' "$0"
            exit 0
            ;;
        *) echo "[ERROR] Unknown arg: $1" >&2; exit 2 ;;
    esac
done

_info() { echo "[INFO] $GATE_NAME: $*" >&2; }
_warn() { echo "[WARN] $GATE_NAME: $*" >&2; }
_fail() { echo "[FAIL] $GATE_NAME: $*" >&2; }
_pass() { echo "[PASS] $GATE_NAME: $*" >&2; }

extract_counter() {
    local json_file="$1" counter_name="$2"
    python3 -c "
import json, sys
try:
    with open('$json_file') as f:
        data = json.load(f)
    for path in (('job','gpu','$counter_name'), ('job','$counter_name'),
                 ('counters','$counter_name'), ('run','counters','$counter_name'),
                 ('$counter_name',)):
        node = data
        ok = True
        for part in path:
            if not isinstance(node, dict) or part not in node:
                ok = False
                break
            node = node[part]
        if ok:
            print(node)
            sys.exit(0)
    print('')
except Exception:
    print('')
" 2>/dev/null
}

# ── Frame-hash authority ─────────────────────────────────────────────────
# Decode a PNG to raw RGBA and hash it.  Prefer ffmpeg; fall back to
# ImageMagick `convert` (the tool used by check_determinism.sh).  Raw-pixel
# hashing is required: PNG byte output may carry writer metadata, while the
# rendered RGBA pixels are the deterministic render contract.
frame_rgba_sha() {
    local png="$1"
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -v error -i "$png" -f rawvideo -pix_fmt rgba - 2>/dev/null | sha256sum | cut -d' ' -f1
    elif command -v convert >/dev/null 2>&1; then
        convert "$png" rgba:- 2>/dev/null | sha256sum | cut -d' ' -f1
    else
        echo ""
    fi
}

# ── Record a scene ───────────────────────────────────────────────────────
# Renders PNG_FRAMES stills of the scene on the Vulkan graph lane (the exact
# render output the native encoder consumes) and, with --video, a
# require_gpu_native NVENC export whose timing sidecar supplies the gate-6
# counters.  Appends one `key=value|` manifest row to $MANIFEST.
record_scene() {
    local scene="$1" dir="$2" manifest="$3"
    local work="$dir/work/$scene"
    mkdir -p "$work"

    local png_hashes=""
    local png_count=0
    for ((fi = 0; fi < PNG_FRAMES; ++fi)); do
        local png="$work/frame_$(printf '%04d' "$fi").png"
        if ! "$CLI_BIN" render "$scene" \
            --frame "$fi" \
            --output "$png" \
            --assets-root "$PWD" \
            --backend vulkan >"$work/render_$fi.log" 2>&1; then
            _fail "scene '$scene' frame $fi render failed (see $work/render_$fi.log)"
            return 1
        fi
        if [[ ! -s "$png" ]]; then
            _fail "scene '$scene' frame $fi produced no PNG output"
            return 1
        fi
        local sha
        sha="$(frame_rgba_sha "$png")"
        if [[ -z "$sha" ]]; then
            _fail "no frame-hash decoder available (need ffmpeg or ImageMagick 'convert')"
            return 1
        fi
        png_hashes+="$sha"$'\n'
        png_count=$((png_count + 1))
    done
    # Single digest over the ordered per-frame hashes (frame order is the
    # contract; hash of hashes keeps the manifest row bounded).
    local frame_digest
    frame_digest="$(printf '%s' "$png_hashes" | sha256sum | cut -d' ' -f1)"

    local video_gate6="" video_gate4="" eff_backend="" enc_backend="" handoff="" video_digest=""
    if [[ "$DO_VIDEO" -eq 1 ]]; then
        local vwork="$dir/video/$scene"
        mkdir -p "$vwork"
        local mp4="$vwork/out.mp4"
        if ! "$CLI_BIN" render "$scene" \
            --frames "0-$((FRAMES - 1))" \
            --assets-root "$PWD" \
            --warmup-renderer \
            --warmup-framebuffers "$WARMUP" \
            --backend vulkan \
            --hardware nvenc \
            --encoder-backend native \
            --gpu-hot-path-mode require_gpu_native \
            --report \
            -o "$mp4" >"$vwork/render.log" 2>&1; then
            _fail "scene '$scene' native video export failed (see $vwork/render.log)"
            return 1
        fi
        local sidecar="$mp4.timing.json"
        if [[ ! -s "$sidecar" ]]; then
            _fail "scene '$scene' timing sidecar missing: $sidecar"
            return 1
        fi
        video_gate6="$(extract_counter "$sidecar" "gpu_surface_copy_frames")"
        video_gate4="$(extract_counter "$sidecar" "rgba_to_nv12_frames")"
        eff_backend="$(extract_counter "$sidecar" "effective_backend")"
        enc_backend="$(extract_counter "$sidecar" "encoder_backend")"
        handoff="$(extract_counter "$sidecar" "surface_handoff_path")"
        if [[ "$video_gate6" == "" || "$video_gate4" == "" ]]; then
            _fail "scene '$scene' sidecar lacks gate-6/gate-4 counters"
            return 1
        fi
        if command -v ffmpeg >/dev/null 2>&1; then
            video_digest="$(ffmpeg -v error -i "$mp4" -f rawvideo -pix_fmt rgba - 2>/dev/null \
                | sha256sum | cut -d' ' -f1)"
        fi
    fi

    {
        printf 'scene=%s|' "$scene"
        printf 'png_frames=%d|' "$png_count"
        printf 'frame_digest=%s|' "$frame_digest"
        printf 'video_gate6=%s|' "$video_gate6"
        printf 'video_gate4=%s|' "$video_gate4"
        printf 'effective_backend=%s|' "$eff_backend"
        printf 'encoder_backend=%s|' "$enc_backend"
        printf 'surface_handoff_path=%s|' "$handoff"
        printf 'video_digest=%s' "$video_digest"
        printf '\n'
    } >> "$manifest"
    _info "recorded scene '$scene': png_frames=$png_count frame_digest=${frame_digest:0:16}.. video_gate6=${video_gate6:--}"
    return 0
}

# ── Manifest helpers ─────────────────────────────────────────────────────
# Parse one manifest into key=value lines for the given scene.
manifest_scene_rows() {
    local manifest="$1" scene="$2"
    grep "^scene=${scene}|" "$manifest" 2>/dev/null || true
}

field_of() {
    local row="$1" key="$2"
    echo "$row" | tr '|' '\n' | grep -E "^${key}=" | cut -d= -f2- | head -1
}

# ── Compare mode ─────────────────────────────────────────────────────────
compare_dirs() {
    local ref_manifest="$1" cand_manifest="$2"
    if [[ ! -f "$ref_manifest" || ! -f "$cand_manifest" ]]; then
        _fail "both a reference and a candidate manifest are required"
        echo "EQUIVALENCE_BLOCKED"
        exit 2
    fi

    local ref_scenes cand_scenes
    ref_scenes="$(cut -d'|' -f1 "$ref_manifest" | sed 's/^scene=//' | sort)"
    cand_scenes="$(cut -d'|' -f1 "$cand_manifest" | sed 's/^scene=//' | sort)"
    if [[ "$ref_scenes" != "$cand_scenes" ]]; then
        _fail "scene sets differ between reference and candidate"
        echo "EQUIVALENCE_FAIL"
        exit 1
    fi

    local pass=0 fail=0 blocked=0
    while IFS= read -r scene; do
        [[ -z "$scene" ]] && continue
        local ref_row cand_row
        ref_row="$(manifest_scene_rows "$ref_manifest" "$scene" | tail -1)"
        cand_row="$(manifest_scene_rows "$cand_manifest" "$scene" | tail -1)"
        if [[ -z "$ref_row" || -z "$cand_row" ]]; then
            _warn "  scene '$scene': row missing on one side (BLOCKED)"
            blocked=$((blocked + 1))
            continue
        fi
        local ref_png cand_png ref_count cand_count
        ref_png="$(field_of "$ref_row" frame_digest)"
        cand_png="$(field_of "$cand_row" frame_digest)"
        ref_count="$(field_of "$ref_row" png_frames)"
        cand_count="$(field_of "$cand_row" png_frames)"
        if [[ "$ref_count" != "$cand_count" ]]; then
            _fail "  scene '$scene': PNG frame count differs (ref=$ref_count cand=$cand_count)"
            fail=$((fail + 1))
            continue
        fi
        if [[ "$ref_png" == "$cand_png" && -n "$ref_png" ]]; then
            _pass "  scene '$scene': rendered-frame parity OK (${ref_png:0:16}..)"
            pass=$((pass + 1))
        else
            _fail "  scene '$scene': rendered-frame parity MISMATCH (ref=${ref_png:0:16}.. cand=${cand_png:0:16}..)"
            fail=$((fail + 1))
        fi

        # Native-lane counters (candidate is the Z3 lane under test).
        local cand_gate6 cand_gate4 ref_gate6 ref_gate4
        cand_gate6="$(field_of "$cand_row" video_gate6)"
        cand_gate4="$(field_of "$cand_row" video_gate4)"
        ref_gate6="$(field_of "$ref_row" video_gate6)"
        ref_gate4="$(field_of "$ref_row" video_gate4)"
        if [[ -z "$cand_gate6" || -z "$cand_gate4" ]]; then
            _warn "  scene '$scene': candidate has no native-video counters (record with --video; BLOCKED)"
            blocked=$((blocked + 1))
        else
            if [[ "$cand_gate6" == "0" ]]; then
                _pass "  scene '$scene': gpu_surface_copy_frames = 0 (gate 6 zeroed)"
                pass=$((pass + 1))
            else
                _fail "  scene '$scene': gpu_surface_copy_frames = $cand_gate6 (Z3 target: 0)"
                fail=$((fail + 1))
            fi
            if [[ -n "$ref_gate4" && "$cand_gate4" != "$ref_gate4" ]]; then
                _fail "  scene '$scene': rgba_to_nv12_frames changed (ref=$ref_gate4 cand=$cand_gate4) — Z3 must not alter gate 4"
                fail=$((fail + 1))
            elif [[ -n "$ref_gate4" ]]; then
                _pass "  scene '$scene': rgba_to_nv12_frames unchanged ($cand_gate4)"
                pass=$((pass + 1))
            else
                _warn "  scene '$scene': reference has no native-video counters; gate-4 invariance not compared"
            fi
            _info "  scene '$scene': handoff=$(field_of "$cand_row" surface_handoff_path) effective=$(field_of "$cand_row" effective_backend) encoder=$(field_of "$cand_row" encoder_backend)"
            if [[ "$ref_gate6" == "0" ]]; then
                _info "  scene '$scene': reference gpu_surface_copy_frames already 0 — parity holds but this comparison does not demonstrate a Z3 delta"
            fi
        fi
    done <<< "$ref_scenes"

    _info "── Summary ──"
    _info "  PASS: $pass   FAIL: $fail   BLOCKED: $blocked"
    if [[ $fail -eq 0 && $blocked -eq 0 ]]; then
        _pass "EQUIVALENCE_PASS: Z3 removed the per-frame D2D copy with identical render output"
        echo "EQUIVALENCE_PASS"
        exit 0
    elif [[ $fail -gt 0 ]]; then
        _fail "EQUIVALENCE_FAIL: $fail check(s) failed"
        echo "EQUIVALENCE_FAIL"
        exit 1
    fi
    _warn "EQUIVALENCE_BLOCKED: $blocked check(s) unverifiable"
    echo "EQUIVALENCE_BLOCKED"
    exit 2
}

# ── Dispatch ─────────────────────────────────────────────────────────────
if [[ "$MODE" == "record" ]]; then
    if [[ ! -x "$CLI_BIN" ]]; then
        _fail "chronon3d_cli not found at: $CLI_BIN"
        echo "EQUIVALENCE_BLOCKED"
        exit 2
    fi
    mkdir -p "$RECORD_DIR/work" "$RECORD_DIR/video"
    manifest_file="$RECORD_DIR/manifest.txt"
    : > "$manifest_file"
    IFS=',' read -r -a scene_list <<< "$SCENES"
    for scene in "${scene_list[@]}"; do
        scene="$(echo "$scene" | xargs)"
        [[ -z "$scene" ]] && continue
        if ! record_scene "$scene" "$RECORD_DIR" "$manifest_file"; then
            _fail "recording aborted at scene '$scene'"
            echo "EQUIVALENCE_BLOCKED"
            exit 2
        fi
    done
    _info "recorded $RECORD_DIR/manifest.txt"
    echo "EQUIVALENCE_RECORDED"
    exit 0
elif [[ "$MODE" == "compare" ]]; then
    compare_dirs "$REF_DIR/manifest.txt" "$CAND_DIR/manifest.txt"
else
    echo "[ERROR] $GATE_NAME: use --record <dir> or --compare <ref> <cand>" >&2
    exit 2
fi
