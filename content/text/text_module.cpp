#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>
#include <chronon3d/timeline/composition.hpp>

namespace chronon3d::content::text {

// ── Forward declarations (fresh compositions) ───────────────────────────

Composition text_center_title();
Composition text_center_glow();
Composition text_subtitle_glow();
Composition text_badge_glow();

} // namespace chronon3d::content::text

namespace chronon3d {

class TextModule : public ExtensionModule {
public:
    std::string_view id() const override { return "text"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::text;

        // Fresh compositions: grid + centered text + glow
        registry.register_composition("TextCenterTitle", text_center_title);
        registry.register_composition("TextCenterGlow", text_center_glow);
        registry.register_composition("TextSubtitleGlow", text_subtitle_glow);
        registry.register_composition("TextBadgeGlow", text_badge_glow);
    }
};

std::unique_ptr<ExtensionModule> create_text_module() {
    return std::make_unique<TextModule>();
}

} // namespace chronon3d
