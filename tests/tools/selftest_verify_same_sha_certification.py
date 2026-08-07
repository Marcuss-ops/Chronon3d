#!/usr/bin/env python3
"""Selftest the same-SHA certifier without building Chronon3D.

The harness creates a temporary git repository, fake ctest executable, fake
build artifact, and a minimal gate manifest. It verifies the happy path plus
wrong SHA, stale binary, unallowlisted skip, and non-deterministic second run.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CERT = ROOT / "tools" / "verify_same_sha_certification.py"


def run(command: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def setup_repo(work: Path) -> tuple[Path, dict[str, str]]:
    repo = work / "repo"
    fakebin = work / "bin"
    build = repo / "build"
    (repo / "tools" / "gates").mkdir(parents=True)
    (repo / "tests").mkdir(parents=True)
    (repo / ".gitignore").write_text("build/\n", encoding="utf-8")
    build.mkdir(parents=True)
    fakebin.mkdir(parents=True)

    (repo / "docs").mkdir()
    (repo / "docs" / "CURRENT_STATUS.md").write_text(
        "> Ultima revisione semantica: 2026-08-07\n"
        "> Ultima baseline certificata: `main@PLACEHOLDER`\n", encoding="utf-8"
    )
    (repo / "tools" / "gates" / "manifest.sh").write_text(
        "DEVELOPER_GATES=(fake_gate.sh)\nCI_PHASES=(build_fast unit_tests)\nWBH_ONLY_GATES=()\n",
        encoding="utf-8",
    )
    (repo / "tools" / "fake_gate.sh").write_text(
        "#!/usr/bin/env bash\nset -euo pipefail\necho GATE_PASS: fake_gate\n", encoding="utf-8"
    )
    (repo / "tools" / "fake_gate.sh").chmod(0o755)
    (repo / "tools" / "execute_gate.sh").write_text(
        "#!/usr/bin/env bash\nset -euo pipefail\nexec bash \"$(dirname \"$0\")/$1\"\n", encoding="utf-8"
    )
    (repo / "tools" / "execute_gate.sh").chmod(0o755)

    source = repo / "tests" / "fake_suite.cpp"
    source.write_text("// source\n", encoding="utf-8")
    binary = build / "fake_suite"
    binary.write_text("#!/usr/bin/env bash\nexit 0\n", encoding="utf-8")
    binary.chmod(0o755)

    ctest = fakebin / "ctest"
    ctest.write_text(
        """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
args = sys.argv[1:]
if '--show-only=json-v1' in args:
    print(json.dumps({'kind':'ctest','version':{'major':1,'minor':0},'tests':[{'name':'fake_suite','properties':[]},{'name':'fake_python','properties':[]}] }))
    raise SystemExit(0)
xml = next(Path(a).resolve() for a in args if a.endswith('.xml'))
state = Path(os.environ['FAKE_CTEST_STATE'])
n = int(state.read_text()) + 1 if state.exists() else 1
state.write_text(str(n))
status = 'passed'
if os.environ.get('FAKE_CTEST_SKIP') == '1':
    status = 'skipped'
if os.environ.get('FAKE_CTEST_NONDET') == '1' and n > 1:
    status = 'failed'
marker = '<failure />' if status == 'failed' else '<skipped />' if status == 'skipped' else ''
xml.write_text('<testsuite tests="2" failures="%s" skipped="%s"><testcase classname="fake" name="fake_suite">%s</testcase><testcase classname="fake" name="fake_python">%s</testcase></testsuite>' % (
    '1' if status == 'failed' else '0', '1' if status == 'skipped' else '0', marker, marker))
raise SystemExit(1 if status == 'failed' else 0)
""", encoding="utf-8"
    )
    ctest.chmod(0o755)
    (repo / "tests" / "fake_script.py").write_text("print('ok')\n", encoding="utf-8")
    (build / "CTestTestfile.cmake").write_text(
        f'add_test([=[fake_suite]=] "{binary}")\n'
        f'add_test(NAME fake_python COMMAND python3 tests/fake_script.py)\n', encoding="utf-8"
    )

    subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.email", "cert@example.invalid"], cwd=repo, check=True)
    subprocess.run(["git", "config", "user.name", "certifier"], cwd=repo, check=True)
    subprocess.run(["git", "add", "."], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "test(cert): fixture"], cwd=repo, check=True)
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    current = repo / "docs" / "CURRENT_STATUS.md"
    current.write_text(current.read_text(encoding="utf-8").replace("PLACEHOLDER", sha[:8]), encoding="utf-8")
    subprocess.run(["git", "add", str(current)], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "docs(cert): fixture sha"], cwd=repo, check=True)
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
    current.write_text(
        re.sub(r"main@[0-9a-f]+", f"main@{sha[:8]}", current.read_text(encoding="utf-8")),
        encoding="utf-8",
    )
    subprocess.run(["git", "add", str(current)], cwd=repo, check=True)
    subprocess.run(["git", "commit", "-qm", "docs(cert): matching head fixture"], cwd=repo, check=True)
    env = os.environ.copy()
    env["PATH"] = f"{fakebin}:{env['PATH']}"
    env["FAKE_CTEST_STATE"] = str(work / "ctest.state")
    return repo, env


def execute(repo: Path, env: dict[str, str], *extra: str) -> subprocess.CompletedProcess[str]:
    return run([sys.executable, str(CERT), "--repo-root", str(repo), "--build-dir", str(repo / "build"),
                "--profile", "developer", "--manifest", str(repo.parent / "result.json"), *extra], repo, env)


def require(condition: bool, message: str, output: str = "") -> None:
    if not condition:
        raise AssertionError(message + (f"\n{output}" if output else ""))


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="same_sha_cert_selftest_") as raw:
        work = Path(raw)
        repo, env = setup_repo(work)
        result = execute(repo, env)
        require(result.returncode == 0, "happy path did not pass", result.stdout)
        manifest = json.loads((repo.parent / "result.json").read_text(encoding="utf-8"))
        require(manifest["registered_tests"] == 2, "NAME COMMAND test was not discovered", result.stdout)

        current = repo / "docs" / "CURRENT_STATUS.md"
        original_docs = current.read_text(encoding="utf-8")
        bad_docs = re.sub(r"main@[0-9a-f]+", "main@deadbeef", original_docs)
        current.write_text(bad_docs, encoding="utf-8")
        subprocess.run(["git", "add", str(current)], cwd=repo, check=True)
        subprocess.run(["git", "commit", "-qm", "test(cert): wrong docs sha fixture"], cwd=repo, check=True)
        wrong_docs = execute(repo, env)
        require(wrong_docs.returncode == 1 and "CURRENT_STATUS.md declares" in wrong_docs.stdout,
                "wrong docs SHA was not rejected", wrong_docs.stdout)

        missing_script = repo / "tests" / "fake_script.py"
        missing_script.unlink()
        subprocess.run(["git", "add", "-u", str(missing_script)], cwd=repo, check=True)
        subprocess.run(["git", "commit", "-qm", "test(cert): missing script fixture"], cwd=repo, check=True)
        missing = execute(repo, env, "--skip-doc-sha")
        require(missing.returncode == 1 and "missing test script" in missing.stdout,
                "missing Python script was not rejected", missing.stdout)
        missing_script.write_text("print('ok')\n", encoding="utf-8")
        subprocess.run(["git", "add", str(missing_script)], cwd=repo, check=True)
        subprocess.run(["git", "commit", "-qm", "test(cert): restore script fixture"], cwd=repo, check=True)

        wrong = execute(repo, env, "--skip-doc-sha", "--target-sha", "HEAD~1")
        require(wrong.returncode == 1 and "SHA mismatch" in wrong.stdout, "wrong SHA was not rejected", wrong.stdout)

        binary = repo / "build" / "fake_suite"
        source = repo / "tests" / "fake_suite.cpp"
        old_time = source.stat().st_mtime_ns - 10_000_000_000
        os.utime(binary, ns=(old_time, old_time))
        stale = execute(repo, env, "--skip-doc-sha")
        require(stale.returncode == 1 and "stale test binary" in stale.stdout, "stale binary was not rejected", stale.stdout)
        now = time.time_ns()
        os.utime(binary, ns=(now, now))

        env_skip = dict(env, FAKE_CTEST_SKIP="1")
        skipped = execute(repo, env_skip, "--skip-doc-sha")
        require(skipped.returncode == 1 and "skipped without allowlist" in skipped.stdout,
                "unallowlisted skip was not rejected", skipped.stdout)

        state = Path(env["FAKE_CTEST_STATE"])
        state.write_text("0")
        env_nondet = dict(env, FAKE_CTEST_NONDET="1")
        nondet = execute(repo, env_nondet, "--skip-doc-sha")
        require(nondet.returncode == 1 and "second CTest execution" in nondet.stdout,
                "non-deterministic second run was not rejected", nondet.stdout)

    print("SAME_SHA_SELFTEST_PASS: happy path + SHA + stale + skip + repeat invariants")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
