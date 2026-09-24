#!/usr/bin/env bash
set -euo pipefail
ROOT="$(git rev-parse --show-toplevel)"
exec bash "${ROOT}/tests/ci/test_check_node_memory_metrics_selftest.sh"
