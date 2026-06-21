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
// Future PRs planned in `docs/FOLLOWUP_TICKETS.md`:
//   - PR 4  `Scene`/`Composition` (factory wrappers)
//   - PR 6  Migration of two example compositions
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
// + PR 3 (Layer, Text) + PR 5 (StyleRegistry, MotionRegistry).
// Scene and Composition are NOT YET available (PR 4, 6).
// This header WILL grow as subsequent PRs land; meanwhile, user
// code that includes only this umbrella header can use ONLY the
// builders + registries + handles below.
// ─────────────────────────────────────────────────────────────────────────

#include <chronon3d/authoring/selector.hpp>
#include <chronon3d/authoring/animator.hpp>
#include <chronon3d/authoring/material.hpp>
#include <chronon3d/authoring/style_registry.hpp>
#include <chronon3d/authoring/motion_registry.hpp>
#include <chronon3d/authoring/text.hpp>
#include <chronon3d/authoring/layer.hpp>
