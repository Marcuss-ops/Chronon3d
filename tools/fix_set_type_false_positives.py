#!/usr/bin/env python3
"""Fix false positives: revert .set_type( to .type = on non-Shape types,
then re-apply .shape.type = → .shape.set_type( only."""

import os, re

DIRS = ['src', 'include', 'tests', 'apps', 'content']
EXT = ('.cpp', '.hpp', '.h')

def fix_file(fpath):
    with open(fpath) as f:
        content = f.read()
    
    old = content
    
    # Step 1: Revert ALL .set_type( → .type = 
    # This catches Fill, GradientDefinition, GradientFill, MotionObject, etc.
    # But we need to handle the closing paren: .set_type(X); → .type = X;
    content = re.sub(r'\.set_type\(([^;]+)\);', r'.type = \1;', content)
    # Handle multiline: .set_type(X\n); → .type = X\n;
    content = re.sub(r'\.set_type\(([^;]+)\n\s*\);', r'.type = \1\n;', content)
    
    # Step 2: Re-apply only on Shape objects: .shape.type = → .shape.set_type(
    # But close the paren at the ; — .shape.set_type(X);
    # Actually this is complex. Let me just mark Shape-specific ones manually.
    # Instead, change .shape.type = X; → node.shape.set_type(X);
    # The pattern: something.shape.type = Type::Val;
    content = re.sub(
        r'(\.shape)\.type\s*=\s*([^;]+);',
        r'\1.set_type(\2);',
        content
    )
    
    if content != old:
        with open(fpath, 'w') as f:
            f.write(content)
        return True
    return False

def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    fixed = 0
    for d in DIRS:
        dirpath = os.path.join(project_root, d)
        if not os.path.isdir(dirpath):
            continue
        for root, dirs, files in os.walk(dirpath):
            for fname in files:
                if fname.endswith(EXT):
                    fpath = os.path.join(root, fname)
                    if fix_file(fpath):
                        fixed += 1
                        print(f"  {fpath}")
    print(f"\nFixed {fixed} files.")

if __name__ == '__main__':
    main()
