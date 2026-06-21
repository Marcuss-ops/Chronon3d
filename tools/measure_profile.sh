#!/usr/bin/env bash
# ============================================================================
# tools/measure_profile.sh
#
# 08 implementation — Misurazione canonica di un profilo di build.
# Per ogni profilo nominato (core/motion/video/extended) misura:
#   1. Tempo di `cmake --preset linux-profile-<name>`.
#   2. Numero di dipendenze installate in vcpkg_installed/<name>.
#   3. (opzionale, --build) Tempo di `cmake --build --parallel N`.
#   4. (opzionale, --build) Dimensione dei binari:
#         chronon3d_cli, chronon3d_sdk_impl.a, chronon3d_tests_fast.
# Output: JSON + Markdown table standardizzati su stdout e in
# build/profiles/<name>/profile-measurement.{json,md}.
#
# Usage:
#   tools/measure_profile.sh <linux-profile-core|motion|video|extended>
#                           [--build] [--parallel N]
#                           [--report-md <path>]
#
# Exit 0 su successo; 2 su profilo sconosciuto; 3 su configure fallita.
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ $# -lt 1 ]]; then
  cat <<USAGE >&2
Usage: tools/measure_profile.sh <profile> [--build] [--parallel N] [--report-md <path>]

Profili supportati: linux-profile-core, linux-profile-motion,
                    linux-profile-video, linux-profile-extended
USAGE
  exit 2
fi

profile="linux-profile-$1"; shift
do_build=0
parallel=""
report_md=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)   do_build=1; shift ;;
    --parallel) parallel="-j $2"; shift 2 ;;
    --report-md) report_md="$2"; shift 2 ;;
    -*) echo "Unknown flag: $1" >&2; exit 2 ;;
    *) echo "extra arg $1" >&2; exit 2 ;;
  esac
done

# Resolve build / vcpkg_installed / binary dirs from the chosen preset.
case "$profile" in
  linux-profile-core)
    build_dir="build/chronon/$profile"
    vcpkg_dir="vcpkg_installed/$profile"
    bin_targets=(chronon3d_sdk_impl.a chronon3d_cli chronon3d_tests_fast)
    ;;
  linux-profile-motion)
    build_dir="build/chronon/$profile"
    vcpkg_dir="vcpkg_installed/$profile"
    bin_targets=(chronon3d_sdk_impl.a chronon3d_cli chronon3d_tests_fast)
    ;;
  linux-profile-video)
    build_dir="build/chronon/$profile"
    vcpkg_dir="vcpkg_installed/$profile"
    bin_targets=(chronon3d_sdk_impl.a chronon3d_cli chronon3d_tests_fast)
    ;;
  linux-profile-extended)
    build_dir="build/chronon/$profile"
    vcpkg_dir="vcpkg_installed/$profile"
    bin_targets=(chronon3d_sdk_impl.a chronon3d_cli chronon3d_tests_fast)
    ;;
  *)
    echo "Profilo sconosciuto: linux-profile-$1 (atteso: core|motion|video|extended)" >&2
    exit 2 ;;
esac

mkdir -p "$build_dir" "$vcpkg_dir" "build/profiles/$profile"

# -- 1. configure -----------------------------------------------------------------------
t0=$(date +%s)
if ! cmake -S . --preset "$profile" -B "$build_dir" > "$build_dir/profile-configure.log" 2>&1; then
  echo "configure failed: $build_dir/profile-configure.log" >&2
  exit 3
fi
t1=$(date +%s)
configure_sec=$((t1 - t0))

# -- 2. deps installed (vcpkg) ----------------------------------------------------------
deps_count=0
if [[ -d "$vcpkg_dir/x64-linux" ]]; then
  deps_count=$(find "$vcpkg_dir/x64-linux" -maxdepth 4 -name "*.cmake" -path "*/share/*" \
               2>/dev/null | wc -l)
fi

# -- 3. build (optional) ----------------------------------------------------------------
build_sec=""
build_status="skipped"
if [[ "$do_build" -eq 1 ]]; then
  t0=$(date +%s)
  if cmake --build "$build_dir" ${parallel:-} \
       > "$build_dir/profile-build.log" 2>&1; then
    build_status="ok"
  else
    build_status="failed"
  fi
  t1=$(date +%s)
  build_sec=$((t1 - t0))
fi

# -- 4. binary size ---------------------------------------------------------------------
declare -A bin_sizes
# Advisory metric: SDK archive may live under an OBJECT-library subpath
# rather than flat under $build_dir. We attempt a flat find first and
# fall back to a recursive search; size = 0 means "binary was not
# produced in this profile" (e.g., motion profile without CHRONON3D_USE_BLEND2D).
for tgt in "${bin_targets[@]}"; do
  found=$(find "$build_dir" -name "$tgt" -type f 2>/dev/null | head -n 1)
  if [[ -z "$found" ]]; then
    # Recursive fallback (in case target was built into a subdir).
    found=$(find "$build_dir/../.." -name "$tgt" -type f 2>/dev/null | head -n 1)
  fi
  if [[ -n "$found" && -f "$found" ]]; then
    sz=$(stat -c '%s' "$found" 2>/dev/null || echo 0)
    bin_sizes[$tgt]=$sz
    bin_paths[$tgt]="$found"
  else
    bin_sizes[$tgt]=0
    bin_paths[$tgt]="(not produced)"
  fi
done

# Emit advisory on any "not produced" target.
for tgt in "${bin_targets[@]}"; do
  if [[ "${bin_sizes[$tgt]}" -eq 0 ]]; then
    echo "[advisory] $tgt: non prodotto in questo profilo (bin_sizes=0). Verificare se i flag CHRONON3D_* abilitano il target." >&2
  fi
done

# -- 5. write JSON ----------------------------------------------------------------------
json_path="build/profiles/$profile/profile-measurement.json"
{
  echo "{"
  echo "  \"profile\": \"$profile\","
  echo "  \"timestamp\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
  echo "  \"configure_sec\": $configure_sec,"
  echo "  \"deps_count\": $deps_count,"
  echo "  \"build_status\": \"$build_status\","
  if [[ -n "$build_sec" ]]; then
    echo "  \"build_sec\": $build_sec,"
  else
    echo "  \"build_sec\": null,"
  fi
  echo "  \"binaries\": {"
  first=1
  for tgt in "${bin_targets[@]}"; do
    [[ $first -eq 0 ]] && echo ","
    printf '    "%s": %s' "$tgt" "${bin_sizes[$tgt]}"
    first=0
  done
  echo ""
  echo "  }"
  echo "}"
} > "$json_path"

# -- 6. write Markdown ------------------------------------------------------------------
md_path="build/profiles/$profile/profile-measurement.md"
if [[ -n "$report_md" ]]; then
  mkdir -p "$(dirname "$report_md")"
  md_path="$report_md"
fi
{
  echo "# Profile measurement — $profile"
  echo
  echo "- Timestamp (UTC): $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "- Configure time: ${configure_sec}s"
  echo "- vcpkg deps installed: ${deps_count}"
  echo "- Build status: ${build_status}"
  if [[ -n "$build_sec" ]]; then
    echo "- Build time: ${build_sec}s"
  fi
  echo
  echo "## Binari"
  echo
  echo "| Target | Bytes | Path |"
  echo "|---|---:|---|"
  for tgt in "${bin_targets[@]}"; do
    printf '| %s | %s | %s |\n' "$tgt" "${bin_sizes[$tgt]}" "${bin_paths[$tgt]}"
  done
  echo
  echo "Size=0 indica che il target non era abilitato dai flag del profilo (advisory only)."
  echo
  echo "## Note"
  echo
  echo "Output JSON: \`build/profiles/$profile/profile-measurement.json\`"
  echo "Configure log: \`$build_dir/profile-configure.log\`"
  if [[ "$do_build" -eq 1 ]]; then
    echo "Build log: \`$build_dir/profile-build.log\`"
  fi
} > "$md_path"

# -- stdout report ----------------------------------------------------------------------
echo "Profile: $profile  configure=${configure_sec}s  deps=${deps_count}  build=${build_status}"
if [[ -n "$build_sec" ]]; then echo "  build=${build_sec}s"; fi
for tgt in "${bin_targets[@]}"; do
  echo "  $tgt = ${bin_sizes[$tgt]} bytes"
done
echo "JSON:  $json_path"
echo "MD:    $md_path"
exit 0
