#!/usr/bin/env python3
"""Convert TextSpec{...} initializers to TextDefinition{...} initializers.

This script performs a best-effort mechanical conversion of inline
TextSpec initializers into the canonical TextDefinition form.  Complex
or ambiguous cases are left for manual review.

Field mapping (TextSpec -> TextDefinition):
  .content    -> .content
  .placement  -> .frame.placement
  .font       -> .style.font
  .layout     -> .frame (most fields) + .paragraph
  .appearance -> .style (color/paint/shadows/material/box_style)

Layout sub-field mapping:
  .box            -> .frame.size
  .anchor         -> .frame.anchor
  .centering_mode -> .frame.centering_mode
  .align          -> .frame.align
  .vertical_align -> .frame.vertical_align
  .wrap           -> .frame.wrap
  .overflow       -> .frame.overflow
  .line_height    -> .frame.line_height
  .tracking       -> .frame.tracking
  .auto_fit       -> .frame.auto_fit
  .min_font_size  -> .frame.min_font_size
  .max_font_size  -> .frame.max_font_size
  .max_lines      -> .frame.max_lines
  .ellipsis       -> .frame.ellipsis
  .paragraph      -> .paragraph

Appearance sub-field mapping:
  .color     -> .style.color
  .paint     -> .style.paint
  .shadows   -> .style.shadows
  .material  -> .style.material
  .box_style -> .style.box_style
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


def split_initializer_fields(body: str):
    """Split a designated-initializer body like '.a = x, .b = y' into fields.

    Returns a list of (name, value) tuples.  value includes nested braces.
    """
    fields = []
    i = 0
    n = len(body)
    current_name = None
    current_value_start = None

    while i < n:
        # Skip whitespace
        while i < n and body[i].isspace():
            i += 1
        if i >= n:
            break

        # Expect .name =
        if body[i] != '.':
            # Malformed, skip
            i += 1
            continue

        name_start = i + 1
        name_end = name_start
        while name_end < n and (body[name_end].isalnum() or body[name_end] == '_'):
            name_end += 1
        name = body[name_start:name_end]

        i = name_end
        while i < n and body[i].isspace():
            i += 1

        if i >= n or body[i] != '=':
            # Malformed, skip
            i = name_end
            continue
        i += 1  # skip '='

        while i < n and body[i].isspace():
            i += 1

        value_start = i
        # Find the end of the value: either until top-level comma, or matching brace
        depth = 0
        in_string = False
        in_char = False
        value_end = i
        while value_end < n:
            c = body[value_end]
            if in_string:
                if c == '\\' and value_end + 1 < n:
                    value_end += 2
                    continue
                if c == '"':
                    in_string = False
            elif in_char:
                if c == '\\' and value_end + 1 < n:
                    value_end += 2
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
                elif c == ',' and depth == 0:
                    break
            value_end += 1

        value = body[value_start:value_end]
        fields.append((name, value.strip()))

        i = value_end
        if i < n and body[i] == ',':
            i += 1

    return fields


def map_layout_fields(layout_fields):
    """Map TextLayoutSpec fields to TextFrame + ParagraphStyle fields."""
    frame_items = []
    paragraph_value = None

    mapping = {
        'box': 'size',
        'anchor': 'anchor',
        'centering_mode': 'centering_mode',
        'align': 'align',
        'vertical_align': 'vertical_align',
        'wrap': 'wrap',
        'overflow': 'overflow',
        'line_height': 'line_height',
        'tracking': 'tracking',
        'auto_fit': 'auto_fit',
        'min_font_size': 'min_font_size',
        'max_font_size': 'max_font_size',
        'max_lines': 'max_lines',
        'ellipsis': 'ellipsis',
    }

    for name, value in layout_fields:
        if name == 'paragraph':
            paragraph_value = value
        elif name in mapping:
            frame_items.append((mapping[name], value))

    return frame_items, paragraph_value


def map_appearance_fields(appearance_fields):
    """Map TextAppearanceSpec fields to TextDefStyle fields."""
    style_items = []
    mapping = {
        'color': 'color',
        'paint': 'paint',
        'shadows': 'shadows',
        'material': 'material',
        'box_style': 'box_style',
    }
    for name, value in appearance_fields:
        if name in mapping:
            style_items.append((mapping[name], value))
    return style_items


def build_initializer(items, indent=4):
    """Build a C++ designated-initializer string from (name, value) items."""
    if not items:
        return "{}"
    prefix = " " * indent
    parts = []
    for name, value in items:
        if value.startswith('{') and value.endswith('}'):
            # Nested struct
            parts.append(f".{name} = {value}")
        else:
            parts.append(f".{name} = {value}")
    return "{\n" + ",\n".join(prefix + p for p in parts) + "\n" + " " * (indent - 4) + "}"


def convert_textspec(text: str) -> str:
    """Convert all TextSpec{...} to TextDefinition{...} in a string.

    Also strips surrounding from_text_spec(...) wrappers.
    """
    result = []
    last = 0
    pattern = re.compile(r'TextSpec\s*\{')

    for m in pattern.finditer(text):
        start = m.end() - 1  # position of opening brace
        end = find_matching_brace(text, start)
        if end == -1:
            continue

        body = text[start + 1:end]
        fields = split_initializer_fields(body)

        content_value = None
        placement_value = None
        font_value = None
        layout_fields = []
        appearance_fields = []

        for name, value in fields:
            if name == 'content':
                content_value = value
            elif name == 'placement':
                placement_value = value
            elif name == 'font':
                font_value = value
            elif name == 'layout':
                # Parse nested layout fields
                if value.startswith('{') and value.endswith('}'):
                    layout_fields = split_initializer_fields(value[1:-1])
                else:
                    layout_fields = []
            elif name == 'appearance':
                if value.startswith('{') and value.endswith('}'):
                    appearance_fields = split_initializer_fields(value[1:-1])
                else:
                    appearance_fields = []

        # Build TextDefinition fields
        def_items = []

        if content_value is not None:
            def_items.append(('content', content_value))

        # style = font + appearance
        style_items = []
        if font_value is not None:
            style_items.append(('font', font_value))
        style_items.extend(map_appearance_fields(appearance_fields))
        if style_items:
            def_items.append(('style', build_initializer(style_items, indent=8)))

        # frame = layout-mapped + placement
        frame_items, paragraph_value = map_layout_fields(layout_fields)
        if placement_value is not None:
            frame_items.insert(0, ('placement', placement_value))
        if frame_items:
            def_items.append(('frame', build_initializer(frame_items, indent=8)))

        # paragraph
        if paragraph_value is not None:
            def_items.append(('paragraph', paragraph_value))

        # spans and animation are left as defaults
        replacement = 'TextDefinition' + build_initializer(def_items, indent=4)

        # Check if this TextSpec{...} is wrapped in from_text_spec(...)
        # Look backwards for 'from_text_spec(' immediately before TextSpec
        wrapper_start = None
        prefix = text[:m.start()]
        # Strip trailing whitespace
        stripped_prefix = prefix.rstrip()
        if stripped_prefix.endswith('from_text_spec('):
            wrapper_start = stripped_prefix.rfind('from_text_spec(')

        if wrapper_start is not None:
            # Find matching paren for from_text_spec(
            paren_start = stripped_prefix.find('(', wrapper_start)
            # The TextSpec{...} starts at m.start(); find the closing paren
            # after the matching brace (end)
            paren_end = end + 1
            while paren_end < len(text) and text[paren_end].isspace():
                paren_end += 1
            if paren_end < len(text) and text[paren_end] == ')':
                result.append(text[last:wrapper_start])
                result.append(replacement)
                last = paren_end + 1
            else:
                result.append(text[last:m.start()])
                result.append(replacement)
                last = end + 1
        else:
            result.append(text[last:m.start()])
            result.append(replacement)
            last = end + 1

    result.append(text[last:])
    return ''.join(result)


def add_include_if_needed(content: str) -> str:
    """Add text_definition.hpp include if file uses TextDefinition."""
    if INCLUDE in content:
        return content
    if 'TextDefinition' not in content:
        return content
    lines = content.splitlines(keepends=True)
    insert_idx = 0
    for i, line in enumerate(lines):
        if line.startswith('#include'):
            insert_idx = i + 1
    lines.insert(insert_idx, INCLUDE + '\n')
    return ''.join(lines)


def rename_text_run(content: str) -> str:
    """Rename text_run( to animated_text( in LayerBuilder calls."""
    return re.sub(r'\.text_run\s*\(', '.animated_text(', content)


def convert_file(path: Path) -> None:
    content = path.read_text(encoding='utf-8')
    original = content
    content = rename_text_run(content)
    content = convert_textspec(content)
    if content != original:
        content = add_include_if_needed(content)
        path.write_text(content, encoding='utf-8')
        print(f'converted {path}')
    else:
        print(f'skipped {path}')


def main():
    roots = [Path(p) for p in sys.argv[1:]]
    for root in roots:
        if not root.exists():
            continue
        if root.is_file():
            convert_file(root)
        else:
            for path in root.rglob('*.cpp'):
                convert_file(path)
            for path in root.rglob('*.hpp'):
                convert_file(path)


if __name__ == '__main__':
    main()
