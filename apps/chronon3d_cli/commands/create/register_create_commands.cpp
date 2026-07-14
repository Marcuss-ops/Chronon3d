// ==============================================================================
// TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE (Blocco 4.2, Commit 2 of 2)
//
// `chronon create <name>` — scaffold a new Chronon3D project from a
// versioned template (e.g. templates/basic/).
//
// Template discovery (resolution chain, in order):
//   1. CHRONON3D_TEMPLATES_DIR env var (runtime override).
//   2. CHRONON3D_BAKED_TEMPLATES_DIR macro from templates_dir.hpp
//      (compile-time baked by CMake configure_file in
//      apps/chronon3d_cli/CMakeLists.txt).
//
// Per audit §19 verbatim "Cosa non aggiungere":
//   - NO project registry
//   - NO template engine
//   - NO package manager
//   - SOLO copia file (with single ${PROJECT_NAME} placeholder substitution,
//     NOT a recursive template engine — one std::string::replace per file).
// ==============================================================================
#include "../../command_registry.hpp"
#include "../../commands.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

#ifdef CHRONON3D_HAS_TEMPLATES_DIR_BAKED
#include "templates_dir.hpp"
#endif

namespace chronon3d::cli {

namespace {

// TICKET-V3-CLI-UNIFICATION-STARTER-TEMPLATE — `${PROJECT_NAME}` literal
// placeholder substituted in each text file at copy-time.  Single pass,
// no template-engine recursion (audit verbatim: "solo copia file").
constexpr const char* kProjectNamePlaceholder = "${PROJECT_NAME}";
constexpr std::size_t kProjectNamePlaceholderLen = 15;  // std::strlen("${PROJECT_NAME}")

struct CreateState {
    std::shared_ptr<CreateArgs> args{std::make_shared<CreateArgs>()};
};

std::filesystem::path resolve_templates_dir() {
    // 1. Env var override (runtime, useful for in-tree dev where the
    //    install prefix's baked path doesn't match the source repo).
    if (const char* env = std::getenv("CHRONON3D_TEMPLATES_DIR");
        env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    // 2. Compile-time baked path (the canonical install path).
#ifdef CHRONON3D_HAS_TEMPLATES_DIR_BAKED
    return std::filesystem::path(CHRONON3D_BAKED_TEMPLATES_DIR);
#else
    return {};  // empty path → caller reports not-found error
#endif
}

bool is_valid_project_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!(std::isalnum(uc) || c == '-' || c == '_')) return false;
    }
    return true;
}

// Recursively copy src → dst, substituting ${PROJECT_NAME} → project_name
// in every regular file.  Atomic per-file write (open in binary mode to
// preserve line endings on Windows; the read happens in text mode for
// std::stringstream simplicity — POSIX line endings only).
//
// Per audit §19: single std::string::find+replace call per file, NOT a
// recursive template engine.  Files with no placeholder are byte-equivalent
// to the source.
void copy_with_substitution(const std::filesystem::path& src_dir,
                            const std::filesystem::path& dst_dir,
                            const std::string& project_name) {
    std::filesystem::create_directories(dst_dir);
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(src_dir)) {
        const auto rel = std::filesystem::relative(entry.path(), src_dir);
        const auto target = dst_dir / rel;
        if (entry.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (entry.is_regular_file()) {
            // Read source content (text mode is fine — POSIX line endings).
            std::ifstream in(entry.path());
            if (!in) {
                throw std::runtime_error("cannot open source file: " +
                                         entry.path().string());
            }
            std::stringstream ss;
            ss << in.rdbuf();
            std::string content = ss.str();

            // Single-pass ${PROJECT_NAME} substitution.
            std::size_t pos = 0;
            while ((pos = content.find(kProjectNamePlaceholder, pos)) !=
                   std::string::npos) {
                content.replace(pos, kProjectNamePlaceholderLen, project_name);
                pos += project_name.size();
            }

            std::filesystem::create_directories(target.parent_path());
            std::ofstream out(target, std::ios::binary);
            if (!out) {
                throw std::runtime_error("cannot write target file: " +
                                         target.string());
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
        }
        // Skip symlinks, special files (none expected in templates/basic/).
    }
}

}  // namespace

void register_create_commands(CLI::App& app, CliContext& ctx) {
    auto state = std::make_shared<CreateState>();
    auto& args = *state->args;

    auto* cmd = app.add_subcommand(
        "create",
        "Scaffold a new Chronon3D project from a versioned template");
    cmd->add_option("<name>", args.name,
                    "Project name (used as directory name; alphanumeric + - + _)")
        ->required();
    cmd->add_option("--template", args.template_name,
                    "Template name (default: basic)")
        ->default_val("basic")
        ->check(CLI::IsMember({"basic"}, CLI::ignore_case));
    cmd->add_flag("--force", args.force,
                  "Overwrite target directory if it exists");
    cmd->allow_windows_style_options();

    cmd->callback([state, &ctx]() {
        const auto& a = *state->args;

        if (!is_valid_project_name(a.name)) {
            fmt::print(stderr,
                "Error: invalid project name '{}'. "
                "Use alphanumeric, dash, underscore (1-64 chars).\n",
                a.name);
            ctx.exit_code = 1;
            return;
        }

        const auto templates_dir = resolve_templates_dir();
        if (templates_dir.empty() ||
            !std::filesystem::is_directory(templates_dir)) {
            fmt::print(stderr,
                "Error: templates directory not found. "
                "Set CHRONON3D_TEMPLATES_DIR or rebuild with "
                "-DCHRONON3D_TEMPLATES_DIR=<path>.\n");
            ctx.exit_code = 1;
            return;
        }

        const auto src = templates_dir / a.template_name;
        if (!std::filesystem::is_directory(src)) {
            fmt::print(stderr,
                "Error: template '{}' not found at {}.\n",
                a.template_name, src.string());
            ctx.exit_code = 1;
            return;
        }

        const auto dst = std::filesystem::current_path() / a.name;
        if (std::filesystem::exists(dst)) {
            if (!a.force) {
                fmt::print(stderr,
                    "Error: directory '{}' already exists. "
                    "Use --force to overwrite.\n",
                    dst.string());
                ctx.exit_code = 1;
                return;
            }
            std::error_code ec;
            std::filesystem::remove_all(dst, ec);
            if (ec) {
                fmt::print(stderr,
                    "Error: failed to remove existing directory '{}': {}\n",
                    dst.string(), ec.message());
                ctx.exit_code = 1;
                return;
            }
        }

        try {
            copy_with_substitution(src, dst, a.name);
        } catch (const std::exception& e) {
            fmt::print(stderr, "Error: copy failed: {}\n", e.what());
            ctx.exit_code = 1;
            return;
        }

        fmt::print("✓ Created project '{}' from template '{}' at {}\n",
                   a.name, a.template_name, dst.string());
        fmt::print("  Next steps:\n");
        fmt::print("    cd {}\n", a.name);
        fmt::print("    cmake -B build\n");
        fmt::print("    cmake --build build\n");
        fmt::print("    ./build/{}\n", a.name);
        ctx.exit_code = 0;
    });
}

}  // namespace chronon3d::cli
