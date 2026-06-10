#!/usr/bin/env python3
"""Bulk-rename ctx.<field> to ctx.<group>.<field> for RenderGraphContext.

Variables that hold a RenderGraphContext: ctx, tile_ctx, node_ctx,
mutable_ctx, bridge_ctx.  All other variable names (rctx, ectx, actx,
graph_ctx, ...) are left alone.

The 'frame' and 'camera' fields are renamed LAST because the result is the
'doubled' path (ctx.frame.frame, ctx.camera.camera).  All other renames must
finish first so we don't accidentally double-rename the new paths.
"""

import os
import re
import glob

# Variables that hold a RenderGraphContext.
VARS = ['ctx', 'tile_ctx', 'node_ctx', 'mutable_ctx', 'bridge_ctx', 'graph_ctx']

# Field renames: (old_field, new_path_after_var).
# Order matters: longer prefixes first to avoid partial-match collisions.
RENAMES = [
    ('dirty_rects_enabled',          'options.dirty_rects_enabled'),
    ('transform_scratch_slot',       'scratch.transform_scratch_slot'),
    ('transform_scratch',            'scratch.transform_scratch'),
    ('ping_write_fb',                'scratch.ping_write_fb'),
    ('ping_write_slot',              'scratch.ping_write_slot'),
    ('clip_rect',                    'tile.clip_rect'),
    ('tile_size',                    'tile.tile_size'),
    ('tile_execution_enabled',       'tile.tile_execution_enabled'),
    ('active_tile_clip',             'tile.active_tile_clip'),
    ('early_exit_skip',              'tile.early_exit_skip'),
    ('dirty_rect',                   'tile.dirty_rect'),
    ('framebuffer_pool',             'resources.framebuffer_pool'),
    ('node_cache',                   'resources.node_cache'),
    ('video_decoder',                'resources.video_decoder'),
    ('camera_2_5d',                  'camera.camera_2_5d'),
    ('has_camera_2_5d',              'camera.has_camera_2_5d'),
    ('projection_ctx',               'camera.projection_ctx'),
    ('light_context',                'camera.light_context'),
    ('optimize_compositing',         'options.optimize_compositing'),
    ('reuse_prev_framebuffer',       'options.reuse_prev_framebuffer'),
    ('skip_initial_clear',           'options.skip_initial_clear'),
    ('graph_structure_unchanged',    'options.graph_structure_unchanged'),
    ('modular_coordinates',          'options.modular_coordinates'),
    ('diagnostics_enabled',          'options.diagnostics_enabled'),
    ('track_dof_depth',              'options.track_dof_depth'),
    ('time_seconds',                 'frame.time_seconds'),
    ('ssaa_factor',                  'options.ssaa_factor'),
    ('cache_enabled',                'options.cache_enabled'),
    ('reusable_inputs',              'scratch.reusable_inputs'),
    ('width',                        'frame.width'),
    ('height',                       'frame.height'),
    ('backend',                      'resources.backend'),
    ('registry',                     'resources.registry'),
    ('profiler',                     'telemetry.profiler'),
    ('counters',                     'telemetry.counters'),
    ('node_telemetry',               'telemetry.node_telemetry'),
    ('layer_telemetry',              'telemetry.layer_telemetry'),
    ('dof_depth',                    'telemetry.dof_depth'),
    # The doubled-path renames MUST be last:
    ('frame',                        'frame.frame'),
    ('camera',                       'camera.camera'),
    ('fps',                          'frame.fps'),
]

# Find all C++ source files under the project tree.
files = []
for ext in ('cpp', 'hpp', 'h', 'c'):
    for root in ('src', 'include', 'apps', 'content', 'tests'):
        files.extend(glob.glob(f'{root}/**/*.{ext}', recursive=True))

changed = 0
for f in files:
    with open(f, 'r', encoding='utf-8') as fh:
        content = fh.read()

    original = content
    for var in VARS:
        for old, new in RENAMES:
            # Match `var.old` only when the next char is NOT a word char or dot
            # (i.e. NOT followed by `.x`, `_x`, or other identifier chars).
            # This prevents re-matching the new path (e.g. ctx.frame.frame
            # must not be re-matched as `ctx.frame` + suffix).
            pattern = re.compile(
                rf'\b{re.escape(var)}\.{re.escape(old)}(?![a-zA-Z0-9_.])'
            )
            replacement = f'{var}.{new}'
            content = pattern.sub(replacement, content)

    if content != original:
        with open(f, 'w', encoding='utf-8') as fh:
            fh.write(content)
        changed += 1

print(f'updated {changed} files')
