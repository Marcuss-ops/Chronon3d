#!/usr/bin/env python3
"""Migrate old flat TextSpec API to new nested struct API — final version.

Key rules:
- Only processes l.text( calls (NOT text::centered_text — that takes CenterTextOptions)
- Processes both .cpp and .hpp files
- Sorts fields within sub-structs by struct declaration order
- Preserves original indentation
"""

import re
import sys
from pathlib import Path

# Declaration order for each sub-struct (to sort designated initializers correctly)
FONT_ORDER = ['font_path', 'font_family', 'font_weight', 'font_style', 'font_size']
LAYOUT_ORDER = ['box', 'anchor', 'centering_mode', 'align', 'vertical_align', 'wrap',
                'overflow', 'line_height', 'tracking', 'auto_fit', 'min_font_size',
                'max_font_size', 'max_lines', 'ellipsis']
APPEARANCE_ORDER = ['color', 'paint', 'shadows', 'material', 'box_style']

# Field name mappings
FONT_FIELDS = set(FONT_ORDER)
LAYOUT_FIELDS = set(LAYOUT_ORDER)
APPEARANCE_FIELDS = set(APPEARANCE_ORDER)


def parse_init_fields(body):
    """Parse designated initializer fields. Returns list of (name, value_str)."""
    fields = []
    i = 0
    body_stripped = body.strip()
    if body_stripped.startswith('{'):
        body_stripped = body_stripped[1:]
    if body_stripped.endswith('}'):
        body_stripped = body_stripped[:-1]
    
    i = 0
    while i < len(body_stripped):
        m = re.match(r'\s*\.(\w+)\s*=\s*', body_stripped[i:])
        if not m:
            i += 1
            continue
        name = m.group(1)
        value_start = i + m.end()
        
        depth = 0
        in_string = False
        j = value_start
        while j < len(body_stripped):
            c = body_stripped[j]
            if in_string:
                if c == '"' and (j == 0 or body_stripped[j-1] != '\\'):
                    in_string = False
                j += 1
            elif c == '"':
                in_string = True
                j += 1
            elif c in '({':
                depth += 1
                j += 1
            elif c in ')}':
                depth -= 1
                if depth < 0:
                    break
                j += 1
            elif c == ',' and depth == 0:
                break
            else:
                j += 1
        
        value = body_stripped[value_start:j].strip()
        if value.endswith(','):
            value = value[:-1].strip()
        
        fields.append((name, value))
        i = j
    
    return fields


def sort_fields(fields, order):
    """Sort fields by their position in the declaration order list.
    Fields not in the order list are placed at the end."""
    order_map = {k: i for i, k in enumerate(order)}
    def key(f):
        return order_map.get(f[0], 999)
    return sorted(fields, key=key)


def migrate_fields(fields):
    """Convert old flat fields to new nested struct fields."""
    content_val = None
    font_parts = []
    layout_parts = []
    appearance_parts = []
    position_val = None
    
    # First pass: collect all fields
    raw_font = []
    raw_layout = []
    raw_appearance = []
    
    for name, value in fields:
        if name == 'text':
            content_val = value
        elif name == 'pos':
            position_val = value
        elif name == 'size':
            raw_layout.append(('box', value))
        elif name in FONT_FIELDS:
            raw_font.append((name, value))
        elif name in LAYOUT_FIELDS:
            raw_layout.append((name, value))
        elif name in APPEARANCE_FIELDS:
            raw_appearance.append((name, value))
        elif name == 'content':
            sub = parse_init_fields('{' + value + '}')
            for sn, sv in sub:
                if sn == 'value':
                    content_val = sv
        elif name == 'font':
            sub = parse_init_fields('{' + value + '}')
            raw_font.extend(sub)
        elif name == 'layout':
            sub = parse_init_fields('{' + value + '}')
            raw_layout.extend(sub)
        elif name == 'appearance':
            sub = parse_init_fields('{' + value + '}')
            raw_appearance.extend(sub)
        elif name == 'position':
            position_val = value
    
    # Sort fields by declaration order
    font_parts = sort_fields(raw_font, FONT_ORDER)
    layout_parts = sort_fields(raw_layout, LAYOUT_ORDER)
    appearance_parts = sort_fields(raw_appearance, APPEARANCE_ORDER)
    
    result = []
    if content_val is not None:
        result.append(('content', '{.value = ' + content_val + '}'))
    if font_parts:
        inner = ', '.join(f'.{k} = {v}' for k, v in font_parts)
        result.append(('font', '{' + inner + '}'))
    if layout_parts:
        inner = ', '.join(f'.{k} = {v}' for k, v in layout_parts)
        result.append(('layout', '{' + inner + '}'))
    if appearance_parts:
        inner = ', '.join(f'.{k} = {v}' for k, v in appearance_parts)
        result.append(('appearance', '{' + inner + '}'))
    if position_val is not None:
        result.append(('position', position_val))
    
    return result


def format_nested(fields, field_indent, closing_indent):
    """Format nested fields with proper indentation."""
    lines = []
    for name, value in fields:
        lines.append(f'{field_indent}.{name} = {value}')
    body = ',\n'.join(lines)
    return '{\n' + body + '\n' + closing_indent + '}'


def find_text_blocks(content):
    """Find all l.text("...", { ... }) blocks. Skips text::centered_text."""
    blocks = []
    i = 0
    while i < len(content):
        m = re.search(r'l\.text\s*\(', content[i:])
        if not m:
            break
        
        func_start = i + m.start()
        arg_start = i + m.end()
        
        # Skip past string literal first arg
        pos = arg_start
        while pos < len(content) and content[pos] in ' \t\n':
            pos += 1
        
        if pos >= len(content) or content[pos] != '"':
            i = func_start + 1
            continue
        
        j = pos + 1
        while j < len(content) and content[j] != '"':
            if content[j] == '\\':
                j += 1
            j += 1
        j += 1
        
        # Find comma
        comma = content.find(',', j)
        if comma == -1:
            i = func_start + 1
            continue
        
        # Find opening brace
        brace_start = comma + 1
        while brace_start < len(content) and content[brace_start] in ' \t\n':
            brace_start += 1
        
        if brace_start >= len(content) or content[brace_start] != '{':
            i = func_start + 1
            continue
        
        # Check for TextSpec prefix
        before = content[comma+1:brace_start].strip()
        has_textspec = 'TextSpec' in before
        
        # Find matching closing brace
        depth = 1
        j = brace_start + 1
        while j < len(content) and depth > 0:
            if content[j] == '{':
                depth += 1
            elif content[j] == '}':
                depth -= 1
            j += 1
        
        if depth != 0:
            i = func_start + 1
            continue
        
        brace_end = j
        
        blocks.append({
            'brace_start': brace_start,
            'brace_end': brace_end,
            'has_textspec': has_textspec,
        })
        
        i = brace_end
    
    return blocks


def migrate_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    original = content
    blocks = find_text_blocks(content)
    
    if not blocks:
        return False
    
    needs_migration = False
    
    for block in reversed(blocks):
        body = content[block['brace_start']:block['brace_end']]
        
        # Compute indentation from original brace position
        line_start = content.rfind('\n', 0, block['brace_start']) + 1
        brace_indent = block['brace_start'] - line_start
        field_indent = ' ' * (brace_indent + 4)
        closing_indent = ' ' * brace_indent
        
        # Parse and check for old-style fields
        fields = parse_init_fields(body)
        old_style = {'text', 'pos', 'size', 'font_size', 'font_family', 
                     'font_weight', 'font_path', 'color', 'paint',
                     'align', 'anchor', 'vertical_align', 'line_height', 'tracking'}
        
        if not ({f[0] for f in fields} & old_style):
            continue
        
        # Migrate
        nested = migrate_fields(fields)
        new_body = format_nested(nested, field_indent, closing_indent)
        
        if not block['has_textspec']:
            new_text = 'TextSpec' + new_body
        else:
            new_text = new_body
        
        content = content[:block['brace_start']] + new_text + content[block['brace_end']:]
        needs_migration = True
    
    if needs_migration:
        bak_path = filepath + '.bak'
        with open(bak_path, 'w') as f:
            f.write(original)
        with open(filepath, 'w') as f:
            f.write(content)
        return True
    return False


def main():
    if len(sys.argv) < 2:
        content_dir = Path(__file__).parent.parent / 'content'
        files = sorted(list(content_dir.rglob('*.cpp')) + list(content_dir.rglob('*.hpp')))
    else:
        files = [Path(f) for f in sys.argv[1:]]
    
    migrated = []
    for f in files:
        if migrate_file(str(f)):
            migrated.append(str(f))
            print(f'  Migrated: {f}')
    
    if migrated:
        print(f'\nMigrated {len(migrated)} file(s). Backups saved as .bak files.')
    else:
        print('No files needed migration.')


if __name__ == '__main__':
    main()
