// -----------------------------------------------------------------------
// content/backgrounds/grid_clean.hpp
//
// Forward declaration for the GridCleanBackground product composition
// registration.  Moved here per the original spec of proposta #1 so the
// opinionated product-grade composition lives in content/ while the
// underlying rasterization utility (PNG-cached grid image) stays in core.
// -----------------------------------------------------------------------

#pragma once

namespace chronon3d::content::backgrounds {

/// Register the GridCleanBackground composition.
/// Safe to call multiple times (idempotent).
void register_grid_clean_background();

} // namespace chronon3d::content::backgrounds
