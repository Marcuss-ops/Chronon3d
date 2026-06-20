#include <chronon3d/layout/layout_solver.hpp>
#include <algorithm>
#include <unordered_map>

namespace chronon3d {

Vec2 anchor_position(Anchor anchor, i32 w, i32 h, f32 margin) {
    const f32 W = static_cast<f32>(w);
    const f32 H = static_cast<f32>(h);
    const f32 m = margin;

    switch (anchor) {
        case Anchor::TopLeft:      return {m,         m};
        case Anchor::TopCenter:    return {W * 0.5f,  m};
        case Anchor::TopRight:     return {W - m,     m};
        case Anchor::MiddleLeft:   return {m,         H * 0.5f};
        case Anchor::Center:       return {W * 0.5f,  H * 0.5f};
        case Anchor::MiddleRight:  return {W - m,     H * 0.5f};
        case Anchor::BottomLeft:   return {m,         H - m};
        case Anchor::BottomCenter: return {W * 0.5f,  H - m};
        case Anchor::BottomRight:  return {W - m,     H - m};
        default:                   return {W * 0.5f,  H * 0.5f};
    }
}

Vec3 anchor_position(const AnchorPlacement& placement, i32 w, i32 h, f32 margin) {
    Vec2 base = anchor_position(placement.anchor, w, h, margin);
    return {
        base.x + placement.offset.x,
        base.y + placement.offset.y,
        placement.depth.has_value() ? *placement.depth : 0.0f
    };
}

namespace {

// ──────────────────────────────────────────────────────────────────────
//  Flow/Grid group collector
//
//  Walks `scene.layers()` in order and partitions layers whose
//  `LayoutRules::flow` or `LayoutRules::grid` is set into buckets keyed
//  by `group_id`. The first layer in a group determines the container
//  type (Flow vs Grid); mixed groups fall back to "skip that group"
//  (silent defensive default — neither container is applied).
// ──────────────────────────────────────────────────────────────────────
struct LayoutGroup {
    enum class Kind { Flow, Grid, Mixed };
    Kind kind{Kind::Flow};
    std::vector<Layer*> members;  // in scene order
};

std::unordered_map<std::string, LayoutGroup> collect_layout_groups(Scene& scene) {
    std::unordered_map<std::string, LayoutGroup> groups;
    for (auto& layer : scene.layers()) {
        const bool has_flow = layer.layout.flow.has_value();
        const bool has_grid = layer.layout.grid.has_value();

        // A layer that sets BOTH flow and grid is a defensive programmer-error
        // signal: the solver cannot honour both at once, so the entire group
        // for this `group_id` is flagged Mixed and silently bypassed in the
        // apply pass. Marked via the flow's id (the more typical primary
        // container); this layer itself is NOT added to the membership list.
        if (has_flow && has_grid) {
            const std::string& id = layer.layout.flow->group_id;
            groups[id].kind = LayoutGroup::Kind::Mixed;
            continue;
        }
        if (!has_flow && !has_grid) continue;

        const std::string& id = has_flow
            ? layer.layout.flow->group_id
            : layer.layout.grid->group_id;

        auto& g = groups[id];
        if (g.members.empty()) {
            g.kind = has_flow ? LayoutGroup::Kind::Flow : LayoutGroup::Kind::Grid;
        }
        g.members.push_back(&layer);
    }
    return groups;
}

// Resolve uniform cell size: (0,0) falls back to canvas / N. Guarantees
// non-zero on the divisor side so we never produce NaN positions.
inline Vec2 resolve_cell_size(const Vec2& declared, int count,
                              f32 canvas_w, f32 canvas_h) {
    if (declared.x > 0.0f && declared.y > 0.0f) return declared;
    if (count <= 1)  return {canvas_w, canvas_h};
    const f32 n = static_cast<f32>(count);
    return {canvas_w / n, canvas_h / n};
}

// Cross-axis CENTER offset for the cell on the cross axis.
// Differs from a pure top-left-of-cell offset by `extent * 0.5f`:
//   Start  → `extent / 2`          (cell top at 0; center at extent/2)
//   Center → `canvas_extent / 2`   (cell CENTERED on the canvas)
//   End    → `canvas_extent - extent/2`   (cell bottom against canvas)
//   Stretch→ `extent / 2`          (v1: same as Start, size-stretch TBD)
inline f32 align_offset(AlignItems align, f32 extent, f32 canvas_extent) {
    if (canvas_extent <= 0.0f) return extent * 0.5f;
    const f32 half = extent * 0.5f;
    switch (align) {
        case AlignItems::Start:   return half;
        case AlignItems::Center:  return canvas_extent * 0.5f;
        case AlignItems::End:     return canvas_extent - half;
        case AlignItems::Stretch: return half;  // v1: same as Start
    }
    return half;
}

// ── Apply a Flow group ────────────────────────────────────────────────
// Items are placed in scene order along main_axis with the given gap.
// If `wrap` is true and the next cell would exceed the canvas main-axis
// extent, we reset cursor and step onto the secondary axis (no clipping).
void apply_flow_group(std::vector<Layer*>& members,
                      const LayoutFlow& flow,
                      f32 canvas_w, f32 canvas_h,
                      std::vector<Vec3>& out_deltas) {
    const f32 W = canvas_w;
    const f32 H = canvas_h;
    const Vec2 cell = resolve_cell_size(flow.cell_size,
                                        static_cast<int>(members.size()),
                                        W, H);

    const f32 extent_main  = (flow.main_axis == Axis::Row) ? W : H;
    const f32 gap          = flow.gap;
    const f32 cell_main    = (flow.main_axis == Axis::Row) ? cell.x : cell.y;
    const f32 cell_cross   = (flow.main_axis == Axis::Row) ? cell.y : cell.x;

    f32 cursor = 0.0f;
    Vec2 line_offset{0.0f, 0.0f};
    int  in_line = 0;

    for (size_t i = 0; i < members.size(); ++i) {
        if (flow.wrap && cursor + cell_main > extent_main && in_line > 0) {
            // Wrap: step onto secondary axis, reset main cursor.
            if (flow.main_axis == Axis::Row) {
                line_offset.y += cell_cross + gap;
            } else {
                line_offset.x += cell_cross + gap;
            }
            cursor = 0.0f;
            in_line = 0;
        }

        // Main axis: cursor in pixels + half-cell to land the cell CENTER on main axis.
        // Cross axis: align_offset returns the cell CENTER coord directly.
        Vec2 pos = line_offset;
        if (flow.main_axis == Axis::Row) {
            pos.x += cursor + cell_main * 0.5f;
            pos.y += align_offset(flow.align, cell_cross, H);
        } else {
            pos.y += cursor + cell_main * 0.5f;
            pos.x += align_offset(flow.align, cell_cross, W);
        }

        Layer& l = *members[i];
        const Vec3 delta{
            pos.x - l.transform.position.x,
            pos.y - l.transform.position.y,
            0.0f,
        };
        l.transform.position.x = pos.x;
        l.transform.position.y = pos.y;
        out_deltas.push_back(delta);

        cursor += cell_main + gap;
        in_line += 1;
    }
}

// ── Apply a Grid group ─────────────────────────────────────────────────
// Row-major: index = r * cols + c. `grid.gap` is uniform horizontal +
// vertical spacing. `fit` controls whether the second axis expands.
void apply_grid_group(std::vector<Layer*>& members,
                      const LayoutGrid& grid,
                      f32 canvas_w, f32 canvas_h,
                      std::vector<Vec3>& out_deltas) {
    if (grid.columns <= 0 || grid.rows <= 0) return;  // defensive no-op

    const f32 W = canvas_w;
    const f32 H = canvas_h;
    const Vec2 cell = resolve_cell_size(grid.cell_size,
                                        static_cast<int>(members.size()),
                                        W, H);

    int effective_cols = grid.columns;
    int effective_rows = grid.rows;
    const int n = static_cast<int>(members.size());

    switch (grid.fit) {
        case GridFit::Auto:
        case GridFit::FixedColumns:
            effective_rows = (n + effective_cols - 1) / effective_cols;
            if (effective_rows <= 0) effective_rows = 1;
            break;
        case GridFit::FixedRows:
            effective_cols = (n + effective_rows - 1) / effective_rows;
            if (effective_cols <= 0) effective_cols = 1;
            break;
    }

    for (size_t i = 0; i < members.size(); ++i) {
        const int r = static_cast<int>(i) / effective_cols;
        const int c = static_cast<int>(i) % effective_cols;
        // Cell CENTER coord: stride per cell is (cell_size + gap); the first
        // cell sits at column 0, so its center is `cell_size / 2`. Each
        // subsequent cell at column c is `c * (cell_size + gap) + cell_size/2`.
        const Vec2 pos{
            static_cast<f32>(c) * (cell.x + grid.gap.x) + cell.x * 0.5f,
            static_cast<f32>(r) * (cell.y + grid.gap.y) + cell.y * 0.5f,
        };
        Layer& l = *members[i];
        const Vec3 delta{
            pos.x - l.transform.position.x,
            pos.y - l.transform.position.y,
            0.0f,
        };
        l.transform.position.x = pos.x;
        l.transform.position.y = pos.y;
        out_deltas.push_back(delta);
    }
}

}  // namespace

void LayoutSolver::solve(Scene& scene, i32 canvas_w, i32 canvas_h) const {
    const f32 W = static_cast<f32>(canvas_w);
    const f32 H = static_cast<f32>(canvas_h);

    // ── Phase 1: Flow/Grid pass (sets positions for grouped layers;
    //    non-grouped layers are untouched).                                   ──
    {
        auto groups = collect_layout_groups(scene);
        // out_deltas reserved for future telemetry; not consumed today.
        std::vector<Vec3> deltas;
        deltas.reserve(scene.layers().size());

        for (auto& [id, group] : groups) {
            if (group.kind == LayoutGroup::Kind::Mixed || group.members.empty()) {
                continue;  // defensive: programmer set both flow and grid
            }
            if (group.kind == LayoutGroup::Kind::Flow) {
                const LayoutFlow& flow = *group.members.front()->layout.flow;
                apply_flow_group(group.members, flow, W, H, deltas);
            } else {
                const LayoutGrid& grid = *group.members.front()->layout.grid;
                apply_grid_group(group.members, grid, W, H, deltas);
            }
        }
    }

    // ── Phase 2: Pin pass (only for layers NOT in a flow/grid container).     ──
    for (auto& layer : scene.layers()) {
        if (!layer.layout.enabled) continue;
        if (layer.layout.flow.has_value() || layer.layout.grid.has_value()) {
            continue;  // positioning owned by Flow/Grid
        }
        if (layer.layout.pin.has_value()) {
            Vec3 pos = anchor_position(*layer.layout.pin, canvas_w, canvas_h,
                                       layer.layout.margin);
            layer.transform.position.x += pos.x;
            layer.transform.position.y += pos.y;
            if (layer.layout.pin->depth.has_value()) {
                layer.transform.position.z += pos.z;
            }
        }
    }

    // ── Phase 3: Safe-area clamp (applies to all enabled layers).             ──
    for (auto& layer : scene.layers()) {
        if (!layer.layout.enabled) continue;
        if (layer.layout.keep_in_safe_area) {
            const SafeArea& sa = layer.layout.safe_area;
            layer.transform.position.x = std::clamp(
                layer.transform.position.x, sa.left, W - sa.right);
            layer.transform.position.y = std::clamp(
                layer.transform.position.y, sa.top, H - sa.bottom);
        }
    }
}

}  // namespace chronon3d
