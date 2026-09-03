#!/usr/bin/env python3
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PRESETS = ROOT / "cmake" / "presets"

OBSOLETE_KEYS = {
    "CHRONON3D_BUILD_CONTENT",
    "CHRONON3D_BUILD_DIAGNOSTICS",
    "CHRONON3D_ENABLE_VIDRUSH",
}


def fail(message: str) -> None:
    raise SystemExit(f"CMAKE_CLEANUP_FAIL: {message}")


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def save_json(path: Path, data: dict) -> None:
    path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def preset_by_name(data: dict) -> dict[str, dict]:
    return {p["name"]: p for p in data.get("configurePresets", [])}


def remove_obsolete_preset_keys() -> None:
    for path in sorted(PRESETS.glob("*.json")):
        if path.name == "vulkan-focused.json":
            continue
        data = load_json(path)
        changed = False
        for preset in data.get("configurePresets", []):
            cache = preset.get("cacheVariables")
            if not isinstance(cache, dict):
                continue
            for key in OBSOLETE_KEYS:
                if key in cache:
                    del cache[key]
                    changed = True
        if changed:
            save_json(path, data)


def clean_preset_descriptions() -> None:
    path = PRESETS / "development.json"
    data = load_json(path)
    presets = preset_by_name(data)
    required = {
        "linux-ci-core-gate",
        "linux-ci-lean-gate",
        "linux-asan",
        "linux-tsan",
        "linux-dev",
        "linux-content-dev",
    }
    missing = required - presets.keys()
    if missing:
        fail(f"development preset(s) missing: {sorted(missing)}")
    presets["linux-ci-core-gate"]["displayName"] = "Linux CI Core Gate"
    presets["linux-ci-core-gate"]["description"] = (
        "Pre-merge core gate: tests on, text/video off. Diagnostics and "
        "verification follow the canonical test policy."
    )
    presets["linux-ci-lean-gate"]["displayName"] = "Linux CI Lean Gate"
    presets["linux-ci-lean-gate"]["description"] = (
        "Pre-merge lean gate: CLI + text + Blend2D + tests, with video and "
        "benchmarks disabled. Diagnostics and verification follow the canonical test policy."
    )
    presets["linux-asan"]["description"] = (
        "AddressSanitizer + UndefinedBehaviorSanitizer debug preset with tests, "
        "CLI, text and Blend2D enabled. Diagnostics are enabled by the canonical test policy."
    )
    presets["linux-tsan"]["description"] = (
        "ThreadSanitizer debug preset with tests, CLI, text and Blend2D enabled. "
        "Diagnostics are enabled by the canonical test policy."
    )
    presets["linux-dev"]["description"] = (
        "Daily developer preset: CLI + telemetry + tests, Debug build, unity enabled. "
        "Bundled compositions are external."
    )
    presets["linux-content-dev"]["displayName"] = "Linux Video Dev (legacy preset name)"
    presets["linux-content-dev"]["description"] = (
        "Video development preset retained under the historical linux-content-dev name "
        "for compatibility. Bundled compositions are external."
    )
    save_json(path, data)

    path = PRESETS / "fast-variants.json"
    data = load_json(path)
    presets = preset_by_name(data)
    presets["linux-turbo"]["description"] = (
        "Minimal Debug CLI preset used by build-fast.sh turbo and turbo-inc; "
        "tests and video are disabled."
    )
    presets["linux-fast-dev-release"]["description"] = (
        "RelWithDebInfo incremental CLI + test build used by build-fast.sh release."
    )
    save_json(path, data)

    path = PRESETS / "linux-fast-dev.json"
    data = load_json(path)
    data["configurePresets"][0]["description"] = (
        "Daily inner-loop preset consumed by build-fast.sh: CLI + tests + text + Blend2D, "
        "with video and benchmarks disabled."
    )
    save_json(path, data)

    path = PRESETS / "linux-video-fast-dev.json"
    data = load_json(path)
    data["configurePresets"][0]["description"] = (
        "GPU/video inner loop: Vulkan, CUDA interop, native FFmpeg, text and CLI; "
        "tests and benchmarks disabled."
    )
    save_json(path, data)

    path = PRESETS / "profiling.json"
    data = load_json(path)
    descriptions = {
        "linux-profile-core": "Hidden core-only profile matrix preset used by the nightly profile-envelope workflow.",
        "linux-profile-motion": "Hidden profile matrix preset with Blend2D and text enabled.",
        "linux-profile-video": "Hidden profile matrix preset with native FFmpeg, telemetry, Blend2D and text enabled.",
        "linux-profile-extended": "Hidden extended feature envelope with mesh, EXR, telemetry, benchmarks and tracing enabled.",
    }
    for preset in data.get("configurePresets", []):
        if preset["name"] in descriptions:
            preset["description"] = descriptions[preset["name"]]
    save_json(path, data)

    path = PRESETS / "optimizations.json"
    data = load_json(path)
    descriptions = {
        "__release-pgo-base": "Shared hidden base for PGO, ThinLTO and BOLT release optimization. Floating-point contraction remains disabled by the root build contract.",
        "release-pgo": "PGO profile-use build. Requires a pre-collected merged.profdata at the configured profile path.",
        "release-thinlto": "ThinLTO release build without profile collection.",
        "release-pgo-thinlto": "Combined PGO profile-use and ThinLTO release build. Requires a pre-collected merged.profdata.",
        "release-pgo-thinlto-bolt": "Combined PGO, ThinLTO and BOLT release build. Requires merged.profdata plus BOLT perf.fdata.",
    }
    for preset in data.get("configurePresets", []):
        if preset["name"] in descriptions:
            preset["description"] = descriptions[preset["name"]]
    save_json(path, data)

    path = PRESETS / "ci.json"
    data = load_json(path)
    presets = preset_by_name(data)
    presets["linux-ci"]["description"] = (
        "Daily CI for the engine core: CLI + tests + install consumer, with bundled compositions external."
    )
    presets["linux-ci-nocontent"]["displayName"] = "Linux CI core (compatibility alias)"
    save_json(path, data)


def repair_cfi_preset() -> None:
    path = PRESETS / "hardening.json"
    data = load_json(path)
    presets = preset_by_name(data)
    cfi = presets.get("linux-cfi")
    if cfi is None:
        fail("linux-cfi preset missing")
    cache = cfi.setdefault("cacheVariables", {})
    cache["CHRONON3D_ENABLE_IPC"] = "ON"
    cache["CHRONON3D_BUILD_DAEMON"] = "ON"
    cfi["description"] = (
        "Clang-only CFI lane for the persistent IPC daemon; requires LTO, IPC and daemon support."
    )
    save_json(path, data)


def restore_windows_presets() -> None:
    windows = {
        "version": 6,
        "include": ["base.json"],
        "configurePresets": [
            {
                "name": "win-release",
                "inherits": "base",
                "condition": {
                    "type": "equals",
                    "lhs": "${hostSystemName}",
                    "rhs": "Windows",
                },
                "displayName": "Windows Release CI",
                "description": (
                    "Windows release CI preset matching the win/win-test aliases "
                    "consumed by .github/workflows/ci.yml."
                ),
                "cacheVariables": {
                    "CMAKE_BUILD_TYPE": "Release",
                    "VCPKG_TARGET_TRIPLET": "x64-windows",
                    "CHRONON3D_BUILD_CLI": "ON",
                    "CHRONON3D_BUILD_TESTS": "ON",
                    "CHRONON3D_ENABLE_IPC": "ON",
                    "CHRONON3D_ENABLE_VERIFICATION": "ON",
                    "VCPKG_MANIFEST_FEATURES": "tests;ipc",
                },
            }
        ],
        "buildPresets": [
            {"name": "win", "configurePreset": "win-release", "jobs": 8}
        ],
        "testPresets": [
            {
                "name": "win-test",
                "configurePreset": "win-release",
                "output": {"outputOnFailure": True},
            }
        ],
    }
    save_json(PRESETS / "windows.json", windows)

    path = ROOT / "CMakePresets.json"
    data = load_json(path)
    required = data.setdefault("cmakeMinimumRequired", {})
    required.update({"major": 3, "minor": 27, "patch": 0})
    include = data.setdefault("include", [])
    windows_include = "cmake/presets/windows.json"
    if windows_include not in include:
        include.append(windows_include)
    save_json(path, data)


def delete_empty_vulkan_preset() -> None:
    path = PRESETS / "vulkan-focused.json"
    if not path.exists():
        return
    if path.read_text(encoding="utf-8").strip():
        fail("vulkan-focused.json is no longer empty; refusing to delete concurrent work")
    path.unlink()


def patch_root_cmake() -> None:
    path = ROOT / "CMakeLists.txt"
    text = path.read_text(encoding="utf-8")

    old = "add_compile_options(-ffp-contract=off)"
    new = """if(MSVC)\n    add_compile_options(/fp:strict)\nelseif(CMAKE_CXX_COMPILER_ID MATCHES \"GNU|Clang\")\n    add_compile_options(-ffp-contract=off)\nendif()"""
    if old in text:
        text = text.replace(old, new, 1)
    elif new not in text:
        fail("root floating-point flag anchor missing")

    old_linker = "if(NOT CMAKE_CXX_LINKER_LAUNCHER)"
    new_linker = "if(NOT CMAKE_CXX_LINKER_LAUNCHER AND NOT MSVC)"
    if old_linker in text:
        text = text.replace(old_linker, new_linker, 1)
    elif new_linker not in text:
        fail("root linker probe anchor missing")

    text = text.replace("--preset linux-release", "--preset linux-fast-dev")

    if "option(CHRONON3D_BOLT_POSTPROCESS" not in text:
        anchor = (
            'option(CHRONON3D_BUILD_TIME_TRACE\n'
            '       "Emit Clang -ftime-trace JSON files for compile-time profiling" OFF)\n'
        )
        if anchor not in text:
            fail("BOLT option insertion anchor missing")
        text = text.replace(
            anchor,
            anchor
            + 'option(CHRONON3D_BOLT_POSTPROCESS\n'
            + '       "Enable LLVM BOLT post-link optimization targets" OFF)\n',
            1,
        )

    include_line = "include(${CMAKE_SOURCE_DIR}/cmake/bolt_postprocess.cmake)"
    if include_line not in text:
        anchor = (
            "if(CHRONON3D_BUILD_BENCHMARKS)\n"
            "    add_subdirectory(apps/chronon3d_bench)\n"
            "endif()\n"
        )
        if anchor not in text:
            fail("BOLT include insertion anchor missing")
        text = text.replace(
            anchor,
            anchor
            + "\nif(CHRONON3D_BOLT_POSTPROCESS)\n"
            + f"    {include_line}\n"
            + "endif()\n",
            1,
        )

    path.write_text(text, encoding="utf-8")


def rewrite_bolt_module() -> None:
    (ROOT / "cmake" / "bolt_postprocess.cmake").write_text(
        """# Canonical BOLT post-link target for the release-pgo-thinlto-bolt preset.\n"
        "find_program(LLVM_BOLT_EXECUTABLE NAMES llvm-bolt bolt)\n"
        "if(NOT LLVM_BOLT_EXECUTABLE)\n"
        "    message(FATAL_ERROR\n"
        "        \"CHRONON3D_BOLT_POSTPROCESS requires llvm-bolt on PATH. \"\n"
        "        \"Use release-pgo-thinlto when BOLT is unavailable.\")\n"
        "endif()\n\n"
        "if(NOT TARGET chronon3d_cli)\n"
        "    message(FATAL_ERROR \"CHRONON3D_BOLT_POSTPROCESS requires CHRONON3D_BUILD_CLI=ON\")\n"
        "endif()\n"
        "if(NOT CHRONON3D_BOLT_DATA_PATH)\n"
        "    message(FATAL_ERROR \"CHRONON3D_BOLT_DATA_PATH must point to a perf2bolt .fdata file\")\n"
        "endif()\n\n"
        "add_custom_target(bolt-postprocess\n"
        "    COMMAND ${LLVM_BOLT_EXECUTABLE}\n"
        "            $<TARGET_FILE:chronon3d_cli>\n"
        "            -data ${CHRONON3D_BOLT_DATA_PATH}\n"
        "            -o $<TARGET_FILE:chronon3d_cli>.bolt\n"
        "            -relocs\n"
        "    DEPENDS chronon3d_cli\n"
        "    COMMENT \"BOLT post-link optimization for chronon3d_cli\"\n"
        "    VERBATIM\n"
        ")\n""",
        encoding="utf-8",
    )


def rewrite_canaries() -> None:
    (ROOT / "cmake" / "Chronon3DCanarySymbols.cmake").write_text(
        """# Canonical SDK archive canaries: AREA|SYMBOL|GUARD|TARGET.\n"
        "# Guards must name real root CMake options; retired compatibility flags are forbidden.\n"
        "set(CHRONON3D_SDK_CANARY_SYMBOLS\n"
        "    \"core|chronon3d::detail::parse_proc_stat|always|chronon3d_core_impl\"\n"
        "    \"animations|chronon3d::temporal::generate_temporal_samples|always|chronon3d_animations\"\n"
        "    \"scene|chronon3d::camera_v1::register_camera_v1_builtins|always|chronon3d_scene\"\n"
        "    \"runtime|chronon3d::RenderSession::arena|always|chronon3d_runtime\"\n"
        "    \"graph|chronon3d::graph::register_pipeline_graph_nodes|always|chronon3d_graph_pipeline\"\n"
        "    \"software_backend|chronon3d::SoftwareRenderer::buffer_ring|always|chronon3d_backend_software\"\n"
        "    \"text_core|chronon3d::build_text_run|CHRONON3D_ENABLE_TEXT|chronon3d_text_core\"\n"
        "    \"diagnostics|chronon3d::renderer::diagnostics::draw_bbox_overlay|CHRONON3D_ENABLE_DIAGNOSTICS|chronon3d_backend_software_diagnostics\"\n"
        "    \"sdk|chronon3d::sdk::RenderEngine|always|chronon3d_runtime\"\n"
        "    \"ar_race|arch:ar_t_post_nm_non_empty|always|chronon3d_sdk_impl\"\n"
        ")\n""",
        encoding="utf-8",
    )


def rewrite_sdk_gate_scripts() -> None:
    (ROOT / "tools" / "sdk" / "check_archive_canaries.sh").write_text(
        r'''#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${SDK_BUILD:?SDK_BUILD env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required}"

impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive" ]] || fail "libchronon3d_sdk_impl.a not found in $SDK_PREFIX"
[[ -d "$SDK_PREFIX/include/chronon3d" ]] || fail "public headers missing from $SDK_PREFIX/include/chronon3d"
find "$SDK_PREFIX/include/chronon3d" -type f -name '*.hpp' -print -quit | grep -q . \
    || fail "no installed public headers found"

text_on="$(cache_var CHRONON3D_ENABLE_TEXT)"; : "${text_on:=ON}"
diag_on="$(cache_var CHRONON3D_ENABLE_DIAGNOSTICS)"; : "${diag_on:=OFF}"
log "canary guards: text=$text_on diagnostics=$diag_on"

canary_file="$REPO_ROOT/cmake/Chronon3DCanarySymbols.cmake"
[[ -f "$canary_file" ]] || fail "canary catalog not found: $canary_file"
canary_entries="$(grep -oE '"[a-z_]+\|[a-zA-Z0-9_:]+\|[a-zA-Z0-9_]+\|[a-zA-Z0-9_]+"' "$canary_file" || true)"
[[ -n "$canary_entries" ]] || fail "no canonical canary entries parsed from $canary_file"

GATE_TMP="$(mktemp_dir chronon3d_install_gate)"
cleanup_register "$GATE_TMP"
ar_before="$GATE_TMP/ar_before.txt"
ar_after="$GATE_TMP/ar_after.txt"
nm_dump="$GATE_TMP/nm.txt"

ar t "$impl_archive" > "$ar_before" || fail "ar t failed on $impl_archive"
ar_count="$(wc -l < "$ar_before" | tr -d ' ')"
(( ar_count >= 2 )) || fail "SDK archive contains only $ar_count object(s)"
nm -C "$impl_archive" > "$nm_dump" || fail "nm -C failed on $impl_archive"
ar t "$impl_archive" > "$ar_after" || fail "post-nm ar t failed on $impl_archive"
ar_count_after="$(wc -l < "$ar_after" | tr -d ' ')"
(( ar_count_after >= 1 )) || fail "post-nm SDK archive listing is empty"
if (( ar_count_after != ar_count )); then
    log "WARN: SDK archive object count drifted across nm: before=$ar_count after=$ar_count_after"
fi

checked=0
skipped=0
missing=0
fail_list=""
while IFS= read -r entry; do
    body="${entry#\"}"
    body="${body%\"}"
    IFS='|' read -r area symbol guard target <<<"$body"
    skip_reason=""
    case "$guard" in
        always) : ;;
        CHRONON3D_ENABLE_TEXT)
            [[ "$text_on" == "ON" ]] || skip_reason="CHRONON3D_ENABLE_TEXT=$text_on"
            ;;
        CHRONON3D_ENABLE_DIAGNOSTICS)
            [[ "$diag_on" == "ON" ]] || skip_reason="CHRONON3D_ENABLE_DIAGNOSTICS=$diag_on"
            ;;
        *) fail "unknown canary guard '$guard' for '$area'" ;;
    esac

    if [[ -n "$skip_reason" ]]; then
        log "SKIP: canary $area [$target] ($skip_reason)"
        skipped=$((skipped + 1))
        continue
    fi

    if [[ "$symbol" == "arch:ar_t_post_nm_non_empty" ]]; then
        log "OK: structural canary $area [$target]"
        checked=$((checked + 1))
    elif grep -F -q -- "$symbol" "$nm_dump"; then
        log "OK: canary $area [$target] :: $symbol"
        checked=$((checked + 1))
    else
        log "FAIL: canary $area [$target] :: '$symbol' missing"
        missing=$((missing + 1))
        fail_list="${fail_list}${fail_list:+, }$area"
    fi
done <<<"$canary_entries"

(( missing == 0 )) || fail "$missing SDK canary symbol(s) missing: $fail_list"
log "Canary gate: $checked present, $skipped skipped, 0 missing"
''',
        encoding="utf-8",
    )

    (ROOT / "tools" / "sdk" / "check_feature_ghosts.sh").write_text(
        r'''#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=./common.sh
source "$HERE/common.sh"

: "${SDK_PREFIX:?SDK_PREFIX env var required}"
: "${SDK_BUILD:?SDK_BUILD env var required}"
: "${REPO_ROOT:?REPO_ROOT env var required}"
: "${PRESET:?PRESET env var required}"

log "diagnostics ghost sweep starting (tests=OFF, diagnostics=OFF)"
cmake -S "$REPO_ROOT" -B "$SDK_BUILD" --preset "$PRESET" \
    -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX" \
    -DCHRONON3D_BUILD_TESTS=OFF \
    -DCHRONON3D_ENABLE_DIAGNOSTICS=OFF 1>&2 \
    || fail "diagnostics ghost sweep reconfigure failed"
cmake --build "$SDK_BUILD" --target chronon3d_sdk_impl -j8 1>&2 \
    || fail "diagnostics ghost sweep SDK rebuild failed"
cmake --install "$SDK_BUILD" --prefix "$SDK_PREFIX" 1>&2 \
    || fail "diagnostics ghost sweep install failed"

impl_archive="$(find "$SDK_PREFIX" -type f -name 'libchronon3d_sdk_impl.a' 2>/dev/null | head -1 || true)"
[[ -n "$impl_archive" ]] || fail "SDK archive missing after diagnostics-OFF rebuild"
GATE_TMP="$(mktemp_dir chronon3d_install_gate_off)"
cleanup_register "$GATE_TMP"
nm_dump="$GATE_TMP/nm_off.txt"
nm -C "$impl_archive" > "$nm_dump" || fail "nm -C failed on diagnostics-OFF archive"

symbol='chronon3d::renderer::diagnostics::draw_bbox_overlay'
if grep -F -q -- "$symbol" "$nm_dump"; then
    fail "GHOST-FAIL: diagnostics symbol leaked into CHRONON3D_ENABLE_DIAGNOSTICS=OFF archive: $symbol"
fi
log "GHOST-OK: diagnostics-OFF archive contains no diagnostics overlay symbol"
''',
        encoding="utf-8",
    )


def remove_config_duplication() -> None:
    path = ROOT / "cmake" / "Chronon3DConfig.cmake.in"
    text = path.read_text(encoding="utf-8")
    duplicate = """# The C ABI target is exported with the short internal name `C` for\n# compatibility with the existing target file.  Re-expose it through the\n# namespaced public spelling used by installed C consumers.\nif(TARGET C AND NOT TARGET Chronon3D::C)\n    add_library(Chronon3D::C ALIAS C)\nendif()\n\n"""
    count = text.count("add_library(Chronon3D::C ALIAS C)")
    if count == 2:
        if duplicate not in text:
            fail("C ABI duplicate alias block shape changed")
        text = text.replace(duplicate, "", 1)
    elif count != 1:
        fail(f"unexpected Chronon3D::C alias count: {count}")
    path.write_text(text, encoding="utf-8")

    path = ROOT / "cmake" / "Chronon3DSdkArchive.cmake"
    text = path.read_text(encoding="utf-8")
    paragraph = """    # TICKET-SDK-PACKAGING-CONSOLIDATION — build the diagnostic body in a\n    # single multi-line string and emit ONE message(FATAL_ERROR ...).\n    # An earlier revision used a `foreach(); message(FATAL_ERROR); endforeach()`\n    # pattern which the cmake runtime halts after the first iteration, so\n    # only the first archive path was reported.  Aggregating the body in a\n    # string ensures every offender is enumerated before cmake exits.\n"""
    if text.count(paragraph) >= 2:
        text = text.replace(paragraph + paragraph, paragraph, 1)
    path.write_text(text, encoding="utf-8")


def validate_no_obsolete_authorities() -> None:
    targets = [
        PRESETS,
        ROOT / "cmake" / "Chronon3DCanarySymbols.cmake",
        ROOT / "tools" / "sdk" / "check_archive_canaries.sh",
        ROOT / "tools" / "sdk" / "check_feature_ghosts.sh",
    ]
    for target in targets:
        files = target.rglob("*") if target.is_dir() else [target]
        for path in files:
            if not path.is_file():
                continue
            body = path.read_text(encoding="utf-8", errors="ignore")
            for key in OBSOLETE_KEYS:
                if key in body:
                    fail(f"obsolete authority {key} remains in {path.relative_to(ROOT)}")


def main() -> None:
    remove_obsolete_preset_keys()
    clean_preset_descriptions()
    repair_cfi_preset()
    restore_windows_presets()
    delete_empty_vulkan_preset()
    patch_root_cmake()
    rewrite_bolt_module()
    rewrite_canaries()
    rewrite_sdk_gate_scripts()
    remove_config_duplication()
    validate_no_obsolete_authorities()


if __name__ == "__main__":
    main()
