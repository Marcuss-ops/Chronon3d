#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

namespace chronon3d::content::text {
} // namespace chronon3d::content::text

namespace chronon3d {

class TextModule : public ExtensionModule {
public:
    std::string_view id() const override { return "text"; }

    void register_with(ExtensionRegistry& registry) override {
        // Text module is intentionally empty for now.
        // Shared text helpers remain available in content/text/text_theme.hpp.
        (void)registry;
    }
};

std::unique_ptr<ExtensionModule> create_text_module() {
    return std::make_unique<TextModule>();
}

} // namespace chronon3d
