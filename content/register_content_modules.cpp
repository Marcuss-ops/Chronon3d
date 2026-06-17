#include "register_content_modules.hpp"

#include <memory>

namespace chronon3d {

// Forward-declare module factory functions (defined in each *_module.cpp).
std::unique_ptr<ExtensionModule> create_minimalist_module();
std::unique_ptr<ExtensionModule> create_special_names_module();
std::unique_ptr<ExtensionModule> create_important_words_module();
std::unique_ptr<ExtensionModule> create_shapes_module();
std::unique_ptr<ExtensionModule> create_images_module();
std::unique_ptr<ExtensionModule> create_two_point_five_d_module();
std::unique_ptr<ExtensionModule> create_anims_module();
std::unique_ptr<ExtensionModule> create_grid_module();
std::unique_ptr<ExtensionModule> create_effects_module();

void register_content_modules() {
    // Register all modules first (no-op if already registered).
    register_module_if_needed("minimalist",     create_minimalist_module);
    register_module_if_needed("special_names",  create_special_names_module);
    register_module_if_needed("important_words",create_important_words_module);
    register_module_if_needed("shapes",         create_shapes_module);
    register_module_if_needed("images",         create_images_module);
    register_module_if_needed("2d5",            create_two_point_five_d_module);
    register_module_if_needed("anims",          create_anims_module);
    register_module_if_needed("grid",           create_grid_module);
    register_module_if_needed("effects",        create_effects_module);

    // Initialize all registered modules exactly once
    // (previously called 9 times — once per per-module function).
    ExtensionRegistry::instance().initialize_all();
}

} // namespace chronon3d
