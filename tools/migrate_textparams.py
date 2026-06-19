#!/usr/bin/env python3
"""Migrate TextParams{...} designated initializers to TextSpec{...}.

Produces NESTED designated initializers grouped by sub-struct —
the only valid C++ form for TextSpec:
    TextSpec{
        .content    = {.value = "..."},
        .font       = {.font_size = 72, .font_path = "..."},
        .layout     = {.box = {900, 160}},
        .appearance = {.color = Color{...}},
        .position   = {0.0f, 0.0f, 0.0f},
    }

Single-line inputs are kept on a single line; multi-line inputs keep
block-style formatting.

Usage:
    python3 migrate_textparams.py FILE [FILE ...]
    python3 migrate_textparams.py DIR
"""
import sys
import os
import re

# Field → (section, name). None means position (top-level field).
SECTION_MAP = {
    'text':             ('content', 'value'),
    'pre_shaped':       ('content', 'pre_shaped'),
    'font_path':        ('font', 'font_path'),
    'font_family':      ('font', 'font_family'),
    'font_weight':      ('font', 'font_weight'),
    'font_style':       ('font', 'font_style'),
    'font_size':        ('font', 'font_size'),
    'size':             ('layout', 'box'),
    'anchor':           ('layout', 'anchor'),
    'centering_mode':   ('layout', 'centering_mode'),
    'align':            ('layout', 'align'),
    'vertical_align':   ('layout', 'vertical_align'),
    'line_height':      ('layout', 'line_height'),
    'tracking':         ('layout', 'tracking'),
    'auto_fit':         ('layout', 'auto_fit'),
    'max_lines':        ('layout', 'max_lines'),
    'ellipsis':         ('layout', 'ellipsis'),
    'min_font_size':    ('layout', 'min_font_size'),
    'max_font_size':    ('layout', 'max_font_size'),
    'overflow':         ('layout', 'overflow'),
    'wrap':             ('layout', 'wrap'),
    'color':            ('appearance', 'color'),
    'box_style':        ('appearance', 'box_style'),
    'paint':            ('appearance', 'paint'),
    'shadows':          ('appearance', 'shadows'),
    'material':         ('appearance', 'material'),
    'pos':              None,  # → .position (top-level)
}

# Render order matches TextSpec's declaration order
SECTION_ORDER = ['content', 'font', 'layout', 'appearance', 'position']
SECTION_LABEL = {
    'content':    '.content',
    'font':       '.font',
    'layout':     '.layout',
    'appearance': '.appearance',
    'position':   '.position',
}


def skip_string_or_comment(s, i):
    """Skip a string literal or comment starting at index i. Returns new index.

    Falls through (returns i) if not a string/comment.
    Handles: "..." (with escapes and raw R"delim(...)delim"),
              '...' (char literal with escapes),
              // line comments, /* */ block comments.
    """
    n = len(s)
    if i >= n:
        return i
    c = s[i]
    if c == '"':
        i += 1
        while i < n:
            ch = s[i]
            if ch == '\\':
                i += 2
                continue
            if ch == '"':
                return i + 1
            i += 1
        return i
    if c == "'":
        i += 1
        while i < n:
            ch = s[i]
            if ch == '\\':
                i += 2
                continue
            if ch == "'":
                return i + 1
            i += 1
        return i
    # Raw string literal: R"delim(...)delim"
    if s[i:i+2] == 'R"':
        m = re.match(r'R"([^()\\]*)\(', s[i:])
        if m:
            delim = m.group(1)
            end_marker = ')' + delim + '"'
            end = s.find(end_marker, i + m.end())
            return end + len(end_marker) if end >= 0 else n
        return i  # not actually a raw string — fall through
    if s[i:i+2] == '//':
        end = s.find('\n', i)
        return end if end >= 0 else n
    if s[i:i+2] == '/*':
        end = s.find('*/', i)
        return end + 2 if end >= 0 else n
    return i


def parse_flat_fields(inner):
    """Parse '.fieldname = value' pairs from a TextParams block inner.

    Returns list of (field_name, value_str) preserving input order.
    Handles nested braces, parens, brackets inside values; strings and
    comments are skipped during parsing.
    """
    out = []
    i = 0
    n = len(inner)
    while i < n:
        # Skip whitespace and comments
        advanced = True
        while advanced:
            advanced = False
            while i < n and inner[i] in ' \t\n\r':
                i += 1
            if i >= n:
                return out
            skip = skip_string_or_comment(inner, i)
            if skip != i:
                # The skipped region may include a newline (line comment);
                # don't treat as part of a value.
                # But skip_string_or_comment on a line comment returns end-of-line
                # (or end-of-string). Fine — we just continue at the new index.
                i = skip
                advanced = True
        # Expect '.' for a designated initializer field
        if inner[i] != '.':
            # Defensive: advance past this char to avoid infinite loops
            i += 1
            continue
        # Read identifier after '.'
        j = i + 1
        while j < n and (inner[j].isalnum() or inner[j] == '_'):
            j += 1
        field = inner[i+1:j]
        i = j
        # Skip whitespace / comments
        while i < n:
            while i < n and inner[i] in ' \t\n\r':
                i += 1
            skip = skip_string_or_comment(inner, i)
            if skip != i:
                i = skip
                continue
            break
        # Expect '='
        if i >= n or inner[i] != '=':
            continue
        i += 1
        # Skip whitespace after '='.  Strings, comments, and raw strings
        # are intentionally NOT skipped here — they may legitimately be
        # the value (e.g. ".text = \"foo\"") or appear inline within it.
        # The value-reading loop below handles strings/comments via
        # skip_string_or_comment, consuming each as a value atom.
        while i < n and inner[i] in ' \t\n\r':
            i += 1
        # Read value until top-level ',' or end
        value_start = i
        depth = 0
        while i < n:
            skip = skip_string_or_comment(inner, i)
            if skip != i:
                i = skip
                continue
            ch = inner[i]
            if ch in '({[':
                depth += 1
            elif ch in ')}]':
                if depth > 0:
                    depth -= 1
            elif ch == ',' and depth == 0:
                break
            i += 1
        value = inner[value_start:i].rstrip()
        out.append((field, value))
        if i < n and inner[i] == ',':
            i += 1
    return out


def render_textspec(flat_fields, single_line):
    """Build TextSpec{...} from a list of (field_name, value_str)."""
    buckets = {sec: [] for sec in SECTION_ORDER[:-1]}  # content/font/layout/appearance
    position_value = None

    for field, value in flat_fields:
        if field not in SECTION_MAP:
            continue  # unknown field — skip
        entry = SECTION_MAP[field]
        if entry is None:
            position_value = value
            continue
        section, name = entry
        buckets[section].append((name, value))

    if not any(buckets.values()) and position_value is None:
        return 'TextSpec{}'

    if single_line:
        parts = []
        for sec in SECTION_ORDER:
            if sec == 'position':
                if position_value is not None:
                    parts.append(f'{SECTION_LABEL[sec]} = {position_value}')
                continue
            items = buckets[sec]
            if not items:
                continue
            inner = ', '.join(f'.{k} = {v}' for k, v in items)
            parts.append(f'{SECTION_LABEL[sec]} = {{{inner}}}')
        if not parts:
            return 'TextSpec{}'
        return 'TextSpec{' + ', '.join(parts) + '}'

    # Multi-line output
    lines = ['TextSpec{']
    last_idx = -1
    for sec in SECTION_ORDER:
        if sec == 'position':
            if position_value is not None:
                last_idx = len(lines)
                lines.append(f'    {SECTION_LABEL[sec]} = {position_value},')
            continue
        items = buckets[sec]
        if not items:
            continue
        inner = ', '.join(f'.{k} = {v}' for k, v in items)
        last_idx = len(lines)
        lines.append(f'    {SECTION_LABEL[sec]} = {{{inner}}},')
    # Strip trailing comma from last populated section
    if last_idx >= 0 and lines[last_idx].endswith(','):
        lines[last_idx] = lines[last_idx].rstrip(',')
    lines.append('}')
    return '\n'.join(lines)


def migrate(content):
    """Find TextParams{...} blocks and rewrite them as nested TextSpec."""
    n = len(content)
    out = []
    i = 0
    while i < n:
        # Skip strings / comments
        advanced = True
        while advanced:
            advanced = False
            while i < n and content[i] in ' \t\n\r':
                i += 1
            if i >= n:
                break
            skip = skip_string_or_comment(content, i)
            if skip != i:
                if skip > i:
                    out.append(content[i:skip])
                    i = skip
                advanced = True
        if i >= n:
            break
        # Look for "TextParams{"
        if content[i:i+11] == 'TextParams{':
            # Brace-match to find matching closing brace
            depth = 1
            j = i + 11
            while j < n and depth > 0:
                skip = skip_string_or_comment(content, j)
                if skip != j:
                    j = skip
                    continue
                ch = content[j]
                if ch == '{':
                    depth += 1
                elif ch == '}':
                    depth -= 1
                j += 1
            if depth == 0:
                # Block spans [i+11, j-1) — the inner text between the braces.
                inner = content[i+11:j-1]
                single_line = '\n' not in inner
                flat_fields = parse_flat_fields(inner)
                rendered = render_textspec(flat_fields, single_line)
                out.append(rendered)
                i = j
                continue
        out.append(content[i])
        i += 1
    return ''.join(out)


def migrate_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        orig = f.read()
    new = migrate(orig)
    if new == orig:
        return False
    with open(path, 'w', encoding='utf-8') as f:
        f.write(new)
    return True


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    files = []
    for a in args:
        if os.path.isdir(a):
            for root, _, fs in os.walk(a):
                if any(x in root for x in ('/build', '/vcpkg_installed', '/.git/', '/tools')):
                    continue
                for f in fs:
                    if f.endswith(('.cpp', '.hpp', '.h', '.cc')):
                        files.append(os.path.join(root, f))
        else:
            files.append(a)
    changed = 0
    for path in sorted(set(files)):
        if migrate_file(path):
            changed += 1
    print(f"{changed} file(s) changed (of {len(files)})", file=sys.stderr)


if __name__ == '__main__':
    main()
