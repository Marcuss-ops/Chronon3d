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
// IPC ingress delegates parsing to the canonical protocol/dispatch codecs.
void handle_render_job() {}
EOF
    write_file "$dir" "apps/chronon3d_cli/daemon/daemon_service_ipc_protocol.cpp" <<'EOF'
void decode_render_job_payload(const std::string& payload) {
    const auto request = nlohmann::json::parse(payload);
    (void)request;
}
EOF
    write_file "$dir" "apps/chronon3d_cli/daemon/daemon_service_ipc_dispatch.cpp" <<'EOF'
void decode_segment_payload(const std::string& payload) {
    const auto request = nlohmann::json::parse(payload.begin(), payload.end());
    (void)request;
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

# ─────────────────────────────────────────────────────────────────────────
# Cases 11-14 — cache taxonomy enforcement (2026-09-13). These run the REAL
# registry (tools/architecture_rules.toml) against a mirror tree, and assert on
# the specific rule name rather than on the exit code: the manifest also fails
# on paths a mirror tree does not have, so only the named rule is evidence.
# The ProgramCache doctrine for compiled GPU artifacts (SPIR-V / Slang / PTX /
# SASS / CUDA graph) is a gate only if a planted SlangCache fails it.
# ─────────────────────────────────────────────────────────────────────────
cache_taxonomy_mirror() {
    write_file "$1" "include/chronon3d/cache/cache_taxonomy.hpp" <<'EOF'
#pragma once
namespace chronon3d::cache {
enum class CacheFamily : unsigned char { ContentCache = 1, ResidencyCache = 2, ProgramCache = 3 };
} // namespace chronon3d::cache
EOF
    write_file "$1" "include/chronon3d/cache/lru_cache.hpp" <<'EOF'
#pragma once
class LruCache {};
EOF
}

case11_gpu_binary_cache_family() {
    cache_taxonomy_mirror "$1"
    write_file "$1" "include/chronon3d/cache/slang_cache.hpp" <<'EOF'
#pragma once
// regression: a lowering promoted to its own cache family
class SlangCache {};
struct PtxRegistry {};
EOF
}

case12_second_cache_engine() {
    cache_taxonomy_mirror "$1"
    write_file "$1" "include/chronon3d/cache/cache_engine.hpp" <<'EOF'
#pragma once
// regression: a second generic cache engine beside LruCache
class CacheEngine {};
EOF
}

# ─────────────────────────────────────────────────────────────────────────
# Cases 13/14 — a rule that cannot find its FILE must FAIL, and a declared
# absence must PASS *with its reason surfaced* (audit P0-1).
#
# The native video decoder TU was split into `_session`/`_decode`/`_helpers`
# while two `[[contract]]` entries kept pointing at the retired monolith, so the
# decoder cache and source-concurrency invariants were enforced by zero code
# and the gate still printed PASS (`optional = true` → "optional file absent").
# These two cases pin the fix: silence is no longer a legal absence, and the
# invariant is enforced by `scan_path_existence` — an existing rule — so it also
# holds for every registry fragment and under `--rule-prefix`.
# ─────────────────────────────────────────────────────────────────────────
case13_contract_stale_target() {
    write_file "$1" "rules.toml" <<'EOF'
[[contract]]
name = "selftest_contract_stale_target"
file = "src/media/video/native_video_frame_decoder.cpp"
required_patterns = ['std::lock_guard<std::mutex>\s+lock\(m_mutex\);']
EOF
}

case14_contract_declared_absence() {
    write_file "$1" "rules.toml" <<'EOF'
[[contract]]
name = "selftest_contract_declared_absence"
file = "src/media/video/retired_tu.cpp"
optional = true
optional_reason = "P0-1: split into _session/_decode/_helpers; contract repointed at the real TU"
required_patterns = ['unused-by-this-fixture']
EOF
}

# expect_rule_in <case> <rule> <PASS|FAIL> <builder> — the verdict of ONE rule in
# a fixture registry: a mirror tree may fail unrelated rules, so only the named
# rule is evidence.
expect_rule_in() {
    local name="$1" rule="$2" want="$3" builder="$4"
    local dir="$WORK/$name"
    mkdir -p "$dir"
    "$builder" "$dir"
    run_check "$dir" "$dir/rules.toml" >/dev/null
    if grep -q "\[${want}\] ${rule}" "$dir/out.log" 2>/dev/null; then
        PASS=$((PASS + 1))
        echo "  [PASS] $name -> $rule=$want as expected"
    else
        FAIL=$((FAIL + 1))
        echo "  [FAIL] $name -> expected $rule=$want"
        sed -n '1,12p' "$dir/out.log" 2>/dev/null | sed 's/^/         /'
    fi
}

# ─────────────────────────────────────────────────────────────────────────
# Fragment self-check (audit P2-4): a fragment that is not reachable from the
# root registry, or that declares no rule, is knowledge no gate reads. The
# FILE-target invariant of every fragment is carried by the aggregate
# `scan_path_existence` rule (the root merges all fragments before evaluating
# it), whose teeth are pinned by cases 13/14.
# ─────────────────────────────────────────────────────────────────────────
fragment_selfcheck() {
    local reg="$TOOLS/architecture_rules.toml"
    local frag base count
    for frag in "$TOOLS"/architecture_rules_*.toml; do
        base="$(basename "$frag")"
        if ! grep -q "\"$base\"" "$reg"; then
            FAIL=$((FAIL + 1))
            echo "  [FAIL] $base — not registered in architecture_rules.toml (no gate reads it)"
            continue
        fi
        count="$("$PYTHON" "$TOOLS/check_architecture.py" --rules "$frag" --list 2>/dev/null | wc -l)"
        if [[ "$count" -lt 1 ]]; then
            FAIL=$((FAIL + 1))
            echo "  [FAIL] $base — registered but declares no rule (vacuous fragment)"
            continue
        fi
        PASS=$((PASS + 1))
        echo "  [PASS] $base — registered, $count rule(s)"
    done
}

# run_rule <case-dir> <rule-name> <PASS|FAIL> — the named rule's own verdict.
expect_rule() {
    local name="$1" rule="$2" want="$3" builder="$4"
    local dir="$WORK/$name"
    mkdir -p "$dir"
    "$builder" "$dir"
    run_check "$dir" "$ROOT/tools/architecture_rules.toml" >/dev/null
    if grep -q "\[${want}\] ${rule}" "$dir/out.log" 2>/dev/null; then
        PASS=$((PASS + 1))
        echo "  [PASS] $name -> $rule=$want as expected"
    else
        FAIL=$((FAIL + 1))
        echo "  [FAIL] $name -> expected $rule=$want"
        grep -E "\[(PASS|FAIL)\] (no_gpu_binary_cache_family|single_cache_primitive)" \
            "$dir/out.log" 2>/dev/null | sed 's/^/         /'
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

# Cache taxonomy: the compliant mirror passes both rules, each plant fails its
# own rule (no false positives, no vacuous pass).
expect_rule cache_taxonomy_mirror no_gpu_binary_cache_family PASS cache_taxonomy_mirror
expect_rule cache_taxonomy_mirror_engine single_cache_primitive PASS cache_taxonomy_mirror
expect_rule case11_gpu_binary_cache_family no_gpu_binary_cache_family FAIL case11_gpu_binary_cache_family
expect_rule case12_second_cache_engine single_cache_primitive FAIL case12_second_cache_engine

# Stale-rule-target invariant (audit P0-1 / P2-4): a contract whose file is gone
# must fail BOTH its own rule and the aggregate path invariant; a declared
# absence must pass with its reason.
expect_rule_in case13_contract_stale_target selftest_contract_stale_target FAIL case13_contract_stale_target
expect_rule_in case13b_stale_target_invariant scan_path_existence FAIL case13_contract_stale_target
expect_rule_in case14_contract_declared_absence selftest_contract_declared_absence PASS case14_contract_declared_absence
fragment_selfcheck

echo
if [[ "$FAIL" == "0" ]]; then
    echo "=== architecture_gate_selftest PASSED ($PASS assertions) ==="
    exit 0
fi
echo "=== architecture_gate_selftest FAILED ($FAIL/$((PASS + FAIL)) assertions) ==="
exit 1
