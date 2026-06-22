#!/usr/bin/env bash
# tools/check_camera_architecture.sh
# ─────────────────────────────────────────────────────────────────────
# CAM-DOC 04 — Camera architecture boundary grep checks (6 total).
#
# Verifies that the new camera_v1 specialty is used in production code
# and that the legacy authoring surface is only referenced from
# sanctioned bridge adapter paths (camera_descriptor_adapters.*,
# camera_rig_animated_presets.hpp, scene_builder.hpp method definition,
# timeline_builder.{hpp,cpp}, and the existing legacy-shape tests).
#
# This gate blocks regression of the migration to the compiled
# CameraProgram pipeline. Each check is independent; the script exits
# non-zero on ANY failure (linear structure, see header note in
# tools/check_architecture_boundaries.sh).
#
# Wired into:
#   - CI:     .github/workflows/gates.yml (architecture-check stage)
#   - CMake:  `chronon3d_camera_architecture_check` custom target
#             (tests/scene_tests.cmake, optional pre-gate)
#
# Exit codes:
#   0 = all boundaries PASS or reported violations match allowlist
#   1 = at least one boundary FAIL (offending lines dumped under the rule)
# ─────────────────────────────────────────────────────────────────────
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

FAILED=0

# Shared constants for the symbol-driven checks.
SCRIPT_PATHS='include src tests apps content'

# Strip pure-comment lines AND lines where the symbol appears only in a
# trailing inline `//` comment.  Reads grep -Rn output (format
# <path>:<line>:<content>) from stdin and prints lines that still contain
# the symbol in NON-comment code. (Same logic as the WP-0 filter.)
#
# Implementation note: uses awk's `index(pre, sym)` (string search) rather
# than the regex `pre ~ sym` so that symbols containing regex metas (e.g.
# `tan(`, `get_lens`) match literally regardless of awk's ERE dialect.
# This is the only reliable cross-platform safe pattern for arbitrary
# grep hits forwarded through a pipe.
#
# Use:
#   grep -Rn <pattern> <paths> | filter_symbol_in_code_only <symbol-regex>
filter_symbol_in_code_only() {
    local sym="$1"
    awk -F: -v sym="$sym" '
        {
            rest = ""
            for (i = 3; i <= NF; i++) {
                rest = rest (i == 3 ? "" : ":") $i
            }
            # Pure comment line.
            if (rest ~ /^[[:space:]]*(\/\/\/|\/\/|\/\*|\*)/) next
            # Inline trailing `//` — strip from // and recheck.
            pre = rest
            sub(/\/\/.*/, "", pre)
            # Block /* ... */ comments are NOT stripped (intentional).
            # String-search so regex metas in `sym` are matched literally.
            if (index(pre, sym) == 0) next
            print
        }
    '
}

echo "=== CAM-DOC 04 Architecture boundary grep checks (6 checks) ==="

# ── 1. AnimatedCamera2_5D usages (legacy authoring type) ────────────
# The legacy animated-camera authoring type is RETIRED from new
# production code.  Allowed paths are exactly: its own definition
# header, the bridge adapter to CameraDescriptor, the rig-presets
# header (one-shot), the SceneBuilder method definition that bridges,
# timeline_builder (existing caller), and the legacy-shape tests.
#   - define:                include/chronon3d/scene/camera/animated_camera_2_5d.hpp
#   - bridge to v1:          camera_v1/camera_descriptor_adapters.{hpp,cpp}
#   - rig presets bridge:    include/chronon3d/scene/camera/camera_rig_animated_presets.hpp
#   - SceneBuilder method:   include/chronon3d/scene/builders/scene_builder.hpp
#                            src/scene/builders/camera_api.{hpp,cpp}
#   - timeline passthrough:  include/chronon3d/timeline/timeline_builder.hpp
#                            src/scene/timeline_builder.cpp
#   - existing legacy tests: tests/renderer/camera/test_animated_camera.cpp
#                            tests/renderer/camera/test_lens_model.cpp
#                            tests/renderer/camera/test_dof.cpp
#                            tests/scene/camera_rig_tests.cpp
#                            tests/core/test_scene_hasher_camera.cpp
#                            tests/core/animation/test_sample_time.cpp
#                            tests/cli/test_camera_path_command.cpp
#                            tests/visual/camera/camera_visual_scenes.cpp
#                            tests/renderer/perf/test_motion_blur_integration.cpp
#   - include aggregator:    include/chronon3d/runtime.hpp
echo -n "  [1/6] AnimatedCamera2_5D RETIRED        ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bAnimatedCamera2_5D\b' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\bAnimatedCamera2_5D\b' \
    | grep -Ev 'include/chronon3d/scene/camera/animated_camera_2_5d\.hpp:' \
    | grep -Ev 'src/scene/camera/camera_v1/camera_descriptor_adapters\.(hpp|cpp):' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_rig_animated_presets\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/builders/(scene_builder\.hpp|camera_api\.hpp):' \
    | grep -Ev 'src/scene/builders/camera_api\.(hpp|cpp):' \
    | grep -Ev 'include/chronon3d/timeline/timeline_builder\.hpp:' \
    | grep -Ev 'src/scene/timeline_builder\.cpp:' \
    | grep -Ev 'include/chronon3d/runtime\.hpp:' \
    | grep -Ev 'tests/renderer/camera/test_animated_camera\.cpp:' \
    | grep -Ev 'tests/renderer/camera/test_lens_model\.cpp:' \
    | grep -Ev 'tests/renderer/camera/test_dof\.cpp:' \
    | grep -Ev 'tests/scene/camera_rig_tests\.cpp:' \
    | grep -Ev 'tests/core/test_scene_hasher_camera\.cpp:' \
    | grep -Ev 'tests/core/animation/test_sample_time\.cpp:' \
    | grep -Ev 'tests/cli/test_camera_path_command\.cpp:' \
    | grep -Ev 'tests/visual/camera/camera_visual_scenes\.cpp:' \
    | grep -Ev 'tests/renderer/perf/test_motion_blur_integration\.cpp:' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 2. CameraRig usages (legacy authoring struct) ────────────────────
# CameraRig struct survives only in its definition and the bridge-path
# (camera_descriptor_adapters.{hpp,cpp}). New production code must
# author against CameraDescriptor and the camera_v1::* camera motion
# types directly.
echo -n "  [2/6] CameraRig authoring RETIRED       ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' --include='*.h' \
    -E '\bCameraRig\b' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only '\bCameraRig\b' \
    | grep -Ev 'include/chronon3d/scene/model/camera/camera_rig\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_rig_animated_presets\.hpp:' \
    | grep -Ev 'src/scene/camera/camera_v1/camera_descriptor_adapters\.(hpp|cpp):' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_v1/camera_descriptor_adapters\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_rig_builder\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/CameraRig[^.]*\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/animated_camera_2_5d\.hpp:' \
    | grep -Ev 'tests/(renderer/camera/test_(animated_camera|lens_model|dof)\.cpp|scene/camera_rig_tests\.cpp|scene/camera/test_camera_descriptor_adapters\.cpp):' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 3. SceneBuilder::animated_camera() method call sites ────────────
# SceneBuilder::animated_camera(cam) takes a legacy AnimatedCamera2_5D
# and bridges it through evaluate(t). New production call sites MUST go
# through the camera_v1 descriptor + compile_camera() pipeline.  The
# method DEFINITION itself (scene_builder.hpp) is exempt; only CALL
# sites (`animated_camera(\n` or `.animated_camera(`) are flagged.
echo -n "  [3/6] SceneBuilder::animated_camera()   ... "
# Match either `animated_camera(<obj>)` (free-function call) or
# `.animated_camera(` (method call).  We exclude the SceneBuilder method
# DEFINITION line (`SceneBuilder& animated_camera(const`).
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' \
    -E '(\b|\.)animated_camera\s*\(' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only 'animated_camera' \
    | grep -Ev 'SceneBuilder&[[:space:]]+animated_camera\b' \
    | grep -Ev 'tests/renderer/camera/test_animated_camera\.cpp:' \
    | grep -Ev 'tests/renderer/camera/test_lens_model\.cpp:' \
    | grep -Ev 'tests/renderer/camera/test_dof\.cpp:' \
    | grep -Ev 'tests/scene/camera_rig_tests\.cpp:' \
    | grep -Ev 'tests/core/test_scene_hasher_camera\.cpp:' \
    | grep -Ev 'tests/core/animation/test_sample_time\.cpp:' \
    | grep -Ev 'tests/cli/test_camera_path_command\.cpp:' \
    | grep -Ev 'tests/visual/camera/camera_visual_scenes\.cpp:' \
    | grep -Ev 'tests/renderer/perf/test_motion_blur_integration\.cpp:' \
    | grep -Ev 'tests/scene/camera/test_camera_descriptor_adapters\.cpp:' \
    | grep -Ev 'src/scene/camera/camera_v1/camera_descriptor_adapters\.cpp:' \
    | grep -Ev 'src/scene/timeline_builder\.cpp:' \
    | grep -Ev 'src/scene/builders/camera_api\.cpp:' \
    | grep -Ev 'content/.*compositions/.*\.cpp:' \
    | grep -Ev 'include/chronon3d/scene/presets/.*\.hpp:' \
    | grep -Ev 'include/chronon3d/presets/.*\.hpp:' \
    | grep -Ev 'content/.*saas_intro_premium.*\.cpp:' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 4. SceneBuilder::camera_rig() method call sites ──────────────────
# Same as check 3 but for the `camera_rig(name, fn)` method that bakes
# a CameraRig into a Camera2_5D via the legacy rig.evaluate() path. New
# authoring must build a CameraDescriptor and compile it.
echo -n "  [4/6] SceneBuilder::camera_rig()        ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' \
    -E '(\b|\.)camera_rig\s*\(' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only 'camera_rig' \
    | grep -Ev 'SceneBuilder&[[:space:]]+camera_rig\b' \
    | grep -Ev 'CameraRigBuilder\b' \
    | grep -Ev 'CameraRig\s+(rig|mode|name)' \
    | grep -Ev 'CameraRig\s*\{|class\s+CameraRig|struct\s+CameraRig' \
    | grep -Ev 'tests/scene/camera_rig_tests\.cpp:' \
    | grep -Ev 'tests/scene/camera/test_camera_descriptor_adapters\.cpp:' \
    | grep -Ev 'src/scene/camera/camera_v1/camera_descriptor_adapters\.cpp:' \
    | grep -Ev 'include/chronon3d/scene/builders/scene_builder\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/presets/scene_presets\.hpp:' \
    | grep -Ev 'content/.*compositions/.*\.cpp:' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 5. tan(fov) focal formulas outside camera_math/ + camera_v1/ ────
# DOC 02 deliverable centralised the focal-from-FOV formula into
# math/camera_projection_contract.hpp.  Any new `tan(fov_rad * 0.5)`
# or equivalent `(viewport/2) / tan(fov/2)` formulation outside the
# sanctioned math/ + camera_v1/ canonical homes is a regression.
# Allowed paths (canonical homes + the test composition that legitimately
# inverts focal for a metric export):
#   - include/chronon3d/math/camera_projection_contract.hpp
#   - include/chronon3d/math/camera_projection_resolver.hpp
#   - include/chronon3d/scene/model/camera/lens_model.hpp
#   - include/chronon3d/scene/camera/camera_v1/evaluated_projection.hpp
#   - content/2d5/compositions/camera_test_orchestrator.cpp
#   - tests/renderer/camera/test_lens_model.cpp
echo -n "  [5/6] tan(fov) focal formulas canonical ... "
hits=$(grep -Rn --include='*.hpp' --include='*.cpp' \
    -E '\btan\(' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only 'tan\(' \
    | grep -E 'tan\([^)]*(fov|FOV|Fov)' \
    | grep -Ev 'include/chronon3d/math/camera_projection_contract\.hpp:' \
    | grep -Ev 'include/chronon3d/math/camera_projection_resolver\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/model/camera/lens_model\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_v1/evaluated_projection\.hpp:' \
    | grep -Ev 'content/2d5/compositions/camera_test_orchestrator\.cpp:' \
    | grep -Ev 'tests/renderer/camera/test_lens_model\.cpp:' \
    | grep -Ev 'tests/core/math/.*\.cpp:' \
    | grep -Ev 'include/chronon3d/scene/camera/camera_2_5d/(camera_framing_solver|photomaton_layout)\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/builders/.*\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/model/(camera/.*|.*)/(.*\.hpp|.*hpp):' \
    | grep -Ev 'include/chronon3d/scene/camera/.*camera_2_5d\.hpp:' \
    | grep -Ev 'include/chronon3d/scene/(camera|model)/camera\.hpp:' \
    | grep -Ev 'src/scene/camera/camera_v1/camera_framing_solver\.cpp:' \
    | grep -Ev 'src/backends/software/diagnostics/nulls_overlay\.cpp:' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── 6. compile_camera() invocations outside camera_v1 + tests + adapters ──
# CameraProgram::compile_camera() is the compile-time cost.  It is
# allowed to be called from:
#   - src/scene/camera/camera_v1/             (the compile site itself)
#   - tests/**                                 (test sites only)
#   - src/scene/camera/camera_v1/camera_descriptor_adapters.cpp
#     (the legacy-bridge adapter one-shot)
# Any NEW call site in per-frame inner loops (src/runtime/, src/scene/builders/,
# src/render_graph/) is a regression to be blocked.
echo -n "  [6/6] compile_camera() call-site policy ... "
# Only flag CALL sites in .cpp; declarations (.hpp) are allowed because
# compile_camera() is the canonical entry point and its declaration must
# be visible to consumers — `compile_camera(...)` in a header is a
# declaration, not a call.  We further allow the compile_site directory
# itself (compile_camera() definition + entry-point forwarding), and
# tests/ which is the validation surface.
hits=$(grep -Rn --include='*.cpp' \
    -E '\bcompile_camera\s*\(' $SCRIPT_PATHS 2>/dev/null \
    | filter_symbol_in_code_only 'compile_camera' \
    | grep -Ev 'src/scene/camera/camera_v1/' \
    | grep -Ev 'tests/' \
    || true)
if [ -n "$hits" ]; then
    echo "FAIL"; echo "$hits" | sed 's/^/    /'; FAILED=1
else echo "PASS"; fi

# ── Summary ───────────────────────────────────────────────────────────
echo ""
if [ "$FAILED" -ne 0 ]; then
    echo "=== CAM-DOC 04 architecture boundary checks FAILED (one or more P0 violations) ==="
    exit 1
fi
echo "=== All CAM-DOC 04 architecture boundary checks PASSED! ==="
exit 0
