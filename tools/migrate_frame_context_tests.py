#!/usr/bin/env python3
"""
Migrate test files from the old FrameContext public-field API to the new
private-field API with make_frame_context() and helper methods.

Run from repo root:
    python3 tools/migrate_frame_context_tests.py tests/...
"""

import re
import sys
from pathlib import Path


def find_balanced_brace(text: str, start: int) -> int:
    """Return the index of the '}' matching the '{' at `start`, or -1."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return i
    return -1


def fix_aggregate_initialization(text: str) -> str:
    # Find all occurrences of "FrameContext <name> {" and replace the whole
    # aggregate initializer with make_frame_context(FrameContextParams{...}).
    pattern = re.compile(r'FrameContext\s+(\w+)\s*\{')
    matches = list(pattern.finditer(text))

    # Process from the end so earlier replacements do not invalidate offsets.
    for m in reversed(matches):
        var = m.group(1)
        open_idx = m.end() - 1  # index of '{'
        close_idx = find_balanced_brace(text, open_idx)
        if close_idx == -1:
            continue

        # Include the trailing semicolon if it follows the closing brace.
        close_end = close_idx + 1
        if close_end < len(text) and text[close_end] == ';':
            close_end += 1

        body = text[open_idx + 1:close_idx]

        # Parse key-value pairs. We accept simple scalar values and brace-
        # enclosed expressions such as Frame{0}, FrameRate{30, 1}, etc.
        pairs = {}
        for key, val in re.findall(r'\.(\w+)\s*=\s*([^,]+(?:\{[^}]*\})?)(?:,|$)', body):
            pairs[key] = val.strip()

        frame_rate = pairs.get('frame_rate', 'FrameRate{30, 1}')
        frame = pairs.get('frame', 'Frame{0}')
        width = pairs.get('width', '1920')
        height = pairs.get('height', '1080')
        duration = pairs.get('duration', 'Frame{0}')

        params = [f'.global_time = SampleTime::from_frame_int({frame}, {frame_rate})']
        if 'duration' in pairs:
            params.append(f'.duration = {duration}')
        params.append(f'.width = {width}')
        params.append(f'.height = {height}')

        for key, val in pairs.items():
            if key in ('frame', 'frame_rate', 'width', 'height', 'duration'):
                continue
            params.append(f'.{key} = {val}')

        params_str = ',\n    '.join(params)
        replacement = f'FrameContext {var} = make_frame_context(FrameContextParams{{\n    {params_str}\n}});'

        text = text[:m.start()] + replacement + text[close_end:]

    return text


def fix_member_accesses(text: str) -> str:
    # Only touch member accesses on known FrameContext variables to avoid
    # corrupting other structs that still expose a public `frame` member
    # (e.g. SampleTime, Keyframe, TemporalSampleKey, FrameInput).
    frame_ctx_vars = r'ctx|context|frame_context'
    # ctx.frame as rvalue -> ctx.frame()
    text = re.sub(rf'\b({frame_ctx_vars})\.frame\b(?!\s*=)', r'\1.frame()', text)
    # ctx.frame_rate as rvalue -> ctx.frame_rate()
    text = re.sub(rf'\b({frame_ctx_vars})\.frame_rate\b(?!\s*=)', r'\1.frame_rate()', text)
    # ctx.duration as rvalue -> ctx.duration()
    text = re.sub(rf'\b({frame_ctx_vars})\.duration\b(?!\s*=)', r'\1.duration()', text)
    # ctx.local_time as rvalue -> ctx.local_time() (but not function call)
    text = re.sub(rf'\b({frame_ctx_vars})\.local_time\b(?!\s*=)(?!\s*\()', r'\1.local_time()', text)
    # ctx.global_time as rvalue -> ctx.global_time()
    text = re.sub(rf'\b({frame_ctx_vars})\.global_time\b(?!\s*=)(?!\s*\()', r'\1.global_time()', text)
    return text


def fix_local_frame_assignment(text: str) -> str:
    # ctx.local_frame = expr; -> ctx = ctx.with_frame(expr); (legacy field)
    text = re.sub(r'\b(ctx|context|frame_context)\.local_frame\s*=\s*([^;]+);', r'\1 = \1.with_frame(\2);', text)
    return text


def fix_frame_assignment(text: str) -> str:
    # ctx.frame = expr; -> ctx = ctx.with_frame(expr);
    text = re.sub(r'\b(ctx|context|frame_context)\.frame\s*=\s*([^;]+);', r'\1 = \1.with_frame(\2);', text)
    return text


def fix_frame_rate_assignment(text: str) -> str:
    # ctx.frame_rate = expr; -> ctx = ctx.with_frame_rate(expr);
    text = re.sub(r'\b(ctx|context|frame_context)\.frame_rate\s*=\s*([^;]+);', r'\1 = \1.with_frame_rate(\2);', text)
    return text


def fix_duration_assignment(text: str) -> str:
    # ctx.duration = expr; -> ctx = ctx.with_duration(expr);
    text = re.sub(r'\b(ctx|context|frame_context)\.duration\s*=\s*([^;]+);', r'\1 = \1.with_duration(\2);', text)
    return text


def add_sample_time_include(text: str) -> str:
    # If we added SampleTime references, ensure the header is included.
    if 'SampleTime' in text and 'sample_time.hpp' not in text:
        text = re.sub(
            r'(#include <chronon3d/core/types/frame_context.hpp>)',
            r'\1\n#include <chronon3d/core/types/sample_time.hpp>',
            text,
        )
    return text


def migrate_file(path: Path) -> None:
    text = path.read_text()
    original = text
    text = fix_aggregate_initialization(text)
    text = fix_frame_assignment(text)
    text = fix_frame_rate_assignment(text)
    text = fix_duration_assignment(text)
    text = fix_member_accesses(text)
    text = add_sample_time_include(text)
    if text != original:
        path.write_text(text)
        print(f'updated {path}')


def main() -> None:
    for arg in sys.argv[1:]:
        p = Path(arg)
        if p.is_file():
            migrate_file(p)
        elif p.is_dir():
            for f in p.rglob('*.cpp'):
                migrate_file(f)
            for f in p.rglob('*.hpp'):
                migrate_file(f)


if __name__ == '__main__':
    main()
