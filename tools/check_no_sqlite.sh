#!/usr/bin/env bash
# check_no_sqlite.sh — irreversible Wave 1D guard.
#
# Wave 1D demolished the SQLite telemetry authority: the runtime store, its
# schema, the gated emitters and the CLI database readers are gone, and the
# engine is events-only (NullTelemetryStore default).  This gate makes the
# demolition irreversible by failing if any production code path or build
# input reintroduces the forbidden surface.  Historical references live only
# in docs/ (documental lint discipline), which this gate deliberately does
# not scan.
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

# Production scopes: engine, public API, CLI apps, build tooling.  tests/
# and docs/ are excluded on purpose (removal notes are legitimate there).
SCOPES=(src include apps cmake tools)

# Forbidden tokens (review gate spec, Wave 1D):
#   sqlite3                       — library/linkage/API name
#   SqliteTelemetry               — demolished store class family
#   telemetry/sqlite              — demolished source directory
#   CHRONON3D_ENABLE_SQLITE_TELEMETRY — demolished feature macro
#   .sqlite                       — database file extension/paths
#   telemetry_schema.sql          — demolished schema asset
PATTERN='sqlite3|SqliteTelemetry|telemetry/sqlite|CHRONON3D_ENABLE_SQLITE_TELEMETRY|\.sqlite|telemetry_schema\.sql'
GLOB='*.{cpp,hpp,inc,h,hxx,cc,cxx,cmake,txt,sh,py}'

# The gate's own source documents the forbidden tokens (above), so exclude it
# from the scan it runs.
violations="$(rg -n -i "$PATTERN" "${SCOPES[@]}" --glob "$GLOB" \
    --glob '!check_no_sqlite.sh' 2>/dev/null || true)"

# Root-level build metadata that pins the dependency/feature surface
# (vcpkg.json overrides/features and the top-level CMakeLists).
for f in vcpkg.json CMakeLists.txt; do
    if [ -f "$f" ]; then
        violations+=$'\n'"$(rg -n -i "$PATTERN" "$f" 2>/dev/null || true)"
    fi
done

violations="$(printf '%s' "$violations" | sed '/^[[:space:]]*$/d')"
if [ -n "$violations" ]; then
    echo "check_no_sqlite: SQLite telemetry authority reintroduced in production:" >&2
    printf '%s\n' "$violations" >&2
    exit 1
fi
echo "check_no_sqlite: PASS (zero SQLite production residue)"
