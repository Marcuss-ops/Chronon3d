#!/usr/bin/env python3
"""Materialize heavy Chronon3d test fixtures on demand.

Font and image fixtures used by the text/render test suites are intentionally
not tracked in the core repo (see .gitignore). This script materializes them
into ``assets/`` from blob SHA-1 pinned objects so bootstrap output is
deterministic:

- blobs present in the local git object store (pinned snapshot commit
  ``a31b162795d95c58e7a4e4d05df83398604487fb``) are extracted with
  ``git cat-file`` — network-free and byte-exact;
- anything missing locally is downloaded from the pinned snapshot on GitHub;
- every payload is verified by reconstructing the Git blob SHA-1 before the
  (atomic) write, regardless of source.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

SNAPSHOT_COMMIT = "a31b162795d95c58e7a4e4d05df83398604487fb"
REPOSITORY = "Marcuss-ops/Chronon3d"
ASSETS_DIR = Path(__file__).resolve().parents[1] / "assets"
FONT_DIR = ASSETS_DIR / "fonts"
IMAGE_DIR = ASSETS_DIR / "images"

# ── Fonts (destination relative to assets/fonts/) ─────────────────────────────
FONTS = {
    # Heavy multilingual/emoji set (original bootstrap fleet).
    "FreeSerif.ttf": "982865a6358c5fa6de7060125f44ab043c286a27",
    "NotoColorEmoji.ttf": "c46a8c1bf861c82131360383ba94a1f09cb79d5f",
    "NotoNaskhArabic-Bold.ttf": "7ae5a31521d53028d67973115d2a1b8c840e230c",
    "NotoNaskhArabic-Regular.ttf": "00a33b3dfe8411968dabf2bfd1deb02bb79ff0f6",
    "NotoSansCJK-Regular.ttc": "a2033d0e4e53f568c3f418a7d5d8c951af3f76c1",
    "NotoSansHebrew-Bold.ttf": "52bc09d9dfff890744b133c76eebabcedd966e71",
    "NotoSansHebrew-Regular.ttf": "493b7d2d89d783e717f76c208f07f8b3690207c7",
    "NotoSansSymbols2-Regular.ttf": "45df8301c7bdfaccff52e30a0973132e94b2e95d",
    "UnifontUpper.otf": "1edc6ccbdaf53064a4ff982c23b9b29f2ef8ae51",
    # UI set demolished from main in 3bed1ea0e; still referenced by presets,
    # text-run suites and the font-fallback stack.
    "Inter-Bold.ttf": "9d7cf220f98402e6908795046cd3c124886fc54a",
    "Inter-Regular.ttf": "7e3bb2f8ce7ae5b69e9f32c1481a06f16ebcfe71",
    "Poppins-Bold.ttf": "1982f38ab21303459aa1155265052ca599fa58d1",
    "Poppins-Regular.ttf": "0bda228ade88b0bb5aac7da2c881d0c3f64d0817",
    "DMSans-Bold.ttf": "811136c6dafecfa0880718f2709ef86e71dc2ff8",
    "DMSans-Regular.ttf": "6c789eadb84764a2d7f75fc989ccb78873ec1540",
    "Georgia_Bold.ttf": "2cfce236e6683ad45f51f9884dfb8778992d7cb9",
}

# ── Images (destination relative to assets/) ──────────────────────────────────
IMAGES = {
    "images/checker.png": "5a7915fc97a11a51fa95b671d25e250ac042c940",
    "images/grid_tile.png": "c635046a3d1b83d3de2ffe508c0e349f297d8de0",
    "images/minimalist_landscape.png": "c92afa28bdbb847b2a421418ef81f26c334e20e2",
    "images/camera_reference.jpg": "8edb0e70c17fd4a167cc061138a8e3a8c27d81f0",
    "test_image.png": "75cad0125b1ed3b577da82a05b75b910770b7656",
}


def git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def blob_in_local_store(expected_sha: str) -> bool:
    probe = subprocess.run(
        ["git", "cat-file", "-e", expected_sha],
        capture_output=True,
    )
    return probe.returncode == 0


def read_local_blob(expected_sha: str) -> bytes:
    return subprocess.run(
        ["git", "cat-file", "blob", expected_sha],
        check=True,
        capture_output=True,
    ).stdout


def fetch_remote_blob(name: str, expected_sha: str) -> bytes:
    url = (
        f"https://raw.githubusercontent.com/{REPOSITORY}/"
        f"{SNAPSHOT_COMMIT}/assets/{name}"
    )
    print(f"fetch   {name}")
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def valid(path: Path, expected_sha: str) -> bool:
    return path.is_file() and git_blob_sha(path.read_bytes()) == expected_sha


def materialize(name: str, expected_sha: str, destination: Path, force: bool) -> None:
    if not force and valid(destination, expected_sha):
        print(f"ok      {name}")
        return

    if blob_in_local_store(expected_sha):
        data = read_local_blob(expected_sha)
    else:
        data = fetch_remote_blob(name, expected_sha)

    actual_sha = git_blob_sha(data)
    if actual_sha != expected_sha:
        raise RuntimeError(
            f"checksum mismatch for {name}: expected {expected_sha}, got {actual_sha}"
        )

    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as temp:
        temp.write(data)
        temp_path = Path(temp.name)
    temp_path.replace(destination)
    print(f"ok      {name}")


def check_all() -> int:
    missing_or_invalid: list[str] = []
    for name, expected_sha in FONTS.items():
        if not valid(FONT_DIR / name, expected_sha):
            print(f"missing assets/fonts/{name}")
            missing_or_invalid.append(name)
    for name, expected_sha in IMAGES.items():
        if not valid(ASSETS_DIR / name, expected_sha):
            print(f"missing assets/{name}")
            missing_or_invalid.append(name)
    return 1 if missing_or_invalid else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify fixtures without downloading them",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="re-materialize fixtures even when their checksum is valid",
    )
    args = parser.parse_args()

    if args.check:
        return check_all()

    try:
        for name, expected_sha in FONTS.items():
            materialize(name, expected_sha, FONT_DIR / name, args.force)
        for name, expected_sha in IMAGES.items():
            materialize(name, expected_sha, ASSETS_DIR / name, args.force)
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return check_all()


if __name__ == "__main__":
    raise SystemExit(main())
