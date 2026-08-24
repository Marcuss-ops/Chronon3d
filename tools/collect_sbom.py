#!/usr/bin/env python3
"""Aggregate vcpkg SPDX package documents into a release SBOM."""
from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import subprocess
import uuid


def git(args: list[str]) -> str:
    try:
        return subprocess.check_output(["git", *args], text=True).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--vcpkg-root", required=True, type=pathlib.Path)
    ap.add_argument("--out", required=True, type=pathlib.Path)
    ap.add_argument("--project", default="Chronon3D")
    ns = ap.parse_args()

    docs = sorted(ns.vcpkg_root.rglob("*.spdx.json"))
    packages: dict[str, dict] = {}
    for path in docs:
        try:
            doc = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError):
            continue
        for package in doc.get("packages", []):
            sid = package.get("SPDXID") or package.get("name")
            if sid:
                packages[sid] = package

    created = dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    document = {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{ns.project}-sbom",
        "documentNamespace": f"https://chronon3d.local/spdx/{uuid.uuid4()}",
        "creationInfo": {"created": created, "creators": ["Tool: Chronon3D collect_sbom.py"]},
        "packages": list(packages.values()),
        "relationships": [],
        "annotations": [{
            "annotationDate": created,
            "annotationType": "OTHER",
            "annotator": "Tool: Chronon3D collect_sbom.py",
            "SPDXID": "SPDXRef-DOCUMENT",
            "comment": json.dumps({"git_sha": git(["rev-parse", "HEAD"]), "vcpkg_root": str(ns.vcpkg_root)}, sort_keys=True),
        }],
    }
    ns.out.parent.mkdir(parents=True, exist_ok=True)
    ns.out.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n")
    print(f"SBOM_PASS: {len(packages)} SPDX packages -> {ns.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
