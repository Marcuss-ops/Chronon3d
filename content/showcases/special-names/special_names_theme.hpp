#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// SpecialNames Theme — minimal palette + sample name.
// All "complexity" lives in the per-composition file (one motion per comp).
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/scene/builders/scene_builder.hpp>

namespace chronon3d::content::special_names {

// ── Palette ─────────────────────────────────────────────────────────────────
inline constexpr Color NAME_TEXT      = {0.98f, 0.96f, 0.92f, 1.0f};   // warm white
inline constexpr Color NAME_TEXT_GOLD = {1.00f, 0.86f, 0.45f, 1.0f};   // gold

// ── Sample name shown in every composition ─────────────────────────────────
// Single source of truth.  Editing DEMO_NAME also requires keeping
// DEMO_NAME_LEFT and DEMO_NAME_RIGHT consistent (left + " " + right must
// equal the full name) — the Split composition reads the halves directly.
inline constexpr const char* DEMO_NAME       = "ALEX MORGAN";
inline constexpr const char* DEMO_NAME_LEFT  = "ALEX";
inline constexpr const char* DEMO_NAME_RIGHT = "MORGAN";

} // namespace chronon3d::content::special_names
