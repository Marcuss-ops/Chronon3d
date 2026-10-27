#!/usr/bin/env bash
# Install the assembler and linkers required by Linux vcpkg and render builds.
set -euo pipefail

missing=()
command -v nasm >/dev/null 2>&1 || missing+=(nasm)
command -v ld >/dev/null 2>&1 || missing+=(binutils)
command -v mold >/dev/null 2>&1 || missing+=(mold)

if [[ ${#missing[@]} -gt 0 ]]; then
  if [[ "${EUID}" -eq 0 ]]; then
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends "${missing[@]}"
  else
    sudo apt-get update -qq
    sudo apt-get install -y -qq --no-install-recommends "${missing[@]}"
  fi
fi

command -v nasm
nasm -v
command -v ld
ld --version | head -n 1
command -v mold
mold --version | head -n 1
