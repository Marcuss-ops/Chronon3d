#!/usr/bin/env python3
"""Fetch heavy Chronon3d test-font fixtures on demand.

The files are pinned to the last core-repository snapshot that contained them and
verified using Git blob SHA-1, so bootstrap output is deterministic after the
binary fixtures are removed from main.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import tempfile
import urllib.request
from pathlib import Path

SNAPSHOT_COMMIT = "a31b162795d95c58e7a4e4d05df83398604487fb"
REPOSITORY = "Marcuss-ops/Chronon3d"
FONT_DIR = Path(__file__).resolve().parents[1] / "assets" / "fonts"

FONTS = {
    "FreeSerif.ttf": "982865a6358c5fa6de7060125f44ab043c286a27",
    "NotoColorEmoji.ttf": "c46a8c1bf861c82131360383ba94a1f09cb79d5f",
    "NotoNaskhArabic-Bold.ttf": "7ae5a31521d53028d67973115d2a1b8c840e230c",
    "NotoNaskhArabic-Regular.ttf": "00a33b3dfe8411968dabf2bfd1deb02bb79ff0f6",
    "NotoSansCJK-Regular.ttc": "a2033d0e4e53f568c3f418a7d5d8c951af3f76c1",
    "NotoSansHebrew-Bold.ttf": "52bc09d9dfff890744b133c76eebabcedd966e71",
    "NotoSansHebrew-Regular.ttf": "493b7d2d89d783e717f76c208f07f8b3690207c7",
    "NotoSansSymbols2-Regular.ttf": "45df8301c7bdfaccff52e30a0973132e94b2e95d",
    "UnifontUpper.otf": "1edc6ccbdaf53064a4ff982c23b9b29f2ef8ae51",
}


def git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def valid(path: Path, expected_sha: str) -> bool:
    return path.is_file() and git_blob_sha(path.read_bytes()) == expected_sha


def download(name: str, expected_sha: str, force: bool) -> None:
    destination = FONT_DIR / name
    if not force and valid(destination, expected_sha):
        print(f"ok      {name}")
        return

    url = (
        f"https://raw.githubusercontent.com/{REPOSITORY}/"
        f"{SNAPSHOT_COMMIT}/assets/fonts/{name}"
    )
    print(f"fetch   {name}")
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read()

    actual_sha = git_blob_sha(data)
    if actual_sha != expected_sha:
        raise RuntimeError(
            f"checksum mismatch for {name}: expected {expected_sha}, got {actual_sha}"
        )

    FONT_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=FONT_DIR, delete=False) as temp:
        temp.write(data)
        temp_path = Path(temp.name)
    temp_path.replace(destination)


def check_all() -> int:
    missing_or_invalid: list[str] = []
    for name, expected_sha in FONTS.items():
        path = FONT_DIR / name
        if valid(path, expected_sha):
            print(f"ok      {name}")
        else:
            print(f"missing {name}")
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
        help="re-download fixtures even when their checksum is valid",
    )
    args = parser.parse_args()

    if args.check:
        return check_all()

    try:
        for name, expected_sha in FONTS.items():
            download(name, expected_sha, args.force)
    except (OSError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return check_all()


if __name__ == "__main__":
    raise SystemExit(main())
