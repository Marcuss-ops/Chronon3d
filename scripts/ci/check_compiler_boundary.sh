#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
TARGETS=("$ROOT/include/chronon3d/compiler" "$ROOT/src/compiler")
PATTERN='vulkan|cuda|libav|runtime/|backends/|Framebuffer|GpuRuntime'

if rg -n -i "$PATTERN" "${TARGETS[@]}" --glob '*.{hpp,cpp,inc}' ; then
    echo "compiler boundary violation" >&2
    exit 1
fi

# The graph lowering implementation is owned by the compiler object library;
# no public graph-compiler facade or legacy GraphBuilder wrapper may remain.
if rg -n 'graph_compile\.hpp|GraphBuilder::build|class GraphBuilder' \
    "$ROOT/include" "$ROOT/src" "$ROOT/apps" --glob '*.{hpp,cpp,inc,cmake}' ; then
    echo "compiler graph boundary violation" >&2
    exit 1
fi

if rg -n 'chronon3d_(render_plan_compiler|graph_builder|graph_compiler)' \
    "$ROOT/CMakeLists.txt" "$ROOT/src" "$ROOT/cmake" "$ROOT/apps" \
    --glob '*.{cmake,txt}' ; then
    echo "retired compiler target reference found" >&2
    exit 1
fi

echo "compiler boundary: PASS"
