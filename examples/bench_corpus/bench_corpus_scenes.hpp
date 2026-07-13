// ═══════════════════════════════════════════════════════════════════════════
// bench_corpus_scenes.hpp — 12-scene benchmark corpus for Chronon3D
//
// TICKET-BENCH-CORPUS-V1 — official Chronon benchmark corpus (12 scenes,
// B00-B11).  Each scene is a pure `Composition` factory function that
// returns a Composition evaluated deterministically at any Frame.
//
// Architecture (per ADR-016 single-source-of-truth):
//   * NO new SDK API surface.  All 12 factories reuse the existing
//     `chronon3d::composition(spec, render_fn)` entry point, the
//     `SceneBuilder` / `LayerBuilder` authoring surface, and the
//     `chronon3d::scene_presets::detail::default_font()` /
//     `text_preset(...)` helpers.
//   * Each factory is inline (header-only) so the linker folds them in
//     individually per call site — no duplicate factory lambdas
//     (Cat-3 anti-dup invariant).
//   * Registration bridge: `register_bench_corpus_compositions()` in
//     `register_bench_corpus.cpp` is the SINGLE canonical registration
//     site for the 12 compositions (mirrors `register_builtin_
//     compositions` canonical entry).
//
// CLI consumption:
//   chronon3d_cli bench BenchB00_EmptyFrame
//   chronon3d_cli video BenchB03_CinematicGlow1080p --start 0 --end 90 --fps 30
//
// Bench corpus spec (F1.1 / docs/PERFORMANCE_BOTTLENECKS.md):
//   B00 EmptyFrame              — overhead of framework alone
//   B01 StaticText1080p         — shaping + rasterization + cache (static)
//   B02 Typewriter200Glyphs     — 200-glyph TextRun with per-character anim
//   B03 CinematicGlow1080p      — glow + shadow + additive blend (canonical)
//   B04 Layers100               — 100 layers with opacity/position/scale anim
//   B05 Blur4K                  — memory-bandwidth intensive (4K box blur)
//   B06 VideoOverlay1080p       — decode + compositing + text + encode
//   B07 NestedPrecomps          — graph + cache + dependency handling
//   B08 DirtyRectSmallMotion    — small-region-changing scene (dirtyrect)
//   B09 LongForm10Minutes       — 18000-frame stability / cache pressure
//   B10 RandomFrameAccess       — non-sequential frame index access
//   B11 Portrait1080x1920       — vertical 1080x1920 portrait pipeline
//
// Determinism: each factory closes over NO time-dependent state; the
// SceneFunction evaluator is pure (FrameContext in, Scene out) so the
// 11/11 baseline suite tests run bit-identical across invocations.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/core/types/frame_context.hpp>

namespace chronon3d::bench_corpus {

// ── B00 — EmptyFrame ────────────────────────────────────────────────────
// Measures pure framework overhead. 1920×1080 single-frame composition
// with NO layers; the renderer still walks the empty Layer list, builds
// the RenderGraph, runs the graph solver, allocates a clear-black
// framebuffer, and outputs. Use as the floor-noise reference for every
// other scene in the corpus.
Composition bench_b00_empty_frame();

// ── B01 — StaticText1080p ──────────────────────────────────────────────
// 1920×1080 scene with ONE static centered TextRun (font_size=128,
// weight=900, white). Measures shaping + rasterization + text-cache hit
// rate across frames 0..90. No animation; expected p50 to be flat.
Composition bench_b01_static_text_1080p();

// ── B02 — Typewriter200Glyphs ──────────────────────────────────────────
// 1920×1080 scene with a 200-glyph TextRun using TextAnimator
// (mode=ByCharacter, delay_per_unit=2 frames, duration=20 frames, cubic
// easing). Frames 0..90 cover the full typewriter reveal. Measures
// per-glyph shaping + per-character animation cost in the worst-case
// "many shapes per frame" regime.
Composition bench_b02_typewriter_200_glyphs();

// ── B03 — CinematicGlow1080p ────────────────────────────────────────────
// Canonical user-spec cinematic-glow landscape. Reuses the canonical
// ChrononGlowFinalAE factory (chronon3d::content::glow_final), so the
// benchmark result IS the production-quality measurement for this scene.
// Wrap content in a Composition for the registry add() signature.
Composition bench_b03_cinematic_glow_1080p();

// ── B04 — Layers100 ────────────────────────────────────────────────────
// 1920×1080 scene with 100 individual layers, each a scaled+rotated
// rounded_rect with independent opacity animation (Frame{0}=0.0,
// Frame{60}=1.0, OutCubic). Measures LayerList build + dirty-rect
// aggregation + per-layer transform evaluation.
Composition bench_b04_layers_100();

// ── B05 — Blur4K ────────────────────────────────────────────────────────
// 3840×2160 (4K) scene with a single full-frame blurred rect
// (LayerBuilder::blur(48.0f) on a 4K-sized layer). Pure memory-
// bandwidth stress; expected alloc-free after warmup if the kernel
// cache reuses the temp buffers.
Composition bench_b05_blur_4k();

// ── B06 — VideoOverlay1080p ─────────────────────────────────────────────
// 1920×1080 scene with a static 1920×1080 fallback image
// (assets/images/camera_reference.jpg) + a centered TextRun overlay
// (font_size=64, white). When the optional mp4 asset
// (assets/videos/dummy.mp4) exists, the bench runner substitutes it;
// otherwise the static fallback exercises the same decode+composite
// path without the codec dependency. Measures decode + compositing +
// encode joint path.
Composition bench_b06_video_overlay_1080p();

// ── B07 — NestedPrecomps ───────────────────────────────────────────────
// 1920×1080 scene with 3 nesting levels (precomp containing precomp
// containing precomp), each level containing 5 rect+text primitives
// with slight opacity stagger (Frame{0+10*i}=1.0). Measures graph
// dependency resolution + per-precomp cache evict policy under nested
// compilation units.
Composition bench_b07_nested_precomps();

// ── B08 — DirtyRectSmallMotion ─────────────────────────────────────────
// 1920×1080 scene with a single small (240×160) rect at center+offset,
// fading in then sliding down over frames 0..90. Everything outside
// the dirty rect stays IDENTICAL across frames — the canonical case
// where dirty_rect should drop full-frame passes to bbox-only. Measures
// dirtyrect efficiency = full_frame_passes_per_frame / 1 (≈1 when
// dirtyrect works correctly, ≈N when it doesn't).
Composition bench_b08_dirty_rect_small_motion();

// ── B09 — LongForm10Minutes ────────────────────────────────────────────
// 1920×1080 scene with a simple static background + 1 fading text
// layer at 30 fps. duration = 18000 frames (10 minutes). Measure steady-
// state cache pressure + RAM growth across a long render. No per-camera
// motion; the longform test is about leak detection + allocator
// fragmentation in the kernel hot path.
Composition bench_b09_long_form_10_minutes();

// ── B10 — RandomFrameAccess ────────────────────────────────────────────
// 1920×1080 static composition (no animation), accessible from any
// Frame index. The bench runner invokes `chronon3d_cli video` with
// a non-contiguous `--frames` argument (e.g. "0,17,42,99,180,271,360")
// to measure indexing cost + frame cache coherence under random access.
Composition bench_b10_random_frame_access();

// ── B11 — Portrait1080x1920 ────────────────────────────────────────────
// Vertical 1080×1920 scene (portrait orientation). Reuses the canonical
// ChrononGlowFinalAE portrait factory, so the benchmark result IS the
// production-quality measurement for portrait phone-form-factor output.
// Wrap content in a Composition for the registry add() signature.
Composition bench_b11_portrait_1080x1920();

// ── Registry entry point ─────────────────────────────────────────────────
// Single function: register all 12 scenes into a CompositionRegistry.
// Mirrors chronon3d::register_builtin_compositions(...) canonical
// entry; called from apps/chronon3d_cli/register_content_compositions.cpp
// (the existing canonical composition-bridge site).
void register_bench_corpus_compositions(CompositionRegistry& registry);

} // namespace chronon3d::bench_corpus
