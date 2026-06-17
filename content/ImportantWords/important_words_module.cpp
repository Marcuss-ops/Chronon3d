#include <chronon3d/extension/extension_module.hpp>
#include <chronon3d/extension/extension_registry.hpp>

#include "important_words_animations.hpp"

namespace chronon3d {

class ImportantWordsModule : public ExtensionModule {
public:
    std::string_view id() const override { return "important_words"; }

    void register_with(ExtensionRegistry& registry) override {
        using namespace content::important_words;
        // One composition per (word, palette) pair: drift-free registration.
        registry.register_composition("ImportantWordDirectorLight", important_word_director_light);
        registry.register_composition("ImportantWordActorWarm",     important_word_actor_warm);
        registry.register_composition("ImportantWordWriterCool",    important_word_writer_cool);
        // Trio cycles all three words × all three palettes.
        registry.register_composition("ImportantWordTrio",           important_word_trio);
    }
};

std::unique_ptr<ExtensionModule> create_important_words_module() {
    return std::make_unique<ImportantWordsModule>();
}

} // namespace chronon3d
