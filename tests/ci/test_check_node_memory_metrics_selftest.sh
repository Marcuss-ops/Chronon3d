#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
gate="${repo_root}/tools/check_node_memory_metrics.sh"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

cat >"${tmp_dir}/clean.json" <<'JSON'
{
  "schema": "chronon3d.stats.v1",
  "session_id": "selftest-clean",
  "scene_id": "fixture",
  "frame_count": 5,
  "nodes": [{
    "node_id": "g0:n1",
    "node_type": "Composite",
    "pixels_read": 16,
    "pixels_written": 16,
    "bytes_read": 256,
    "bytes_written": 256,
    "framebuffer_copies": 0,
    "framebuffer_clears": 0,
    "allocations": 0,
    "allocated_bytes": 0,
    "temporary_buffers": 1,
    "live_bytes": 0,
    "peak_live_bytes": 256
  }]
}
JSON

cat >"${tmp_dir}/allocating.json" <<'JSON'
{
  "schema": "chronon3d.stats.v1",
  "session_id": "selftest-allocating",
  "scene_id": "fixture",
  "frame_count": 5,
  "nodes": [{
    "node_id": "g0:n1",
    "node_type": "Composite",
    "pixels_read": 0,
    "pixels_written": 16,
    "bytes_read": 0,
    "bytes_written": 256,
    "framebuffer_copies": 0,
    "framebuffer_clears": 0,
    "allocations": 1,
    "allocated_bytes": 256,
    "temporary_buffers": 1,
    "live_bytes": 0,
    "peak_live_bytes": 256
  }]
}
JSON

"$gate" "${tmp_dir}/clean.json"
if "$gate" "${tmp_dir}/allocating.json"; then
    echo "SELFTEST_FAIL: gate accepted a non-zero steady-state allocation report" >&2
    exit 1
fi
if "$gate" "${tmp_dir}/missing.json"; then
    echo "SELFTEST_FAIL: gate accepted a missing report" >&2
    exit 1
fi
echo "SELFTEST_PASS: clean, allocating, and missing-report paths verified"
