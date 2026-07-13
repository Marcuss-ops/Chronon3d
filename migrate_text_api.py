#!/usr/bin/env python3
"""Bulk migrate LayerBuilder text API call sites.

Patterns handled:
  l.text("name", TextSpec{...}) -> l.text("name", from_text_spec(TextSpec{...}))
  l.text_run(...) -> l.animated_text(...)
  TextSpec ts; ... l.text("name", ts); -> TextDefinition def = from_text_spec(ts); ... l.text("name", def);

Adds #include <chronon3d/text/text_definition.hpp> when needed.
"""

import re
import sys
from pathlib import Path

INCLUDE = '#include <chronon3d/text/text_definition.hpp>'


def find_matching_brace(text: str, start: int) -> int:
    """Return index of the brace matching the opening brace at start."""
    depth = 0
    in_string = False
    in_char = False
    i = start
    while i < len(text):
        c = text[i]
        if in_string:
            if c == '\\' and i + 1 < len(text):
                i += 2
                continue
            if c == '"':
                in_string = False
        elif in_char:
            if c == '\\' and i + 1 < len(text):
                i += 2
                continue
            if c == "'":
                in_char = False
        else:
            if c == '"':
                in_string = True
            elif c == "'":
                in_char = True
            elif c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return i
        i += 1
    return -1


def wrap_textspec_in_text_calls(content: str) -> str:
    """Wrap TextSpec{...} after l.text("...", with from_text_spec(...)."""
    pattern = re.compile(r'(l\.text\s*\(\s*"[^"]+"\s*,\s*)TextSpec\s*\{')
    result = []
    last = 0
    for m in pattern.finditer(content):
        start = m.end() - 1  # position of opening brace of TextSpec{...}
        end = find_matching_brace(content, start)
        if end == -1:
            continue
        # Replace the TextSpec{...} part with from_text_spec(TextSpec{...})
        result.append(content[last:m.end()])
        result.append('from_text_spec(')
        result.append(content[start:end + 1])
        result.append(')')
        last = end + 1
    result.append(content[last:])
    return ''.join(result)


def rename_text_run(content: str) -> str:
    """Rename text_run( to animated_text( in LayerBuilder calls."""
    return re.sub(r'\.text_run\s*\(', '.animated_text(', content)


def add_include_if_needed(path: Path, content: str) -> str:
    """Add text_definition.hpp include if file uses from_text_spec or TextDefinition."""
    if INCLUDE in content:
        return content
    if 'from_text_spec' not in content and 'TextDefinition' not in content:
        return content
    # Find a good place to add the include
    lines = content.splitlines(keepends=True)
    insert_idx = 0
    for i, line in enumerate(lines):
        if line.startswith('#include'):
            insert_idx = i + 1
    lines.insert(insert_idx, INCLUDE + '\n')
    return ''.join(lines)


def migrate_file(path: Path) -> None:
    content = path.read_text(encoding='utf-8')
    original = content
    content = rename_text_run(content)
    content = wrap_textspec_in_text_calls(content)
    if content != original:
        content = add_include_if_needed(path, content)
        path.write_text(content, encoding='utf-8')
        print(f'migrated {path}')
    else:
        print(f'skipped {path}')


def main():
    roots = [Path(p) for p in sys.argv[1:]]
    for root in roots:
        if not root.exists():
            continue
        if root.is_file():
            migrate_file(root)
        else:
            for path in root.rglob('*.cpp'):
                migrate_file(path)
            for path in root.rglob('*.hpp'):
                migrate_file(path)


if __name__ == '__main__':
    main()
