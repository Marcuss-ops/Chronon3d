#include <chronon3d/layout/layout_solver.hpp>
#include <chronon3d/text/text_run_shape.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
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

Vec2 intrinsic_layer_size(const Layer& layer) {
    f32 width = 0.0f;
    f32 height = 0.0f;
    for (const auto& node : layer.nodes) {
        Vec2 size{};
        switch (node.shape.type()) {
            case ShapeType::Rect: size = node.shape.rect().size; break;
            case ShapeType::RoundedRect: size = node.shape.rounded_rect().size; break;
            case ShapeType::Circle: size = Vec2{node.shape.circle().radius * 2.0f}; break;
            case ShapeType::Image: size = node.shape.image().size; break;
            case ShapeType::TiledImage: size = node.shape.tiled_image().image.size; break;
            case ShapeType::TextRun: {
                const auto& handle = node.shape.text_run_shape_handle();
                if (handle.value && handle.value->layout)
                    size = handle.value->layout->bounds;
                break;
            }
            case ShapeType::Path: {
                const auto& commands = node.shape.path().commands;
                f32 min_x = std::numeric_limits<f32>::infinity();
                f32 min_y = min_x;
                f32 max_x = -min_x;
                f32 max_y = -min_x;
                for (const auto& command : commands) {
                    const std::array<Vec2, 3> points{command.p0, command.p1, command.p2};
                    const std::size_t count = command.type == PathCommandType::CubicTo ? 3
                        : command.type == PathCommandType::QuadraticTo ? 2
                        : command.type == PathCommandType::Close ? 0 : 1;
                    for (std::size_t i = 0; i < count; ++i) {
                        min_x = std::min(min_x, points[i].x);
                        min_y = std::min(min_y, points[i].y);
                        max_x = std::max(max_x, points[i].x);
                        max_y = std::max(max_y, points[i].y);
                    }
                }
                if (std::isfinite(min_x)) size = {max_x - min_x, max_y - min_y};
                break;
            }
            default: break;
        }
        if (std::isfinite(size.x) && std::isfinite(size.y) && size.x > 0.0f && size.y > 0.0f) {
            width = std::max(width, size.x);
            height = std::max(height, size.y);
        }
    }
    const f32 sx = std::abs(layer.transform.scale.x);
    const f32 sy = std::abs(layer.transform.scale.y);
    return {width * (sx > 0.0f ? sx : 1.0f),
            height * (sy > 0.0f ? sy : 1.0f)};
}

Vec2 resolve_item_size(const Layer& layer, const Vec2& declared, bool intrinsic,
                       const Vec2& fallback) {
    if (!intrinsic) return fallback;
    const Vec2 measured = intrinsic_layer_size(layer);
    return {declared.x > 0.0f ? declared.x : measured.x > 0.0f ? measured.x : fallback.x,
            declared.y > 0.0f ? declared.y : measured.y > 0.0f ? measured.y : fallback.y};
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
    if (flow.intrinsic_size) {
        const Vec2 fallback = resolve_cell_size(flow.cell_size,
            static_cast<int>(members.size()), W, H);
        std::vector<Vec2> sizes;
        sizes.reserve(members.size());
        for (const auto* member : members)
            sizes.push_back(resolve_item_size(*member, flow.cell_size, true, fallback));
        const f32 extent_main = flow.main_axis == Axis::Row ? W : H;
        const auto main_size = [&](std::size_t i) {
            return flow.main_axis == Axis::Row ? sizes[i].x : sizes[i].y;
        };
        const auto cross_size = [&](std::size_t i) {
            return flow.main_axis == Axis::Row ? sizes[i].y : sizes[i].x;
        };
        std::vector<std::pair<std::size_t, std::size_t>> lines;
        std::size_t begin = 0;
        f32 occupied_line = 0.0f;
        for (std::size_t i = 0; i < members.size(); ++i) {
            const f32 next = (i == begin ? 0.0f : occupied_line + flow.gap) + main_size(i);
            if (flow.wrap && i > begin && next > extent_main) {
                lines.emplace_back(begin, i);
                begin = i;
                occupied_line = main_size(i);
            } else {
                occupied_line = next;
            }
        }
        if (begin < members.size()) lines.emplace_back(begin, members.size());

        f32 cross_cursor = 0.0f;
        for (const auto [line_begin, line_end] : lines) {
            const std::size_t count = line_end - line_begin;
            f32 line_cross = 0.0f;
            f32 occupied = flow.gap * static_cast<f32>(count > 0 ? count - 1 : 0);
            for (std::size_t i = line_begin; i < line_end; ++i) {
                line_cross = std::max(line_cross, cross_size(i));
                occupied += main_size(i);
            }
            const f32 free_space = std::max(0.0f, extent_main - occupied);
            f32 leading = 0.0f;
            f32 gap = flow.gap;
            switch (flow.justify) {
                case JustifyContent::Start: break;
                case JustifyContent::Center: leading = free_space * 0.5f; break;
                case JustifyContent::End: leading = free_space; break;
                case JustifyContent::SpaceBetween:
                    if (count > 1) gap += free_space / static_cast<f32>(count - 1);
                    break;
                case JustifyContent::SpaceAround: {
                    const f32 extra = free_space / static_cast<f32>(count);
                    leading = extra * 0.5f; gap += extra; break;
                }
                case JustifyContent::SpaceEvenly: {
                    const f32 extra = free_space / static_cast<f32>(count + 1);
                    leading = extra; gap += extra; break;
                }
            }
            f32 main_cursor = leading;
            for (std::size_t i = line_begin; i < line_end; ++i) {
                const f32 item_main = main_size(i), item_cross = cross_size(i);
                f32 cross = cross_cursor;
                if (flow.align == AlignItems::Center) cross += (line_cross - item_cross) * 0.5f;
                else if (flow.align == AlignItems::End) cross += line_cross - item_cross;
                Vec2 pos;
                if (flow.main_axis == Axis::Row)
                    pos = {main_cursor + item_main * 0.5f, cross + item_cross * 0.5f};
                else
                    pos = {cross + item_cross * 0.5f, main_cursor + item_main * 0.5f};
                Layer& layer = *members[i];
                out_deltas.push_back({pos.x - layer.transform.position.x,
                    pos.y - layer.transform.position.y, 0.0f});
                layer.transform.position.x = pos.x;
                layer.transform.position.y = pos.y;
                main_cursor += item_main + gap;
            }
            cross_cursor += line_cross + flow.gap;
        }
        return;
    }
    const Vec2 cell = resolve_cell_size(flow.cell_size,
                                        static_cast<int>(members.size()),
                                        W, H);

    const f32 extent_main  = (flow.main_axis == Axis::Row) ? W : H;
    const f32 gap          = flow.gap;
    const f32 cell_main    = (flow.main_axis == Axis::Row) ? cell.x : cell.y;
    const f32 cell_cross   = (flow.main_axis == Axis::Row) ? cell.y : cell.x;

    // First partition the uniform cells into lines.  Keeping this explicit
    // makes justify-content apply independently to every wrapped line, which
    // is the useful Flexbox behaviour for card rows.
    std::vector<std::pair<size_t, size_t>> lines;
    size_t line_begin = 0;
    f32 cursor = 0.0f;
    for (size_t i = 0; i < members.size(); ++i) {
        if (flow.wrap && i > line_begin && cursor + cell_main > extent_main) {
            lines.emplace_back(line_begin, i);
            line_begin = i;
            cursor = 0.0f;
        }
        cursor += cell_main + gap;
    }
    if (line_begin < members.size()) lines.emplace_back(line_begin, members.size());

    for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const auto [begin, end] = lines[line_index];
        const size_t count = end - begin;
        const f32 occupied = static_cast<f32>(count) * cell_main
                           + static_cast<f32>(count > 0 ? count - 1 : 0) * gap;
        const f32 free_space = std::max(0.0f, extent_main - occupied);

        f32 leading = 0.0f;
        f32 distributed_gap = gap;
        switch (flow.justify) {
        case JustifyContent::Start:
            break;
        case JustifyContent::Center:
            leading = free_space * 0.5f;
            break;
        case JustifyContent::End:
            leading = free_space;
            break;
        case JustifyContent::SpaceBetween:
            if (count > 1) distributed_gap += free_space / static_cast<f32>(count - 1);
            break;
        case JustifyContent::SpaceAround: {
            const f32 extra = free_space / static_cast<f32>(count);
            leading = extra * 0.5f;
            distributed_gap += extra;
            break;
        }
        case JustifyContent::SpaceEvenly: {
            const f32 extra = free_space / static_cast<f32>(count + 1);
            leading = extra;
            distributed_gap += extra;
            break;
        }
        }

        const f32 cross_offset = static_cast<f32>(line_index) * (cell_cross + gap);
        for (size_t i = begin; i < end; ++i) {
            const f32 line_index_offset = static_cast<f32>(i - begin);
            const f32 main_position = leading
                + line_index_offset * (cell_main + distributed_gap)
                + cell_main * 0.5f;

            // Main axis: distributed cursor + half-cell to land the cell
            // center. Cross axis: align_offset returns the cell center.
            Vec2 pos{0.0f, 0.0f};
            if (flow.main_axis == Axis::Row) {
                pos.x = main_position;
                pos.y = align_offset(flow.align, cell_cross, H) + cross_offset;
            } else {
                pos.y = main_position;
                pos.x = align_offset(flow.align, cell_cross, W) + cross_offset;
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
        }
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
    if (grid.intrinsic_size) {
        int cols = grid.columns;
        int rows = grid.rows;
        const int n = static_cast<int>(members.size());
        switch (grid.fit) {
            case GridFit::Auto:
            case GridFit::FixedColumns:
                rows = std::max(1, (n + cols - 1) / cols); break;
            case GridFit::FixedRows:
                cols = std::max(1, (n + rows - 1) / rows); break;
        }
        const Vec2 fallback = resolve_cell_size(grid.cell_size, n, W, H);
        std::vector<Vec2> sizes;
        sizes.reserve(members.size());
        std::vector<f32> column_widths(static_cast<std::size_t>(cols), 0.0f);
        std::vector<f32> row_heights(static_cast<std::size_t>(rows), 0.0f);
        for (std::size_t i = 0; i < members.size(); ++i) {
            const Vec2 size = resolve_item_size(*members[i], grid.cell_size, true, fallback);
            sizes.push_back(size);
            const auto col = i % static_cast<std::size_t>(cols);
            const auto row = i / static_cast<std::size_t>(cols);
            column_widths[col] = std::max(column_widths[col], size.x);
            row_heights[row] = std::max(row_heights[row], size.y);
        }
        for (std::size_t i = 0; i < members.size(); ++i) {
            const std::size_t col = i % static_cast<std::size_t>(cols);
            const std::size_t row = i / static_cast<std::size_t>(cols);
            f32 x = 0.0f, y = 0.0f;
            for (std::size_t c = 0; c < col; ++c) x += column_widths[c] + grid.gap.x;
            for (std::size_t r = 0; r < row; ++r) y += row_heights[r] + grid.gap.y;
            const f32 dx = column_widths[col] - sizes[i].x;
            const f32 dy = row_heights[row] - sizes[i].y;
            const Vec2 pos{x + column_widths[col] * 0.5f - dx * 0.5f,
                           y + row_heights[row] * 0.5f - dy * 0.5f};
            Layer& layer = *members[i];
            out_deltas.push_back({pos.x - layer.transform.position.x,
                pos.y - layer.transform.position.y, 0.0f});
            layer.transform.position.x = pos.x;
            layer.transform.position.y = pos.y;
        }
        return;
    }
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
