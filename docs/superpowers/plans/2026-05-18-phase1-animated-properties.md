# Phase 1: Animated Properties Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `AnimatedValue<T>` into Layer transforms so composition fns can declare keyframe-driven position, opacity, scale, rotation, and anchor without manual per-frame interpolation.

**Architecture:** Extend `AnimatedTransform` with opacity/anchor tracks; add it as a field on `Layer`; give `LayerBuilder` a stored `current_frame` and `*_anim()` accessor methods that return refs to those tracks; `build()` bakes animated values at the stored frame. `SceneBuilder` passes `current_frame_` to each `LayerBuilder` it constructs.

**Tech Stack:** C++20, doctest, existing `AnimatedValue<T>`, `Easing`, `math::from_euler`.

---

## File Map

| File | Action |
|------|--------|
| `include/chronon3d/animation/animated_transform.hpp` | Extend: add `anchor`, `opacity`, `rotation_euler` (Vec3 degrees); update `evaluate()` |
| `include/chronon3d/scene/layer/layer.hpp` | Add `AnimatedTransform anim_transform{}` field |
| `src/scene/layer.cpp` | Copy-assign `anim_transform` in the custom copy ctor/op= |
| `include/chronon3d/scene/builders/layer_builder.hpp` | Add `Frame m_current_frame`; add `*_anim()` declarations |
| `src/scene/layer_builder.cpp` | Add frame to ctor; implement `*_anim()`; update `build()` |
| `src/scene/scene_builder.cpp` | Pass `current_frame_` to `LayerBuilder` in all `layer`, `adjustment_layer`, `null_layer`, `precomp_layer`, `video_layer` templates |
| `tests/core/animation/test_animated_transform.cpp` | New: unit tests for extended `AnimatedTransform` |
| `tests/scene/test_layer_builder_animated.cpp` | New: integration tests for animated layerbuilder |
| `tests/CMakeLists.txt` | Register the two new test files |

---

## Task 1: Extend `AnimatedTransform`

**Files:**
- Modify: `include/chronon3d/animation/animated_transform.hpp`

- [ ] **Step 1: Write the failing test**

Create `tests/core/animation/test_animated_transform.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/animation/animated_transform.hpp>

using namespace chronon3d;

TEST_CASE("AnimatedTransform: evaluate position at frame") {
    AnimatedTransform at;
    at.position.key(0, Vec3{0, 0, 0}).key(60, Vec3{120, 0, 0});
    Transform t = at.evaluate(30);
    CHECK(t.position.x == doctest::Approx(60.0f));
    CHECK(t.position.y == doctest::Approx(0.0f));
}

TEST_CASE("AnimatedTransform: evaluate opacity") {
    AnimatedTransform at;
    at.opacity.key(0, 0.0f).key(30, 1.0f);
    CHECK(at.evaluate(0).opacity  == doctest::Approx(0.0f));
    CHECK(at.evaluate(15).opacity == doctest::Approx(0.5f));
    CHECK(at.evaluate(30).opacity == doctest::Approx(1.0f));
}

TEST_CASE("AnimatedTransform: evaluate anchor") {
    AnimatedTransform at;
    at.anchor.key(0, Vec3{0, 0, 0}).key(60, Vec3{100, 50, 0});
    Transform t = at.evaluate(60);
    CHECK(t.anchor.x == doctest::Approx(100.0f));
    CHECK(t.anchor.y == doctest::Approx(50.0f));
}

TEST_CASE("AnimatedTransform: is_animated true when any track has keys") {
    AnimatedTransform at;
    CHECK_FALSE(at.is_animated());
    at.opacity.key(0, 0.0f);
    CHECK(at.is_animated());
}

TEST_CASE("AnimatedTransform: static (no keys) returns defaults") {
    AnimatedTransform at;
    Transform t = at.evaluate(999);
    CHECK(t.position.x == doctest::Approx(0.0f));
    CHECK(t.opacity    == doctest::Approx(1.0f));
    CHECK(t.scale.x    == doctest::Approx(1.0f));
}
```

- [ ] **Step 2: Register test in CMakeLists**

In `tests/CMakeLists.txt`, inside `chronon3d_core_tests` source list, add after the last animation test file:

```cmake
    core/animation/test_animated_transform.cpp
```

- [ ] **Step 3: Run to verify it fails**

```
cmake --build build/chronon/win-debug --target chronon3d_core_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_core_tests --output-on-failure
```

Expected: compile error — `AnimatedTransform` missing `anchor`, `opacity` fields.

- [ ] **Step 4: Implement extended `AnimatedTransform`**

Replace entire `include/chronon3d/animation/animated_transform.hpp`:

```cpp
#pragma once

#include <chronon3d/math/transform.hpp>
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/math/math_base.hpp>

namespace chronon3d {

struct AnimatedTransform {
    AnimatedValue<Vec3> position{Vec3(0.0f)};
    AnimatedValue<Vec3> rotation_euler{Vec3(0.0f)};  // degrees XYZ, converted to Quat
    AnimatedValue<Vec3> scale{Vec3(1.0f)};
    AnimatedValue<Vec3> anchor{Vec3(0.0f)};
    AnimatedValue<f32>  opacity{1.0f};

    [[nodiscard]] Transform evaluate(Frame frame) const {
        Transform t;
        t.position = position.evaluate(frame);
        t.rotation = math::from_euler(rotation_euler.evaluate(frame));
        t.scale    = scale.evaluate(frame);
        t.anchor   = anchor.evaluate(frame);
        t.opacity  = opacity.evaluate(frame);
        return t;
    }

    [[nodiscard]] bool is_animated() const {
        return position.is_animated() || rotation_euler.is_animated() ||
               scale.is_animated()    || anchor.is_animated() ||
               opacity.is_animated();
    }
};

} // namespace chronon3d
```

- [ ] **Step 5: Run tests**

```
cmake --build build/chronon/win-debug --target chronon3d_core_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_core_tests --output-on-failure
```

Expected: all 5 new tests pass.

- [ ] **Step 6: Commit**

```
git add include/chronon3d/animation/animated_transform.hpp
git add tests/core/animation/test_animated_transform.cpp
git add tests/CMakeLists.txt
git commit -m "feat(anim): extend AnimatedTransform with opacity, anchor, rotation_euler"
```

---

## Task 2: Add `anim_transform` to `Layer`

**Files:**
- Modify: `include/chronon3d/scene/layer/layer.hpp`
- Modify: `src/scene/layer.cpp`

- [ ] **Step 1: Add field to `Layer`**

In `include/chronon3d/scene/layer/layer.hpp`, add include after existing includes:

```cpp
#include <chronon3d/animation/animated_transform.hpp>
```

Inside `struct Layer { ... }`, add after `Transform transform{};`:

```cpp
    AnimatedTransform anim_transform{};
```

- [ ] **Step 2: Update copy ctor/op= in `src/scene/layer.cpp`**

In the copy constructor `Layer::Layer(const Layer& other)`, add inside the initializer list (after `material(other.material),`):

```cpp
      anim_transform(other.anim_transform),
```

In `Layer::operator=(const Layer& other)`, add after `material = other.material;`:

```cpp
    anim_transform = other.anim_transform;
```

- [ ] **Step 3: Build to verify no regressions**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_scene_tests --output-on-failure
```

Expected: all existing scene tests still pass.

- [ ] **Step 4: Commit**

```
git add include/chronon3d/scene/layer/layer.hpp src/scene/layer.cpp
git commit -m "feat(layer): add anim_transform field to Layer"
```

---

## Task 3: `LayerBuilder` — frame storage + `*_anim()` accessors

**Files:**
- Modify: `include/chronon3d/scene/builders/layer_builder.hpp`
- Modify: `src/scene/layer_builder.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/scene/test_layer_builder_animated.cpp`:

```cpp
#include <doctest/doctest.h>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/scene/builders/layer_builder.hpp>
#include <chronon3d/core/frame_context.hpp>

using namespace chronon3d;

TEST_CASE("LayerBuilder: position_anim bakes into transform at frame") {
    FrameContext ctx{.frame = 30, .resource = std::pmr::get_default_resource()};
    SceneBuilder sb(ctx);
    sb.layer("hero", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.position_anim()
            .key(0,  Vec3{0, 0, 0})
            .key(60, Vec3{120, 0, 0});
    });
    Scene scene = sb.build();
    REQUIRE(scene.layers().size() == 1);
    // At frame 30, halfway → position.x == 60
    CHECK(scene.layers()[0].transform.position.x == doctest::Approx(60.0f));
}

TEST_CASE("LayerBuilder: opacity_anim bakes into transform at frame") {
    FrameContext ctx{.frame = 15, .resource = std::pmr::get_default_resource()};
    SceneBuilder sb(ctx);
    sb.layer("fade", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.opacity_anim()
            .key(0,  0.0f)
            .key(30, 1.0f);
    });
    Scene scene = sb.build();
    REQUIRE(scene.layers().size() == 1);
    CHECK(scene.layers()[0].transform.opacity == doctest::Approx(0.5f));
}

TEST_CASE("LayerBuilder: static position() still works when no anim keys") {
    LayerBuilder b("layer");
    b.rect("r", {.size = {10, 10}});
    b.position({100, 200, 0});
    Layer l = b.build();
    CHECK(l.transform.position.x == doctest::Approx(100.0f));
    CHECK(l.transform.position.y == doctest::Approx(200.0f));
}

TEST_CASE("LayerBuilder: scale_anim bakes into transform") {
    FrameContext ctx{.frame = 30, .resource = std::pmr::get_default_resource()};
    SceneBuilder sb(ctx);
    sb.layer("s", [](LayerBuilder& b) {
        b.rect("r", {.size = {10, 10}});
        b.scale_anim()
            .key(0,  Vec3{1, 1, 1})
            .key(60, Vec3{2, 2, 2});
    });
    Scene scene = sb.build();
    CHECK(scene.layers()[0].transform.scale.x == doctest::Approx(1.5f));
}
```

- [ ] **Step 2: Register in CMakeLists**

In `tests/CMakeLists.txt`, in the `chronon3d_scene_tests` source list, add:

```cmake
    scene/test_layer_builder_animated.cpp
```

- [ ] **Step 3: Run to confirm it fails**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
```

Expected: compile error — `position_anim`, `opacity_anim`, `scale_anim` not declared.

- [ ] **Step 4: Add declarations to `layer_builder.hpp`**

In `include/chronon3d/scene/builders/layer_builder.hpp`, add to the private section:

```cpp
private:
    Layer m_layer;
    Frame m_current_frame{0};  // ADD THIS
```

Add to the public section (after `LayerBuilder& opacity(f32 value);`):

```cpp
    // Animated property tracks — return ref so callers can chain .key()
    AnimatedValue<Vec3>& position_anim();
    AnimatedValue<Vec3>& scale_anim();
    AnimatedValue<Vec3>& rotate_anim();   // euler degrees, baked to Quat in build()
    AnimatedValue<Vec3>& anchor_anim();
    AnimatedValue<f32>&  opacity_anim();
```

Change the constructor declaration:

```cpp
    explicit LayerBuilder(std::string name,
                          Frame current_frame = 0,
                          std::pmr::memory_resource* res = std::pmr::get_default_resource());
```

Add includes at top of header:

```cpp
#include <chronon3d/animation/animated_value.hpp>
#include <chronon3d/animation/animated_transform.hpp>
```

- [ ] **Step 5: Implement in `src/scene/layer_builder.cpp`**

Change constructor at line 11:

```cpp
LayerBuilder::LayerBuilder(std::string name, Frame current_frame, std::pmr::memory_resource* res)
    : m_layer(res), m_current_frame(current_frame) {
    m_layer.name = std::pmr::string{name, res};
}
```

Add the new accessor implementations (before the existing `build()` at the end):

```cpp
AnimatedValue<Vec3>& LayerBuilder::position_anim() { return m_layer.anim_transform.position; }
AnimatedValue<Vec3>& LayerBuilder::scale_anim()    { return m_layer.anim_transform.scale; }
AnimatedValue<Vec3>& LayerBuilder::rotate_anim()   { return m_layer.anim_transform.rotation_euler; }
AnimatedValue<Vec3>& LayerBuilder::anchor_anim()   { return m_layer.anim_transform.anchor; }
AnimatedValue<f32>&  LayerBuilder::opacity_anim()  { return m_layer.anim_transform.opacity; }
```

Update `build()` to merge animated values (add after the `depth_role` block, before `return`):

```cpp
Layer LayerBuilder::build() {
    if (m_layer.depth_role != DepthRole::None) {
        m_layer.transform.position.z =
            DepthRoleResolver::z_for(m_layer.depth_role) + m_layer.depth_offset;
    }
    // Bake animated transform: merge animated axes into static transform.
    // Animated tracks override static setters only for the axes that have keys.
    if (m_layer.anim_transform.is_animated()) {
        Transform baked = m_layer.anim_transform.evaluate(m_current_frame);
        if (m_layer.anim_transform.position.is_animated())
            m_layer.transform.position = baked.position;
        if (m_layer.anim_transform.rotation_euler.is_animated())
            m_layer.transform.rotation = baked.rotation;
        if (m_layer.anim_transform.scale.is_animated())
            m_layer.transform.scale = baked.scale;
        if (m_layer.anim_transform.anchor.is_animated())
            m_layer.transform.anchor = baked.anchor;
        if (m_layer.anim_transform.opacity.is_animated())
            m_layer.transform.opacity = baked.opacity;
    }
    return std::move(m_layer);
}
```

- [ ] **Step 6: Run tests**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests -j8
ctest --test-dir build/chronon/win-debug -R chronon3d_scene_tests --output-on-failure
```

Expected: all 4 new animated tests pass; existing scene tests unaffected.

- [ ] **Step 7: Commit**

```
git add include/chronon3d/scene/builders/layer_builder.hpp
git add src/scene/layer_builder.cpp
git add tests/scene/test_layer_builder_animated.cpp
git add tests/CMakeLists.txt
git commit -m "feat(layer): add position/opacity/scale/rotate/anchor _anim() to LayerBuilder"
```

---

## Task 4: `SceneBuilder` — pass `current_frame_` to `LayerBuilder`

**Files:**
- Modify: `include/chronon3d/scene/builders/scene_builder.hpp`

The `SceneBuilder` layer templates construct `LayerBuilder` without passing a frame. Update all five variants.

- [ ] **Step 1: Update all templates in `scene_builder.hpp`**

In `include/chronon3d/scene/builders/scene_builder.hpp`, find each `LayerBuilder builder(std::move(name), scene_.resource());` call and change to:

```cpp
LayerBuilder builder(std::move(name), current_frame_, scene_.resource());
```

There are 5 occurrences: `layer`, `adjustment_layer`, `null_layer`, `precomp_layer`, `video_layer`. Change all 5.

- [ ] **Step 2: Verify all tests still pass**

```
cmake --build build/chronon/win-debug --target chronon3d_scene_tests chronon3d_renderer_tests -j8
ctest --test-dir build/chronon/win-debug -R "chronon3d_scene_tests|chronon3d_renderer_tests" --output-on-failure
```

Expected: all tests pass including the new animated ones.

- [ ] **Step 3: Full test suite**

```
cmake --build build/chronon/win-debug --target chronon3d_tests -j8
ctest --test-dir build/chronon/win-debug --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```
git add include/chronon3d/scene/builders/scene_builder.hpp
git commit -m "feat(scene): pass current_frame to LayerBuilder in all SceneBuilder layer variants"
```
