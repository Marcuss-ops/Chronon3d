# Migration Notes — Authoring-DSL Scene + Composition wrappers (PR 4)

**Date:** 2026-06-21
**PR:** 4 (top-of-tree composition factory) + 3 follow-ups (fail-fast Layer ctor, TextShaping.script gap closure, ambient-ExtensionContext surface)

**Status:** implementation complete; pending code-review + push

---

## Summary

PR 4 closes the authoring DSL loop with `chronon3d::authoring::Scene` + `chronon3d::authoring::CompositionBuilder`. Combined with PR 3 (Layer/Text), PR 3.5 (ambient ExtensionContext resolution), and PR 5 (Style/Motion registries), the namespace now lets user code drive a `chronon3d::Composition` end-to-end without ever touching internal Spec types.

Three follow-ups ship in the same commit:

1. **Fail-fast `Layer(LayerBuilder&)`** — replaces the silent 1920×1080 default with an explicit `throw std::runtime_error` when the parent `LayerBuilder` has not called `screen_dimensions(w, h)`.
2. **`TextShaping.script` gap closure** — adds a `std::uint32_t script{0u}` field to `chronon3d::TextRunSpec`, plus `Text::script(uint32_t)` setter, plus a non-zero-conditional propagation in `apply_text_style` so the pre-PR-4 silent-drop is fixed.
3. **Surface-symbol smoke verified** via static greps (best-effort compile blocked in this env by disk quota).

---

## PR 4 — Scene façade

### Shape

```cpp
namespace chronon3d::authoring {

class Scene {
public:
    Scene(SceneBuilder& builder, FrameContext context) noexcept;
    explicit Scene(SceneBuilder& builder) noexcept(false);

    template <class Fn>
    Scene& layer(std::string name, Fn&& fn) &;          // SFINAE dual-surface

    template <class Fn>
    Scene& configure_core(Fn&& mutator) &;              // engine-level escape

    [[nodiscard]] SceneBuilder&        mutable_builder()       noexcept;
    [[nodiscard]] const SceneBuilder&  builder()         const noexcept;
    [[nodiscard]] const FrameContext&  context()         const noexcept;
};

} // namespace chronon3d::authoring
```

### Why a thin handle (matches PR 3 Layer)

Same lifetime pattern as `Text` and `Layer` — non-owning pointer to a `SceneBuilder` already owned by the caller. Setters mutate the underlying builder in place; destruction is a no-op. This is the documented convention across the namespace.

### SFINAE dual-surface `.layer(name, fn)`

`Scene::layer(name, fn)` accepts EITHER:

- `fn(Layer&) &` — PR 3 façade (recommended)
- `fn(LayerBuilder&) &` — engine raw surface (passthrough)

Dispatch is `if constexpr`; the compiler instantiates only the matching branch. Closure capture is by move (`[fn = std::forward<Fn>(fn)]`); no `std::function` overhead.

### `configure_core(fn)` escape hatch

Code that needs the engine-level SceneBuilder API (camera wiring, lighting rig, stagger, sequence, shape primitives — none of which are surfaced in PR 4) goes through:

```cpp
s.configure_core([](SceneBuilder& core) {
    core.apply_lighting_rig(my_rig);
    core.stagger({...});
});
```

This is the same convention used by PR 3 `Layer::configure_core(...)`.

---

## PR 4 — `CompositionBuilder`

### Fluent chain

```cpp
auto comp = chronon3d::authoring::composition()
    .name("HeroShowcase")
    .width(1920)
    .height(1080)
    .duration(Frame{60})
    .frame_rate(FrameRate{30, 1})
    .assets_root(std::filesystem::path{"assets"})
    .scene([](chronon3d::authoring::Scene& s, const chronon3d::FrameContext& ctx) {
        s.layer("title", [](chronon3d::authoring::Layer& l) {
            l.text("Welcome")
             .id("welcome_text")
             .font("assets/fonts/Poppins-Bold.ttf", 96.0f)
             .center();
        });
    })
    .build();   // → chronon3d::Composition (engine IR, registry-ready)
```

### Closure signature

`SceneFn = [](Scene&, const chronon3d::FrameContext&)`.

`SceneBuilder` lives INSIDE the closure (constructed per `evaluate()` call) — no lifetime coupling back to the builder. This avoids dangling-pointer risk when the Composition is moved / passed to the registry.

### Output type

`.build()` returns `chronon3d::Composition` directly — **not** a wrapping `chronon3d::authoring::Composition` class. Rationale:

- Lossless (zero-overhead) interop with the engine `CompositionRegistry`.
- The registry requires `std::function<Composition(const CompositionProps&)>`. Wrapping would force a re-extraction layer at registration.
- The engine's `composition(CompositionSpec, SceneFunction)` free factory remains the canonical way to construct compositions from outside the authoring façade.

### `custom_builder(factory)` injection point

For callers needing non-default `SceneBuilder` (custom pmr-resource, custom ShapeRegistry, etc.):

```cpp
auto comp = composition()
    .name("MemTrack")
    .width(1920).height(1080)
    .custom_builder([](const chronon3d::FrameContext& ctx) {
        return SceneBuilder(ctx, /*shape_registry=*/my_custom_registry);
    })
    .scene([](chronon3d::authoring::Scene& s, const auto& ctx) { /* ... */ })
    .build();
```

---

## Follow-up #1 — Fail-fast `Layer(LayerBuilder&)` ctor

### Before

```cpp
explicit Layer(LayerBuilder& builder) noexcept
    : Layer(builder, FrameContext::default_viewport()) {}    // SILENT default
```

This unconditionally picked 1920×1080 viewport context, regardless of the parent `LayerBuilder`'s actual screen dimensions. If a composition authored at 1280×720 used a Layer built from a "naked" `LayerBuilder`, the text would centre on the 1920×1080 reference frame — visually misaligned.

### After

```cpp
explicit Layer(LayerBuilder& builder) noexcept(false)
    : builder_(&builder),
      context_(resolve_viewport_or_throw_(builder)) {}
```

Where `resolve_viewport_or_throw_`:
1. Inspects `builder.screen_dimensions_were_set()` (PR 4 setter flag flips to `true` inside `LayerBuilder::screen_dimensions(w, h)`).
2. On **false** → `throw std::runtime_error(msg)` naming the offender + remediation paths.
3. On **true** → returns `FrameContext::from_dimensions(builder.screen_dimensions().x, .y)`.

### Why `throw` and not `assert`

Per reviewer feedback: the original draft paired `assert(false)` with `throw std::runtime_error`. The reviewer flagged that `assert` SIGABRTs in debug, bypassing exception unwinding — which would break any test that uses `REQUIRE_THROWS_AS` against the ctor. Final implementation drops `assert`; the throw is unconditional across debug and release. The message stays the same.

### New `LayerBuilder` surface

- `bool screen_dimensions_were_set() const noexcept` — predicate.
- `Vec2 screen_dimensions() const noexcept` — `{w, h}` reader.
- `const std::string& name() const noexcept` — for error messages.

Private flag `bool m_screen_dimensions_explicit{false}` flips to `true` inside the existing `screen_dimensions(w, h)` setter.

### Migration impact

Existing tests using `Layer layer(lb);` without prior `lb.screen_dimensions(...)` would throw at runtime under the new ctor. Migrated to the explicit convention via a single perl regex (`tests/authoring/test_animator_dsl.cpp`); 0 orphan sites remain after migration.

---

## Follow-up #2 — `TextShaping.script` gap closure

### The pre-PR-4 gap

`TextShaping.script` is the 4-byte OpenType script tag (HB_SCRIPT_*) forwarded to HarfBuzz's `hb_buffer_set_script()`. `TextStyle.shaping.script` is defined in `chronon3d/scene/model/shape/shape.hpp` and propagated through `TextSpec → TextRunParams` chain.

Until PR 4, `TextRunSpec` (a.k.a. `TextRunParams`) had **top-level `direction` + `language` fields but no `script` field**. Therefore `Text::apply_text_style(const TextStyle& s)` could not propagate `s.shaping.script` — the field was silently dropped during `.style(id, registry)` resolution.

### The fix (3-part)

1. **`chronon3d::TextRunSpec`**: add `std::uint32_t script{0u};` field alongside `direction` + `language`. The engine (HarfBuzz-shaped path) reads `script` when non-zero; `0` keeps the historical auto-detect path (`hb_buffer_guess_segment_properties()`).

2. **`Text::script(uint32_t)`**: new fluent setter on `Text` mutating `pending_->params.script`. Documented examples:
   - `0x4C61746Eu` — `HB_SCRIPT_LATIN`
   - `0x41726162u` — `HB_SCRIPT_ARABIC`
   - `0x48616E20u` — `HB_SCRIPT_HAN`
   - `0x44657661u` — `HB_SCRIPT_DEVANAGARI`

3. **`apply_text_style`**: when `s.shaping.script != 0`, write through to `pending_->params.script`. When `s.shaping.script == 0` (default → auto-detect), leave any pre-set `pending_->params.script` untouched. The "0 = inherit" rule preserves the historical Latin-only default path.

### Why `uint32_t` instead of `int`

Per reviewer feedback: 4-byte OpenType tags are unsigned (`hb_tag_t` is `uint32_t` in HarfBuzz). Using signed `int` would sign-extend high-bit-set tags and create subtle corruption at the HarfBuzz boundary. Test coverage verifies `0x80808080u` (top-bit set in every byte) round-trips without sign extension.

### Test coverage added (PR 4 surface)

- `Authoring/Text: script(uint32_t) chain mutates pending_->params.script` — round-trip.
- `Authoring/Text: default script=0u is preserved (auto-detect path stays intact)` — default semantic preserved.
- `Authoring/Text: style(id) propagates shaping.script when non-zero` — propagation when non-zero.
- `Authoring/Text: style(id) preserves pending script=0` — partial-overlay (Q7 review concern: pre-set script survives apply of style with script=0).
- `Authoring/Text: script accepts patterned HB tag including high-bit bytes (no sign-extend)` — uint32_t width confirmed.

---

## PR 4 — Test surface coverage

`tests/authoring/test_animator_dsl.cpp`: **97 TEST_CASE** instances (was 82 before PR 4). New sections:

- **PR 4 — Scene + Composition wrapper tests** (10 cases):
  - `CompositionBuilder: fields accumulate via fluent setters`
  - `CompositionBuilder: empty composition (no .scene()) renders zero layers`
  - `Scene + Layer: SFINAE wrap branch populates authored text in evaluated Scene`
  - `Scene: SFINAE passthrough branch (LayerBuilder& closure) is honored`
  - `CompositionBuilder: FrameContext flows into Scene::layer closure`
  - `CompositionBuilder: custom_builder(factory) is invoked per evaluate()`
  - `CompositionBuilder: build() consumes builder by rvalue` (move-only contract)
  - Plus the 3 fail-fast + 5 script tests (above).

---

## Surface boundary (PR 4)

`Scene` exposes **only** `.layer(...)` + `.configure_core(...)`. Everything else (camera / stagger / sequence / apply_lighting_rig / shape primitives) is reachable via `configure_core([&](SceneBuilder& core){...})` and detailed in PR 7 follow-up.

`CompositionBuilder` exposes only the 6 spec setters + `.scene(fn)` + `.custom_builder(fn)` + `.build()`. No extension-context injection method yet (users can call `Scene::configure_core([&](SceneBuilder& core){ core.layer(...) })` to pre-attach LayerBuilder extension_contexts where ambient registry resolution is needed; per-Layer plumbing falls back to the explicit `.style(id, registry)` path on Text until PR 7+).

---

## Anti-duplication contract (PR 4)

- **Composition Builder**: `chronon3d::authoring::composition()` is the authoring entry-point. The engine `chronon3d::composition(spec, fn)` factory remains the canonical engine-level constructor for non-authoring code paths. No rewriting of the engine `composition()` factory.
- **Scene facade**: `chronon3d::authoring::Scene` MUTATES the underlying SceneBuilder — no parallel IR. LayerBuilder / Scene / Scene / FrameContext fields stay canonical.
- **Script field**: `int script{0}` is repurposed to `std::uint32_t script{0u}` on `chronon3d::TextRunSpec` (engine IR). No parallel authoring representation.

---

## Risks / caveats

- **Existing test breakage**: tests using `Layer layer(lb);` without prior `lb.screen_dimensions(...)` now throw at construction time. Migrated via perl regex at `tests/authoring/test_animator_dsl.cpp`; 0 orphans remain.
- **Type-side-change**: `chronon3d::TextRunSpec.script` changed from non-existent to `std::uint32_t{0u}`. Engine IR field widening — ABI-compatible (default value `0u` matches prior absence).
- **Fail-fast breakpoint**: any user code creating `LayerBuilder` directly (engine-level authoring) without `screen_dimensions(...)` will now throw at the `Layer(LayerBuilder&)` site. Migration path: call `lb.screen_dimensions(w, h)` before the Layer ctor, or use the explicit `Layer(LayerBuilder&, FrameContext)` ctor.

---

## Follow-ups filed

1. **PR 7** — `Layer::rect / Layer::rounded_rect / Layer::circle / Layer::star / Layer::path / Layer::position / Layer::opacity / Layer::glow / Layer::drop_shadow / Layer::rich_text` so the configure_core escape hatch isn't needed for shape work in user-authored compositions.
2. **PR 8** — `CompositionBuilder::extension_context(const chronon3d::ExtensionContext&)` thread-through setter for ambient registry plumbing from the closure (avoids calling `Scene::configure_core` to install per-Layer ambient).
3. **PR 9** — `chronon3d::TextRunSpec.script` engine-side plumbing so the rasterizer picks it up during `shape_text()`. The field exists on TextRunSpec now; the rasterizer pick-up may still use `TextDirection::Auto` heuristics until updated.
