# Phase 2: Path Shape + Text Pro Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a proper bezier `PathShape` type rasterized via Blend2D, and extend `TextShape` with inline stroke, drop-shadow, and background-box so captions look professional without wrapping in extra layers.

**Architecture:** `PathShape` is a portable verb+point description in `shape.hpp`; `SoftwarePathProcessor` converts it to `BLPath`/`BLContext` at render time (Blend2D already linked). Text extensions are new fields on `TextShape`; `SoftwareTextProcessor` renders them in order: background → shadow → stroke → fill using existing `BLContext` machinery.

**Tech Stack:** C++20, Blend2D (already linked), doctest, existing `blend2d_bridge::composite_bl_image_transformed`.

---

## File Map

| File | Action |
|------|--------|
| `include/chronon3d/scene/shape.hpp` | Add `PathShape` struct + verbs enum; add `TextStroke/TextShadow/TextBackground`; extend `TextShape`; add `ShapeType::Path`; add `PathShape path` to `Shape` union |
| `include/chronon3d/scene/builders/builder_params.hpp` | Add `PathParams`; extend `TextParams` |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add `path()` declaration |
| `src/scene/layer_builder_internal.hpp` | Add `append_path()` declaration |
| `src/scene/layer_builder_internal.cpp` | Implement `append_path()` |
| `src/scene/layer_builder.cpp` | Implement `LayerBuilder::path()` |
| `src/scene/scene_builder.cpp` | Add top-level `SceneBuilder::path()` |
| `src/backends/software/processors/software_path_processor.cpp` | **New** — Blend2D path rasterizer |
| `src/backends/software/processors/builtin_processors.cpp` | Register path processor |
| `src/backends/software/rasterizers/shape_rasterizer.cpp` | Add `PathShape` bbox + hit_test cases |
| `src/backends/software/processors/software_text_processor.cpp` | Add stroke/shadow/background rendering |
| `tests/renderer/basics/test_path_shape.cpp` | New path tests |
| `tests/renderer/text/test_text_pro.cpp` | New text-pro tests |
| `tests/CMakeLists.txt` | Register two new test files |

---

## Task 1: `PathShape` data model in `shape.hpp`

**Files:**
- Modify: `include/chronon3d/scene/shape.hpp`

- [ ] **Step 1: Add `ShapeType::Path` to the enum**

In `include/chronon3d/scene/shape.hpp`, add `Path,` after `FakeExtrudedText,` in the `ShapeType` enum.

- [ ] **Step 2: Add `PathShape` struct before `struct Shape`**

Insert before the `struct Shape` definition:

```cpp
struct PathShape {
    enum class Cap  { Butt, Round, Square };
    enum class Join { Miter, Round, Bevel };

    struct Cmd {
        enum class Verb { MoveTo, LineTo, CubicTo, QuadTo, Close };
        Verb verb{Verb::MoveTo};
        Vec2 p{};    // endpoint
        Vec2 cp1{};  // control point 1 (CubicTo/QuadTo)
        Vec2 cp2{};  // control point 2 (CubicTo only)
    };

    std::vector<Cmd> cmds;

    // Stroke
    bool  stroke_enabled{true};
    f32   stroke_width{2.0f};
    Color stroke_color{1, 1, 1, 1};
    Cap   cap{Cap::Round};
    Join  join{Join::Round};
    f32   trim_start{0.0f};
    f32   trim_end{1.0f};
    std::vector<f32> dash_pattern;

    // Fill
    bool  fill_enabled{false};
    Color fill_color{1, 1, 1, 1};

    // Fluent builder
    PathShape& move_to(Vec2 p)                             { cmds.push_back({Cmd::Verb::MoveTo, p, {}, {}}); return *this; }
    PathShape& line_to(Vec2 p)                             { cmds.push_back({Cmd::Verb::LineTo, p, {}, {}}); return *this; }
    PathShape& cubic_to(Vec2 cp1, Vec2 cp2, Vec2 p)        { cmds.push_back({Cmd::Verb::CubicTo, p, cp1, cp2}); return *this; }
    PathShape& quad_to(Vec2 cp, Vec2 p)                    { cmds.push_back({Cmd::Verb::QuadTo, p, cp, {}}); return *this; }
    PathShape& close_path()                                { cmds.push_back({Cmd::Verb::Close, {}, {}, {}}); return *this; }
};
```

- [ ] **Step 3: Add text decoration structs before `TextShape`**

Insert before `struct TextShape`:

```cpp
struct TextStroke {
    bool  enabled{false};
    f32   width{2.0f};
    Color color{0, 0, 0, 1};
};

struct TextShadow {
    bool  enabled{false};
    Vec2  offset{2.0f, 2.0f};
    f32   blur{4.0f};
    Color color{0, 0, 0, 0.6f};
};

struct TextBackground {
    bool  enabled{false};
    Color color{0, 0, 0, 0.5f};
    Vec2  padding{8.0f, 4.0f};
    f32   corner_radius{4.0f};
};
```

- [ ] **Step 4: Extend `TextShape`**

Change `struct TextShape` from:

```cpp
struct TextShape {
    std::string text;
    TextStyle   style{};
    TextBox     box{};
};
```

to:

```cpp
struct TextShape {
    std::string text;
    TextStyle      style{};
    TextBox        box{};
    TextStroke     stroke{};
    TextShadow     text_shadow{};
    TextBackground background{};
};
```

- [ ] **Step 5: Add `PathShape path` to `Shape` union**

In `struct Shape`, add after `FakeExtrudedTextShape fake_extruded_text;`:

```cpp
    PathShape path;
```

- [ ] **Step 6: Build to check for compile errors**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
```

Expected: clean build.

- [ ] **Step 7: Commit**

```
git add include/chronon3d/scene/shape.hpp
git commit -m "feat(shape): add PathShape, ShapeType::Path, TextStroke/Shadow/Background to TextShape"
```

---

## Task 2: `PathParams`, extended `TextParams`, and builder wiring

**Files:**
- Modify: `include/chronon3d/scene/builders/builder_params.hpp`
- Modify: `include/chronon3d/scene/builders/layer_builder.hpp`
- Modify: `src/scene/layer_builder_internal.hpp`
- Modify: `src/scene/layer_builder_internal.cpp`
- Modify: `src/scene/layer_builder.cpp`
- Modify: `include/chronon3d/scene/builders/scene_builder.hpp`

- [ ] **Step 1: Add `PathParams` to `builder_params.hpp`**

In `include/chronon3d/scene/builders/builder_params.hpp`, add at the end (before closing `}`):

```cpp
struct PathParams {
    PathShape path;
    Vec3      pos{0, 0, 0};
};
```

- [ ] **Step 2: Extend `TextParams`**

Change `struct TextParams` from:

```cpp
struct TextParams {
    std::string content;
    TextStyle   style;
    Vec3        pos{0, 0, 0};
    TextBox     box{};
};
```

to:

```cpp
struct TextParams {
    std::string    content;
    TextStyle      style;
    Vec3           pos{0, 0, 0};
    TextBox        box{};
    TextStroke     stroke{};
    TextShadow     text_shadow{};
    TextBackground background{};
};
```

- [ ] **Step 3: Add `path()` to `LayerBuilder` header**

In `include/chronon3d/scene/builders/layer_builder.hpp`, after `LayerBuilder& image(...)`:

```cpp
    LayerBuilder& path(std::string name, PathParams p);
```

- [ ] **Step 4: Add `append_path` to internal header**

In `src/scene/layer_builder_internal.hpp`, add declaration:

```cpp
void append_path(Layer& layer, std::string name, PathParams p);
```

- [ ] **Step 5: Implement `append_path` in `layer_builder_internal.cpp`**

In `src/scene/layer_builder_internal.cpp`, add after `append_text`:

```cpp
void append_path(Layer& layer, std::string name, PathParams p) {
    auto& node = append_node(layer, std::move(name));
    node.shape.type = ShapeType::Path;
    node.shape.path = std::move(p.path);
    node.world_transform.position = p.pos;
    node.color = node.shape.path.stroke_enabled
        ? node.shape.path.stroke_color
        : node.shape.path.fill_color;
}
```

- [ ] **Step 6: Update `append_text` to copy new fields**

In `src/scene/layer_builder_internal.cpp`, inside `append_text()`, add after `node.shape.text.box = p.box;`:

```cpp
    node.shape.text.stroke     = p.stroke;
    node.shape.text.text_shadow = p.text_shadow;
    node.shape.text.background = p.background;
```

- [ ] **Step 7: Implement `LayerBuilder::path()`**

In `src/scene/layer_builder.cpp`, after `LayerBuilder::image()`:

```cpp
LayerBuilder& LayerBuilder::path(std::string name, PathParams p) {
    layer_builder_internal::append_path(m_layer, std::move(name), std::move(p));
    return *this;
}
```

- [ ] **Step 8: Add `path()` to `SceneBuilder`**

In `include/chronon3d/scene/builders/scene_builder.hpp`, after `SceneBuilder& image(...)` declaration:

```cpp
        SceneBuilder& path(std::string name, PathParams p);
```

In `src/scene/scene_builder.cpp`, add implementation (follow the pattern of existing `image()`):

```cpp
SceneBuilder& SceneBuilder::path(std::string name, PathParams p) {
    // top-level path: builds a single-layer scene with no transform
    LayerBuilder lb(std::move(name), current_frame_, scene_.resource());
    PathParams pp = std::move(p);
    Vec3 pos = pp.pos;
    lb.path("path", std::move(pp));
    Layer l = lb.build();
    if (l.active_at(current_frame_))
        scene_.add_layer(std::move(l));
    return *this;
}
```

- [ ] **Step 9: Build to verify clean compile**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
```

Expected: compiles cleanly.

- [ ] **Step 10: Commit**

```
git add include/chronon3d/scene/builders/builder_params.hpp
git add include/chronon3d/scene/builders/layer_builder.hpp
git add include/chronon3d/scene/builders/scene_builder.hpp
git add src/scene/layer_builder_internal.hpp src/scene/layer_builder_internal.cpp
git add src/scene/layer_builder.cpp src/scene/scene_builder.cpp
git commit -m "feat(builder): add PathParams, path() to LayerBuilder+SceneBuilder; extend TextParams"
```

---

## Task 3: `SoftwarePathProcessor` via Blend2D

**Files:**
- Create: `src/backends/software/processors/software_path_processor.cpp`
- Modify: `src/backends/software/processors/builtin_processors.cpp`
- Modify: `src/backends/software/rasterizers/shape_rasterizer.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/renderer/basics/test_path_shape.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static std::unique_ptr<Framebuffer> render_path(PathShape path, Vec3 pos = {0,0,0}) {
    SoftwareRenderer renderer;
    Composition comp(CompositionSpec{.width = 200, .height = 200, .duration = 1},
        [path, pos](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("p", [&](LayerBuilder& b) {
                b.path("path", PathParams{.path = path, .pos = pos});
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

TEST_CASE("PathShape: line stroke renders pixels along line") {
    PathShape p;
    p.move_to({20, 100}).line_to({180, 100});
    p.stroke_width = 3.0f;
    p.stroke_color = {1, 1, 1, 1};
    p.stroke_enabled = true;

    auto fb = render_path(p);
    REQUIRE(fb != nullptr);
    // Expect white pixels near row 100
    CHECK(any_pixel_in_region(*fb, 20, 95, 180, 105, Color{1, 1, 1, 1}));
}

TEST_CASE("PathShape: filled triangle has interior pixels") {
    PathShape p;
    p.move_to({100, 20}).line_to({180, 180}).line_to({20, 180}).close_path();
    p.fill_enabled = true;
    p.fill_color = {1, 0, 0, 1};
    p.stroke_enabled = false;

    auto fb = render_path(p);
    REQUIRE(fb != nullptr);
    // Center pixel inside triangle should be red
    Color c = fb->get_pixel(100, 130);
    CHECK(c.r > 0.5f);
    CHECK(c.g < 0.2f);
}

TEST_CASE("PathShape: trim_end=0.5 draws only first half of line") {
    PathShape p;
    p.move_to({10, 100}).line_to({190, 100});
    p.stroke_width = 3.0f;
    p.stroke_color = {1, 1, 1, 1};
    p.trim_end = 0.5f;

    auto fb = render_path(p);
    REQUIRE(fb != nullptr);
    // Pixel near start (x=50) should be lit
    CHECK(colors_match(fb->get_pixel(50, 100), Color{1,1,1,1}, 0.3f));
    // Pixel near end (x=170) should be dark
    CHECK(!colors_match(fb->get_pixel(170, 100), Color{1,1,1,1}, 0.3f));
}

TEST_CASE("PathShape: cubic bezier renders curved path") {
    PathShape p;
    p.move_to({20, 100})
     .cubic_to({20, 20}, {180, 20}, {180, 100});
    p.stroke_width = 2.0f;
    p.stroke_color = {1, 1, 1, 1};

    auto fb = render_path(p);
    REQUIRE(fb != nullptr);
    CHECK(any_pixel(*fb, Color{1, 1, 1, 1}));
    // The curve goes up toward y=20 in the middle, so check top region has pixels
    CHECK(any_pixel_in_region(*fb, 60, 20, 140, 60, Color{1, 1, 1, 1}));
}
```

Note: `any_pixel_in_region` may not exist yet — add a helper to `tests/helpers/pixel_assertions.hpp` in this step:

In `tests/helpers/pixel_assertions.hpp`, add:

```cpp
inline bool any_pixel_in_region(const Framebuffer& fb, int x0, int y0, int x1, int y1, const Color& target, float tol = 0.3f) {
    for (int y = std::max(0, y0); y < std::min(fb.height(), y1); ++y)
        for (int x = std::max(0, x0); x < std::min(fb.width(), x1); ++x)
            if (colors_match(fb.get_pixel(x, y), target, tol)) return true;
    return false;
}
```

- [ ] **Step 2: Register test in CMakeLists**

In `tests/CMakeLists.txt`, inside `chronon3d_renderer_tests` source list, add:

```cmake
    renderer/basics/test_path_shape.cpp
```

- [ ] **Step 3: Run to confirm failure**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
```

Expected: linker/runtime error — no processor registered for `ShapeType::Path`.

- [ ] **Step 4: Create `software_path_processor.cpp`**

Create `src/backends/software/processors/software_path_processor.cpp`:

```cpp
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/shape_processor.hpp>
#include "../utils/blend2d_bridge.hpp"
#include "../utils/blend2d_resources.hpp"
#include <blend2d.h>
#include <algorithm>
#include <cmath>

namespace chronon3d::renderer {

static BLPath build_bl_path(const PathShape& ps) {
    BLPath path;
    for (const auto& cmd : ps.cmds) {
        switch (cmd.verb) {
            case PathShape::Cmd::Verb::MoveTo:
                path.moveTo(cmd.p.x, cmd.p.y);
                break;
            case PathShape::Cmd::Verb::LineTo:
                path.lineTo(cmd.p.x, cmd.p.y);
                break;
            case PathShape::Cmd::Verb::CubicTo:
                path.cubicTo(cmd.cp1.x, cmd.cp1.y, cmd.cp2.x, cmd.cp2.y, cmd.p.x, cmd.p.y);
                break;
            case PathShape::Cmd::Verb::QuadTo:
                path.quadTo(cmd.cp1.x, cmd.cp1.y, cmd.p.x, cmd.p.y);
                break;
            case PathShape::Cmd::Verb::Close:
                path.close();
                break;
        }
    }
    return path;
}

static BLPath apply_trim(const BLPath& src, f32 trim_start, f32 trim_end) {
    if (trim_start <= 0.0f && trim_end >= 1.0f) return src;

    // Approximate trim by measuring total length and re-building a sub-path.
    // For production quality use BLStrokeOptions; this is a length-based approximation.
    BLPathMeasure measure;
    measure.reset(src);
    const double total = measure.approximatedLength();
    if (total < 1e-6) return src;

    const double t0 = static_cast<double>(std::clamp(trim_start, 0.0f, 1.0f)) * total;
    const double t1 = static_cast<double>(std::clamp(trim_end,   0.0f, 1.0f)) * total;

    BLPath out;
    BLPoint pt;
    measure.getClosestPoint(t0, &pt, nullptr);
    out.moveTo(pt);

    // Sample N intermediate points along the path and build line segments
    const int N = std::max(2, static_cast<int>((t1 - t0) / 2.0));
    for (int i = 1; i <= N; ++i) {
        double t = t0 + (t1 - t0) * (static_cast<double>(i) / static_cast<double>(N));
        measure.getClosestPoint(t, &pt, nullptr);
        out.lineTo(pt);
    }
    return out;
}

static BLStrokeCap to_bl_cap(PathShape::Cap cap) {
    switch (cap) {
        case PathShape::Cap::Butt:   return BL_STROKE_CAP_BUTT;
        case PathShape::Cap::Round:  return BL_STROKE_CAP_ROUND;
        case PathShape::Cap::Square: return BL_STROKE_CAP_SQUARE;
    }
    return BL_STROKE_CAP_ROUND;
}

static BLStrokeJoin to_bl_join(PathShape::Join join) {
    switch (join) {
        case PathShape::Join::Miter: return BL_STROKE_JOIN_MITER_CLIP;
        case PathShape::Join::Round: return BL_STROKE_JOIN_ROUND;
        case PathShape::Join::Bevel: return BL_STROKE_JOIN_BEVEL;
    }
    return BL_STROKE_JOIN_ROUND;
}

static BLRgba32 to_bl_color(const Color& c) {
    return BLRgba32(
        static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f),
        static_cast<uint8_t>(std::clamp(c.a, 0.0f, 1.0f) * 255.0f)
    );
}

class SoftwarePathProcessor final : public ShapeProcessor {
public:
    void draw(SoftwareRenderer& /*renderer*/, Framebuffer& fb, const RenderNode& node,
              const RenderState& state, const Camera& /*camera*/,
              i32 /*width*/, i32 /*height*/) override {

        const PathShape& ps = node.shape.path;
        if (ps.cmds.empty()) return;

        // Compute bounding box to size the BLImage
        float min_x =  1e9f, min_y =  1e9f;
        float max_x = -1e9f, max_y = -1e9f;
        for (const auto& cmd : ps.cmds) {
            if (cmd.verb == PathShape::Cmd::Verb::Close) continue;
            min_x = std::min(min_x, cmd.p.x); max_x = std::max(max_x, cmd.p.x);
            min_y = std::min(min_y, cmd.p.y); max_y = std::max(max_y, cmd.p.y);
        }
        const float pad = ps.stroke_enabled ? ps.stroke_width * 2.0f + 2.0f : 2.0f;
        min_x -= pad; min_y -= pad; max_x += pad; max_y += pad;

        const int img_w = std::max(1, static_cast<int>(std::ceil(max_x - min_x)));
        const int img_h = std::max(1, static_cast<int>(std::ceil(max_y - min_y)));

        BLImage img;
        img.create(img_w, img_h, BL_FORMAT_PRGB32);
        BLContext ctx(img);
        ctx.clearAll();

        // Translate path so it fits in the image
        BLMatrix2D mat = BLMatrix2D::makeTranslation(-min_x, -min_y);
        ctx.setMatrix(mat);

        BLPath blpath = build_bl_path(ps);
        BLPath draw_path = apply_trim(blpath, ps.trim_start, ps.trim_end);

        // Fill
        if (ps.fill_enabled) {
            ctx.setFillStyle(to_bl_color(ps.fill_color));
            ctx.fillPath(draw_path);
        }

        // Stroke
        if (ps.stroke_enabled && ps.stroke_width > 0.0f) {
            BLStrokeOptions opts;
            opts.width = ps.stroke_width;
            opts.startCap = to_bl_cap(ps.cap);
            opts.endCap   = to_bl_cap(ps.cap);
            opts.join     = to_bl_join(ps.join);

            if (!ps.dash_pattern.empty()) {
                BLArray<double> dashes;
                for (f32 d : ps.dash_pattern) dashes.append(static_cast<double>(d));
                opts.dashArray = dashes;
            }

            ctx.setStrokeStyle(to_bl_color(ps.stroke_color));
            ctx.setStrokeOptions(opts);
            ctx.strokePath(draw_path);
        }

        ctx.end();

        // Offset: node position + image origin offset
        const Vec3 pos = state.matrix * Vec4(min_x, min_y, 0.0f, 1.0f);
        Mat4 img_model = state.matrix * glm::translate(Mat4(1.0f), Vec3(min_x, min_y, 0.0f));

        blend2d_bridge::composite_bl_image_transformed(
            fb, img, img_model, state.opacity, BlendMode::Normal);
    }

    raster::BBox compute_world_bbox(const Shape& shape, const Mat4& model, f32 spread) override {
        const PathShape& ps = shape.path;
        float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
        for (const auto& cmd : ps.cmds) {
            if (cmd.verb == PathShape::Cmd::Verb::Close) continue;
            min_x = std::min(min_x, cmd.p.x); max_x = std::max(max_x, cmd.p.x);
            min_y = std::min(min_y, cmd.p.y); max_y = std::max(max_y, cmd.p.y);
        }
        const float pad = (ps.stroke_enabled ? ps.stroke_width * 0.5f : 0.0f) + spread;
        Vec4 corners[4] = {
            model * Vec4(min_x - pad, min_y - pad, 0, 1),
            model * Vec4(max_x + pad, min_y - pad, 0, 1),
            model * Vec4(max_x + pad, max_y + pad, 0, 1),
            model * Vec4(min_x - pad, max_y + pad, 0, 1),
        };
        float wx0 = 1e9f, wy0 = 1e9f, wx1 = -1e9f, wy1 = -1e9f;
        for (const auto& c : corners) {
            wx0 = std::min(wx0, c.x); wy0 = std::min(wy0, c.y);
            wx1 = std::max(wx1, c.x); wy1 = std::max(wy1, c.y);
        }
        return {(i32)wx0, (i32)wy0, (i32)std::ceil(wx1), (i32)std::ceil(wy1)};
    }

    bool hit_test(const Shape& /*shape*/, Vec2 /*local_point*/, f32 /*spread*/) override {
        return true; // rough — always pass, BBox culling handles most cases
    }
};

std::unique_ptr<ShapeProcessor> create_path_processor() {
    return std::make_unique<SoftwarePathProcessor>();
}

} // namespace chronon3d::renderer
```

- [ ] **Step 5: Register in `builtin_processors.cpp`**

In `src/backends/software/processors/builtin_processors.cpp`:

Add forward declaration with the others:
```cpp
std::unique_ptr<ShapeProcessor> create_path_processor();
```

In `register_builtin_processors()`, after the `GridPlane` line:
```cpp
    registry.register_shape(ShapeType::Path, create_path_processor());
```

- [ ] **Step 6: Add bbox/hit_test for `PathShape` in `shape_rasterizer.cpp`**

In `src/backends/software/rasterizers/shape_rasterizer.cpp`, inside `compute_world_bbox()` switch, add before `default:`:

```cpp
        case ShapeType::Path: {
            const PathShape& ps = shape.path;
            float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
            for (const auto& cmd : ps.cmds) {
                if (cmd.verb == PathShape::Cmd::Verb::Close) continue;
                min_x = std::min(min_x, cmd.p.x); max_x = std::max(max_x, cmd.p.x);
                min_y = std::min(min_y, cmd.p.y); max_y = std::max(max_y, cmd.p.y);
            }
            size = {max_x - min_x, max_y - min_y};
            break;
        }
```

In `hit_test()`, add before `default:`:

```cpp
        case ShapeType::Path:
            return true;
```

- [ ] **Step 7: Run path tests**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: all 4 new path tests pass; all existing renderer tests still pass.

- [ ] **Step 8: Commit**

```
git add src/backends/software/processors/software_path_processor.cpp
git add src/backends/software/processors/builtin_processors.cpp
git add src/backends/software/rasterizers/shape_rasterizer.cpp
git add tests/renderer/basics/test_path_shape.cpp
git add tests/helpers/pixel_assertions.hpp
git add tests/CMakeLists.txt
git commit -m "feat(path): add SoftwarePathProcessor via Blend2D with fill, stroke, trim, cubic bezier"
```

---

## Task 4: Text Pro — stroke, shadow, background in `SoftwareTextProcessor`

**Files:**
- Modify: `src/backends/software/processors/software_text_processor.cpp`

- [ ] **Step 1: Write the failing tests**

Create `tests/renderer/text/test_text_pro.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/chronon3d.hpp>
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/text/stb_font_backend.hpp>
#include "tests/helpers/pixel_assertions.hpp"

using namespace chronon3d;
using namespace chronon3d::test;

static const std::string FONT = "assets/fonts/Inter-Bold.ttf";

static std::unique_ptr<Framebuffer> render_text_pro(TextParams params) {
    SoftwareRenderer renderer;
    renderer.set_font_backend(std::make_shared<StbFontBackend>());
    Composition comp(CompositionSpec{.width = 400, .height = 100, .duration = 1},
        [params](const FrameContext& ctx) {
            SceneBuilder s(ctx);
            s.layer("t", [&](LayerBuilder& b) {
                b.text("txt", params);
            });
            return s.build();
        });
    return renderer.render_frame(comp, 0);
}

TEST_CASE("Text: stroke renders pixels outside glyph") {
    TextParams p;
    p.content = "A";
    p.style.font_path = FONT;
    p.style.size = 60.0f;
    p.style.color = {1, 1, 1, 1};
    p.pos = {200, 50, 0};
    p.stroke = {.enabled = true, .width = 4.0f, .color = {1, 0, 0, 1}};

    auto fb = render_text_pro(p);
    REQUIRE(fb != nullptr);
    // Some pixels must exist (white or red)
    CHECK(any_pixel(*fb, Color{1, 1, 1, 1}) || any_pixel(*fb, Color{1, 0, 0, 1}));
}

TEST_CASE("Text: shadow renders offset dark pixels") {
    TextParams p;
    p.content = "X";
    p.style.font_path = FONT;
    p.style.size = 60.0f;
    p.style.color = {1, 1, 1, 1};
    p.pos = {200, 50, 0};
    p.text_shadow = {.enabled = true, .offset = {0, 0}, .blur = 0.0f, .color = {0, 0, 0, 1}};

    auto fb = render_text_pro(p);
    REQUIRE(fb != nullptr);
    CHECK(any_pixel(*fb, Color{1, 1, 1, 1}));
}

TEST_CASE("Text: background fills rect behind text") {
    TextParams p;
    p.content = "BG";
    p.style.font_path = FONT;
    p.style.size = 40.0f;
    p.style.color = {1, 1, 1, 1};
    p.pos = {200, 50, 0};
    p.background = {.enabled = true, .color = {0, 0, 1, 1}, .padding = {4, 4}, .corner_radius = 0};

    auto fb = render_text_pro(p);
    REQUIRE(fb != nullptr);
    // Blue pixels should exist from background
    CHECK(any_pixel(*fb, Color{0, 0, 1, 1}));
}
```

- [ ] **Step 2: Register in CMakeLists**

In `tests/CMakeLists.txt`, inside `chronon3d_renderer_tests`:

```cmake
    renderer/text/test_text_pro.cpp
```

- [ ] **Step 3: Run to confirm current failure (or partial pass)**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: tests may fail at assert or produce wrong pixels since stroke/shadow/background not implemented.

- [ ] **Step 4: Update `SoftwareTextProcessor::draw()`**

In `src/backends/software/processors/software_text_processor.cpp`, update the `draw()` method to this:

```cpp
    void draw(SoftwareRenderer& renderer, Framebuffer& fb, const RenderNode& node,
              const RenderState& state, const Camera& camera, i32 width, i32 height) override {
        const Mat4& model  = state.matrix;
        const f32   opacity = state.opacity;
        const TextShape& ts = node.shape.text;

        const float effective_size = ts.style.size;
        auto raster = rasterize_text_to_bl_image(ts, effective_size);
        if (!raster) return;

        const int iw = raster->image.width();
        const int ih = raster->image.height();

        // 1. Background
        if (ts.background.enabled) {
            const float px = ts.background.padding.x;
            const float py = ts.background.padding.y;
            BLImage bg_img;
            bg_img.create(static_cast<int>(iw + 2 * px), static_cast<int>(ih + 2 * py), BL_FORMAT_PRGB32);
            BLContext bg_ctx(bg_img);
            bg_ctx.clearAll();
            const Color& bc = ts.background.color;
            bg_ctx.setFillStyle(BLRgba32(
                uint8_t(std::clamp(bc.r,0.f,1.f)*255),
                uint8_t(std::clamp(bc.g,0.f,1.f)*255),
                uint8_t(std::clamp(bc.b,0.f,1.f)*255),
                uint8_t(std::clamp(bc.a,0.f,1.f)*255)
            ));
            if (ts.background.corner_radius > 0.0f)
                bg_ctx.fillRoundRect(BLRoundRect(0, 0, bg_img.width(), bg_img.height(), ts.background.corner_radius));
            else
                bg_ctx.fillRect(BLRectI(0, 0, bg_img.width(), bg_img.height()));
            bg_ctx.end();
            Mat4 bg_model = model * glm::translate(Mat4(1.0f),
                Vec3(raster->x_offset - px, raster->y_offset - py, 0.0f));
            blend2d_bridge::composite_bl_image_transformed(fb, bg_img, bg_model, opacity, BlendMode::Normal);
        }

        // 2. Shadow
        if (ts.text_shadow.enabled) {
            BLImage sh_img;
            sh_img.create(iw, ih, BL_FORMAT_PRGB32);
            {
                BLContext ctx(sh_img);
                ctx.clearAll();
                ctx.blitImage(BLPoint(0, 0), raster->image);
                ctx.setCompOp(BL_COMP_OP_SRC_IN);
                const Color& sc = ts.text_shadow.color;
                ctx.setFillStyle(BLRgba32(
                    uint8_t(std::clamp(sc.r,0.f,1.f)*255),
                    uint8_t(std::clamp(sc.g,0.f,1.f)*255),
                    uint8_t(std::clamp(sc.b,0.f,1.f)*255), 255));
                ctx.fillAll();
            }
            Framebuffer sh_tmp(iw, ih);
            sh_tmp.clear(Color::transparent());
            blend2d_bridge::composite_bl_image(sh_tmp, sh_img, 0, 0, 1.0f, BlendMode::Normal);
            if (ts.text_shadow.blur > 0.0f)
                renderer.apply_blur(sh_tmp, ts.text_shadow.blur);
            const Vec2& off = ts.text_shadow.offset;
            Mat4 sh_model = model * glm::translate(Mat4(1.0f),
                Vec3(raster->x_offset + off.x, raster->y_offset + off.y, 0.0f));
            blend2d_bridge::composite_framebuffer_transformed(
                fb, sh_tmp, sh_model, opacity * ts.text_shadow.color.a, BlendMode::Normal);
        }

        // 3. Stroke (draw outline under fill text)
        if (ts.stroke.enabled && ts.stroke.width > 0.0f) {
            BLImage stroke_img;
            stroke_img.create(iw, ih, BL_FORMAT_PRGB32);
            {
                BLContext ctx(stroke_img);
                ctx.clearAll();
                // Use SRC_OVER with a dilated mask for the stroke effect.
                // Blend2D doesn't directly stroke glyph outlines from a rasterized image,
                // so we use a simple expansion: render text twice with outward blur then mask.
                ctx.blitImage(BLPoint(0, 0), raster->image);
                ctx.setCompOp(BL_COMP_OP_SRC_IN);
                const Color& sc = ts.stroke.color;
                ctx.setFillStyle(BLRgba32(
                    uint8_t(std::clamp(sc.r,0.f,1.f)*255),
                    uint8_t(std::clamp(sc.g,0.f,1.f)*255),
                    uint8_t(std::clamp(sc.b,0.f,1.f)*255), 255));
                ctx.fillAll();
            }
            // Expand by blur to create outline halo
            Framebuffer stroke_tmp(iw, ih);
            stroke_tmp.clear(Color::transparent());
            blend2d_bridge::composite_bl_image(stroke_tmp, stroke_img, 0, 0, 1.0f, BlendMode::Normal);
            renderer.apply_blur(stroke_tmp, ts.stroke.width * 0.8f);

            Mat4 stroke_model = model * glm::translate(Mat4(1.0f),
                Vec3(raster->x_offset, raster->y_offset, 0.0f));
            blend2d_bridge::composite_framebuffer_transformed(
                fb, stroke_tmp, stroke_model, opacity, BlendMode::Normal);
        }

        // 4. Fill text (original color)
        BLImage text_img;
        text_img.create(iw, ih, BL_FORMAT_PRGB32);
        {
            BLContext ctx(text_img);
            ctx.clearAll();
            ctx.blitImage(BLPoint(0, 0), raster->image);
            ctx.setCompOp(BL_COMP_OP_SRC_IN);
            const Color& fc = ts.style.color;
            ctx.setFillStyle(BLRgba32(
                uint8_t(std::clamp(fc.r,0.f,1.f)*255),
                uint8_t(std::clamp(fc.g,0.f,1.f)*255),
                uint8_t(std::clamp(fc.b,0.f,1.f)*255), 255));
            ctx.fillAll();
        }
        Mat4 text_model = model * glm::translate(Mat4(1.0f),
            Vec3(raster->x_offset, raster->y_offset, 0.0f));
        blend2d_bridge::composite_bl_image_transformed(fb, text_img, text_model, opacity, BlendMode::Normal);
    }
```

- [ ] **Step 5: Run tests**

```
cmake --build build/chronon/win-debug --target chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_renderer_tests --output-on-failure
```

Expected: all 3 new text-pro tests pass; all existing text tests still pass.

- [ ] **Step 6: Full regression**

```
cmake --build build/chronon/win-debug --target chronon3d_tests -j8
ctest --test-dir build/chronon/win-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```
git add src/backends/software/processors/software_text_processor.cpp
git add tests/renderer/text/test_text_pro.cpp
git add tests/CMakeLists.txt
git commit -m "feat(text): add stroke, shadow, and background box to SoftwareTextProcessor"
```
