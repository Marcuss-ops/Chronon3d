#!/usr/bin/env python3
"""Certify all registered CTest suites and manifest-defined gates on one SHA.

This is intentionally an orchestration tool, not a second test registry:
CTest discovery is the source of truth for suites and tools/gates/manifest.sh
is the source of truth for gates.

Exit codes:
  0: certification PASS
  1: certification FAIL (contract violation, failed test/gate, stale artifact)
  2: BLOCKED/INTERNAL (missing tool or malformed certification environment)

Example:
  python3 tools/verify_same_sha_certification.py \
      --build-dir build/chronon/linux-fast-dev --profile ci

The output manifest is written even on failure, so a failed certification is
still an auditable artifact rather than an ambiguous terminal log.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import subprocess
import sys
import shlex
import shutil
import tempfile
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


SCRIPT_NAME = "verify_same_sha_certification"
MANIFEST_SCHEMA = "chronon3d.same-sha-certification.v1"


class CertificationError(RuntimeError):
    """A malformed or unavailable certification environment."""


class CertificationFailure(RuntimeError):
    """A certification invariant was observed to fail."""


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_command(
    command: list[str],
    *,
    cwd: Path,
    env: dict[str, str] | None = None,
    timeout: int | None = None,
) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            check=False,
        )
    except FileNotFoundError as exc:
        raise CertificationError(f"required command not found: {command[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        return subprocess.CompletedProcess(command, 124, output + "\nTIMEOUT\n")


def git(repo: Path, *args: str) -> str:
    result = run_command(["git", *args], cwd=repo)
    if result.returncode != 0:
        raise CertificationError(f"git {' '.join(args)} failed:\n{result.stdout}")
    return result.stdout.strip()


def resolve_repo_root(explicit: str | None) -> Path:
    if explicit:
        root = Path(explicit).resolve()
    else:
        try:
            root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel"))
        except CertificationError as exc:
            raise CertificationError("not inside a git repository; use --repo-root") from exc
    if not (root / ".git").exists():
        raise CertificationError(f"repository root has no .git directory: {root}")
    return root


def git_sha(repo: Path, requested: str | None) -> tuple[str, str]:
    head = git(repo, "rev-parse", "HEAD")
    target = requested or head
    try:
        resolved = git(repo, "rev-parse", "--verify", f"{target}^{{commit}}")
    except CertificationError as exc:
        raise CertificationFailure(f"target SHA cannot be resolved: {target}") from exc
    if resolved != head:
        raise CertificationFailure(
            f"SHA mismatch: target={target} resolves to {resolved}, HEAD={head}"
        )
    return head, target


def current_status_sha(repo: Path) -> str | None:
    path = repo / "docs" / "CURRENT_STATUS.md"
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    # Only inspect the current-status header, not historical baseline links.
    header = "\n".join(line for line in text.splitlines()[:20] if line.startswith(">"))
    matches = re.findall(r"\bmain@([0-9a-f]{7,40})\b", header)
    return matches[-1] if matches else None


def check_preconditions(repo: Path, head: str, require_docs_sha: bool) -> list[str]:
    failures: list[str] = []
    status = git(repo, "status", "--porcelain")
    if status:
        failures.append("working tree is dirty; same-SHA certification requires a clean checkout")

    if require_docs_sha:
        documented = current_status_sha(repo)
        if not documented:
            failures.append("docs/CURRENT_STATUS.md has no current main@<sha> header")
        else:
            try:
                documented_full = git(repo, "rev-parse", "--verify", f"{documented}^{{commit}}")
            except CertificationError:
                failures.append(f"docs/CURRENT_STATUS.md declares unknown main@{documented}")
            else:
                relation = run_command(
                    ["git", "merge-base", "--is-ancestor", documented_full, head], cwd=repo
                )
                if relation.returncode != 0:
                    failures.append(
                        f"docs/CURRENT_STATUS.md declares main@{documented}, which is not an ancestor of HEAD {head}"
                    )

    return failures


def load_gate_manifest(repo: Path, profile: str) -> list[str]:
    manifest = repo / "tools" / "gates" / "manifest.sh"
    if not manifest.is_file():
        raise CertificationError(f"gate manifest missing: {manifest}")
    shell = r'''set -euo pipefail
source "$1"
case "$2" in
  developer) values=("${DEVELOPER_GATES[@]}") ;;
  ci)        values=("${DEVELOPER_GATES[@]}" "${CI_PHASES[@]}") ;;
  wbh|all)   values=("${DEVELOPER_GATES[@]}" "${CI_PHASES[@]}" "${WBH_ONLY_GATES[@]}") ;;
  *) echo "invalid profile" >&2; exit 2 ;;
esac
printf '%s\n' "${values[@]}"
'''
    result = run_command(["bash", "-c", shell, "gate-manifest", str(manifest), profile], cwd=repo)
    if result.returncode != 0:
        raise CertificationError(f"could not load gate manifest:\n{result.stdout}")
    gates = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not gates:
        raise CertificationError(f"gate profile {profile!r} is empty")
    if len(gates) != len(set(gates)):
        raise CertificationFailure(f"gate profile {profile!r} contains duplicate entries")
    return gates


def discover_ctest(repo: Path, build_dir: Path, artifact_dir: Path) -> tuple[list[dict[str, Any]], Path]:
    result = run_command(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"], cwd=repo
    )
    if result.returncode != 0:
        raise CertificationError(f"CTest discovery failed:\n{result.stdout}")
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise CertificationError(f"CTest discovery was not JSON:\n{result.stdout}") from exc
    tests = payload.get("tests")
    if not isinstance(tests, list) or not tests:
        raise CertificationFailure("CTest discovery returned no registered tests")
    names = [item.get("name") for item in tests if isinstance(item, dict)]
    if any(not isinstance(name, str) or not name for name in names):
        raise CertificationFailure("CTest discovery contains a test without a name")
    if len(names) != len(set(names)):
        raise CertificationFailure("CTest discovery contains duplicate test names")
    discovery = artifact_dir / "ctest-discovery.json"
    discovery.parent.mkdir(parents=True, exist_ok=True)
    discovery.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return tests, discovery


def parse_ctest_commands(build_dir: Path) -> dict[str, list[str]]:
    """Extract executable argv from generated CTestTestfile.cmake.

    CTest JSON discovery does not expose commands consistently across CTest
    versions. Generated test files do, including tests with extra arguments.
    The parser accepts the canonical CMake-generated bracket-name form and
    keeps the complete argv so a missing executable can never be mistaken for
    a registered passing suite.
    """
    commands: dict[str, list[str]] = {}
    for path in build_dir.rglob("CTestTestfile.cmake"):
        text = path.read_text(encoding="utf-8", errors="replace")
        offset = 0
        while True:
            start = text.find("add_test(", offset)
            if start < 0:
                break
            index = start + len("add_test(")
            depth = 1
            quote = False
            escaped = False
            while index < len(text) and depth:
                char = text[index]
                if quote:
                    if escaped:
                        escaped = False
                    elif char == "\\":
                        escaped = True
                    elif char == '"':
                        quote = False
                else:
                    if char == '"':
                        quote = True
                    elif char == "(":
                        depth += 1
                    elif char == ")":
                        depth -= 1
                index += 1
            if depth:
                raise CertificationError(f"unterminated add_test command in {path}")
            body = text[start + len("add_test("):index - 1].strip()
            name_match = re.match(r"\[=\[(.*?)\]=\]", body, re.DOTALL)
            if name_match:
                name = name_match.group(1)
                command_text = body[name_match.end():].strip()
            else:
                named = re.match(r"NAME\s+(\S+)\s+COMMAND\s+(.+)", body, re.DOTALL)
                if not named:
                    raise CertificationError(f"unsupported add_test name syntax in {path}: {body[:80]}")
                name = named.group(1)
                command_text = named.group(2).strip()
            try:
                argv = shlex.split(command_text)
            except ValueError as exc:
                raise CertificationError(f"cannot parse CTest command for {name}: {exc}") from exc
            if not argv:
                raise CertificationError(f"empty add_test command for {name} in {path}")
            commands[name] = argv
            offset = index
    return commands


def relevant_input_mtime(repo: Path, build_dir: Path, candidate: Path) -> tuple[int | None, str]:
    """Return newest source input mtime for one Ninja target.

    Ninja's dependency graph is the authoritative mapping from a CTest binary
    to its sources. The bounded fallback keeps the tool usable with a Make
    build while avoiding a repository-wide watermark that creates unrelated
    stale failures.
    """
    result = run_command(
        ["ninja", "-C", str(build_dir), "-t", "inputs", candidate.name], cwd=repo
    )
    if result.returncode == 0:
        inputs: list[Path] = []
        for raw in result.stdout.splitlines():
            path = Path(raw.strip())
            if not path.is_absolute():
                path = build_dir / path
            if path.is_file() and path.suffix in {".cpp", ".hpp", ".h", ".cmake"}:
                inputs.append(path)
        if inputs:
            return max(path.stat().st_mtime_ns for path in inputs), "ninja"

    # Make/fake-build fallback: only test sources whose filename contains the
    # target stem, rather than unrelated source files in the repository.
    stem = candidate.name.lower().replace("chronon3d_", "")
    fallback = [
        path for path in (repo / "tests").rglob("*")
        if path.is_file()
        and path.suffix in {".cpp", ".hpp", ".h", ".cmake"}
        and stem in path.name.lower()
    ]
    if fallback:
        return max(path.stat().st_mtime_ns for path in fallback), "test-source-name-fallback"
    return None, "unavailable"


def check_test_artifacts(
    repo: Path, build_dir: Path, tests: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[str]]:
    commands = parse_ctest_commands(build_dir)
    artifacts: list[dict[str, Any]] = []
    failures: list[str] = []

    for test in tests:
        name = str(test["name"])
        argv = commands.get(name, [])
        command = " ".join(shlex.quote(part) for part in argv)
        record: dict[str, Any] = {"name": name, "command": command, "argv": argv, "kind": "unknown"}
        if not argv:
            failures.append(f"registered test has no resolvable command: {name}")
            record["kind"] = "missing"
            artifacts.append(record)
            continue

        candidate = Path(argv[0])
        if not candidate.is_file() and not candidate.is_absolute():
            resolved = shutil.which(argv[0])
            if resolved:
                candidate = Path(resolved)
        # Python/interpreter-driven tests are executable tests even when the
        # script itself is outside the repository; their interpreter is the
        # artifact CTest will invoke. Binary suites must be present locally.
        if candidate.is_file():
            record["kind"] = "binary" if candidate.is_relative_to(build_dir) else "executable"
            record["path"] = str(candidate)
            record["mtime_ns"] = candidate.stat().st_mtime_ns
            if not os.access(candidate, os.X_OK):
                failures.append(f"test command is not executable: {name} ({candidate})")

            # Validate script arguments for direct Python, `env python`, and
            # `python -m` commands. Module mode has no filesystem script to
            # validate; file mode is resolved relative to CTest's repository
            # working directory, matching Chronon3DTestSuite.cmake.
            interpreter_index: int | None = None
            if candidate.name.startswith(("python", "pypy")):
                interpreter_index = 0
            elif candidate.name == "env":
                for index, token in enumerate(argv[1:], start=1):
                    if token == "--":
                        continue
                    if token.startswith("-") or "=" in token:
                        continue
                    if Path(token).name.startswith(("python", "pypy")):
                        interpreter_index = index
                        break
            if interpreter_index is not None:
                python_args = argv[interpreter_index + 1:]
                if "-m" not in python_args:
                    script = next(
                        (Path(token) for token in python_args if not token.startswith("-")),
                        None,
                    )
                    if script is not None:
                        resolved_script = script if script.is_absolute() else repo / script
                        if not resolved_script.is_file():
                            failures.append(f"missing test script: {name} ({resolved_script})")

            if record["kind"] == "binary":
                newest_input, stale_method = relevant_input_mtime(repo, build_dir, candidate)
                record["stale_check"] = stale_method
                if newest_input is None:
                    failures.append(f"cannot verify freshness of test binary: {name} ({candidate})")
                elif candidate.stat().st_mtime_ns < newest_input:
                    failures.append(
                        f"stale test binary: {name} ({candidate}) is older than its target inputs"
                    )
        elif argv[0].endswith((".sh", ".py")) or "gate" in name:
            record["kind"] = "script"
            record["path"] = argv[0]
            if not Path(argv[0]).is_file():
                failures.append(f"missing test script: {name} ({argv[0]})")
        else:
            failures.append(f"missing test executable: {name} ({argv[0]})")
            record["kind"] = "missing"
        artifacts.append(record)
    return artifacts, failures


def parse_junit(path: Path) -> dict[str, str]:
    if not path.is_file():
        raise CertificationError(f"CTest did not produce JUnit output: {path}")
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise CertificationError(f"invalid CTest JUnit output: {path}") from exc
    result: dict[str, str] = {}
    for case in root.iter("testcase"):
        name = case.attrib.get("name")
        if not name:
            continue
        if case.find("skipped") is not None or case.attrib.get("status", "").lower() == "skipped":
            status = "skipped"
        elif case.find("failure") is not None or case.find("error") is not None:
            status = "failed"
        else:
            status = "passed"
        result[name] = status
    if not result:
        raise CertificationFailure(f"CTest JUnit output has no testcases: {path}")
    return result


def run_ctest_once(repo: Path, build_dir: Path, output: Path, timeout: int) -> tuple[int, dict[str, str], str]:
    result = run_command(
        [
            "ctest", "--test-dir", str(build_dir), "--output-on-failure",
            "--output-junit", str(output), "--no-tests=error",
        ],
        cwd=repo,
        timeout=timeout,
    )
    return result.returncode, parse_junit(output), result.stdout


def skip_allowed(name: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatch(name, pattern) for pattern in patterns)


def run_gate(repo: Path, build_dir: Path, gate: str, timeout: int) -> dict[str, Any]:
    started = time.monotonic()
    if gate == "build_fast":
        command = ["cmake", "--build", str(build_dir), "--target", "chronon3d_tests"]
    elif gate == "unit_tests":
        # The actual result is populated by the canonical CTest phase below;
        # this phase is marked executed by the orchestrator, never assumed.
        return {"executed": True, "status": "delegated", "exit_code": 0, "duration_s": 0.0}
    else:
        path = repo / "tools" / gate
        if not path.is_file():
            return {"executed": False, "status": "missing", "exit_code": None, "path": str(path)}
        if path.suffix == ".py":
            command = ["python3", str(path)]
        else:
            command = ["bash", str(repo / "tools" / "execute_gate.sh"), gate, "origin", "main"]
    result = run_command(command, cwd=repo, timeout=timeout)
    output = result.stdout
    return {
        "executed": True,
        "status": "passed" if result.returncode == 0 else "failed",
        "exit_code": result.returncode,
        "duration_s": round(time.monotonic() - started, 3),
        "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
        "output_tail": output[-2000:],
    }


def certify(args: argparse.Namespace) -> tuple[dict[str, Any], int]:
    repo = resolve_repo_root(args.repo_root)
    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        raise CertificationError(f"build directory does not exist: {build_dir}")

    started_at = utc_now()
    manifest: dict[str, Any] = {
        "schema": MANIFEST_SCHEMA,
        "certifier": SCRIPT_NAME,
        "started_at": started_at,
        "profile": args.profile,
        "build_profile": args.build_profile,
        "target_sha": args.target_sha,
        "git_sha": None,
        "registered_tests": 0,
        "executed_tests": 0,
        "passed": 0,
        "failed": 0,
        "skipped": 0,
        "allowed_skips": args.allow_skip,
        "tests": [],
        "gates": {},
        "artifacts": {},
        "failures": [],
        "deterministic_second_run": False,
    }

    try:
        head, _ = git_sha(repo, args.target_sha)
    except CertificationFailure as exc:
        manifest["failures"].append(str(exc))
        manifest["finished_at"] = utc_now()
        return manifest, 1
    manifest["git_sha"] = head
    preflight_failures = check_preconditions(repo, head, not args.skip_doc_sha)
    if preflight_failures:
        manifest["failures"].extend(preflight_failures)
        manifest["finished_at"] = utc_now()
        return manifest, 1

    try:
        artifact_dir = Path(args.artifact_dir) if args.artifact_dir else Path(
            tempfile.mkdtemp(prefix=f"chronon3d-same-sha-{head[:12]}-")
        )
        artifact_dir = artifact_dir.resolve()
        artifact_dir.mkdir(parents=True, exist_ok=True)
        tests, discovery_path = discover_ctest(repo, build_dir, artifact_dir)
        manifest["registered_tests"] = len(tests)
        manifest["artifacts"]["ctest_discovery"] = {
            "path": str(discovery_path), "sha256": sha256_file(discovery_path)
        }
        # Execute the manifest-defined gates before artifact checks. In CI/WBH
        # profiles this lets build_fast refresh binaries before the anti-stale
        # invariant is evaluated; a stale binary must never be certified merely
        # because the build phase appears later in the report.
        gates = load_gate_manifest(repo, args.profile)
        for gate in gates:
            # Run each concrete gate twice. We compare the observable verdict
            # (status + exit code), not log bytes: timings and temporary paths
            # are expected to differ while a same-SHA gate verdict must not.
            runs: list[dict[str, Any]] = []
            if gate == "unit_tests":
                runs.append(run_gate(repo, build_dir, gate, args.timeout))
            else:
                for _ in range(args.repeat):
                    runs.append(run_gate(repo, build_dir, gate, args.timeout))
            verdicts = [
                (run.get("status"), run.get("exit_code"), run.get("executed"))
                for run in runs
            ]
            deterministic = all(verdict == verdicts[0] for verdict in verdicts[1:])
            record: dict[str, Any] = {
                "runs": runs,
                "deterministic": deterministic,
                "executed": all(run.get("executed", False) for run in runs),
                "status": "passed" if deterministic and all(run.get("status") in {"passed", "delegated"} for run in runs) else "failed",
                "exit_code": 0 if deterministic and all(run.get("exit_code") == 0 for run in runs) else 1,
            }
            manifest["gates"][gate] = record
            if not record["executed"]:
                manifest["failures"].append(f"gate was not executed: {gate}")
            if not record["deterministic"]:
                manifest["failures"].append(f"gate verdict changed between runs: {gate}")
            if record["status"] == "failed":
                manifest["failures"].append(
                    f"gate failed or was non-deterministic: {gate}"
                )

        artifact_records, artifact_failures = check_test_artifacts(repo, build_dir, tests)
        manifest["artifacts"]["test_commands"] = artifact_records
        manifest["failures"].extend(artifact_failures)

        # The CI phase entry is tied to the same CTest runs, so it cannot claim
        # success merely because a gate name exists in the manifest.
        run_summaries: list[dict[str, Any]] = []
        for index in range(args.repeat):
            junit = artifact_dir / f"ctest-run-{index + 1}.xml"
            rc, statuses, output = run_ctest_once(repo, build_dir, junit, args.timeout)
            run_summaries.append({"exit_code": rc, "statuses": statuses})
            manifest["artifacts"][f"ctest_run_{index + 1}"] = {
                "path": str(junit),
                "sha256": sha256_file(junit),
                "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
            }

        expected = {str(test["name"]) for test in tests}
        first = run_summaries[0]["statuses"]
        for name in expected - set(first):
            manifest["failures"].append(f"registered test was not executed: {name}")
        for name in set(first) - expected:
            manifest["failures"].append(f"unregistered test executed: {name}")
        if len(run_summaries) >= 2:
            signatures = [
                (summary["exit_code"], sorted(summary["statuses"].items()))
                for summary in run_summaries
            ]
            manifest["deterministic_second_run"] = all(sig == signatures[0] for sig in signatures[1:])
            if not manifest["deterministic_second_run"]:
                manifest["failures"].append("second CTest execution produced a different result signature")
        else:
            manifest["deterministic_second_run"] = True

        statuses = first
        manifest["executed_tests"] = len(statuses)
        manifest["passed"] = sum(value == "passed" for value in statuses.values())
        manifest["failed"] = sum(value == "failed" for value in statuses.values())
        manifest["skipped"] = sum(value == "skipped" for value in statuses.values())
        manifest["tests"] = [
            {"name": name, "status": statuses[name],
             "allowlisted_skip": statuses[name] == "skipped" and skip_allowed(name, args.allow_skip)}
            for name in sorted(statuses)
        ]
        if any(summary["exit_code"] != 0 for summary in run_summaries) or manifest["failed"]:
            manifest["failures"].append("CTest suite execution failed")
        unexpected_skips = [
            name for name, status in statuses.items()
            if status == "skipped" and not skip_allowed(name, args.allow_skip)
        ]
        manifest["failures"].extend(
            f"test skipped without allowlist: {name}" for name in sorted(unexpected_skips)
        )

        if "unit_tests" in manifest["gates"]:
            unit = manifest["gates"]["unit_tests"]
            unit["status"] = "passed" if manifest["executed_tests"] and not manifest["failed"] and manifest["deterministic_second_run"] else "failed"
            unit["exit_code"] = 0 if unit["status"] == "passed" else 1
            unit["runs"][0]["status"] = unit["status"]
            unit["runs"][0]["exit_code"] = unit["exit_code"]

    except CertificationFailure as exc:
        manifest["failures"].append(str(exc))
    except CertificationError:
        raise

    manifest["finished_at"] = utc_now()
    return manifest, 0 if not manifest["failures"] else 1


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", help="repository root (defaults to git root)")
    parser.add_argument("--build-dir", default="build/chronon/linux-fast-dev")
    parser.add_argument("--build-profile", default="linux-fast-dev")
    parser.add_argument("--profile", choices=["developer", "ci", "wbh", "all"], default="ci")
    parser.add_argument("--target-sha", help="commit to certify; defaults to HEAD")
    parser.add_argument("--manifest", help="output JSON path (default: /tmp/chronon3d-same-sha-<sha>.json)")
    parser.add_argument("--artifact-dir", help="directory retaining discovery/JUnit artifacts")
    parser.add_argument("--allow-skip", action="append", default=[], metavar="GLOB")
    parser.add_argument("--skip-doc-sha", action="store_true", help="do not require CURRENT_STATUS to name this SHA")
    parser.add_argument("--repeat", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=1800, help="per gate/CTest timeout in seconds")
    args = parser.parse_args(argv)
    if args.repeat < 1:
        parser.error("--repeat must be >= 1")
    if args.timeout < 1:
        parser.error("--timeout must be >= 1")
    return args


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        manifest, code = certify(args)
    except (CertificationError, OSError) as exc:
        manifest = {
            "schema": MANIFEST_SCHEMA,
            "certifier": SCRIPT_NAME,
            "started_at": utc_now(),
            "finished_at": utc_now(),
            "profile": args.profile,
            "build_profile": args.build_profile,
            "git_sha": None,
            "target_sha": args.target_sha,
            "registered_tests": 0,
            "executed_tests": 0,
            "passed": 0,
            "failed": 0,
            "skipped": 0,
            "allowed_skips": args.allow_skip,
            "tests": [],
            "gates": {},
            "artifacts": {},
            "failures": [f"CERTIFICATION_BLOCKED: {exc}"],
            "deterministic_second_run": False,
        }
        code = 2

    output = Path(args.manifest) if args.manifest else Path(
        f"/tmp/chronon3d-same-sha-{(manifest['git_sha'] or 'blocked')[:12]}.json"
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Certification manifest: {output}")
    print(f"SHA: {manifest['git_sha']}")
    print(
        "Tests: registered={registered_tests} executed={executed_tests} "
        "passed={passed} failed={failed} skipped={skipped}".format(**manifest)
    )
    print(f"Gates: {len(manifest['gates'])} declared")
    if manifest["failures"]:
        print(f"SAME_SHA_CERTIFICATION_FAIL: {len(manifest['failures'])} invariant(s)")
        for failure in manifest["failures"]:
            print(f"  - {failure}")
    else:
        print("SAME_SHA_CERTIFICATION_PASS")
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
