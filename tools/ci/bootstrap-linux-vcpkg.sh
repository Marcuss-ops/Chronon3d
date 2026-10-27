#!/usr/bin/env bash
# Install the system assembler required by vcpkg's FFmpeg port on Linux.
set -euo pipefail

if ! command -v nasm >/dev/null 2>&1; then
  if [[ "${EUID}" -eq 0 ]]; then
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends nasm
  else
    sudo apt-get update -qq
    sudo apt-get install -y -qq --no-install-recommends nasm
  fi
fi

command -v nasm
nasm -v
