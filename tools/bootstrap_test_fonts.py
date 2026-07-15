#!/usr/bin/env python3
"""Install deterministic test-font fixtures from pinned GitHub blob objects.

The repository intentionally does not duplicate the binary font.  CI and
verification machines fetch the exact Google Fonts blob through GitHub's blob
API, verify the Git object checksum, and write it below the selected asset root.
No configure-time implicit download occurs unless CHRONON3D_BOOTSTRAP_TEST_FONTS
is enabled.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import sys
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

REPOSITORY = "google/fonts"
POPPINS_REGULAR_BLOB = "0bda228ade88b0bb5aac7da2c881d0c3f64d0817"
POPPINS_OFL_BLOB = "76df3b565672e3248a5715339d092d4cb6c75019"

FIXTURES = (
    (POPPINS_REGULAR_BLOB, Path("assets/fonts/Poppins-Regular.ttf")),
    (POPPINS_OFL_BLOB, Path("assets/fonts/Poppins-OFL.txt")),
)


def git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def fetch_blob(blob_sha: str) -> bytes:
    url = f"https://api.github.com/repos/{REPOSITORY}/git/blobs/{blob_sha}"
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "Chronon3D-test-font-bootstrap",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            payload = json.load(response)
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot fetch pinned blob {blob_sha}: {error}") from error

    if payload.get("sha") != blob_sha:
        raise RuntimeError(
            f"GitHub returned blob {payload.get('sha')!r}, expected {blob_sha}"
        )
    if payload.get("encoding") != "base64":
        raise RuntimeError(
            f"unsupported blob encoding {payload.get('encoding')!r} for {blob_sha}"
        )

    try:
        data = base64.b64decode(payload["content"], validate=False)
    except (KeyError, ValueError) as error:
        raise RuntimeError(f"invalid base64 payload for blob {blob_sha}") from error

    actual = git_blob_sha(data)
    if actual != blob_sha:
        raise RuntimeError(
            f"blob checksum mismatch: downloaded {actual}, expected {blob_sha}"
        )
    return data


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as handle:
        temporary = Path(handle.name)
        handle.write(data)
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(path)


def ensure_fixture(root: Path, blob_sha: str, relative_path: Path) -> None:
    destination = root / relative_path
    if destination.is_file():
        existing = destination.read_bytes()
        if git_blob_sha(existing) == blob_sha:
            print(f"[FONT-BOOTSTRAP] verified existing {relative_path}")
            return
        print(
            f"[FONT-BOOTSTRAP] replacing checksum-mismatched {relative_path}",
            file=sys.stderr,
        )

    data = fetch_blob(blob_sha)
    write_atomic(destination, data)
    print(
        f"[FONT-BOOTSTRAP] installed {relative_path} "
        f"({len(data)} bytes, git-blob={blob_sha})"
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-root",
        type=Path,
        required=True,
        help="Root below which assets/fonts fixtures are installed",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.output_root.expanduser().resolve()
    try:
        for blob_sha, relative_path in FIXTURES:
            ensure_fixture(root, blob_sha, relative_path)
    except RuntimeError as error:
        print(f"[FONT-BOOTSTRAP-FAIL] {error}", file=sys.stderr)
        return 1

    print(f"[FONT-BOOTSTRAP-OK] deterministic fixtures ready under {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
