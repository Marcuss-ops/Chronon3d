#!/usr/bin/env bash
# tools/benchmark_host_info.sh
# ─────────────────────────────────────────────────────────────────────────
# Gather the current benchmark host attributes and emit them as YAML/JSON.
# This script is used by the performance certification pipeline to record
# the exact machine configuration alongside every benchmark run.
#
# Usage:
#   tools/benchmark_host_info.sh [--json] [--yaml]
#
# Defaults to YAML output.  The emitted record is intended to be appended
# to a benchmark report or stored in the telemetry database.
#
# Dependencies:
#   - python3 (used only for JSON string escaping)
#   - cc/clang/gcc (best-effort compiler detection)
#   - lscpu, nproc, sensors (best-effort hardware probing)
# ─────────────────────────────────────────────────────────────────────────

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMAT="${1:-yaml}"
if [[ "$FORMAT" == "--json" ]]; then
    FORMAT="json"
fi
if [[ "$FORMAT" == "--yaml" ]]; then
    FORMAT="yaml"
fi

# ── Helpers ─────────────────────────────────────────────────────────────
get_cpu_model() {
    if [[ -f /proc/cpuinfo ]]; then
        awk -F': ' '/model name/{print $2; exit}' /proc/cpuinfo | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
    else
        echo "unknown"
    fi
}

get_physical_cores() {
    if command -v lscpu >/dev/null 2>&1; then
        lscpu -p 2>/dev/null | awk -F',' '/^[^#]/{print $1}' | sort -u | wc -l
    else
        echo "0"
    fi
}

get_logical_cores() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    else
        echo "0"
    fi
}

get_ram_gb() {
    if [[ -f /proc/meminfo ]]; then
        awk '/MemTotal/{printf "%.0f\n", $2 / 1024 / 1024}' /proc/meminfo
    else
        echo "0"
    fi
}

get_kernel() {
    uname -r
}

get_compiler() {
    if command -v cc >/dev/null 2>&1; then
        cc --version | head -n1
    elif command -v clang >/dev/null 2>&1; then
        clang --version | head -n1
    elif command -v gcc >/dev/null 2>&1; then
        gcc --version | head -n1
    else
        echo "unknown"
    fi
}

get_chronon_commit() {
    if git -C "$ROOT_DIR" rev-parse HEAD >/dev/null 2>&1; then
        git -C "$ROOT_DIR" rev-parse HEAD
    else
        echo "unknown"
    fi
}

get_cmake_preset() {
    # Allow the runner to override the preset via environment variable.
    if [[ -n "${CHRONON3D_BENCHMARK_PRESET:-}" ]]; then
        echo "$CHRONON3D_BENCHMARK_PRESET"
    else
        echo "linux-fast-dev"
    fi
}

get_cpu_governor() {
    local gov=""
    if [[ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]]; then
        gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || true)
    fi
    if [[ -z "$gov" ]] && command -v cpupower >/dev/null 2>&1; then
        gov=$(cpupower frequency-info -p 2>/dev/null | awk -F': ' '/governor/{print $2; exit}' || true)
    fi
    echo "${gov:-unknown}"
}

get_temperature_c() {
    # Best-effort reading from thermal zones.
    local temp=""
    if command -v sensors >/dev/null 2>&1; then
        temp=$(sensors -u 2>/dev/null | awk '/temp1_input/{print $2; exit}')
    fi
    if [[ -z "$temp" ]] && [[ -f /sys/class/thermal/thermal_zone0/temp ]]; then
        temp=$(awk '{printf "%.1f", $1/1000}' /sys/class/thermal/thermal_zone0/temp 2>/dev/null || true)
    fi
    echo "${temp:-unavailable}"
}

# ── Gather values ──────────────────────────────────────────────────────
CPU_MODEL=$(get_cpu_model)
PHYSICAL_CORES=$(get_physical_cores)
LOGICAL_CORES=$(get_logical_cores)
RAM_GB=$(get_ram_gb)
KERNEL=$(get_kernel)
COMPILER=$(get_compiler)
CHRONON_COMMIT=$(get_chronon_commit)
CMAKE_PRESET=$(get_cmake_preset)
GOVERNOR=$(get_cpu_governor)
TEMPERATURE=$(get_temperature_c)

# ── Emit YAML ──────────────────────────────────────────────────────────
emit_yaml() {
    local temp_yaml
    if [[ "$TEMPERATURE" == "unavailable" ]]; then
        temp_yaml="\"$TEMPERATURE\""
    else
        temp_yaml="$TEMPERATURE"
    fi
    cat <<EOF
host:
  cpu_model: "$CPU_MODEL"
  physical_cores: $PHYSICAL_CORES
  logical_cores: $LOGICAL_CORES
  ram_gb: $RAM_GB
  kernel: "$KERNEL"
  compiler: "$COMPILER"
  chronon_commit: "$CHRONON_COMMIT"
  cmake_preset: "$CMAKE_PRESET"
  cpu_governor: "$GOVERNOR"
  temperature_c: $temp_yaml
EOF
}

# ── Emit JSON ──────────────────────────────────────────────────────────
emit_json() {
    local temp_json
    if [[ "$TEMPERATURE" == "unavailable" ]]; then
        temp_json="\"$TEMPERATURE\""
    else
        temp_json="$TEMPERATURE"
    fi
    cat <<EOF
{
  "host": {
    "cpu_model": $(printf '%s' "$CPU_MODEL" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'),
    "physical_cores": $PHYSICAL_CORES,
    "logical_cores": $LOGICAL_CORES,
    "ram_gb": $RAM_GB,
    "kernel": $(printf '%s' "$KERNEL" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'),
    "compiler": $(printf '%s' "$COMPILER" | python3 -c 'import json,sys; print(json.dumps(sys.stdin.read()))'),
    "chronon_commit": "$CHRONON_COMMIT",
    "cmake_preset": "$CMAKE_PRESET",
    "cpu_governor": "$GOVERNOR",
    "temperature_c": $temp_json
  }
}
EOF
}

# ── Main ────────────────────────────────────────────────────────────────
case "$FORMAT" in
    json)
        emit_json
        ;;
    yaml)
        emit_yaml
        ;;
    *)
        echo "Usage: $0 [--yaml|--json]" >&2
        exit 1
        ;;
esac
