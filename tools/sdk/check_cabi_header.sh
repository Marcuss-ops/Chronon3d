#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HEADER="$ROOT/include/chronon/abi.h"
[[ -f "$HEADER" ]] || { echo "missing canonical ABI header: $HEADER" >&2; exit 1; }

# The contract must remain POD/opaque and independent of the implementation.
if grep -n '^#include' "$HEADER" | grep -v '<stdint.h>' ||
   grep -nE 'std::|namespace|class |vector|memory|TextLayout|FontResolver|GlyphCache|AnimationEngine|CameraSolver|SceneGraph|Renderer' "$HEADER"; then
    echo "canonical ABI header contains implementation/C++ surface" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
printf '#include <chronon/abi.h>\nint main(void) { return (int)chronon_abi_version(); }\n' > "$TMP/probe.c"
printf '#include <chronon/abi.h>\nint main() { return (int)chronon_abi_version(); }\n' > "$TMP/probe.cpp"
cc -std=c11 -Wall -Wextra -Werror -I"$ROOT/include" -fsyntax-only "$TMP/probe.c"
c++ -std=c++20 -Wall -Wextra -Werror -I"$ROOT/include" -fsyntax-only "$TMP/probe.cpp"
echo "CABI_HEADER_PASS: chronon/abi.h is self-contained for C and C++"
