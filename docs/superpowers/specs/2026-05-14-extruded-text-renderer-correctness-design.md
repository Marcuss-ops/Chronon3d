# Extruded Text Renderer — Engine Correctness

**Date:** 2026-05-14
**Branch:** feature/full-engine-stabilization
**Files affected:**
- `src/renderer/software/specialized/fake_extruded_text_renderer.cpp`
- `include/chronon3d/renderer/software/fake_extruded_text_renderer.hpp`
- `src/renderer/software/software_renderer.cpp`

---

## Context

`FakeExtrudedTextRenderer` produces 3D extruded glyphs via earcut triangulation + perspective projection. Four engine-level correctness gaps exist that manifest as rendering artifacts:

1. Byte-by-byte text iteration breaks multi-byte (UTF-8) codepoints
2. Hole contours are assigned to the wrong outer island in complex glyphs
3. Depth sort is scoped per-object; overlapping objects sort incorrectly
4. Triangles straddling the near plane are discarded entirely instead of clipped

---

## Fix 1 — UTF-8 Codepoint Decoding

### Problem
```cpp
for (char ch : s.text)
    const int cp = (int)(unsigned char)ch;  // byte, not codepoint
```
`è` (U+00E8) encodes as `0xC3 0xA8` in UTF-8. Each byte becomes a separate, wrong codepoint passed to stbtt.

### Design
Add a free function in the `.cpp` file:
```cpp
static std::vector<int> utf8_to_codepoints(std::string_view s);
```
Standard 1/2/3/4-byte UTF-8 decode. Returns `U+FFFD` (replacement character) for invalid sequences.

Replace **all** text iteration in `draw()`:
- The initial `x_cur` calculation (advance sum)
- The main per-character geometry loop

No interface changes. Cache key already uses `int codepoint`.

---

## Fix 2 — Hole-to-Island Assignment (Point-in-Polygon)

### Problem
Current logic assigns each CCW contour (hole) to the first CW island found:
```cpp
for (auto& island : islands) {
    island.polygon.push_back(std::move(c));
    placed = true;
    break;   // always takes the first
}
```
For `B`, `R`, `D`, `P`, `Q`: the two holes end up in the same polygon ring as the first outer, which earcut triangulates incorrectly (fills the holes).

### Design
After separating outers (area > 0) and holes (area < 0):

For each hole:
1. Take its first vertex as a test point.
2. For each outer contour, run a **ray-casting point-in-polygon** test.
3. Collect all outers that contain the point.
4. Assign the hole to the outer with the **smallest positive area** (most immediate parent).
5. If no outer contains the point (degenerate font data), create a new island for it.

**Ray-casting**: cast a horizontal ray from test point to +∞, count edge crossings. Odd = inside.

This is the standard winding-rule behavior used by all major 2D renderers.

---

## Fix 3 — Global Painter's Algorithm (Inter-Object Depth Sort)

### Problem
`FakeExtrudedTextRenderer::draw()` collects and sorts primitives per call. Two separate `FakeExtrudedText` nodes (e.g., "BACK" and "FRONT" at different Z positions) are drawn sequentially, so later nodes always appear on top regardless of depth.

### Design

**`FakeExtrudedTextRenderer`** gains a two-phase API:

```cpp
// Phase 1: called once per FakeExtrudedText node during scene traversal
void collect(const RenderNode& node, const RenderState& state,
             const Camera& camera, i32 width, i32 height,
             TextRenderer& text_renderer);

// Phase 2: called once per frame after all nodes are visited
void flush(Framebuffer& fb);

// Reset accumulator (called at frame start by SoftwareRenderer)
void begin_frame();
```

`collect()` is the current `draw()` body minus the sort+rasterize step — it populates `m_quads` and `m_tris` member vectors.

`flush()` runs the existing `stable_sort` + rasterize loop over all accumulated primitives, then clears the vectors.

`begin_frame()` clears the vectors (guard against missed flushes).

**`SoftwareRenderer`** changes:
- Calls `m_fake_extruded_text_renderer.begin_frame()` at the start of each frame.
- In `draw_node()`, the `FakeExtrudedText` branch calls `collect()` instead of `draw()`.
- After the scene traversal loop completes, calls `m_fake_extruded_text_renderer.flush(fb)`.

The sort key (`cam_depth`) is already view-space Z, which is consistent across objects sharing the same camera. No change to sort logic.

---

## Fix 4 — Near Plane Triangle Clipping (Sutherland-Hodgman)

### Problem
```cpp
auto proj3 = [&](const Vec3& w, bool& ok) -> Vec2 {
    Vec4 p = camera.view_matrix() * Vec4(w, 1.0f);
    if (p.z > -1.0f) { ok = false; return {}; }  // discard entire vertex
    ...
};
```
When a triangle or quad has any vertex with `p.z > -1.0f`, the whole primitive is rejected. This causes geometry to abruptly pop out rather than clip cleanly as the camera approaches.

### Design
Add a static helper:
```cpp
static std::vector<Vec3> clip_polygon_near(std::span<const Vec3> world_pts, float near_z,
                                           const Mat4& view);
```
Sutherland-Hodgman clipping against the single plane `view_z < near_z` (i.e., `p.z < -near_z` in view space, default `near_z = 1.0f`). Returns 0–N clipped world-space vertices.

Apply before projection in both `collect()` (for quads and triangles):
1. Transform vertices to view space.
2. Clip against near plane → may yield 0 (fully behind), same N (fully in front), or N+1 (straddling) vertices.
3. Re-project clipped vertices to screen space.
4. Fan-triangulate the resulting polygon.

This replaces the current early-out `ok = false` with actual geometry.

---

## Interface Summary

### `.hpp` changes

```cpp
class FakeExtrudedTextRenderer {
public:
    void begin_frame();
    void collect(const RenderNode&, const RenderState&, const Camera&,
                 i32 width, i32 height, TextRenderer&);
    void flush(Framebuffer&);
    void clear_cache() { m_glyph_cache.clear(); }

private:
    // existing:
    const GlyphGeometry& get_glyph(...);
    std::unordered_map<std::string, GlyphGeometry> m_glyph_cache;

    // new accumulators:
    struct SideQ { Vec2 v[4]; Color c0, c1; float depth; };
    struct Tri3D { Vec2 v[3]; Color colors[3]; float depth; };
    std::vector<SideQ> m_quads;
    std::vector<Tri3D> m_tris;
};
```

`draw()` is removed. `SoftwareRenderer` is the only caller — backward compatibility is not a concern.

---

## Test Coverage

Each fix maps directly to a test image already defined in the test suite:

| Fix | Test |
|-----|------|
| UTF-8 decoding | `05_accents_extruded` (ÈLITE ÀREA) |
| Hole assignment | `03_extrusion_glyph_holes` (BOARD) |
| Global depth sort | `06_depth_sorting_text` (BACK/FRONT) |
| Near clip | `08_near_clipping_text` (CLOSI) |

---

## Out of Scope

- Z-buffer / per-pixel depth testing (future, if painter's algorithm proves insufficient for complex scenes)
- Kerning pairs (separate feature)
- Multi-line text layout (separate feature)
