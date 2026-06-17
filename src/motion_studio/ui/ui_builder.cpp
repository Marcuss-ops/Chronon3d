// ═════════════════════════════════════════════════════════════════════════════
//  UiBuilder implementation — declarative UI composer for motion_studio.
//
//  emit() runs the LayoutResolver, then walks every non-container node and
//  emits one Chronon `layer` per component into the supplied SceneBuilder.
//  All components use center-anchored Vec3 positions internally: the
//  resolver produces top-left coordinates in canvas pixels, and we add
//  size/2 to get the centre before calling `l.position(centre)`.
//
//  Text inside a card uses TextParams::pos which is *relative* to the
//  layer's centre, so it offsets visibly from the centre without extra
//  layer.position() calls.
// ═════════════════════════════════════════════════════════════════════════════

#include <chronon3d/motion_studio/ui/ui_builder.hpp>
#include <chronon3d/scene/builders/builder_params.hpp>
#include <chronon3d/scene/builders/scene_builder.hpp>
#include <chronon3d/text/text_animator.hpp>

#include <cassert>
#include <utility>

namespace chronon3d::motion_studio {

// ── Ctors / stack manipulators ─────────────────────────────────────────

UiBuilder::UiBuilder(const chronon3d::FrameContext& ctx)
    : ctx_(ctx) {
    root_   = std::make_unique<UiNode>();
    root_->id      = "root";
    root_->element = "root";
    stack_.push_back(root_.get());
}

UiBuilder& UiBuilder::row(f32 gap) {
    push("__row_" + std::to_string(stack_.size()), "row")->gap = gap;
    return *this;
}

UiBuilder& UiBuilder::column(f32 gap) {
    auto* n = push("__column_" + std::to_string(stack_.size()), "column");
    n->direction = AxisDir::Column;
    n->gap = gap;
    return *this;
}

UiBuilder& UiBuilder::grid(int cols, f32 gap) {
    auto* n = push("__grid_" + std::to_string(stack_.size()), "grid");
    n->grid_cols = std::max(1, cols);
    n->gap = gap;
    return *this;
}

UiBuilder& UiBuilder::end() {
    if (stack_.size() > 1) stack_.pop_back();
    return *this;
}

// ── Padding on the current container ──────────────────────────────────

UiBuilder& UiBuilder::padding(f32 all_sides) {
    if (!stack_.empty()) stack_.back()->padding = Padding::all(all_sides);
    return *this;
}

UiBuilder& UiBuilder::padding(f32 v, f32 h) {
    if (!stack_.empty()) {
        auto& p = stack_.back()->padding;
        p.left = p.right = h;
        p.top = p.bottom = v;
    }
    return *this;
}

UiBuilder& UiBuilder::padding(f32 top, f32 right, f32 bottom, f32 left) {
    if (!stack_.empty()) {
        auto& p = stack_.back()->padding;
        p.top = top; p.right = right; p.bottom = bottom; p.left = left;
    }
    return *this;
}

// ── Sizing modifiers for the *next* child ─────────────────────────────

UiBuilder& UiBuilder::fill()         { next_size_ = Size2D{}; next_size_.w_mode = SizeMode::FillRemaining; next_size_.h_mode = SizeMode::FillRemaining; has_next_size_ = true; return *this; }
UiBuilder& UiBuilder::fill_h()       { next_size_.w_mode = SizeMode::FillRemaining;                                    has_next_size_ = true; return *this; }
UiBuilder& UiBuilder::fill_v()       { next_size_.h_mode = SizeMode::FillRemaining;                                    has_next_size_ = true; return *this; }
UiBuilder& UiBuilder::fixed(f32 w, f32 h) { next_size_ = Size2D{}; next_size_.w_mode = SizeMode::Fixed; next_size_.w_value = w; next_size_.h_mode = SizeMode::Fixed; next_size_.h_value = h; has_next_size_ = true; return *this; }

void UiBuilder::apply_next_size(UiNode& child) const {
    if (!has_next_size_) return;
    child.size = next_size_;
}

// ── Components ───────────────────────────────────────────────────────

UiBuilder& UiBuilder::button(const std::string& id, const std::string& label) {
    ButtonParams p; p.label = label;
    return button(id, p);
}

UiBuilder& UiBuilder::button(const std::string& id, const ButtonParams& p) {
    auto* n = push(id, "button");
    n->text_value = p.label;
    n->bg_color   = p.bg_color;
    n->fg_color   = p.fg_color;
    n->radius     = p.radius;
    if (has_next_size_) { apply_next_size(*n); }
    else {
        n->size.w_mode = SizeMode::Fixed; n->size.w_value = std::max(p.min_width, p.label.size() * 9.0f + 32.0f);
        n->size.h_mode = SizeMode::Fixed; n->size.h_value = p.height;
    }
    return *this;
}

UiBuilder& UiBuilder::dashboard_card(const std::string& id, const DashCardParams& p) {
    auto* n = push(id, "dashboard_card");
    n->text_value     = p.title;
    n->subtext_value  = p.value;
    n->trend_value    = p.trend;
    n->trend_positive = p.trend_positive;
    n->accent_color   = p.accent;
    n->bg_color       = {0.08f, 0.10f, 0.16f, 1.0f};
    n->fg_color       = {0.95f, 0.96f, 1.0f, 1.0f};
    if (has_next_size_) { apply_next_size(*n); }
    else {
        n->size.w_mode = SizeMode::FitContent;
        n->size.h_mode = SizeMode::FitContent;
    }
    return *this;
}

UiBuilder& UiBuilder::input_field(const std::string& id,
                                  const std::string& placeholder,
                                  const std::string& value) {
    auto* n = push(id, "input_field");
    n->placeholder_value = placeholder;
    n->typed_value       = value;
    n->bg_color          = {0.08f, 0.09f, 0.12f, 0.95f};
    n->fg_color          = {0.92f, 0.94f, 1.0f, 1.0f};
    n->radius            = 10.0f;
    if (has_next_size_) { apply_next_size(*n); }
    else {
        n->size.w_mode = SizeMode::FitContent;
        n->size.h_mode = SizeMode::Fixed; n->size.h_value = 44.0f;
    }
    return *this;
}

// ── Helpers ────────────────────────────────────────────────────────────

UiNode* UiBuilder::push(const std::string& id, const std::string& element) {
    auto node = std::make_unique<UiNode>();
    node->id      = id;
    node->element = element;
    node->parent  = stack_.back();
    UiNode* raw = node.get();
    stack_.back()->children.push_back(std::move(node));
    stack_.push_back(raw);
    if (has_next_size_) {
        raw->size = next_size_;
    }
    return raw;
}

// ── emit ───────────────────────────────────────────────────────────────

void UiBuilder::emit(chronon3d::SceneBuilder& s) {
    // 1. Solve layout — uses the canvas size from FrameContext.
    resolver_.solve(*root_, Vec2{
        static_cast<f32>(ctx_.width),
        static_cast<f32>(ctx_.height),
    });

    // 2. Collect component rectangles for InteractionSimulator.
    components_.clear();
    std::function<void(const UiNode&)> collect = [&](const UiNode& n) {
        if (!n.is_container()) {
            ComponentRect r;
            r.id      = n.id;
            r.element = n.element;
            r.center  = n.center();
            r.size    = n.resolved_size;
            components_.push_back(r);
        }
        for (const auto& c : n.children) collect(*c);
    };
    collect(*root_);

    // 3. Emit layers in DFS order.  Container layers disappear (their children
    //    are positioned independently), each leaf becomes one layer.
    std::function<void(const UiNode&)> walk = [&](const UiNode& n) {
        if (!n.is_container()) {
            const Vec3 centre{
                n.center().x,
                n.center().y,
                0.0f,
            };
            const std::string layer_id = n.id;
            if (n.element == "button") {
                s.layer(layer_id, [&n, centre, layer_id](chronon3d::LayerBuilder& l) {
                    l.position(centre);
                    l.rounded_rect("bg", chronon3d::RoundedRectParams{
                        .size    = n.resolved_size,
                        .radius  = n.radius,
                        .color   = n.bg_color,
                        .pos     = {0.0f, 0.0f, 0.0f},
                    });
                    if (!n.text_value.empty()) {
                        chronon3d::TextParams tp;
                        tp.text      = n.text_value;
                        tp.size      = {n.resolved_size.x, n.resolved_size.y};
                        tp.pos       = {0.0f, 0.0f, 0.0f};
                        tp.font_size = std::min(20.0f, n.resolved_size.y * 0.55f);
                        tp.color     = n.fg_color;
                        tp.align     = chronon3d::TextAlign::Center;
                        tp.vertical_align = chronon3d::VerticalAlign::Middle;
                        tp.font_path  = "assets/fonts/Inter-Bold.ttf";
                        tp.font_family = "Inter";
                        tp.font_weight = 700;
                        tp.anchor = chronon3d::TextAnchor::Center; // may need enum verification
                        l.text("label", tp);
                    }
                    (void)layer_id;
                });
            } else if (n.element == "dashboard_card") {
                s.layer(layer_id, [&n, centre, layer_id](chronon3d::LayerBuilder& l) {
                    l.position(centre);
                    // Background.
                    l.rounded_rect("bg", chronon3d::RoundedRectParams{
                        .size   = n.resolved_size,
                        .radius = n.radius,
                        .color  = n.bg_color,
                        .pos    = {0.0f, 0.0f, 0.0f},
                    });
                    // Accent stripe at top.
                    l.rect("accent", chronon3d::RectParams{
                        .size   = {n.resolved_size.x - 24.0f, 3.0f},
                        .color  = n.accent_color,
                        .pos    = {0.0f, -n.resolved_size.y * 0.5f + 14.0f, 0.0f},
                    });
                    // Title label (top-left of card).
                    chronon3d::TextParams title;
                    title.text      = n.text_value;
                    title.size      = {n.resolved_size.x - 24.0f, 28.0f};
                    title.pos       = {-n.resolved_size.x * 0.5f + 12.0f,
                                        -n.resolved_size.y * 0.5f + 32.0f, 0.0f};
                    title.font_size = 14.0f;
                    title.color     = {0.65f, 0.69f, 0.78f, 1.0f};
                    title.align     = chronon3d::TextAlign::Left;
                    title.vertical_align = chronon3d::VerticalAlign::Middle;
                    title.font_path  = "assets/fonts/Inter-Bold.ttf";
                    title.font_family = "Inter";
                    title.font_weight = 600;
                    title.anchor = chronon3d::TextAnchor::TopLeft;
                    l.text("title", title);

                    // Value (big number, centred).
                    chronon3d::TextParams value;
                    value.text      = n.subtext_value;
                    value.size      = {n.resolved_size.x - 24.0f, 48.0f};
                    value.pos       = {0.0f, 4.0f, 0.0f};
                    value.font_size = 32.0f;
                    value.color     = n.fg_color;
                    value.align     = chronon3d::TextAlign::Center;
                    value.vertical_align = chronon3d::VerticalAlign::Middle;
                    value.font_path  = "assets/fonts/Inter-Bold.ttf";
                    value.font_family = "Inter";
                    value.font_weight = 800;
                    value.anchor = chronon3d::TextAnchor::Center;
                    l.text("value", value);

                    // Trend (bottom-left of card).
                    std::string arrow = n.trend_positive ? "▲ " : "▼ ";
                    chronon3d::TextParams trend;
                    trend.text      = arrow + std::to_string(static_cast<int>(std::abs(n.trend_value))) + "%";
                    trend.size      = {n.resolved_size.x - 24.0f, 24.0f};
                    trend.pos       = {-n.resolved_size.x * 0.5f + 12.0f,
                                       n.resolved_size.y * 0.5f - 24.0f, 0.0f};
                    trend.font_size = 13.0f;
                    trend.color     = n.trend_positive
                        ? chronon3d::Color{0.30f, 0.85f, 0.50f, 1.0f}
                        : chronon3d::Color{0.95f, 0.40f, 0.40f, 1.0f};
                    trend.align     = chronon3d::TextAlign::Left;
                    trend.vertical_align = chronon3d::VerticalAlign::Middle;
                    trend.font_path  = "assets/fonts/Inter-Bold.ttf";
                    trend.font_family = "Inter";
                    trend.font_weight = 700;
                    trend.anchor = chronon3d::TextAnchor::BottomLeft;
                    l.text("trend", trend);
                    (void)layer_id;
                });
            } else if (n.element == "input_field") {
                s.layer(layer_id, [&n, centre, layer_id](chronon3d::LayerBuilder& l) {
                    l.position(centre);
                    // Background.
                    l.rounded_rect("bg", chronon3d::RoundedRectParams{
                        .size   = n.resolved_size,
                        .radius = n.radius,
                        .color  = n.bg_color,
                        .pos    = {0.0f, 0.0f, 0.0f},
                    });
                    // Bottom accent rule (one-pixel style hint).
                    l.rect("rule", chronon3d::RectParams{
                        .size  = {n.resolved_size.x - 24.0f, 1.0f},
                        .color = n.accent_color,
                        .pos   = {0.0f, n.resolved_size.y * 0.5f - 6.0f, 0.0f},
                    });
                    const std::string shown = n.typed_value.empty()
                        ? n.placeholder_value
                        : n.typed_value;
                    const chronon3d::Color shown_color = n.typed_value.empty()
                        ? chronon3d::Color{0.55f, 0.60f, 0.70f, 1.0f}
                        : n.fg_color;
                    chronon3d::TextParams tp;
                    tp.text      = shown;
                    tp.size      = {n.resolved_size.x - 24.0f, n.resolved_size.y};
                    tp.pos       = {-n.resolved_size.x * 0.5f + 14.0f, 0.0f, 0.0f};
                    tp.font_size = 16.0f;
                    tp.color     = shown_color;
                    tp.align     = chronon3d::TextAlign::Left;
                    tp.vertical_align = chronon3d::VerticalAlign::Middle;
                    tp.font_path  = "assets/fonts/Inter-Bold.ttf";
                    tp.font_family = "Inter";
                    tp.font_weight = 500;
                    tp.anchor = chronon3d::TextAnchor::Center;
                    l.text("text", tp);
                    (void)layer_id;
                });
            }
            return;
        }
        for (const auto& c : n.children) walk(*c);
    };
    walk(*root_);
}

// ── Inspector ──────────────────────────────────────────────────────────

std::optional<Vec2> UiBuilder::component_center(const std::string& id) const {
    for (const auto& c : components_) if (c.id == id) return c.center;
    return std::nullopt;
}

std::optional<Vec2> UiBuilder::component_size(const std::string& id) const {
    for (const auto& c : components_) if (c.id == id) return c.size;
    return std::nullopt;
}

std::optional<UiBuilder::ComponentRect>
UiBuilder::component_rect(const std::string& id) const {
    for (const auto& c : components_) if (c.id == id) return c;
    return std::nullopt;
}

} // namespace chronon3d::motion_studio
