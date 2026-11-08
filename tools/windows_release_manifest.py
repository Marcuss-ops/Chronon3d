#!/usr/bin/env python3
"""Emit the SHA-256 receipt for a Windows release build.

The Windows CI lane builds the release CLI from the `win-release` preset. A
release artifact without an identity is not evidence, so this tool records the
artifact bytes next to the commit that produced them:

  windows-release.sha256  coreutils-style `<sha256>  <path>` lines, sorted;
  windows-release.json    schema_version 1 receipt with commit, version,
                          preset, artifact sizes and hashes.

The tool is fail-closed: a missing build tree, a malformed commit, a missing
version file or an empty artifact set exits non-zero. CI must not upload an
empty manifest and call it a release.

Usage:
  python3 tools/windows_release_manifest.py \
      --build-dir build/chronon/win-release \
      --commit "$GITHUB_SHA" \
      --version-file version.txt \
      --preset win-release \
      --out-dir windows-release-receipt
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

SCHEMA_VERSION = 1
COMMIT_PATTERN = re.compile(r"^[0-9a-f]{40}$")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_artifacts(build_dir: Path) -> list[Path]:
    """Return the release CLI plus any DLLs shipped beside it."""
    candidates = sorted(build_dir.rglob("chronon3d_cli.exe"))
    if not candidates:
        raise SystemExit(
            f"WINDOWS_RELEASE_MANIFEST_FAIL: no chronon3d_cli.exe under {build_dir}"
        )

    cli = candidates[0]
    artifacts = [cli]
    artifacts.extend(sorted(cli.parent.glob("*.dll")))
    return artifacts


def relative_artifact_path(path: Path, build_dir: Path) -> str:
    try:
        relative = path.relative_to(build_dir)
    except ValueError as error:
        raise SystemExit(
            f"WINDOWS_RELEASE_MANIFEST_FAIL: artifact {path} is outside {build_dir}"
        ) from error
    return relative.as_posix()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Write the same-SHA SHA-256 receipt for a Windows release build."
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        required=True,
        help="Configured win-release build tree (build/chronon/win-release).",
    )
    parser.add_argument(
        "--commit",
        required=True,
        help="Full 40-hex git commit SHA the artifacts were built from.",
    )
    parser.add_argument(
        "--version-file",
        type=Path,
        default=Path("version.txt"),
        help="Project version file (default: version.txt).",
    )
    parser.add_argument(
        "--preset",
        default="win-release",
        help="CMake configure preset that produced the artifacts.",
    )
    parser.add_argument(
        "--platform",
        default="windows",
        help="Platform label recorded in the receipt.",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        required=True,
        help="Directory that receives windows-release.sha256 and windows-release.json.",
    )
    parser.add_argument(
        "--generated-at",
        default=None,
        help="Override the receipt timestamp (ISO-8601 UTC, for tests).",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    commit = args.commit.strip().lower()
    if not COMMIT_PATTERN.fullmatch(commit):
        raise SystemExit(
            f"WINDOWS_RELEASE_MANIFEST_FAIL: commit must be a full 40-hex SHA, got {args.commit!r}"
        )

    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        raise SystemExit(f"WINDOWS_RELEASE_MANIFEST_FAIL: build tree not found: {build_dir}")

    if not args.version_file.is_file():
        raise SystemExit(
            f"WINDOWS_RELEASE_MANIFEST_FAIL: version file not found: {args.version_file}"
        )
    version = args.version_file.read_text(encoding="utf-8").strip()
    if not version:
        raise SystemExit(
            f"WINDOWS_RELEASE_MANIFEST_FAIL: version file is empty: {args.version_file}"
        )

    artifacts = []
    seen = set()
    for artifact in find_artifacts(build_dir):
        relative = relative_artifact_path(artifact, build_dir)
        if relative in seen:
            continue
        seen.add(relative)
        artifacts.append(
            {
                "path": relative,
                "sha256": sha256_file(artifact),
                "bytes": artifact.stat().st_size,
            }
        )

    generated_at = args.generated_at
    if generated_at is None:
        generated_at = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

    receipt = {
        "schema_version": SCHEMA_VERSION,
        "platform": args.platform,
        "preset": args.preset,
        "version": version,
        "commit": commit,
        "generated_at": generated_at,
        "artifacts": artifacts,
    }

    args.out_dir.mkdir(parents=True, exist_ok=True)
    checksum_path = args.out_dir / "windows-release.sha256"
    receipt_path = args.out_dir / "windows-release.json"

    checksum_lines = [
        f"{artifact['sha256']}  {artifact['path']}\n" for artifact in artifacts
    ]
    checksum_path.write_text("".join(checksum_lines), encoding="utf-8")
    receipt_path.write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )

    print(
        "WINDOWS_RELEASE_MANIFEST_PASS: "
        f"commit={commit[:12]} artifacts={len(artifacts)} "
        f"receipt={receipt_path} checksums={checksum_path}"
    )
    for artifact in artifacts:
        print(f"  {artifact['sha256']}  {artifact['path']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
