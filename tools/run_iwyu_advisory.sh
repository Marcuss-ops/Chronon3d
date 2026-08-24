#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build/chronon/linux-fast-dev}"
command -v include-what-you-use >/dev/null 2>&1 || {
    echo "IWYU_ADVISORY_SKIP: include-what-you-use not installed"; exit 0;
}
[[ -f "$build_dir/compile_commands.json" ]] || {
    echo "IWYU_ADVISORY_FAIL: compile_commands.json missing at $build_dir" >&2; exit 1;
}
out="${IWYU_REPORT:-iwyu-report.txt}"
python3 - "$build_dir/compile_commands.json" "$out" <<'PY'
import json, pathlib, shlex, subprocess, sys
commands = json.loads(pathlib.Path(sys.argv[1]).read_text())
out = pathlib.Path(sys.argv[2])
lines = []
for entry in commands:
    raw = entry.get("arguments")
    argv = list(raw) if raw else shlex.split(entry.get("command", ""))
    if not argv:
        continue
    # compile_commands starts with the compiler executable. IWYU is the
    # replacement driver, so retain every compiler argument after it.
    command = argv[1:]
    try:
        p = subprocess.run(["include-what-you-use", *command], text=True,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        if p.stdout.strip():
            lines.append(f"## {entry.get('file', '<unknown>')}\n{p.stdout}")
    except OSError as exc:
        lines.append(f"## runner error: {exc}")
out.write_text("\n".join(lines))
print(f"IWYU_ADVISORY_PASS: report={out} diagnostics={len(lines)}")
PY
