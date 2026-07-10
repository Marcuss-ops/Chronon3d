// ═══════════════════════════════════════════════════════════════════════════
// chronon3d/render.hpp — D2: canonical rendering umbrella.
//
// One-stop include for rendering.  Covers:
//   - SoftwareRenderer & backends
//   - RenderSettings & RenderJob (D1)
//   - SDK render engine (public API)
//   - Render pipeline & graph
//   - Image I/O
//
// Prefer this over the deprecated <chronon3d/runtime.hpp> umbrella.
// ═══════════════════════════════════════════════════════════════════════════

#pragma once

// ── SDK public surface ──────────────────────────────────────────────
#include <chronon3d/sdk/render_engine.hpp>
#include <chronon3d/sdk/render_output.hpp>
#include <chronon3d/sdk/render_error.hpp>
#include <chronon3d/sdk/render_request.hpp>
#include <chronon3d/sdk/render_settings.hpp>

// ── Renderer ────────────────────────────────────────────────────────
#include <chronon3d/backends/software/software_renderer.hpp>
#include <chronon3d/backends/software/render_settings.hpp>

// ── Render pipeline ─────────────────────────────────────────────────
#include <chronon3d/render_graph/pipeline/render_pipeline.hpp>
#include <chronon3d/runtime/render_pipeline.hpp>

// ── RenderJob (D1) ──────────────────────────────────────────────────
#include <chronon3d/timeline/render_job.hpp>

// ── Image I/O ───────────────────────────────────────────────────────
#include <chronon3d/backends/image/image_writer.hpp>

// ── Core render types ───────────────────────────────────────────────
#include <chronon3d/core/memory/framebuffer.hpp>

// NOTE: SoftwareBackend and NodeCache are internal types — include
// <chronon3d/advanced.hpp> if you need them.
