#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
header="$root/include/chronon3d/media/video/native_video_frame_decoder.hpp"
source="$root/src/media/video/native_video_frame_decoder.cpp"
test_source="$root/tests/video/test_native_video_frame_decoder.cpp"

if [[ ! -f "$header" || ! -f "$source" || ! -f "$test_source" ]]; then
    echo "GATE_FAIL: native decoder concurrency contract files are missing" >&2
    exit 1
fi

# The map lock may protect session lookup/creation only. Stateful FFmpeg work
# must be guarded by the Session lock, and the regression test must exercise
# two different sources concurrently.
if ! rg -q 'std::mutex m_mutex;' "$header" ||
   ! rg -q 'std::mutex mutex;' "$header" ||
   ! rg -q 'std::lock_guard<std::mutex> lock\(m_mutex\);' "$source" ||
   ! rg -q 'std::lock_guard<std::mutex> session_lock\(session->mutex\);' "$source" ||
   ! rg -q 'std::async\(std::launch::async' "$test_source" ||
   ! rg -q 'source_a' "$test_source" ||
   ! rg -q 'source_b' "$test_source"; then
    echo "GATE_FAIL: decoder does not expose per-session locking plus concurrent-source regression coverage" >&2
    exit 1
fi

echo "GATE_PASS: decoder session map lock is separate from per-source decode lock"
