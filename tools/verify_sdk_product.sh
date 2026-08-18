#!/usr/bin/env bash
# ═════════════════════════════════════════════════════════════════════════════
# tools/verify_sdk_product.sh
#
# Single-shot SDK product certification.  One command verifies the public
# integration contract end-to-end against the INSTALLED package (never the
# source tree) and prints exactly one terminal marker on success:
#
#     CHRONON_SDK_PRODUCT_PASS
#
# The 15 checks:
#   [ 1] install SDK                    (configure + build + install)
#   [ 2] C++ consumer configure         (tests/install_consumer against prefix)
#   [ 3] C++ consumer build
#   [ 4] C++ render                     (run check_install + check_full)
#   [ 5] C consumer build               (check_c_api)
#   [ 6] C ABI version check            (chronon_abi_version() == 2)
#   [ 7] C RenderPlan compile           (valid JSON plan -> CHRONON_OK)
#   [ 8] C render                       (run check_c_api -> C_ABI_CONSUMER_PASS)
#   [ 9] Go consumer                    (sdk_consumers/03_go)
#   [10] Rust consumer                  (sdk_consumers/04_rust)
#   [11] Python consumer                (sdk_consumers/05_python via ctypes)
#   [12] RenderPlan schema validation   (invalid JSON plan -> rejected)
#   [13] no internal headers            (static grep of consumer sources)
#   [14] no internal CMake targets      (check_public_targets.sh — all consumers)
#   [15] relocation test                (find_package from a moved prefix)
#
# Checks [9]/[10]/[11] drive the permanent sdk_consumers/{03_go,04_rust,05_python}
# mini-projects against the installed package.  They SKIP (exit 2, non-fatal)
# only when the corresponding project directory is absent.  Every other check
# must PASS for the CHRONON_SDK_PRODUCT_PASS marker.
#
# Environment:
#   CHRONON3D_SDK_PRODUCT_PRESET   CMake preset for the SDK build (default: linux-ci)
#   CHRONON3D_SDK_PRODUCT_FAST=1   reuse an existing SDK_BUILD + SDK_PREFIX
#   SDK_BUILD / SDK_PREFIX         required in FAST mode
#   CHRONON3D_SDK_PRODUCT_KEEP=1   keep temp dirs for debugging
# ═════════════════════════════════════════════════════════════════════════════
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${REPO_ROOT:-$(cd "$HERE/.." && pwd)}"

PRESET="${CHRONON3D_SDK_PRODUCT_PRESET:-linux-ci}"
FAST="${CHRONON3D_SDK_PRODUCT_FAST:-0}"
KEEP="${CHRONON3D_SDK_PRODUCT_KEEP:-0}"
EXPECTED_ABI=2

log()  { printf '[verify_sdk_product] %s\n' "$*" >&2; }
fail() { log "FAIL: $*"; exit 1; }

mktemp_dir() {
    mktemp -d "${TMPDIR:-/tmp}/${1:-chronon3d_sdk_product}.XXXXXX"
}

command -v cmake >/dev/null 2>&1 || fail "cmake not on PATH"

# ── Result tracking ────────────────────────────────────────────────────────
NAMES=(); STATUSES=()
PASSED=0; FAILED=0; SKIPPED=0

record() {
    NAMES+=("$1"); STATUSES+=("$2")
    case "$2" in
        PASS) PASSED=$((PASSED + 1)) ;;
        FAIL) FAILED=$((FAILED + 1)) ;;
        SKIP) SKIPPED=$((SKIPPED + 1)) ;;
    esac
}

check() {
    local num="$1" name="$2" fn="$3" rc
    log "── [$num/15] $name ──"
    set +e
    "$fn"
    rc=$?
    set -e
    case "$rc" in
        0) log "[$num/15] PASS  $name"; record "$name" PASS ;;
        1) log "[$num/15] FAIL  $name"; record "$name" FAIL ;;
        2) log "[$num/15] SKIP  $name"; record "$name" SKIP ;;
        *) log "[$num/15] FAIL  $name (rc=$rc)"; record "$name" FAIL ;;
    esac
}

# ── Work dirs (created lazily so FAST mode never touches the build) ────────
WORK_DIR="$(mktemp_dir chronon3d_sdk_product)"
trap 'rc=$?; if [[ "$KEEP" != "1" ]]; then rm -rf "$WORK_DIR"; fi; exit "$rc"' EXIT

# ── Check 1: install SDK ────────────────────────────────────────────────────
chk_install() {
    if [[ "$FAST" == "1" ]]; then
        : "${SDK_BUILD:?FAST mode requires SDK_BUILD}"
        : "${SDK_PREFIX:?FAST mode requires SDK_PREFIX}"
        [[ -f "$SDK_BUILD/CMakeCache.txt" ]] || { log "no CMakeCache.txt in $SDK_BUILD"; return 1; }
        if [[ ! -f "$SDK_PREFIX/lib/cmake/Chronon3D/Chronon3DConfig.cmake" ]]; then
            log "FAST: installing SDK into $SDK_PREFIX"
            cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" >&2 || return 1
        fi
    else
        SDK_BUILD="$(mktemp_dir chronon3d_sdk_product_build)"
        SDK_PREFIX="$(mktemp_dir chronon3d_sdk_product_prefix)"
        log "configuring SDK (preset=$PRESET, CLI OFF)"
        # The SDK install rules install chronon3d_cli (install(TARGETS
        # chronon3d_cli ...)) whenever the target exists, but this
        # certification only builds the SDK archive + C ABI and never
        # exercises the CLI.  Force CHRONON3D_BUILD_CLI=OFF so
        # `cmake --install` does not try to install a CLI binary that was
        # never built.  (linux-ci leaves the option at its ON default.)
        cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
            -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
            -DCHRONON3D_BUILD_CLI=OFF >&2 || return 1
        log "building SDK archive + C ABI"
        cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl chronon3d_c >&2 || return 1
        log "installing SDK into $SDK_PREFIX"
        cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" >&2 || return 1
    fi
    [[ -f "$SDK_PREFIX/lib/cmake/Chronon3D/Chronon3DConfig.cmake" ]] \
        || { log "missing Chronon3DConfig.cmake in prefix"; return 1; }
    [[ -f "$SDK_PREFIX/include/chronon3d/c_api/chronon3d.h" ]] \
        || { log "missing C ABI header in prefix"; return 1; }
    [[ -e "$SDK_PREFIX/lib/libchronon3d_c.so" ]] \
        || { log "missing libchronon3d_c.so in prefix"; return 1; }
    # The C ABI .so must be self-contained: no third-party runtime NEEDED
    # entries (harfbuzz/freetype/blend2d/xxhash/TBB must be statically linked
    # in).  Enforced by the dedicated gate script.
    SDK_PREFIX="$SDK_PREFIX" bash "$HERE/sdk/check_cabi_self_contained.sh" \
        || { log "installed C ABI .so is not self-contained"; return 1; }
    # The C ABI .so must satisfy the frozen ABI2 symbol/SONAME contract: no
    # baseline symbol removed, no type (signature) change, and SOVERSION
    # (SONAME major) equal to the ABI major.  Enforced by the dedicated gate.
    SDK_PREFIX="$SDK_PREFIX" bash "$HERE/sdk/check_cabi_abi_gate.sh" \
        || { log "installed C ABI .so failed the ABI symbol/SONAME gate"; return 1; }
    # Derive the vcpkg closure from the SDK build so consumers can resolve the
    # SDK's transitive find_dependency() packages at configure time.
    if [[ -z "${VCPKG_INSTALLED_DIR:-}" && -f "$SDK_BUILD/CMakeCache.txt" ]]; then
        VCPKG_INSTALLED_DIR="$(sed -n 's/^VCPKG_INSTALLED_DIR:.*=//p' "$SDK_BUILD/CMakeCache.txt" | head -n 1)"
        [[ -n "$VCPKG_INSTALLED_DIR" && -d "$VCPKG_INSTALLED_DIR" ]] || VCPKG_INSTALLED_DIR=""
    fi
    if [[ -z "${VCPKG_TARGET_TRIPLET:-}" && -f "$SDK_BUILD/CMakeCache.txt" ]]; then
        VCPKG_TARGET_TRIPLET="$(sed -n 's/^VCPKG_TARGET_TRIPLET:.*=//p' "$SDK_BUILD/CMakeCache.txt" | head -n 1)"
        [[ -n "$VCPKG_TARGET_TRIPLET" ]] || VCPKG_TARGET_TRIPLET="x64-linux"
    fi
    return 0
}

# Shared consumer configure helper (uses the installed SDK + vcpkg closure).
configure_consumer() {
    local build_dir="$1" prefix="$2"
    local prefix_path="$prefix"
    [[ -n "${VCPKG_INSTALLED_DIR:-}" ]] \
        && prefix_path="${prefix_path};${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET:-x64-linux}"
    [[ -n "${CMAKE_PREFIX_PATH:-}" ]] && prefix_path="${prefix_path};${CMAKE_PREFIX_PATH}"
    local args=(
        -S "$REPO_ROOT/tests/install_consumer" -B "$build_dir"
        "-DCMAKE_PREFIX_PATH=$prefix_path"
        -DCMAKE_BUILD_TYPE=Release
        "-DVCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR:-}"
        "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET:-x64-linux}"
    )
    command -v ninja >/dev/null 2>&1 && args+=( -G Ninja )
    local vcpkg_tc="$REPO_ROOT/vcpkg_bootstrap/scripts/buildsystems/vcpkg.cmake"
    if [[ -f "$vcpkg_tc" ]]; then
        args+=( "-DCMAKE_TOOLCHAIN_FILE=$vcpkg_tc" )
    elif [[ -n "${CMAKE_TOOLCHAIN_FILE:-}" ]]; then
        args+=( "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}" )
    fi
    cmake "${args[@]}" >&2 || return 1
}

CONS_BUILD=""

# ── Checks 2-4: C++ consumer ────────────────────────────────────────────────
chk_cpp_configure() {
    [[ -n "${SDK_PREFIX:-}" ]] || return 1
    CONS_BUILD="$(mktemp_dir chronon3d_sdk_product_consumer)"
    configure_consumer "$CONS_BUILD" "$SDK_PREFIX"
}

chk_cpp_build() {
    [[ -n "$CONS_BUILD" && -d "$CONS_BUILD" ]] || return 1
    cmake --build "$CONS_BUILD" --target check_install check_full >&2
}

chk_cpp_render() {
    [[ -n "$CONS_BUILD" && -x "$CONS_BUILD/check_install" ]] || return 1
    local out rc
    set +e
    out="$(cd "$CONS_BUILD" && ./check_install 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"[BOUNDARY-OK]"* ]] || { log "check_install: $out"; return 1; }
    set +e
    out="$(cd "$CONS_BUILD" && ./check_full 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"[FULL-OK]"* ]] || { log "check_full: $out"; return 1; }
    return 0
}

# ── Check 5: C consumer build ───────────────────────────────────────────────
chk_c_build() {
    [[ -n "$CONS_BUILD" && -d "$CONS_BUILD" ]] || return 1
    cmake --build "$CONS_BUILD" --target check_c_api >&2
}

# ── Checks 6, 7, 12: Python ctypes probe (single embedded program) ─────────
PYPROBE=""

write_pyprobe() {
    [[ -n "$PYPROBE" && -f "$PYPROBE" ]] && return 0
    PYPROBE="$WORK_DIR/cabi_probe.py"
    cat > "$PYPROBE" <<'PYEOF'
import ctypes
import sys

CHRONON_OK = 0
CHRONON_ERROR_BUFFER_TOO_SMALL = 9

VALID_PLAN = (
    b'{"schema":"chronon.render-plan","version":1,'
    b'"canvas":{"width":8,"height":8,"fps":30,"duration_frames":1},'
    b'"layers":[{"id":"bg","type":"color","color":[0.2,0.4,0.6,1.0]}],'
    b'"output":{"path":"out.png"}}'
)
INVALID_PLAN = (
    b'{"schema":"chronon.render-plan","version":1,'
    b'"layers":[],"output":{"path":"out.png"}}'
)

class EngineConfig(ctypes.Structure):
    _fields_ = [("struct_size", ctypes.c_uint32),
                ("abi_version", ctypes.c_uint32),
                ("assets_root", ctypes.c_char_p),
                ("flags", ctypes.c_uint32)]

class ErrorInfo(ctypes.Structure):
    _fields_ = [("struct_size", ctypes.c_uint32),
                ("status", ctypes.c_int),
                ("message", ctypes.c_char_p),
                ("code", ctypes.c_char_p),
                ("component", ctypes.c_char_p),
                ("node_id", ctypes.c_char_p),
                ("asset", ctypes.c_char_p)]

class FrameInfo(ctypes.Structure):
    _fields_ = [("width", ctypes.c_uint32),
                ("height", ctypes.c_uint32),
                ("stride", ctypes.c_uint32),
                ("pixel_format", ctypes.c_uint32),
                ("size", ctypes.c_uint64)]


def load(lib):
    c = ctypes.CDLL(lib)
    c.chronon_abi_version.restype = ctypes.c_uint32
    c.chronon_engine_create_v2.argtypes = [
        ctypes.POINTER(EngineConfig),
        ctypes.POINTER(ctypes.c_void_p),
        ctypes.POINTER(ErrorInfo),
    ]
    c.chronon_engine_create_v2.restype = ctypes.c_int
    c.chronon_plan_compile_json_n.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint64,
        ctypes.POINTER(ctypes.c_void_p),
    ]
    c.chronon_plan_compile_json_n.restype = ctypes.c_int
    c.chronon_render_frame_into.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint64,
        ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(FrameInfo),
    ]
    c.chronon_render_frame_into.restype = ctypes.c_int
    c.chronon_plan_destroy.argtypes = [ctypes.c_void_p]
    c.chronon_engine_destroy.argtypes = [ctypes.c_void_p]
    return c


def make_engine(c):
    cfg = EngineConfig(ctypes.sizeof(EngineConfig), c.chronon_abi_version(), None, 0)
    err = ErrorInfo(ctypes.sizeof(ErrorInfo), 0, None, None, None, None, None)
    engine = ctypes.c_void_p()
    if c.chronon_engine_create_v2(ctypes.byref(cfg), ctypes.byref(engine),
                                  ctypes.byref(err)) != CHRONON_OK or not engine:
        raise RuntimeError("engine create failed: %s"
                           % (err.message.decode() if err.message else "no error"))
    return engine


def compile_plan(c, engine, source):
    plan = ctypes.c_void_p()
    status = c.chronon_plan_compile_json_n(engine, source, len(source),
                                           ctypes.byref(plan))
    return status, plan


def cmd_abi(c):
    version = c.chronon_abi_version()
    if version != 2:
        print("ABI_VERSION_MISMATCH: %d" % version)
        return 1
    print("ABI_VERSION_OK: 2")
    return 0


def cmd_compile(c):
    engine = make_engine(c)
    try:
        status, plan = compile_plan(c, engine, VALID_PLAN)
        if status != CHRONON_OK or not plan:
            print("COMPILE_PLAN_FAILED: status=%d" % status)
            return 1
        print("COMPILE_PLAN_OK")
        return 0
    finally:
        c.chronon_engine_destroy(engine)


def cmd_schema(c):
    engine = make_engine(c)
    try:
        status, plan = compile_plan(c, engine, INVALID_PLAN)
        if status == CHRONON_OK:
            print("SCHEMA_REJECT_FAILED: invalid plan was accepted")
            return 1
        print("SCHEMA_REJECT_OK: status=%d" % status)
        return 0
    finally:
        c.chronon_engine_destroy(engine)


def cmd_consume(c):
    engine = make_engine(c)
    plan = ctypes.c_void_p()
    try:
        status, plan = compile_plan(c, engine, VALID_PLAN)
        if status != CHRONON_OK or not plan:
            print("PYTHON_CONSUMER_FAILED: compile status=%d" % status)
            return 1
        info = FrameInfo()
        status = c.chronon_render_frame_into(engine, plan, 0, None, 0,
                                             ctypes.byref(info))
        if status != CHRONON_ERROR_BUFFER_TOO_SMALL or info.size == 0:
            print("PYTHON_CONSUMER_FAILED: size query status=%d" % status)
            return 1
        buf = ctypes.create_string_buffer(info.size)
        status = c.chronon_render_frame_into(engine, plan, 0, buf, info.size,
                                             ctypes.byref(info))
        if status != CHRONON_OK:
            print("PYTHON_CONSUMER_FAILED: render status=%d" % status)
            return 1
        if not any(buf.raw):
            print("PYTHON_CONSUMER_FAILED: rendered frame is empty")
            return 1
        print("PYTHON_CONSUMER_PASS")
        return 0
    finally:
        c.chronon_plan_destroy(plan)
        c.chronon_engine_destroy(engine)


def main():
    if len(sys.argv) != 3:
        print("usage: cabi_probe.py <lib.so> <abi|compile|schema|consume>")
        return 2
    lib, sub = sys.argv[1], sys.argv[2]
    c = load(lib)
    if sub == "abi":
        return cmd_abi(c)
    if sub == "compile":
        return cmd_compile(c)
    if sub == "schema":
        return cmd_schema(c)
    if sub == "consume":
        return cmd_consume(c)
    print("unknown subcommand: %s" % sub)
    return 2


if __name__ == "__main__":
    sys.exit(main())
PYEOF
}

CABI_LIB() {
    [[ -n "${SDK_PREFIX:-}" ]] || return 1
    echo "$SDK_PREFIX/lib/libchronon3d_c.so"
}

run_pyprobe() {
    write_pyprobe
    python3 "$PYPROBE" "$(CABI_LIB)" "$1"
}

chk_abi_version() { run_pyprobe abi; }
chk_c_compile()   { run_pyprobe compile; }
chk_schema()      { run_pyprobe schema; }

# ── Check 11: Python consumer (permanent sdk_consumers/05_python project) ────
chk_python() {
    [[ -d "$REPO_ROOT/sdk_consumers/05_python" ]] || { log "sdk_consumers/05_python not present"; return 2; }
    [[ -n "${SDK_PREFIX:-}" ]] || { log "SDK_PREFIX not set"; return 1; }
    local out rc
    set +e
    out="$(SDK_PREFIX="$SDK_PREFIX" bash "$REPO_ROOT/sdk_consumers/05_python/run.sh" 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"PYTHON_CONSUMER_PASS"* ]] || { log "python consumer: $out"; return 1; }
    return 0
}

# ── Check 8: C render (run check_c_api binary) ──────────────────────────────
chk_c_render() {
    [[ -n "$CONS_BUILD" && -x "$CONS_BUILD/check_c_api" ]] || return 1
    local out rc
    set +e
    out="$(cd "$CONS_BUILD" && LD_LIBRARY_PATH="$SDK_PREFIX/lib:${LD_LIBRARY_PATH:-}" ./check_c_api 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"C_ABI_CONSUMER_PASS"* ]] || { log "check_c_api: $out"; return 1; }
    return 0
}

# ── Checks 9-10: Go / Rust consumers (permanent sdk_consumers projects) ─────
# Each project's run.sh resolves SDK_PREFIX and builds+renders against the
# installed package only (never the source tree).  SKIP only when the project
# directory is absent; otherwise a build/render/marker failure is a FAIL.
chk_go() {
    [[ -d "$REPO_ROOT/sdk_consumers/03_go" ]] || { log "sdk_consumers/03_go not present"; return 2; }
    [[ -n "${SDK_PREFIX:-}" ]] || { log "SDK_PREFIX not set"; return 1; }
    local out rc
    set +e
    out="$(SDK_PREFIX="$SDK_PREFIX" bash "$REPO_ROOT/sdk_consumers/03_go/run.sh" 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"GO_CONSUMER_PASS"* ]] || { log "go consumer: $out"; return 1; }
    return 0
}

chk_rust() {
    [[ -d "$REPO_ROOT/sdk_consumers/04_rust" ]] || { log "sdk_consumers/04_rust not present"; return 2; }
    [[ -n "${SDK_PREFIX:-}" ]] || { log "SDK_PREFIX not set"; return 1; }
    local out rc
    set +e
    out="$(SDK_PREFIX="$SDK_PREFIX" bash "$REPO_ROOT/sdk_consumers/04_rust/run.sh" 2>&1)"; rc=$?
    set -e
    [[ "$rc" -eq 0 && "$out" == *"RUST_CONSUMER_PASS"* ]] || { log "rust consumer: $out"; return 1; }
    return 0
}

# ── Check 13: no internal headers in consumer sources ───────────────────────
chk_no_internal_headers() {
    local banned rx hits
    banned='internal/|render_graph/|runtime/|advanced'
    rx="^[[:space:]]*#[[:space:]]*include[[:space:]]*<(chronon3d/(${banned})|.*internal\\.hpp|.*runtime\\.hpp|.*/test/)"
    hits="$(grep -rnE "$rx" "$REPO_ROOT/tests/install_consumer" --include='*.cpp' --include='*.c' --include='*.hpp' 2>/dev/null || true)"
    if [[ -n "$hits" ]]; then
        log "internal headers leaked into consumer sources:"
        printf '%s\n' "$hits" >&2
        return 1
    fi
    return 0
}

# ── Check 14: no internal CMake targets in consumer build ───────────────────
# Comprehensive public-target rule gate: scans every external consumer project
# (sdk_consumers/, tests/install_consumer, tests/package_consumer, examples/*,
# templates/basic) and fails on any link to an internal or non-public target.
# See tools/check_public_targets.sh.
chk_no_internal_targets() {
    REPO_ROOT="$REPO_ROOT" bash "$REPO_ROOT/tools/check_public_targets.sh"
}

# ── Check 15: relocation test ───────────────────────────────────────────────
chk_relocate() {
    [[ -n "${SDK_PREFIX:-}" ]] || return 1
    local reloc_prefix reloc_build
    reloc_prefix="$WORK_DIR/relocated-prefix"
    reloc_build="$(mktemp_dir chronon3d_sdk_product_reloc)"
    log "cloning installed prefix for relocation"
    if ! cp -al "$SDK_PREFIX" "$reloc_prefix" 2>/dev/null; then
        rm -rf "$reloc_prefix"
        cp -a "$SDK_PREFIX" "$reloc_prefix"
    fi
    configure_consumer "$reloc_build" "$reloc_prefix" || return 1
    cmake --build "$reloc_build" --target check_c_api >&2 || return 1
    return 0
}

# ── Driver ──────────────────────────────────────────────────────────────────
log "verify_sdk_product starting (preset=$PRESET fast=$FAST repo=$REPO_ROOT)"

check 1  "install SDK"               chk_install
check 2  "C++ consumer configure"    chk_cpp_configure
check 3  "C++ consumer build"        chk_cpp_build
check 4  "C++ render"                chk_cpp_render
check 5  "C consumer build"          chk_c_build
check 6  "C ABI version check"       chk_abi_version
check 7  "C RenderPlan compile"      chk_c_compile
check 8  "C render"                  chk_c_render
check 9  "Go consumer"               chk_go
check 10 "Rust consumer"             chk_rust
check 11 "Python consumer"           chk_python
check 12 "RenderPlan schema validation" chk_schema
check 13 "no internal headers"       chk_no_internal_headers
check 14 "no internal CMake targets" chk_no_internal_targets
check 15 "relocation test"           chk_relocate

log "──────────────────────────────────────────────────────────────"
log "passed: $PASSED  failed: $FAILED  skipped: $SKIPPED"
if [[ "$FAILED" -ne 0 ]]; then
    log "CHRONON_SDK_PRODUCT_FAIL"
    exit 1
fi
echo "CHRONON_SDK_PRODUCT_PASS"
exit 0
