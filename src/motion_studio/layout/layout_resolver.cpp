// ═════════════════════════════════════════════════════════════════════════════
//  LayoutResolver — flex-style solver for motion_studio UiNode trees.
//
//  Algorithm:
//    1. compute_intrinsic_size — bottom-up walk.
//       • Leaves with Fixed mode → declared w_value / h_value.
//       • Containers (row/column/grid) → sum of children + gaps + padding.
//       • FillRemaining children contribute 0 to intrinsic size; they
//         receive whatever free space is left after Fixed/FitContent.
//       • Component nodes (button/dashboard_card/input_field) have a
//         sensible per-element default size declared inline here.
//    2. layout_node — top-down walk.
//       • The root absorbs the canvas size.
//       • Each container carves out its inner box (size − padding) and
//         places children along its main axis using direction + justify,
//         then aligns them on the cross axis using the parent's align.
//
//  Result: every node's resolved_position (top-left) and resolved_size
//  are populated.  Components' absolute centre is position + size/2,
//  which layers convert to center-anchored positions.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/motion_studio/layout/layout_resolver.hpp>

#include <algorithm>
#include <vector>

namespace chronon3d::motion_studio {

// ─── File-local helpers (defined before layout_node so they're in scope) ──

namespace {

/// Default intrinsic size per leaf element kind — keeps FitContent meaningful
/// even when the caller doesn't pass a Fixed size.
/// ── Critically: this evaluates each axis independently so that
/// {w: Fixed, h: FillRemaining} and {w: FitContent, h: FillRemaining}
/// both yield h=0 (so the parent can stretch). ──
Vec2 default_leaf_size(const UiNode& node) {
    f32 w_default = 100.0f;
    f32 h_default = 40.0f;
    if (node.element == "dashboard_card") { w_default = 240.0f; h_default = 140.0f; }
    else if (node.element == "button")    { w_default = 120.0f; h_default = 44.0f;  }
    else if (node.element == "input_field"){ w_default = 280.0f; h_default = 44.0f; }

    auto w = [&]() -> f32 {
        switch (node.size.w_mode) {
            case SizeMode::Fixed:         return node.size.w_value;
            case SizeMode::FillRemaining: return 0.0f;
            default:                      return w_default;
        }
    }();
    auto h = [&]() -> f32 {
        switch (node.size.h_mode) {
            case SizeMode::Fixed:         return node.size.h_value;
            case SizeMode::FillRemaining: return 0.0f;
            default:                      return h_default;
        }
    }();
    return Vec2{w, h};
}

/// Cross-axis placement: places a child of size `child_extent` inside a
/// box that starts at `cross_origin` and extends `cross_extent`.
f32 align_cross(AlignItem a, f32 cross_origin, f32 cross_extent, f32 child_extent) {
    switch (a) {
        case AlignItem::Center:
            return cross_origin + std::max(0.0f, (cross_extent - child_extent) * 0.5f);
        case AlignItem::End:
            return cross_origin + std::max(0.0f, cross_extent - child_extent);
        case AlignItem::Stretch:
        case AlignItem::Start:
        default:
            return cross_origin;
    }
}

} // namespace

// ─── LayoutResolver methods ─────────────────────────────────────────────

void LayoutResolver::solve(UiNode& root, Vec2 canvas_size) {
    canvas_size_ = canvas_size;
    root.resolved_size      = canvas_size;
    root.resolved_position  = Vec2{0.0f, 0.0f};
    compute_intrinsic_size(root);
    layout_node(root);
}

Vec2 LayoutResolver::compute_intrinsic_size(const UiNode& node) const {
    if (!node.is_container()) {
        return default_leaf_size(node);
    }

    const bool horizontal = (node.element == "row") ||
                            ((node.element == "grid") &&
                             node.direction == AxisDir::Row);
    const bool is_grid = (node.element == "grid");
    const int  cols    = std::max(1, node.grid_cols);

    if (!is_grid) {
        f32 main = 0.0f;
        f32 cross = 0.0f;
        int counted = 0;
        for (const auto& child : node.children) {
            const Vec2 cs = compute_intrinsic_size(*child);
            if (horizontal) {
                main  += cs.x;
                cross  = std::max(cross, cs.y);
            } else {
                cross  = std::max(cross, cs.x);
                main  += cs.y;
            }
            const bool fill_on_main = horizontal
                ? child->size.w_mode == SizeMode::FillRemaining
                : child->size.h_mode == SizeMode::FillRemaining;
            if (!fill_on_main) ++counted;
        }
        if (counted > 1) main += node.gap * (counted - 1);
        return Vec2{
            horizontal ? main + node.padding.left + node.padding.right
                       : cross + node.padding.left + node.padding.right,
            horizontal ? cross + node.padding.top + node.padding.bottom
                       : main + node.padding.top + node.padding.bottom,
        };
    }

    // Grid — measure rows.
    f32 column_width_max = 0.0f;
    f32 column_width_sum = 0.0f;
    std::vector<f32> col_w(cols, 0.0f);
    std::vector<f32> row_h;
    int cells = 0;
    for (const auto& child : node.children) {
        const Vec2 cs = compute_intrinsic_size(*child);
        col_w[cells] = std::max(col_w[cells], cs.x);
        if (static_cast<int>(row_h.size()) <= cells / cols) row_h.push_back(0.0f);
        row_h[cells / cols] = std::max(row_h[cells / cols], cs.y);
        ++cells;
    }
    for (f32 w : col_w) column_width_sum += w;
    for (f32 w : col_w) column_width_max = std::max(column_width_max, w);

    const f32 gap_main_total = (cols > 1 ? node.gap * (cols - 1) : 0.0f);
    const f32 gap_cross_total = (row_h.size() > 1 ? node.gap * static_cast<f32>(row_h.size() - 1) : 0.0f);

    (void)column_width_max;
    return Vec2{
        column_width_sum + gap_main_total + node.padding.left + node.padding.right,
        std::accumulate(row_h.begin(), row_h.end(), 0.0f) +
            gap_cross_total + node.padding.top + node.padding.bottom,
    };
}

void LayoutResolver::layout_node(UiNode& node) {
    // Recurse first so children's resolved_size is correct in case the
    // container wants to redistribute fill space.
    for (auto& child : node.children) {
        layout_node(*child);
    }
    if (!node.is_container() || node.children.empty()) return;

    const Vec2 inner{
        std::max(0.0f, node.resolved_size.x - node.padding.left - node.padding.right),
        std::max(0.0f, node.resolved_size.y - node.padding.top  - node.padding.bottom),
    };
    const Vec2 inner_origin{
        node.resolved_position.x + node.padding.left,
        node.resolved_position.y + node.padding.top,
    };

    if (node.element == "row") {
        f32 main_intrinsic = 0.0f;
        int count = 0;
        int fill_count = 0;
        for (const auto& c : node.children) {
            main_intrinsic += c->resolved_size.x;
            ++count;
            if (c->size.w_mode == SizeMode::FillRemaining) ++fill_count;
        }
        if (count > 1) main_intrinsic += node.gap * (count - 1);

        f32 extra = std::max(0.0f, inner.x - main_intrinsic);
        f32 start_x = inner_origin.x;
        f32 gap_x   = node.gap;

        if (fill_count > 0) {
            const f32 per = extra / static_cast<f32>(fill_count);
            for (auto& c : node.children) {
                if (c->size.w_mode == SizeMode::FillRemaining) c->resolved_size.x += per;
            }
            extra = 0.0f;
        } else {
            switch (node.justify) {
                case Justify::Center:      start_x = inner_origin.x + extra * 0.5f;                              break;
                case Justify::End:         start_x = inner_origin.x + extra;                                     break;
                case Justify::SpaceBetween:
                    if (count > 1) gap_x += extra / static_cast<f32>(count - 1);
                    break;
                case Justify::SpaceAround:
                    if (count > 0) {
                        const f32 each = extra / static_cast<f32>(count);
                        start_x = inner_origin.x + each * 0.5f;
                        gap_x   += each;
                    }
                    break;
                default: break;
            }
        }

        f32 cursor_x = start_x;
        for (auto& c : node.children) {
            const f32 cross_y = align_cross(node.align, inner_origin.y, inner.y, c->resolved_size.y);
            c->resolved_position = Vec2{cursor_x, cross_y};
            cursor_x += c->resolved_size.x + gap_x;
        }
        return;
    }

    if (node.element == "column") {
        f32 main_intrinsic = 0.0f;
        int count = 0;
        int fill_count = 0;
        for (const auto& c : node.children) {
            main_intrinsic += c->resolved_size.y;
            ++count;
            if (c->size.h_mode == SizeMode::FillRemaining) ++fill_count;
        }
        if (count > 1) main_intrinsic += node.gap * (count - 1);

        f32 extra = std::max(0.0f, inner.y - main_intrinsic);
        f32 start_y = inner_origin.y;
        f32 gap_y   = node.gap;

        if (fill_count > 0) {
            const f32 per = extra / static_cast<f32>(fill_count);
            for (auto& c : node.children) {
                if (c->size.h_mode == SizeMode::FillRemaining) c->resolved_size.y += per;
            }
            extra = 0.0f;
        } else {
            switch (node.justify) {
                case Justify::Center:      start_y = inner_origin.y + extra * 0.5f;                              break;
                case Justify::End:         start_y = inner_origin.y + extra;                                     break;
                case Justify::SpaceBetween:
                    if (count > 1) gap_y += extra / static_cast<f32>(count - 1);
                    break;
                case Justify::SpaceAround:
                    if (count > 0) {
                        const f32 each = extra / static_cast<f32>(count);
                        start_y = inner_origin.y + each * 0.5f;
                        gap_y   += each;
                    }
                    break;
                default: break;
            }
        }

        f32 cursor_y = start_y;
        for (auto& c : node.children) {
            const f32 cross_x = align_cross(node.align, inner_origin.x, inner.x, c->resolved_size.x);
            c->resolved_position = Vec2{cross_x, cursor_y};
            cursor_y += c->resolved_size.y + gap_y;
        }
        return;
    }

    if (node.element == "grid") {
        const int cols = std::max(1, node.grid_cols);
        std::vector<f32> col_w(cols, 0.0f);
        std::vector<f32> row_h;
        int cells_in_row = 0;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            auto& c = node.children[i];
            col_w[cells_in_row] = std::max(col_w[cells_in_row], c->resolved_size.x);
            const int r = static_cast<int>(i) / cols;
            if (static_cast<int>(row_h.size()) <= r) row_h.push_back(0.0f);
            row_h[r] = std::max(row_h[r], c->resolved_size.y);
            ++cells_in_row;
            if (cells_in_row == cols) cells_in_row = 0;
        }
        f32 cursor_y = inner_origin.y;
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            auto& c = node.children[i];
            const int r = static_cast<int>(i) / cols;
            const int col = static_cast<int>(i) % cols;
            c->resolved_position = Vec2{inner_origin.x, cursor_y};
            // advance col cursor:
            f32 col_x = inner_origin.x;
            for (int cc = 0; cc < col; ++cc) col_x += col_w[cc] + node.gap;
            c->resolved_position.x = col_x;
            if (col == cols - 1 || i + 1 == node.children.size()) {
                cursor_y += row_h[r] + node.gap;
            }
        }
        return;
    }
}

} // namespace chronon3d::motion_studio
