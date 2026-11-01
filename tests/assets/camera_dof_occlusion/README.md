# Camera DoF and occlusion visual assets

These PNGs are flat image cards for the small camera test scenes. None is a
mesh or a 3D model. The checker and grate are transparent cards; the corner
room and point lights are gradient plates.

| Asset | Scene | Intended use |
|---|---|---|
| `checkerboard_generated.png` | depth grid | high contrast edges across focus distances |
| `bokeh_points_generated.png` | point lights | compare circular and polygonal out-of-focus highlights |
| `corner_room_gradient_generated.png` | AO diagnostic | planar contact, floating disc, wall/floor corner, and sloped region |
| `nearfield_grate_generated.png` | integration | defocused foreground card over a distant occlusion pattern |

The generated plates are visual fixtures, not pixel oracles. Automated DoF
and alpha-occlusion gates use deterministic renderer-created cards in
`tests/render_graph/nodes/test_per_pixel_dof_node_rg_integration.cpp` so their
pixel assertions do not depend on generated texture details.

Chronon3D currently has no SSAO, HBAO, or GTAO pass. The corner-room PNG can
exercise depth, compositing, and DoF interactions, but it does not certify
ambient-occlusion shading or occlusion culling. Those require a renderer pass
and an explicit depth/normal sampling contract before an AO test can be called
functional.
