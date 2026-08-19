#!/usr/bin/env python3
"""Small NVCC-compatible PTX wrapper for environments with NVRTC only.

FFmpeg's CUDA filters need NVCC only to turn their .cu kernels into PTX.  The
runtime driver is already present on the render host, while the full CUDA
compiler is not.  This wrapper translates the Makefile's `-ptx -o` invocation
to NVRTC and is intentionally limited to that build operation.
"""

from __future__ import annotations

import pathlib
import re
import sys

import cupy
from cupy.cuda import compiler


def main(argv: list[str]) -> int:
    output: str | None = None
    source: str | None = None
    options: list[str] = []
    include_dirs: list[str] = []
    arch = "86"
    i = 0
    while i < len(argv):
        arg = argv[i]
        if arg == "-o" and i + 1 < len(argv):
            output = argv[i + 1]
            i += 2
            continue
        if arg.endswith(".cu"):
            source = arg
            i += 1
            continue
        if arg.startswith("-gencode") and i + 1 < len(argv):
            spec = argv[i + 1]
            match = re.search(r"arch=compute_(\d+)", spec)
            if match:
                arch = match.group(1)
            i += 2
            continue
        if arg.startswith("-I"):
            include_dirs.append(arg[2:] or argv[i + 1])
            if arg == "-I":
                i += 1
            i += 1
            continue
        if arg in {"-ptx", "-O0", "-O1", "-O2", "-O3", "-lineinfo"}:
            i += 1
            continue
        i += 1

    if not output or not source:
        raise SystemExit("nvrtc wrapper supports only: ... -ptx -o output input.cu")

    source_path = pathlib.Path(source)
    # FFmpeg's out-of-tree makefiles rewrite source paths through a temporary
    # `.../src/...` link.  The temporary prefix can disappear before NVRTC
    # opens the file; the build-directory `src` link is stable.
    if not source_path.exists() and "/src/" in source:
        source_path = pathlib.Path.cwd() / "src" / source.split("/src/", 1)[1]
    if not source_path.exists():
        raise SystemExit(f"CUDA source does not exist: {source}")
    options.extend("-I" + path for path in include_dirs)
    options.extend(("-I" + str(source_path.parent),
                   "-I" + str(source_path.parent.parent)))
    options.extend(("--std=c++11", "--device-as-default-execution-space"))
    ptx, _ = compiler.compile_using_nvrtc(
        source_path.read_text(), options=tuple(options), arch=arch,
        filename=source_path.name, cache_in_memory=False,
    )
    pathlib.Path(output).write_bytes(ptx)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
