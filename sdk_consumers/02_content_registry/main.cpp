// sdk_consumers/02_content_registry/main.cpp
//
// ═══════════════════════════════════════════════════════════════════════════
// Chronon3D — Content Registry Consumer Example
//
// Minimal standalone consumer that demonstrates the single canonical content
// registry (CompositionRegistry) and its typed content-type accessors:
//
//   1. #include <chronon3d/core/composition/composition_registry.hpp>
//   2. Register content of the four canonical types (Phrase, ImportantWord,
//      Image, NamedText) into ONE registry — no per-type registries.
//   3. Query them via phrases() / important_words() / images() / named_texts().
//
// Link ONLY the public SDK target (see CMakeLists.txt for the
// find_package and add_subdirectory snippets).
// ═══════════════════════════════════════════════════════════════════════════

#include <chronon3d/core/composition/composition_registry.hpp>
#include <chronon3d/timeline/composition.hpp>
#include <chronon3d/timeline/composition_descriptor.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace c3d = chronon3d;

namespace {

// Minimal Composition factory — the example only exercises registry
// registration and resolution, so the scene body is a stub.
c3d::Composition make_minimal(const char* name) {
    c3d::CompositionSpec spec;
    spec.name     = name;
    spec.duration = c3d::Frame{1};
    return c3d::Composition(std::move(spec),
                            [](const c3d::FrameContext&) { return c3d::Scene{}; });
}

} // namespace

int main() {
    c3d::CompositionRegistry registry;

    // One registry, four content types — each tagged with its canonical
    // content_category value instead of living in a separate system.
    const auto add = [&registry](std::string id, std::string_view category) {
        registry.add(c3d::make_composition_descriptor(
            c3d::CompositionDescriptor{.id       = std::move(id),
                                       .category = std::string{category}},
            [] { return make_minimal("stub"); }));
    };

    add("phrase-keep-moving", c3d::content_category::Phrase);
    add("word-focus",         c3d::content_category::ImportantWord);
    add("image-landscape",    c3d::content_category::Image);
    add("name-director",      c3d::content_category::NamedText);

    const auto print = [](const char* label,
                          const std::vector<c3d::CompositionDescriptor>& items) {
        std::printf("%s: %zu\n", label, items.size());
        for (const auto& descriptor : items) {
            std::printf("  - %s\n", descriptor.id.c_str());
        }
    };

    print("phrases",         registry.phrases());
    print("important_words", registry.important_words());
    print("images",          registry.images());
    print("named_texts",     registry.named_texts());

    return 0;
}
