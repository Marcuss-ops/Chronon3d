// ═══════════════════════════════════════════════════════════════════════════
// chronon3d::authoring — fluent DSL façade over Chronon3D Spec types.
//
// This namespace hosts public-facing authoring builders that produce the
// canonical Spec types (`TextAnimatorSpec`, `GlyphSelectorSpec`, …) the
// engine consumes. The Spec types stay in `chronon3d::*` as the internal
// Intermediate Representation; the authoring layer simply rewrites user
// code from "spec-by-hand" into a fluent, Remotion-style API.
//
// PR 1 ships:
//   - `Animator`        (TextAnimatorSpec builder)
//   - `Selector`        (GlyphSelectorSpec builder)
//
// PR 2 ships:
//   - `Material`        (TextMaterial wrapper, fluent bevel/glow/shadow/
//                        gradient/inner_shadow/highlight/emissive)
//   - `material::*`    factories (premium/neon/glass/flat)
//
// PR 5 ships (promoted ahead of PR 3+4 per user direction):
//   - `StyleRegistry`  (string key → TextStyle, e.g.
//                       "youtube.hero.premium")
//   - `MotionRegistry` (string key → TextAnimatorSpec, e.g.
//                       "text.reveal.soft")
//
// PR 3 ships:
//   - `Layer`  (thin handle over `LayerBuilder`; exposes `.text(content)`)
//   - `Text`   (thin handle over `PendingTextRun`; mutates `spec.text`
//               directly; consumes `Material` / `Animator` builders via
//               their `release()` friends, and `StyleRegistry` / `MotionRegistry`
//               via `resolve()` lookups; offers `configure_core(Fn)` as a
//               Level-3 escape hatch).
//
// PR 3.5 ships (ambient-resolution round, after PR 3 review feedback):
//   - `chronon3d::ExtensionContext` extended with optional `StyleRegistry*`
//     + `MotionRegistry*` pointer fields (default nullptr; existing 12+
//     construction sites unchanged).
//   - `LayerBuilder::extension_context(...)` setter + getter.
//   - `Layer::text(...)` pins the registries from the parent
//     `LayerBuilder`'s `ExtensionContext` into the returned `Text` handle.
//   - New ambient-resolution methods on `Text`: `.style(id)` and
//     `.motion(id)` resolve against the pinned registries, no-op when
//     the relevant pointer is null.  Existing explicit variants
//     `.style(id, registry)` and `.motion(id, registry)` unchanged.
//
// PR 4 ships (top-of-tree composition factory):
//   - `chronon3d::authoring::Scene`  (thin handle over `SceneBuilder`;
//     SFINAE-dispatched `.layer(name, fn)` accepts EITHER a closure
//     taking `Layer&` (PR 3 surface — wraps the SceneBuilder-spawned
//     LayerBuilder) OR a closure taking `LayerBuilder&` (engine raw
//     surface — passthrough); other verbs (camera, stagger, sequence,
//     apply_lighting_rig, shape primitives) reachable via
//     `.configure_core(Fn)`).
//   - `chronon3d::authoring::CompositionBuilder`  (fluent spec-builder
//     with `.name/.width/.height/.duration/.frame_rate/.assets_root`
//     setters + `.scene(fn)` render-fn setter + `.custom_builder(fn)`
//     injection point for callers needing non-default SceneBuilder +
//     `.build()` terminal that returns the engine
//     `chronon3d::Composition` directly so the registry is unchanged).
//   - Free `composition()` factory returns an empty CompositionBuilder
//     — mirrors the engine's `chronon3d::composition(...)` with a
//     different return type.
//
// Future PRs planned in `docs/FOLLOWUP_TICKETS.md`:
//   - PR 6  Migration of two example compositions
//   - PR 7  Layer shape surface (Layer::rect/path/star/glow/...)
//
// ── Anti-duplication contract ───────────────────────────────────────────
//
// Each builder's `release()` is a private `&&` overload, callable only by
// `friend` declarations in the same translation unit. The consumer of a
// builder is fixed at compile time: `Animator::release()` is consumed by
// `Text::animate()` (PR 3); `Selector::release()` is consumed by
// `Animator::select()`. Reader code in the authoring layer NEVER spells
// the Spec types directly — they remain opaque to user code.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// ─────────────────────────────────────────────────────────────────────────
// Currently usable — PR 1 (Animator, Selector) + PR 2 (Material)
// + PR 3 (Layer, Text) + PR 3.5 (ambient-ExtensionContext resolution) +
//   PR 4 (Scene, Composition) + PR 5 (StyleRegistry, MotionRegistry).
// Future PRs will add layer shape primitives (PR 7) and migration of
// example compositions (PR 6); meanwhile, user code that includes only
// this umbrella header gets the full authoring façade.
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/authoring/selector.hpp>
#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/authoring/layer.hpp>
#include <chronon3d/authoring/scene.hpp>
#include <chronon3d/authoring/composition.hpp>
