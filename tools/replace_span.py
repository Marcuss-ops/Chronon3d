import os
import glob
import re

files = glob.glob('include/chronon3d/render_graph/nodes/*.hpp') + glob.glob('src/render_graph/nodes/*.cpp')

for f in files:
    with open(f, 'r') as file:
        content = file.read()
    
    new_content = content
    
    # Replace execute inputs and input_bboxes
    new_content = new_content.replace('const std::vector<std::shared_ptr<Framebuffer>>&', 'std::span<const std::shared_ptr<Framebuffer>>')
    new_content = new_content.replace('const std::vector<std::optional<raster::BBox>>&', 'std::span<const std::optional<raster::BBox>>')
    
    # Add #include <span> if not present
    if 'std::span' in new_content and '<span' not in new_content:
        # find the last include and append there
        lines = new_content.split('\n')
        last_include = -1
        for i, line in enumerate(lines):
            if line.startswith('#include'):
                last_include = i
        if last_include != -1:
            lines.insert(last_include + 1, '#include <span>')
            new_content = '\n'.join(lines)
        else:
            new_content = '#include <span>\n' + new_content

    if new_content != content:
        with open(f, 'w') as file:
            file.write(new_content)
        print(f"Updated {f}")

