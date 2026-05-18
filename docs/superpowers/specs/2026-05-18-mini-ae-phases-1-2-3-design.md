# Chronon3d Mini-AE: Phases 1-2-3 Design

> Date: 2026-05-18 | Status: Approved

---

## Scope

Three independent sub-projects, each self-contained and testable:

- **Phase 1** — Animated Properties: wire `AnimatedValue<T>` into layer transforms
- **Phase 2** — Path Shape + Text Pro: Blend2D-backed bezier paths, text stroke/shadow/background
- **Phase 3** — Effects Expansion + Mask Feather: new effects, feathered masks

Each phase is a separate PR.

---

## Phase 1: Animated Properties

### Problem

`AnimatedValue<T>` and `AnimatedTransform` already exist but are **not connected** to the rendering pipeline. `Layer.transform` holds a static `Transform`. `LayerBuilder` only exposes static setters (`position(Vec3)`, `opacity(f32)`). There is no way today to declare keyframe-driven layer properties at the composition level.

### Design

#### 1.1 Extend `AnimatedTransform`

File: `include/chronon3d/animation/animated_transform.hpp`

Add `opacity` and `anchor` tracks; update `evaluate()` to return a full `Transform`:

```cpp
struct AnimatedTransform {
    AnimatedValue<Vec3> position{Vec3(0.0f)};
    AnimatedValue<Vec3> rotation_euler{Vec3(0.0f)};   // degrees, converted to Quat
    AnimatedValue<Vec3> scale{Vec3(1.0f)};
    AnimatedValue<Vec3> anchor{Vec3(0.0f)};
    AnimatedValue<f32>  opacity{1.0f};

    [[nodiscard]] Transform evaluate(Frame frame) const;
    [[nodiscard]] bool is_animated() const;
};
```

`evaluate()` converts `rotation_euler.evaluate(frame)` via `math::from_euler()` to produce the Quat in the returned `Transform`.

#### 1.2 Add `anim_transform` to `Layer`

File: `include/chronon3d/scene/layer/layer.hpp`

```cpp
struct Layer {
    // ... existing fields ...
    AnimatedTransform anim_transform{};   // NEW — evaluated and merged at build time
};
```

#### 1.3 `LayerBuilder` — accept frame, expose anim accessors

File: `include/chronon3d/scene/builders/layer_builder.hpp`

Constructor adds `Frame current_frame`:
```cpp
explicit LayerBuilder(std::string name,
                      Frame current_frame = 0,
                      std::pmr::memory_resource* res = ...);
```

New fluent accessors (return ref to animated track — callers chain `.key()`):
```cpp
AnimatedValue<Vec3>& position_anim();
AnimatedValue<Vec3>& scale_anim();
AnimatedValue<Vec3>& rotate_anim();   // euler degrees
AnimatedValue<Vec3>& anchor_anim();
AnimatedValue<f32>&  opacity_anim();
```

`build()` evaluates `anim_transform` at `m_current_frame` and writes into `m_layer.transform` **only for animated axes** — static setters (`position(Vec3)`) still work and take priority over unanimated tracks.

#### 1.4 `SceneBuilder` passes frame to `LayerBuilder`

File: `src/scene/scene_builder.cpp`

```cpp
template <typename Fn>
SceneBuilder& layer(std::string name, Fn&& fn) {
    LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
    //                                   ^^^^^^^^^^^^^^^ NEW
    std::forward<Fn>(fn)(builder);
    Layer l = builder.build();
    if (l.active_at(current_frame_))
        scene_.add_layer(std::move(l));
    return *this;
}
```

Same change applies to `adjustment_layer`, `null_layer`, `precomp_layer`, `video_layer`.

#### 1.5 Usage example

```cpp
sb.layer("title", [&](LayerBuilder& b) {
    b.text("t", {.content = "Hello", .style = {.size = 48}});
    b.position_anim()
        .key(0,  {960, -100, 0})
        .key(20, {960,  540, 0}, Easing::OutBack);
    b.opacity_anim()
        .key(0,  0.0f)
        .key(15, 1.0f, Easing::OutCubic);
});
```

#### 1.6 Tests

- `AnimatedTransform::evaluate()` returns correct position/opacity at boundary and mid-frames
- `LayerBuilder` with animated position produces correct `Transform` after `build()`
- `SceneBuilder` passes current frame through; layer at frame 10 has position interpolated correctly
- Static setters still work unaffected when no `_anim()` tracks are set

---

## Phase 2: Path Shape + Text Pro

### Problem

- No bezier/free-form path shape type in the engine
- `TextStyle` has no stroke, drop shadow, or background box at the shape level (only as layer-level effects)

Blend2D is **already in the project** (`blend2d_bridge.hpp`, used by `SoftwareTextProcessor`). No new library dependency needed.

### 2A: PathShape

#### 2A.1 `PathShape` struct

File: `include/chronon3d/scene/shape.hpp`

Add `ShapeType::Path` to the enum. Add:

```cpp
struct PathShape {
    enum class Cap  { Butt, Round, Square };
    enum class Join { Miter, Round, Bevel };

    struct Cmd {
        enum class Verb { MoveTo, LineTo, CubicTo, QuadTo, Close } verb;
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
    Fill  fill{};       // reuses existing Fill for gradient support

    // Fluent builder helpers
    PathShape& move_to(Vec2 p);
    PathShape& line_to(Vec2 p);
    PathShape& cubic_to(Vec2 cp1, Vec2 cp2, Vec2 p);
    PathShape& quad_to(Vec2 cp, Vec2 p);
    PathShape& close_path();
};
```

Shape union in `Shape` adds: `PathShape path;`

#### 2A.2 `PathParams`

File: `include/chronon3d/scene/builders/builder_params.hpp`

```cpp
struct PathParams {
    PathShape path;
    Vec3      pos{0, 0, 0};
};
```

#### 2A.3 `LayerBuilder` and `SceneBuilder` API

```cpp
LayerBuilder& path(std::string name, PathParams p);
SceneBuilder& path(std::string name, PathParams p);
```

#### 2A.4 `SoftwarePathProcessor`

File: `src/backends/software/processors/software_path_processor.cpp`

Converts `PathShape` to `BLPath`, uses `BLContext` with:
- `setStrokeOptions` for cap/join
- `BLDashOptions` for dash pattern
- trim via sub-path length measurement and `BLPath::addPath` with range
- fill via `fillPath` (solid or gradient via `BLGradient`)
- Composites the BLImage back via existing `blend2d_bridge::composite_bl_image_transformed`

`compute_world_bbox` for `PathShape`: compute AABB of all cmd points + stroke_width/2.

#### 2A.5 Tests

- `PathShape` fluent builder: move_to/line_to/cubic_to/close round-trips
- `SoftwarePathProcessor` renders a triangle with fill and stroke — pixel-level smoke test (non-zero pixels in expected region)
- Trim path [0.25, 0.75] renders only middle portion
- Dash pattern produces non-continuous stroke

---

### 2B: Text Pro (stroke, shadow, background)

#### 2B.1 Extend `TextShape`

File: `include/chronon3d/scene/shape.hpp`

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

struct TextShape {
    // ... existing fields ...
    TextStroke     stroke{};
    TextShadow     text_shadow{};
    TextBackground background{};
};
```

#### 2B.2 `TextParams` update

Add `TextStroke stroke`, `TextShadow text_shadow`, `TextBackground background` to `TextParams` in `builder_params.hpp`.

#### 2B.3 `LayerBuilder::text` update

Pass through the new fields from `TextParams` into the `RenderNode`.

#### 2B.4 `SoftwareTextProcessor` rendering

Order: background → text shadow → stroke → fill text

- **Background**: rasterize layout bounding box via `BLContext::fillRoundRect`, blit before text
- **Text shadow**: rasterize text at shadow color, offset, apply Gaussian blur (already have `apply_blur`), composite with shadow opacity
- **Stroke**: use Blend2D's `setStrokeStyle` + `strokeGlyphRun` for outline text rendering
- **Fill**: existing path

All drawn via `BLContext` into a `BLImage`, composited via existing `blend2d_bridge`.

#### 2B.5 Tests

- Text with stroke renders non-background pixels in stroke color outside glyph boundary
- Text with shadow produces blurred dark pixels offset from text
- Text with background renders rounded rect behind text
- All three combined render correctly

---

## Phase 3: Effects Expansion + Mask Feather

### 3A: New Effects

#### 3A.1 `LayerBuilder` API additions

```cpp
LayerBuilder& hue_saturation(f32 hue_shift_deg, f32 saturation_scale = 1.0f, f32 lightness = 0.0f);
LayerBuilder& vignette(f32 intensity = 0.6f, f32 radius = 0.75f);
LayerBuilder& color_correction(Color shadows = {}, Color midtones = {}, Color highlights = {});
```

#### 3A.2 Implementation

File: `src/backends/software/utils/render_effects_processor.hpp` (or a new `software_new_effects.cpp`)

- **HueSaturation**: per-pixel RGB→HSL→apply shift+scale→HSL→RGB. CPU loop, no library needed.
- **Vignette**: per-pixel multiply by `1 - intensity * smoothstep(radius, 1.0, dist_from_center)`.
- **ColorCorrection**: per-pixel split into shadows/midtones/highlights by luminance, add tint color.

Effect params added to `EffectParams` variant and registered in the effect registry.

#### 3A.3 Tests

- `HueSaturation` with hue_shift=180 inverts hue of a solid red pixel to cyan
- `Vignette` darkens corner pixels more than center pixels
- `ColorCorrection` with warm highlights adds reddish tint to bright pixels

---

### 3B: Mask Feather

#### 3B.1 Data model

File: `include/chronon3d/scene/mask/mask.hpp`

```cpp
struct Mask {
    // ... existing fields ...
    f32 feather{0.0f};  // NEW — blur radius for soft edge (0 = hard edge)
};

struct RectMaskParams        { /* ... */ f32 feather{0.0f}; };
struct RoundedRectMaskParams { /* ... */ f32 feather{0.0f}; };
struct CircleMaskParams      { /* ... */ f32 feather{0.0f}; };
```

#### 3B.2 Implementation

`pixel_passes_mask` in `shape_rasterizer.cpp` today returns bool. Change to return `f32` (alpha weight 0..1):

```cpp
f32 mask_alpha(const RenderState& state, i32 x, i32 y);
```

When `feather == 0`: returns 0.0f or 1.0f (existing behavior).
When `feather > 0`: compute signed distance from mask boundary, map to smooth alpha via `smoothstep(-feather, feather, sdf)`.

All call sites that currently do `if (!pixel_passes_mask(...)) continue;` change to multiply pixel alpha by `mask_alpha(...)`.

#### 3B.3 Tests

- Hard mask (feather=0): pixels outside fully clipped
- Feathered mask (feather=20): pixels at boundary have alpha between 0 and 1
- Inverted feathered mask works symmetrically

---

## Files Touched Summary

### Phase 1
| File | Change |
|------|--------|
| `include/chronon3d/animation/animated_transform.hpp` | Add opacity/anchor tracks, update evaluate() |
| `include/chronon3d/scene/layer/layer.hpp` | Add `anim_transform` field |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add Frame param + `*_anim()` accessors |
| `src/scene/layer_builder_internal.cpp` | Implement `*_anim()`, update `build()` |
| `src/scene/scene_builder.cpp` | Pass current_frame to LayerBuilder in all layer variants |
| `tests/` | New animated properties tests |

### Phase 2
| File | Change |
|------|--------|
| `include/chronon3d/scene/shape.hpp` | Add PathShape, TextStroke/Shadow/Background |
| `include/chronon3d/scene/builders/builder_params.hpp` | Add PathParams, update TextParams |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add `path()` method |
| `src/scene/layer_builder_internal.cpp` | Implement path() |
| `src/scene/scene_builder.cpp` | Add top-level `path()` |
| `src/backends/software/processors/software_path_processor.cpp` | New — Blend2D path rasterizer |
| `src/backends/software/processors/software_text_processor.cpp` | Add stroke/shadow/background |
| `src/backends/software/processors/builtin_processors.cpp` | Register path processor |
| `src/backends/software/rasterizers/shape_rasterizer.cpp` | Add PathShape bbox/hit_test |
| `tests/` | New path + text pro tests |

### Phase 3
| File | Change |
|------|--------|
| `include/chronon3d/scene/mask/mask.hpp` | Add feather field |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add hue_saturation/vignette/color_correction |
| `src/backends/software/rasterizers/shape_rasterizer.cpp` | mask_alpha() returning f32 |
| `src/backends/software/utils/render_effects_processor.hpp` | New effect implementations |
| `src/backends/software/processors/builtin_processors.cpp` | Register new effects |
| `tests/` | New effect + mask feather tests |

---

## Constraints

- No new public library dependencies for Phases 1 and 3 (all CPU-only using existing STL/Blend2D/our math)
- Phase 2 uses Blend2D which is already linked
- All new tests must pass in the existing CMake + CTest harness
- No breaking changes to existing composition API — all new methods are additive
- `AnimatedValue<T>` is unchanged; its existing easing library is sufficient (no tweeny integration needed for Phase 1)
