#!/usr/bin/env bash
# tools/architecture_gate_selftest.sh — certify the certifier.
#
# check_architecture.py is the single authority for static architecture
# invariants (Phase 11). A gate that cannot be shown to FAIL on a broken
# tree is not a gate — it is a comment. This selftest builds deliberately
# broken fixture trees and asserts check_architecture.py exits 1 for each,
# then rebuilds a *compliant* Phase-9 mirror tree and asserts the real
# tools/architecture_rules_wave9.toml rules exit 0 on it (and exit 1 when
# a single JSON re-forward / second normalize / warm dispatcher is planted).
#
# Usage: bash tools/architecture_gate_selftest.sh
# Exit:  0 = every fixture behaved as expected, 1 = a fixture regressed.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS="$ROOT/tools"
WAVE9_RULES="$TOOLS/architecture_rules_wave9.toml"

# Pick a python that can load the TOML registry (tomllib >= 3.11).
PYTHON=""
for candidate in python3.11 python3.12 python3.13 python3; do
    if command -v "$candidate" >/dev/null 2>&1 \
        && "$candidate" -c 'import tomllib' 2>/dev/null; then
        PYTHON="$candidate"
        break
    fi
done
if [[ -z "$PYTHON" ]]; then
    echo "architecture_gate_selftest: no python with tomllib found" >&2
    exit 1
fi

PASS=0
FAIL=0
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# run_check <case-dir> <rules-file> — runs the checker against the fixture
# root and echoes its exit code.
run_check() {
    local case_dir="$1" rules="$2"
    ( cd "$ROOT" && "$PYTHON" "$TOOLS/check_architecture.py" \
        --root "$case_dir" --rules "$rules" >"$case_dir/out.log" 2>&1 )
    echo $?
}

# expect_fail <name> <builder>
#   builder(dir) writes rules.toml + a broken fixture tree under $WORK/<name>.
#   The checker must exit 1 (the gate must catch the violation).
expect_fail() {
    local name="$1" builder="$2"
    local dir="$WORK/$name"
    mkdir -p "$dir"
    "$builder" "$dir"
    local rc
    rc="$(run_check "$dir" "$dir/rules.toml")"
    if [[ "$rc" == "1" ]]; then
        PASS=$((PASS + 1))
        echo "  [PASS] $name -> GATE_FAIL as expected"
    else
        FAIL=$((FAIL + 1))
        echo "  [FAIL] $name -> expected exit 1, got $rc"
        sed -n '1,20p' "$dir/out.log" 2>/dev/null
    fi
}

write_file() { # write_file <dir> <relpath> <<'EOF' ...
    local dir="$1" rel="$2"
    mkdir -p "$(dirname "$dir/$rel")"
    cat > "$dir/$rel"
}

# ─────────────────────────────────────────────────────────────────────────
# Case 1 — forbidden_symbol (scan) fires on a planted JSON re-forward marker
# ─────────────────────────────────────────────────────────────────────────
case1_forbidden_symbol() {
    write_file "$1" "rules.toml" <<'EOF'
[[forbidden_symbol]]
name = "selftest_forbidden_symbol"
patterns = ['\bforward_payload\b']
scan_paths = ["apps"]
skip_comments = true
EOF
    write_file "$1" "apps/x/forwarder.cpp" <<'EOF'
// regression: re-serializing the payload after normalization
std::string make_payload() { return forward_payload.dump(); }
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Case 2 — occurrence_cap fires on a third normalize authority
# ─────────────────────────────────────────────────────────────────────────
case2_occurrence_cap() {
    write_file "$1" "rules.toml" <<'EOF'
[[occurrence_cap]]
name = "selftest_occurrence_cap"
pattern = '\bnormalize_render_job\s*\('
scan_paths = ["apps"]
max_count = 1
skip_comments = true
EOF
    for i in 1 2; do
        write_file "$1" "apps/x/executor_$i.cpp" <<EOF
#include "job.hpp"
void run_$i() { normalize_render_job(make_request()); }
EOF
    done
}

# ─────────────────────────────────────────────────────────────────────────
# Case 3 — contract fires when the canonical declaration disappears
# ─────────────────────────────────────────────────────────────────────────
case3_contract_missing() {
    write_file "$1" "rules.toml" <<'EOF'
[[contract]]
name = "selftest_contract"
file = "apps/daemon/daemon_render_job_payload.hpp"
required_pattern = 'decode_render_job_payload\s*\('
EOF
    write_file "$1" "apps/daemon/daemon_render_job_payload.hpp" <<'EOF'
// canonical payload decoder accidentally deleted
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Case 4 — unique_owner fires on a legacy duplicate authority
# ─────────────────────────────────────────────────────────────────────────
case4_unique_owner() {
    write_file "$1" "rules.toml" <<'EOF'
[[unique_source_owner]]
name = "selftest_unique_owner"
canonical_pattern = 'class CanonicalAuthority'
canonical_path = "include/canonical.hpp"
legacy_patterns = ['\bCanonicalAuthority\b']
EOF
    write_file "$1" "include/canonical.hpp" <<'EOF'
class CanonicalAuthority { public: void go(); };
EOF
    write_file "$1" "apps/legacy_authority.cpp" <<'EOF'
// a second, parallel authority must fail the unique-owner gate
void go() { CanonicalAuthority legacy_copy; (void)legacy_copy; }
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Case 5 — forbidden_include catches a render-plan dependency in the frozen
# compiler output header.
# ─────────────────────────────────────────────────────────────────────────
case5_forbidden_include() {
    write_file "$1" "rules.toml" <<'EOF'
[[forbidden_include]]
name = "selftest_compiled_program_no_render_plan_include"
patterns = ['#include\s*[<\"]chronon3d/render_plan/']
scan_paths = ["include/chronon3d/compiler/compiled_program.hpp"]
skip_comments = true
EOF
    write_file "$1" "include/chronon3d/compiler/compiled_program.hpp" <<'EOF'
#include <chronon3d/render_plan/render_budget.hpp>
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Cases 9/10 — cmake_dag rule kind (Phase 11/12): forbidden_target fires on a
# recreated facade, forbidden_edge fires on a leaf -> aggregate link.
# ─────────────────────────────────────────────────────────────────────────
case9_cmake_forbidden_target() {
    write_file "$1" "rules.toml" <<'EOF'
[[cmake_dag]]
name = "selftest_cmake_forbidden_target"
kind = "forbidden_target"
targets = ["chronon3d_software"]
EOF
    write_file "$1" "CMakeLists.txt" <<'EOF'
# regression: recreating the demolished aggregate (even as ALIAS)
add_library(chronon3d_software ALIAS chronon3d_pipeline)
EOF
}

case10_cmake_forbidden_edge() {
    write_file "$1" "rules.toml" <<'EOF'
[[cmake_dag]]
name = "selftest_cmake_forbidden_edge"
kind = "forbidden_edge"
source = "chronon3d_media_direct_yuv"
forbidden_deps = ["chronon3d_pipeline"]
EOF
    write_file "$1" "src/media/video/direct_yuv/CMakeLists.txt" <<'EOF'
# regression: leaf depending on the whole-pipeline aggregate
add_library(chronon3d_media_direct_yuv STATIC)
target_link_libraries(chronon3d_media_direct_yuv PRIVATE
    chronon3d_pipeline
)
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Wave-9 compliant mirror tree — exercises the REAL architecture_rules_wave9
# fragment on a miniature Phase-9-clean tree (rules.toml is a byte copy of
# the canonical fragment so the selftest can never drift from it).
# ─────────────────────────────────────────────────────────────────────────
wave9_mirror() {
    local dir="$1"
    write_file "$dir" "rules.toml" < "$WAVE9_RULES"
    write_file "$dir" "apps/chronon3d_cli/daemon/daemon_render_job_payload.hpp" <<'EOF'
#pragma once
#include <string>
namespace runtime { namespace job { struct RenderJobRequest; } }
[[nodiscard]] runtime::job::RenderJobRequest decode_render_job_payload(
    const std::string& payload);
EOF
    write_file "$dir" "apps/chronon3d_cli/daemon/daemon_service_ipc.cpp" <<'EOF'
#include "daemon_render_job_payload.hpp"
#include <string>

runtime::job::RenderJobRequest decode_render_job_payload(
    const std::string& payload) {
    const auto request = nlohmann::json::parse(payload);
    (void)request;
    return {};
}

ipc::Reply ipc_assemble_segments(const std::string& payload) {
    const auto request = nlohmann::json::parse(payload);
    (void)request;
    return {};
}
EOF
    write_file "$dir" "apps/chronon3d_cli/daemon/daemon_service.hpp" <<'EOF'
#pragma once
#include <string>
namespace runtime { namespace job { struct NormalizedRenderJob; } }
void handle_render_job(const runtime::job::NormalizedRenderJob& job);
EOF
    write_file "$dir" "apps/chronon3d_cli/commands/render/command_render_plan_api.inc" <<'EOF'
// CLI boundary: parse happened at the flag layer, normalize happens here.
int run_render_plan_file(runtime::job::RenderJobRequest request) {
    const auto normalized = runtime::job::normalize_render_job(request);
    if (!normalized) { return 1; }
    return execute_normalized_render_job(*normalized);
}

int execute_normalized_render_job(const runtime::job::NormalizedRenderJob& job) {
    (void)job;
    return 0;
}
EOF
    write_file "$dir" "apps/chronon3d_cli/commands/render/command_render_plan.hpp" <<'EOF'
#pragma once
namespace runtime { namespace job { struct RenderJobRequest; struct NormalizedRenderJob; } }
int run_render_plan_file(const runtime::job::NormalizedRenderJob& job);
EOF
    write_file "$dir" "apps/chronon3d_cli/commands/render/register_render_commands_body.inc" <<'EOF'
void register_dispatcher(const runtime::job::NormalizedRenderJob& job) {
    (void)job;
}
EOF
}

wave9_broken_reforward() {
    wave9_mirror "$1"
    write_file "$1" "apps/chronon3d_cli/daemon/forwarder.cpp" <<'EOF'
// regression: JSON re-forward after normalization
std::string forward_payload = request.dump();
EOF
}

wave9_broken_second_normalize() {
    wave9_mirror "$1"
    write_file "$1" "apps/chronon3d_cli/commands/render/evil_executor.inc" <<'EOF'
// regression: a third normalize authority inside the execution layer
auto normalized = runtime::job::normalize_render_job(make_request(args));
EOF
}

wave9_broken_warm_dispatcher() {
    wave9_mirror "$1"
    write_file "$1" "apps/chronon3d_cli/commands/render/warm.hpp" <<'EOF'
// regression: warm renderer as a second execution authority
using WarmRenderJobDispatcher = RenderJobDispatcher;
EOF
}

# Case 5 — the compliant mirror must PASS the real wave9 rules (expect_pass
# with the wave9 builders; the broken variants must each FAIL).
expect_pass() {
    local name="$1" builder="$2"
    local dir="$WORK/$name"
    mkdir -p "$dir"
    "$builder" "$dir"
    local rc
    rc="$(run_check "$dir" "$dir/rules.toml")"
    if [[ "$rc" == "0" ]]; then
        PASS=$((PASS + 1))
        echo "  [PASS] $name -> GATE_PASS as expected"
    else
        FAIL=$((FAIL + 1))
        echo "  [FAIL] $name -> expected exit 0, got $rc"
        sed -n '1,20p' "$dir/out.log" 2>/dev/null
    fi
}

echo "architecture_gate_selftest: certifying check_architecture.py (python=$PYTHON)"

expect_fail case1_forbidden_symbol case1_forbidden_symbol
expect_fail case2_occurrence_cap case2_occurrence_cap
expect_fail case3_contract_missing case3_contract_missing
expect_fail case4_unique_owner case4_unique_owner
expect_fail case5_forbidden_include case5_forbidden_include

# Real wave9 fragment: compliant tree passes, each single reintroduction fails.
expect_pass case5_wave9_compliant wave9_mirror
expect_fail case6_wave9_reforward wave9_broken_reforward
expect_fail case7_wave9_second_normalize wave9_broken_second_normalize
expect_fail case8_wave9_warm_dispatcher wave9_broken_warm_dispatcher
expect_fail case9_cmake_forbidden_target case9_cmake_forbidden_target
expect_fail case10_cmake_forbidden_edge case10_cmake_forbidden_edge

echo
if [[ "$FAIL" == "0" ]]; then
    echo "=== architecture_gate_selftest PASSED ($PASS assertions) ==="
    exit 0
fi
echo "=== architecture_gate_selftest FAILED ($FAIL/$((PASS + FAIL)) assertions) ==="
exit 1
