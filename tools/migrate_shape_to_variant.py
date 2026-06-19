#!/usr/bin/env python3
"""Replace Shape member field accesses with method calls for variant migration.

Before: shape.rect.size  → After: shape.rect().size
Before: shape.type == X  → After: shape.type() == X
Before: node.shape.circle.radius → After: node.shape.circle().radius
"""

import os
import re
import sys

SHAPE_FIELDS = [
    'rect', 'rounded_rect', 'circle', 'line', 'path',
    'text', 'image', 'grid_background', 'fake_box3d',
    'grid_plane', 'fake_extruded_text',
]

DIRS = ['src', 'include', 'tests', 'apps', 'content']
EXTENSIONS = ('.cpp', '.hpp', '.h')

def needs_migration(line: str) -> bool:
    """Check if this line accesses a Shape field as a member variable."""
    for field in SHAPE_FIELDS:
        # Match: .field. or .field followed by non-word char
        if f'.{field}.' in line:
            return True
        # Match: .field at end of line or followed by ; ) , }
        if re.search(rf'\.{field}\s*[;),}}\]]', line):
            return True
    # Match: .type where type is not a function call
    if re.search(r'\.type\s*[=!<>]', line):
        return True
    if re.search(r'\.type\s*\)', line):
        return True
    if re.search(r'\.type\s*$', line):
        return True
    return False

def migrate_line(line: str) -> str:
    """Replace field accesses with method calls."""
    result = line

    # Replace .field. → .field().
    for field in SHAPE_FIELDS:
        result = result.replace(f'.{field}.', f'.{field}().')

    # Replace .field at end of line/token (but not already a function call)
    for field in SHAPE_FIELDS:
        # .field followed by whitespace and then ; ) , } ] : =
        result = re.sub(
            rf'\.{field}(\s*[;),}}\]]:)',
            rf'.{field}()\1',
            result
        )

    # Replace .type where it's used as a value (not a method call)
    # .type == → .type() ==
    result = re.sub(r'\.type\s*==', '.type() ==', result)
    result = re.sub(r'\.type\s*!=', '.type() !=', result)
    # .type = → .set_type( or keep as is... 
    # Actually, .type = ShapeType::X should become .set_type(ShapeType::X)
    result = re.sub(r'\.type\s*=\s*', '.set_type(', result)
    # If we added set_type(, we need to close it if followed by ; 
    # This is complex — handle type assignment separately

    return result


def main():
    project_root = '/home/pierone/Pyt/Chronon3d'
    changed_files = 0
    changed_lines = 0

    for d in DIRS:
        dirpath = os.path.join(project_root, d)
        if not os.path.isdir(dirpath):
            continue
        for root, dirs, files in os.walk(dirpath):
            for fname in files:
                if not fname.endswith(EXTENSIONS):
                    continue
                fpath = os.path.join(root, fname)
                with open(fpath, 'r') as f:
                    lines = f.readlines()

                new_lines = []
                file_changed = False
                for line in lines:
                    if needs_migration(line):
                        new_line = migrate_line(line)
                        if new_line != line:
                            file_changed = True
                            changed_lines += 1
                        new_lines.append(new_line)
                    else:
                        new_lines.append(line)

                if file_changed:
                    changed_files += 1
                    print(f"  {fpath}")
                    with open(fpath, 'w') as f:
                        f.writelines(new_lines)

    print(f"\nMigrated {changed_lines} lines across {changed_files} files.")


if __name__ == '__main__':
    main()
