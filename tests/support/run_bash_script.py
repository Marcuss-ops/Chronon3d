#!/usr/bin/env python3
"""Run a repository Bash script with normalized line endings on Windows."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("check", "run"))
    parser.add_argument("script", type=Path)
    parser.add_argument("script_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    script_args = args.script_args
    if script_args and script_args[0] == "--":
        script_args = script_args[1:]

    source = args.script.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    command = ["bash", "-n"]
    environment = os.environ.copy()
    if args.mode == "run":
        if environment.get("CHRONON_STEADY_STATE_REPORT"):
            environment["CHRONON_STEADY_STATE_REPORT"] = _windows_to_wsl(
                environment["CHRONON_STEADY_STATE_REPORT"]
            )
        # Run the normalized script from a file in its original directory.
        # Bash -s leaves BASH_SOURCE[0] empty, which breaks scripts using
        # `set -u` and resolving paths relative to their own source file.
        with tempfile.NamedTemporaryFile(
            mode="wb", suffix=".sh", dir=args.script.resolve().parent,
            delete=False,
        ) as normalized_script:
            normalized_script.write(source)
            normalized_path = Path(normalized_script.name)
        cleanup_cli = None
        for index, value in enumerate(script_args[:-1]):
            if value != "--cli":
                continue
            candidate = Path(script_args[index + 1])
            try:
                is_script_cli = candidate.is_file() and candidate.read_bytes().startswith(b"#!")
            except OSError:
                is_script_cli = False
            if not is_script_cli:
                continue
            source_path = _windows_to_wsl(str(candidate.resolve()))
            copy_command = (
                'tmpdir="$(mktemp -d /tmp/chronon-script-cli.XXXXXX)" || exit; '
                f'cp -- {shlex.quote(source_path)} "$tmpdir/cli" && '
                'sed -i "s/\\r$//" "$tmpdir/cli" && '
                'chmod 700 "$tmpdir/cli" && printf "%s" "$tmpdir/cli"'
            )
            wsl = shutil.which("wsl.exe") or shutil.which("wsl")
            launcher = [wsl, "-e", "bash", "-lc"] if wsl else ["bash", "-lc"]
            copy = subprocess.run(
                [*launcher, copy_command],
                capture_output=True, text=True, check=False,
            )
            if copy.returncode != 0:
                raise RuntimeError(f"could not stage Bash test CLI: {copy.stderr}")
            cleanup_cli = copy.stdout.strip()
            script_args[index + 1] = cleanup_cli
            break
        command = [
            "bash", _windows_to_wsl(str(normalized_path)),
            *(_windows_to_wsl(arg) for arg in script_args),
        ]
        repository = Path.cwd()
        git_file = repository / ".git"
        if git_file.is_file():
            match = re.match(r"gitdir:\s*(.+)", git_file.read_text().strip(), re.IGNORECASE)
            if match:
                # The gitdir file points to <common>/.git/worktrees/<name>;
                # keep the configured source directory as the work tree.
                git_directory = _windows_to_wsl(match.group(1))
                work_tree = _windows_to_wsl(str(repository))
                source = (
                    f"export GIT_DIR={shlex.quote(git_directory)}\n"
                    f"export GIT_WORK_TREE={shlex.quote(work_tree)}\n"
                ).encode() + source
        ripgrep = shutil.which("rg.exe") or shutil.which("rg")
        if ripgrep:
            # WSL does not search for the implicit `.exe` suffix. Expose the
            # Windows ripgrep binary as a Bash function instead of relying on
            # its translated Windows PATH entry.
            executable = _windows_to_wsl(str(Path(ripgrep)))
            source = (
                (
                    "rg() { local args=() arg; for arg in \"$@\"; do "
                    "if [[ \"$arg\" =~ ^/mnt/([a-z])/(.*)$ ]]; then "
                    "args+=(\"${BASH_REMATCH[1]^^}:/${BASH_REMATCH[2]}\"); "
                    "else args+=(\"$arg\"); fi; done; "
                    f"{shlex.quote(executable)} \"${{args[@]}}\"; }}\n"
                ).encode()
                + source
            )
        # WSL does not consistently import arbitrary Windows environment
        # variables into a child Bash process. Re-export the test contract
        # variables in the normalized script so script behavior matches Linux.
        inherited = {
            key: value for key, value in environment.items()
            if key.startswith(("CHRONON3D_", "CHRONON_STEADY_STATE_"))
            or key == "GPU_CORPUS_TEST_SCENARIO"
        }
        exports = "".join(
            f"export {key}={shlex.quote(value)}\n"
            for key, value in sorted(inherited.items())
        ).encode()
        cleanup_path = None
        wsl = shutil.which("wsl.exe") or shutil.which("wsl")
        path_head = environment.get("PATH", "").split(os.pathsep, 1)[0]
        path_directory = Path(path_head)
        if wsl and path_directory.is_dir():
            try:
                contains_scripts = any(
                    child.is_file() and child.read_bytes().startswith(b"#!")
                    for child in path_directory.iterdir()
                )
            except OSError:
                contains_scripts = False
            if contains_scripts:
                wsl_source = _windows_to_wsl(str(path_directory.resolve()))
                stage_path_command = (
                    'tmpdir="$(mktemp -d /tmp/chronon-script-path.XXXXXX)" || exit; '
                    f'cp -R -- {shlex.quote(wsl_source)}/. "$tmpdir/" || exit; '
                    'for file in "$tmpdir"/*; do '
                    '[[ -f "$file" ]] || continue; '
                    'if [[ "$(head -c 2 "$file")" == "#!" ]]; then '
                    'sed -i "s/\\r$//" "$file" && chmod 700 "$file" || exit; '
                    'fi; done; printf "%s" "$tmpdir"'
                )
                staged_path = subprocess.run(
                    [wsl, "-e", "bash", "-lc", stage_path_command],
                    capture_output=True, text=True, check=False,
                )
                if staged_path.returncode != 0:
                    raise RuntimeError(
                        f"could not stage Bash test PATH: {staged_path.stderr}"
                    )
                cleanup_path = staged_path.stdout.strip()
                exports = (
                    f"export PATH={shlex.quote(cleanup_path)}:\"$PATH\"\n".encode()
                    + exports
                )
        source = exports + source
        normalized_path.write_bytes(source)
        try:
            completed = subprocess.run(
                command, cwd=Path.cwd(), env=environment, check=False
            )
            return completed.returncode
        finally:
            normalized_path.unlink(missing_ok=True)
            if cleanup_cli:
                cleanup_command = (
                    f"rm -f -- {shlex.quote(cleanup_cli)} && "
                    f"rmdir -- {shlex.quote(cleanup_cli.rsplit('/', 1)[0])}"
                )
                subprocess.run(
                    [*launcher, cleanup_command],
                    check=False,
                )
            if cleanup_path:
                cleanup_path_command = (
                    f"find {shlex.quote(cleanup_path)} -maxdepth 1 -type f -delete && "
                    f"rmdir -- {shlex.quote(cleanup_path)}"
                )
                subprocess.run(
                    [wsl, "-e", "bash", "-lc", cleanup_path_command],
                    check=False,
                )
    completed = subprocess.run(
        command, input=source, cwd=Path.cwd(), env=environment, check=False
    )
    return completed.returncode


def _windows_to_wsl(value: str) -> str:
    normalized = value.replace("\\", "/")
    match = re.match(r"^([A-Za-z]):/(.*)$", normalized)
    if not match:
        return normalized
    return f"/mnt/{match.group(1).lower()}/{match.group(2)}"


if __name__ == "__main__":
    raise SystemExit(main())
