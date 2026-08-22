#!/usr/bin/env bash
# ============================================================================
# tools/check_trace_production_build.sh — production tracing propagation gate
# ============================================================================
#
# Verifies the actual CMake compile database, rather than a hand-written
# compiler invocation. Every production TU that emits CHRONON_TRACE_* (and
# every TU directly including a production header that emits those macros)
# must be present in compile_commands.json and must compile with:
#
#     -DCHRONON3D_ENABLE_TRACING
#
# This gate deliberately does not infer success from the final link command:
# a translation unit compiled with tracing disabled has already lost its
# Perfetto events before linking starts.
#
# Usage:
#   bash tools/check_trace_production_build.sh [path/to/compile_commands.json]
#
# Env vars:
#   CHRONON3D_TRACE_PRODUCTION_COMPILE_COMMANDS
#       Compile database path (overridden by the positional argument).
#   CHRONON3D_TRACE_PRODUCTION_STRICT
#       Missing trace-bearing TUs fail the gate (default: 1). Set to 0 only
#       for a deliberately partial build; missing TUs remain visible as
#       NOT_BUILT diagnostics.
#   CHRONON3D_TRACE_PRODUCTION_ALLOW_MISSING
#       Comma-separated source paths allowed to be absent from the database
#       in strict mode (for example, a disabled Vulkan feature).
#
# Exit codes:
#   0 = all discovered/expected production TUs carry the tracing define
#   1 = tracing propagation failure or strict missing-TU failure
#   2 = gate internal error (missing Python, unreadable/malformed database)
# ============================================================================

set -euo pipefail

GATE_NAME="check_trace_production_build"
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
cd "$REPO_ROOT" || {
    echo "GATE_FAIL: cannot cd to repository root $REPO_ROOT" >&2
    exit 2
}

COMPILE_COMMANDS="${1:-${CHRONON3D_TRACE_PRODUCTION_COMPILE_COMMANDS:-build/chronon/linux-fast-dev/compile_commands.json}}"
STRICT="${CHRONON3D_TRACE_PRODUCTION_STRICT:-1}"
ALLOW_MISSING="${CHRONON3D_TRACE_PRODUCTION_ALLOW_MISSING:-}"

if ! command -v python3 >/dev/null 2>&1; then
    echo "GATE_FAIL: python3 is required to parse compile_commands.json" >&2
    exit 2
fi

if [[ ! -f "$COMPILE_COMMANDS" ]]; then
    echo "GATE_FAIL: compile_commands.json not found: $COMPILE_COMMANDS" >&2
    echo "  hint: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
    exit 2
fi

python3 - "$REPO_ROOT" "$COMPILE_COMMANDS" "$STRICT" "$ALLOW_MISSING" <<'PY'
import json
import re
import sys
from pathlib import Path

repo_root = Path(sys.argv[1]).resolve()
compile_commands_path = Path(sys.argv[2])
if not compile_commands_path.is_absolute():
    compile_commands_path = (repo_root / compile_commands_path).resolve()
strict = sys.argv[3] != "0"
allow_missing = {
    item.strip().replace("\\", "/")
    for item in sys.argv[4].split(",")
    if item.strip()
}

try:
    with compile_commands_path.open(encoding="utf-8") as stream:
        database = json.load(stream)
except (OSError, json.JSONDecodeError) as exc:
    print(f"GATE_FAIL: cannot parse {compile_commands_path}: {exc}", file=sys.stderr)
    raise SystemExit(2)

if not isinstance(database, list):
    print("GATE_FAIL: compile_commands.json root must be an array", file=sys.stderr)
    raise SystemExit(2)

production_roots = [repo_root / "src", repo_root / "apps" / "chronon3d_cli"]
source_extensions = {".c", ".cc", ".cpp", ".cxx"}
header_extensions = {".h", ".hh", ".hpp", ".hxx"}
trace_call = re.compile(r"(?m)^[ \t]*CHRONON_TRACE_[A-Z0-9_]+[ \t]*\(")
include_line = re.compile(r"^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]", re.MULTILINE)

# Remove comments before looking for macro invocations. This prevents the
# gate from treating documentation examples as compiled tracing call-sites.
def without_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def relative_to_repo(path: Path) -> str:
    try:
        return path.resolve().relative_to(repo_root).as_posix()
    except ValueError:
        return path.as_posix()


def is_production_path(path: Path) -> bool:
    resolved = path.resolve()
    return any(root.resolve() == resolved or root.resolve() in resolved.parents
               for root in production_roots)

# Discover direct .cpp call-sites and production headers with inline macro
# call-sites. Headers are mapped to TUs that directly include them; this
# covers inline instrumentation such as nvml_sampler.hpp without pretending
# that a header itself is a translation unit.
trace_sources = set()
trace_headers = []
for root in production_roots:
    if not root.is_dir():
        continue
    for path in root.rglob("*"):
        if path.suffix.lower() not in source_extensions | header_extensions:
            continue
        try:
            text = without_comments(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError):
            continue
        if not trace_call.search(text):
            continue
        if path.suffix.lower() in source_extensions:
            trace_sources.add(path.resolve())
        else:
            trace_headers.append((path.resolve(), text))

# The compilation database is the authority for which TUs are present in the
# selected production configuration. Read the sibling cache as well so
# option-gated production sources are required only when their feature is ON.
entries_by_source = {}
cache_options = {}
cache_path = compile_commands_path.parent / "CMakeCache.txt"
if cache_path.is_file():
    try:
        for line in cache_path.read_text(encoding="utf-8").splitlines():
            match = re.match(r"^([^:#=]+):[^=]*=(.*)$", line)
            if match:
                cache_options[match.group(1)] = match.group(2)
    except (OSError, UnicodeDecodeError) as exc:
        print(f"GATE_FAIL: cannot read {cache_path}: {exc}", file=sys.stderr)
        raise SystemExit(2)


def option_enabled(name: str):
    if name not in cache_options:
        return None
    return cache_options[name].upper() in {"1", "ON", "YES", "TRUE"}


def required_when_feature_enabled(path: str) -> bool:
    if path == "src/backends/vulkan/vulkan_backend.cpp":
        enabled = option_enabled("CHRONON3D_ENABLE_VULKAN")
        return enabled is None or enabled
    if path == "src/media/video/native_video_frame_decoder.cpp":
        video = option_enabled("CHRONON3D_ENABLE_VIDEO")
        ffmpeg = option_enabled("CHRONON3D_ENABLE_NATIVE_FFMPEG")
        return video is None or ffmpeg is None or (video and ffmpeg)
    if path.startswith("apps/chronon3d_cli/commands/video/"):
        enabled = option_enabled("CHRONON3D_ENABLE_VIDEO")
        return enabled is None or enabled
    return True

for entry in database:
    if not isinstance(entry, dict):
        print("GATE_FAIL: compile_commands.json contains a non-object entry", file=sys.stderr)
        raise SystemExit(2)
    raw_file = entry.get("file")
    if not isinstance(raw_file, str) or not raw_file:
        print("GATE_FAIL: compile_commands entry has no file field", file=sys.stderr)
        raise SystemExit(2)
    directory = Path(entry.get("directory") or repo_root)
    source_path = Path(raw_file)
    if not source_path.is_absolute():
        source_path = directory / source_path
    source_path = source_path.resolve()
    if source_path.suffix.lower() not in source_extensions or not is_production_path(source_path):
        continue
    entries_by_source.setdefault(source_path, []).append(entry)

# Associate inline-tracing headers with direct including production TUs. The
# basename fallback handles both quoted local includes and project-root paths.
for header_path, header_text in trace_headers:
    header_rel = relative_to_repo(header_path)
    header_name = header_path.name
    for source_path in entries_by_source:
        try:
            source_text = source_path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        includes = include_line.findall(source_text)
        if any(include == header_rel or include.endswith("/" + header_name)
               for include in includes):
            trace_sources.add(source_path)

# A source containing a call-site but absent from the database is not silently
# treated as PASS: in strict mode that means the selected production build did
# not compile part of its tracing surface (usually an option-gated backend).
missing_sources = sorted(
    relative_to_repo(path) for path in trace_sources if path not in entries_by_source
)

failures = []
checked_entries = 0
for source_path in sorted(trace_sources, key=lambda item: item.as_posix()):
    rel = relative_to_repo(source_path)
    if source_path not in entries_by_source:
        allowed = (
            rel in allow_missing
            or source_path.as_posix() in allow_missing
            or not required_when_feature_enabled(rel)
        )
        if strict and not allowed:
            failures.append((rel, "NOT_BUILT", "no compile_commands entry"))
        else:
            reason = "feature disabled" if not required_when_feature_enabled(rel) else "allowed partial configuration"
            print(f"  NOT_BUILT source={rel} ({reason})")
        continue

    for entry in entries_by_source[source_path]:
        checked_entries += 1
        command = entry.get("command")
        if command is None:
            arguments = entry.get("arguments")
            command = " ".join(arguments) if isinstance(arguments, list) else ""
        has_define = bool(re.search(
            r"(?:^|[ \t])-DCHRONON3D_ENABLE_TRACING(?:=[^ \t]+)?(?:$|[ \t])",
            command,
        ))
        output = str(entry.get("output") or "")
        target_match = re.search(r"CMakeFiles/([^/]+)\.dir/", output)
        target = target_match.group(1) if target_match else "unknown-target"
        if not has_define:
            failures.append((rel, "TRACE_OFF", f"target={target}"))
        else:
            print(f"  PASS target={target} source={rel}")

if not trace_sources:
    print("GATE_FAIL: no production CHRONON_TRACE_* call-sites discovered", file=sys.stderr)
    raise SystemExit(1)

if failures:
    print(f"GATE_FAIL: {len(failures)} production tracing violation(s)", file=sys.stderr)
    for source, kind, detail in failures:
        print(f"  {kind} source={source} ({detail})", file=sys.stderr)
    if missing_sources:
        print("  hint: enable the production feature that owns each NOT_BUILT TU, or", file=sys.stderr)
        print("        use CHRONON3D_TRACE_PRODUCTION_ALLOW_MISSING only for an intentional partial build", file=sys.stderr)
    raise SystemExit(1)

print(
    f"GATE_PASS: {checked_entries} production compile command(s) carry "
    "-DCHRONON3D_ENABLE_TRACING"
)
print(
    "[INFO] check_trace_production_build: production tracing define verified "
    f"for {len(trace_sources)} trace-bearing TU(s)"
)
PY
PY_STATUS=$?
exit "$PY_STATUS"
