#!/usr/bin/env bash
# check_no_process_wide_caches.sh
#
# Protects the runtime ownership boundary for caches.  Cache state belongs to
# RenderRuntime (or to an explicitly owned resource manager); retired
# process-wide entry points must not be reintroduced.

set -euo pipefail

GATE_NAME=check_no_process_wide_caches
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT"

SEARCH_PATHS=(include src tests apps)
PATTERN='global_curve_cache|set_global_capacity_bytes|global_capacity_bytes|ImageCache::global_|set_global_cache_config|s_global_cache_config|static[[:space:]]+(std::)?(unordered_map|unordered_set|map|set|TextLayoutCache)[^;]*cache'

matches="$(rg -n --hidden \
    --glob '!build/**' \
    --glob '!build-*/**' \
    --glob '!cmake-build-*/**' \
    --glob '!*vcpkg_installed/**' \
    --glob '!node_modules/**' \
    --glob '*.{h,hpp,hh,c,cc,cpp,cxx}' \
    -e "$PATTERN" "${SEARCH_PATHS[@]}" 2>/dev/null || true)"

if [[ -n "$matches" ]]; then
    echo "GATE_FAIL: process-wide cache pattern(s) detected:" >&2
    echo "$matches" | sed 's/^/  /' >&2
    exit 1
fi

echo "GATE_PASS: no retired process-wide cache entry points"
echo "[INFO] ${GATE_NAME}: cache ownership remains runtime-scoped"
exit 0
