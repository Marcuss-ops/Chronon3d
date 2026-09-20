#pragma once

// ---------------------------------------------------------------------------
//  layout_flow_grid.hpp
//
//  Flex-style Flow container (linear row/column with optional wrap) and
//  uniform Grid container (rows × columns with gap). Both produce the same
//  Vec3 positioning primitive already consumed by `LayoutSolver::solve`.
//
//  Identification: layers in `Scene::layers()` are grouped by `group_id`
//  (string). Every layer whose `LayoutRules::flow` (or `.grid`) shares the
//  same `group_id` forms a single container, processed in scene order. A
//  layer not opting in to either is unaffected.
//
//  Cross-axis alignment is set on the container, not on individual children:
//    - `Start`     → packed at canvas cross-axis start (default)
//    - `Center`    → centered along the cross axis
//    - `End`       → packed at canvas cross-axis end
//    - `Stretch`   → reserved; same effect as Start in v1 (size-stretch not
//                    addressed here because Layer has no intrinsic size yet)
//
//  Cell size is uniform across the container because the existing layout
//  system does not measure per-layer intrinsic sizes. Callers should set
//  `cell_size` to the desired (W, H) per cell. If `cell_size` is zero,
//  the pass falls back to `canvas_size / N` so the layout is still
//  deterministic and visible.
// ---------------------------------------------------------------------------

#include <chronon3d/math/glm_types.hpp>
#include <chronon3d/core/types/types.hpp>

#include <string>

namespace chronon3d {

/// Direction of the main axis of a `LayoutFlow`.
/// `Row`    → main axis is horizontal (X); secondary is vertical (Y).
/// `Column` → main axis is vertical (Y);   secondary is horizontal (X).
enum class Axis {
    Row,
    Column,
};

/// Cross-axis alignment for items inside a `LayoutFlow` or `LayoutGrid`.
enum class AlignItems {
    Start,    // cross-axis packed at canvas start (Top for Row, Left for Column)
    Center,   // cross-axis centered
    End,      // cross-axis packed at canvas end (Bottom for Row, Right for Column)
    Stretch,  // reserved; in v1 behaves like Start (size-stretch unimplemented)
};

/// Distribution of free space along a flow's main axis.
///
/// This is the Flexbox-style `justify-content` counterpart to `AlignItems`.
/// When the line is wider than the available extent, the content remains
/// packed at Start and is never given a negative gap.
enum class JustifyContent {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

/// How a `LayoutGrid` grows when the item count exceeds the declared axes.
enum class GridFit {
    Auto,           // columns = `columns`; rows grows with item count
    FixedColumns,   // columns = `columns`; rows grows with item count
    FixedRows,      // rows = `rows`; columns grows with item count
};

/// Flex-style linear container.
///
/// Every layer in `Scene::layers()` whose `LayoutRules::flow` has the
/// matching `group_id` is laid out along `main_axis` with `gap` between
/// adjacent items. If `wrap` is true and the cumulative cell extent
/// exceeds the canvas main-axis length, the flow wraps onto the
/// secondary axis and continues.
struct LayoutFlow {
    Axis        main_axis{ Axis::Row };
    f32         gap{ 0.0f };
    bool        wrap{ false };
    AlignItems  align{ AlignItems::Start };
    JustifyContent justify{ JustifyContent::Start };
    std::string group_id{};
    Vec2        cell_size{ 0.0f, 0.0f };   // uniform (W,H) per cell; (0,0) ⇒ auto from canvas/N
};

/// Uniform grid container.
///
/// Every layer in `Scene::layers()` whose `LayoutRules::grid` has the
/// matching `group_id` flows row-major into a `columns × rows` grid with
/// `gap` between cells. `fit` controls how the grid expands when the
/// item count exceeds the declared axes.
///
/// Defensive default: `columns <= 0` or `rows <= 0` makes the pass a
/// no-op for that group (cannot be silently misconfigured into producing
/// a divide-by-zero).
struct LayoutGrid {
    int         columns{ 1 };
    int         rows{ 1 };
    Vec2        gap{ 0.0f, 0.0f };
    GridFit     fit{ GridFit::Auto };
    std::string group_id{};
    Vec2        cell_size{ 0.0f, 0.0f };
};

}  // namespace chronon3d
