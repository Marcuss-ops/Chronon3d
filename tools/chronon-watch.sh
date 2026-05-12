#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

latest_stamp() {
  local latest=0
  local paths=(
    "include/chronon"
    "scenes"
    "src"
    "CMakeLists.txt"
    "CMakePresets.json"
    "vcpkg.json"
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

bash "$ROOT_DIR/chronon-linux.sh"

last_stamp="$(latest_stamp)"
echo "Chronon watch running. Watching include/chronon, scenes, src, and build files."

while true; do
  sleep 1
  current_stamp="$(latest_stamp)"
  if [[ "$current_stamp" != "$last_stamp" ]]; then
    last_stamp="$current_stamp"
    echo "Change detected, rebuilding ChrononTemplate..."
    CHRONON_SKIP_DEPS=1 bash "$ROOT_DIR/chronon-linux.sh"
  fi
done
