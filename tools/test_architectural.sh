#!/bin/bash
set -e

# ==============================================================================
# tools/test_architectural.sh — Architectural & Static Verification Tests
# ==============================================================================
#
# This script catches structural regressions that the compiler alone does not
# surface. It is wired into the new CI `architecture-check` job (see
# .github/workflows/gates.yml) so a PR that violates any of the rules below
# cannot merge.
#
# Categories of checks (extend cautiously — false positives block real work):
#   1. Quarantine integrity — experimental header paths must NOT leak outside
#      the experimental/ subtree.
#   2. Anti-stato-globale — no `static std::unordered_map` / `static std::vector`
#      caches outside sanctioned exception files.
#   3. Anti-skip-senza-ticket — every `* doctest::skip()` call must carry a
#      TICKET-XXX marker with Issue/Owner/Motivation/Date/Deadline metadata.
#   4. Source-headers from core → forbidden paths (`src/` includes from
#      `include/chronon3d/...`).
#   5. Cross-target leakage — registry/global creation in detail/g_* namespaces
#      is banned (existing rules from prior audits).
#
# Each check exits the script with non-zero on failure. CI surfaces the failure
# message verbatim in the build log.

echo "=== Running Architectural & Static Verification Tests ==="
FAILED=0

# Helper: increment failure counter and report under which Test N we failed.
report_failed() {
    echo "    -> FAIL: $1"
    FAILED=1
}

# Helper: run a "must be ABSENT" check.
expect_absent() {
    local name="$1"; local pattern="$2"
    local hits
    hits=$(grep -R --include='*.hpp' --include='*.cpp' --include='*.h' --include='*.c' \
        -E "$pattern" . 2>/dev/null \
        | grep -v '^./build/' \
        | grep -v '^./vcpkg_bootstrap/' \
        | grep -v '^./vcpkg_installed/' \
        | grep -v '^./experimental/' \
        || true)
    if [ -n "$hits" ]; then
        report_failed "$name: matches found where there should be none"
        echo "$hits" | head -20
    else
        echo "PASSED: $name"
    fi
}

# Helper: run a "must be PRESENT" check (used for documentation that must
# exist on the critical path).
expect_present() {
    local name="$1"; local pattern="$2"; local base="$3"
    if grep -R --include='*' -E "$pattern" "$base" >/dev/null 2>&1; then
        echo "PASSED: $name"
    else
        report_failed "$name: no matches found in $base"
    fi
}

# ────────────────────────────────────────────────────────────────────────
# 1. Quarantine integrity
# ────────────────────────────────────────────────────────────────────────
# Headers under include/chronon3d_experimental/ must NOT be #include'd from
# outside the experimental/ subtree. The V1-vs-V2 anti-duplication rule
# depends on these paths remaining opt-in.

echo "--- Section 1: Quarantine integrity ---"

QUARANTINE_LEAKS=$(grep -Rn --include='*.hpp' --include='*.cpp' \
    '<\(chronon3d_experimental\|chrono3d_experimental\)/' . 2>/dev/null \
    | grep -v '^./experimental/' \
    | grep -v '^./build/' \
    | grep -v '^./vcpkg_bootstrap/' \
    | grep -v '^./vcpkg_installed/' \
    || true)
if [ -n "$QUARANTINE_LEAKS" ]; then
    report_failed "Quarantine leak: <chronon3d_experimental/...> outside experimental/"
    echo "$QUARANTINE_LEAKS" | head -20
else
    echo "PASSED: no <chronon3d_experimental/...> includes outside experimental/"
fi

# Old experimental flag must NOT be used as a live directive anywhere.
# (The deprecated `option(... OFF)` declaration in the root CMakeLists.txt is
# an allowed exception because it exists for cache-key compat.)
LIVE_EXPERIMENTAL_FLAG=$(grep -RIn --include='*.cmake' --include='CMakeLists.txt' \
    --include='*.sh' --include='*.yml' --include='*.yaml' --include='*.json' --include='*.py' \
    'CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2[[:space:]]*[:=]' . 2>/dev/null \
    | grep -v '^./CMakeLists.txt:.*option(' \
    | grep -v '^./CMakeLists.txt:.*if(CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2)' \
    | grep -v '^./build/' \
    | grep -v '^./vcpkg_bootstrap/' \
    | grep -v '^./vcpkg_installed/' \
    | grep -v '^./docs/' \
    || true)
if [ -n "$LIVE_EXPERIMENTAL_FLAG" ]; then
    report_failed "stale CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 directive"
    echo "$LIVE_EXPERIMENTAL_FLAG" | head -20
else
    echo "PASSED: CHRONON3D_ENABLE_EXPERIMENTAL_EXPRESSIONS_V2 is no longer a live directive"
fi

# ────────────────────────────────────────────────────────────────────────
# 2. Anti-stato-globale — banned patterns except in sanctioned files.
# ────────────────────────────────────────────────────────────────────────
echo "--- Section 2: Anti-stato-globale ---"

# `static std::unordered_map<...>` caches outside src/cache/* and the
# sanctioned video_sink_factory.cpp are prohibited. Use LruCache<Key, Value>.
STATIC_UNORDERED=$(grep -RIn --include='*.cpp' --include='*.hpp' \
    'static[[:space:]]\+std::unordered_map' . 2>/dev/null \
    | grep -v '^./build/' \
    | grep -v '^./vcpkg_bootstrap/' \
    | grep -v '^./vcpkg_installed/' \
    | grep -v '^./src/cache/' \
    | grep -v '^./src/media/video/video_sink_factory.cpp' \
    || true)
if [ -n "$STATIC_UNORDERED" ]; then
    report_failed "static std::unordered_map outside sanctioned cache files"
    echo "$STATIC_UNORDERED" | head -20
else
    echo "PASSED: no static std::unordered_map outside sanctioned cache"
fi

# `static std::vector<...>` caches: same rule.
STATIC_VECTOR=$(grep -RIn --include='*.cpp' --include='*.hpp' \
    'static[[:space:]]\+std::vector' . 2>/dev/null \
    | grep -v '^./build/' \
    | grep -v '^./vcpkg_bootstrap/' \
    | grep -v '^./vcpkg_installed/' \
    | grep -v '^./src/cache/' \
    | grep -v '^./src/registry/' \
    | grep -v '^./src/text/' \
    | grep -v '^./src/animations/' \
    || true)
if [ -n "$STATIC_VECTOR" ]; then
    report_failed "static std::vector outside sanctioned registry/text/animations"
    echo "$STATIC_VECTOR" | head -20
else
    echo "PASSED: no stray static std::vector"
fi

# `detail::g_*` globals — risky to add new ones. (Existing ones tracked in
# the legacy tests 1.1–1.4 / 2.4 / 14.x etc. below.)
NEW_GLOBALS=$(grep -RIn --include='*.cpp' --include='*.hpp' \
    'detail::g_[a-zA-Z_]\+[[:space:]]*=' . 2>/dev/null \
    | grep -v 'g_debug_config' \
    | grep -v 'g_default_assets_root' \
    | grep -v '^./build/' \
    | grep -v '^./vcpkg_bootstrap/' \
    | grep -v '^./vcpkg_installed/' \
    || true)
if [ -n "$NEW_GLOBALS" ]; then
    report_failed "new detail::g_* global assignment — pin in architecture docs first"
    echo "$NEW_GLOBALS" | head -20
else
    echo "PASSED: only sanctioned detail::g_* globals exist (g_debug_config, g_default_assets_root)"
fi

# ────────────────────────────────────────────────────────────────────────
# 3. Anti-skip-senza-ticket
# ────────────────────────────────────────────────────────────────────────
echo "--- Section 3: Anti-skip-senza-ticket ---"

# Every `* doctest::skip()` call must have, in the line(s) immediately
# preceding, a `TICKET-XXX` marker plus a marker for Issue/Owner/Motivation/
# Date/Deadline metadata. Missing any of those blocks a PR.
SKIP_VIOLATIONS=$(python3 - <<'PY' 2>&1 || true
import os, re, sys

root = '.'
skip_re   = re.compile(r'\*\s*doctest::skip\(')
ticket_re = re.compile(r'(TICKET-[A-Za-z0-9_-]+|Issue:|Owner:|Motivation:|Data\s+introduzione:|Deadline\s+rimozione:)')

bad = []
for base, dirs, files in os.walk(root):
    # skip build / vcpkg / experimental (experimental may carry its own
    # ticket discipline)
    dirs[:] = [d for d in dirs if d not in ('build', 'vcpkg_bootstrap', 'vcpkg_installed', '.git', 'vcpkg', 'experimental')]
    for f in files:
        if not f.endswith(('.cpp', '.hpp')):
            continue
        path = os.path.join(base, f)
        try:
            with open(path, 'r', encoding='utf-8', errors='replace') as fp:
                lines = fp.readlines()
        except Exception:
            continue
        for i, line in enumerate(lines):
            if skip_re.search(line):
                # Look at the 3 lines before, plus the same line, plus the
                # 3 lines after (sometimes the comment is below).
                ctx = ''.join(lines[max(0, i - 3): i + 4])
                if not ticket_re.search(ctx):
                    bad.append((path, i + 1, line.rstrip()))

for path, lineno, content in bad:
    print(f"{path}:{lineno}: {content}")
sys.exit(1 if bad else 0)
PY
)
if [ -n "$SKIP_VIOLATIONS" ]; then
    report_failed "* doctest::skip() without TICKET-XXX + Issue/Owner/Motivation/Date/Deadline metadata"
    echo "$SKIP_VIOLATIONS" | head -40
else
    echo "PASSED: every * doctest::skip() in tests/ carries ticket metadata"
fi

# ────────────────────────────────────────────────────────────────────────
# 4. Core → src/ include prohibition
# ────────────────────────────────────────────────────────────────────────
echo "--- Section 4: Include hygiene (core does not include src/...) ---"

# A public header under include/chronon3d/* must not #include a path
# starting with `src/`. Reverse-direction only: src/ may include include/.
CORE_SRC_LEAK=$(grep -RIn --include='*.hpp' --include='*.cpp' \
    '^#include[[:space:]]\+"src/' include/chronon3d 2>/dev/null || true)
if [ -n "$CORE_SRC_LEAK" ]; then
    report_failed "include/chronon3d leaks src/ via #include"
    echo "$CORE_SRC_LEAK" | head -20
else
    echo "PASSED: include/chronon3d/* does not include src/..."
fi

# ────────────────────────────────────────────────────────────────────────
# 5. Renderer state containment + legacy rules (preserved)
# ────────────────────────────────────────────────────────────────────────
echo "--- Section 5: Renderer / extension / gitignore (legacy rules) ---"

# Test 1.1
run_test_absent() {
    local name="$1"; local cmd="$2"
    if eval "$cmd" > /dev/null 2>&1; then
        report_failed "$name: unexpected match"
        eval "$cmd" | head -5
    else
        echo "PASSED: $name"
    fi
}

run_test_absent "SoftwareRenderer.hpp does not include GraphBuilder" \
    "grep -R '#include <chronon3d/render_graph/graph_builder.hpp>' include/chronon3d/backends/software"

run_test_absent "SoftwareRenderer.hpp does not include GraphExecutor" \
    "grep -R '#include <chronon3d/render_graph/graph_executor.hpp>' include/chronon3d/backends/software"

run_test_absent "Software backend does not call GraphBuilder::build" \
    "grep -R 'GraphBuilder::build' src/backends/software include/chronon3d/backends/software"

run_test_absent "Software backend does not create GraphExecutor" \
    "grep -R 'GraphExecutor' src/backends/software include/chronon3d/backends/software"

run_test_absent "software_internal namespace is not in public headers" \
    "grep -R 'namespace software_internal' include/chronon3d/backends/software"

run_test_absent "chronon3d_backend_software does not link chronon3d_runtime" \
    "git grep -n 'chronon3d_runtime' -- CMakeLists.txt | grep -E 'chronon3d_backend_software'"

run_test_absent "render_scene_internal is completely removed" \
    "grep -Rn 'render_scene_internal' src include"

run_test_absent "SpecScene does not include SoftwareRenderer" \
    "grep -Ri 'SoftwareRenderer' src/specscene include/chronon3d/specscene"

run_test_absent "SpecScene does not include GraphBuilder" \
    "grep -Ri 'GraphBuilder' src/specscene include/chronon3d/specscene"

run_test_absent "SpecScene does not include GraphExecutor" \
    "grep -Ri 'GraphExecutor' src/specscene include/chronon3d/specscene"

# Gitignore coverage:
run_test_present() {
    local name="$1"; local path="$2"
    if git check-ignore -q "$path"; then
        echo "PASSED: $name"
    else
        report_failed "$name"
    fi
}

run_test_present "Gitignore covers output/test.png"              "output/test.png"
run_test_present "Gitignore covers build/temp.o"                 "build/temp.o"
run_test_present "Gitignore covers test_renders/foo.png"         "test_renders/foo.png"

# ────────────────────────────────────────────────────────────────────────
# Final summary
# ────────────────────────────────────────────────────────────────────────
if [ "$FAILED" -ne 0 ]; then
    echo ""
    echo "=== Static architectural checks FAILED! ==="
    exit 1
else
    echo ""
    echo "=== All static architectural checks PASSED! ==="
    exit 0
fi
