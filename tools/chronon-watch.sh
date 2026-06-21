#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_DIR"

latest_stamp() {
  local latest=0
  local paths=(
    "include/chronon3d"
    "apps/chronon3d_cli"
    "examples"
    "src"
    "tests"
    "CMakeLists.txt"
    "CMakePresets.json"
    "vcpkg.json"
    "tools"
  )

  for path in "${paths[@]}"; do
    if [[ -d "$path" ]]; then
      while IFS= read -r -d '' file; do
        local stamp
        stamp="$(stat -c '%Y' "$file")"
        if (( stamp > latest )); then
          latest="$stamp"
        fi
      done < <(find "$path" -type f -print0)
    elif [[ -f "$path" ]]; then
      local stamp
      stamp="$(stat -c '%Y' "$path")"
      if (( stamp > latest )); then
        latest="$stamp"
      fi
    fi
  done

  echo "$latest"
}

bash "$SCRIPT_DIR/chronon-linux.sh"

last_stamp="$(latest_stamp)"
echo "Chronon watch running. Watching include/chronon3d, apps/chronon3d_cli, examples, src, tests, and build files."

while true; do
  sleep 1
  current_stamp="$(latest_stamp)"
  if [[ "$current_stamp" != "$last_stamp" ]]; then
    last_stamp="$current_stamp"
    echo "Change detected, rebuilding Chronon3d..."
    CHRONON_SKIP_DEPS=1 bash "$SCRIPT_DIR/chronon-linux.sh"
  fi
done
