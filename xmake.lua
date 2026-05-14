-- Project configuration
set_project("Chronon3d")
set_version("0.1.0")

-- Rules and modes
add_rules("mode.debug", "mode.release", "mode.asan", "mode.tsan")
set_languages("c++20")

-- Policies
set_policy("build.warning", true)
set_policy("build.ccache", true)
set_policy("build.across_targets_in_parallel", true)

-- Options
option("profiling")
    set_default(false)
    set_showmenu(true)
option("examples")
    set_default(false)
    set_showmenu(true)
option("video")
    set_default(false)
    set_showmenu(true)
    set_description("Enable FFmpeg-based video output and tests")
option_end()

-- Dependencies
add_requires("glm", "spdlog", "stb", "cli11", "highway", "taskflow", "concurrentqueue", "meshoptimizer", "xxhash", "fmt", "doctest", "toml++")
add_requires("enkits", "mimalloc", "robin-hood-hashing", "tinyexpr")

if has_config("video") then
    add_requires("ffmpeg", {configs = {shared = false, avdevice = false, avfilter = false, avformat = true, avcodec = true, swresample = false, swscale = true, postproc = false}})
end

if has_config("profiling") then
    add_requires("tracy", {configs = {on_demand = true}})
end

-- Core Library (Interface)
target("chronon3d")
    set_kind("headeronly")
    add_headerfiles("include/(chronon3d/**.hpp)")
    add_includedirs("include", {public = true})
    add_packages("glm", {public = true})
    
    if is_plat("windows") then
        add_cxflags("/EHsc", "/fp:strict", "/utf-8", {public = true})
        add_defines("_DISABLE_STRING_VIEW_FIND_FIRST_NOT_OF_OPTIMIZATION", {public = true})
    else
        add_cxflags("-ffp-contract=off", {public = true})
    end

-- Core Implementation Library
target("chronon3d_core")
    set_kind("static")
    add_files("src/core/**.cpp")
    add_deps("chronon3d")
    add_packages("meshoptimizer", "taskflow")
    if has_config("examples") then
        add_defines("CHRONON3D_BUILD_EXAMPLES")
    end
    after_load(function(target)
        for _, dep in ipairs(target:deps()) do
            assert(dep:name() ~= "chronon3d_video",
                "[GUARDRAIL] chronon3d_core non può dipendere da chronon3d_video (FFmpeg nel percorso caldo)")
        end
    end)

-- Registry Library
target("chronon3d_registry")
    set_kind("static")
    add_files("src/registry/*.cpp")
    add_deps("chronon3d")

-- Cache Library
target("chronon3d_cache")
    set_kind("static")
    add_files("src/cache/*.cpp")
    add_deps("chronon3d")
    add_packages("xxhash", "robin-hood-hashing", {public = true})

-- Render Graph Library
target("chronon3d_graph")
    set_kind("static")
    add_files("src/render_graph/**.cpp", "src/renderer/software/graph/**.cpp")
    add_deps("chronon3d", "chronon3d_cache")
    after_load(function(target)
        for _, dep in ipairs(target:deps()) do
            assert(dep:name() ~= "chronon3d_video",
                "[GUARDRAIL] chronon3d_graph non può dipendere da chronon3d_video (FFmpeg nel percorso caldo)")
        end
    end)
    add_packages("fmt", "highway", "meshoptimizer", "xxhash", "toml++", {public = true})

-- Effects Library
target("chronon3d_effects")
    set_kind("static")
    add_files("src/effects/*.cpp", "src/effects/evaluation/*.cpp")
    add_deps("chronon3d")
    add_packages("meshoptimizer", {public = true})

-- Software Renderer Library
target("chronon3d_software")
    set_kind("static")
    add_files("src/renderer/software/*.cpp")
    add_files("src/renderer/software/rasterizers/*.cpp")
    add_files("src/renderer/software/specialized/*.cpp")
    add_files("src/renderer/software/utils/*.cpp")
    add_files("src/renderer/text/*.cpp")
    add_files("src/renderer/assets/*.cpp")
    add_files("src/renderer/video/*.cpp")
    add_deps("chronon3d", "chronon3d_graph", "chronon3d_cache", "chronon3d_effects")
    add_packages("spdlog", "stb", "highway", "fmt", "xxhash", "toml++", "enkits", "tinyexpr", {public = true})
    if has_config("video") then
        add_defines("CHRONON_WITH_VIDEO", {public = true})
    end

-- Renderer Implementation Library
target("chronon3d_renderer")
    set_kind("static")
    add_files("src/scene/**.cpp")
    add_files("src/layout/**.cpp")
    add_files("src/specscene/**.cpp")
    add_deps("chronon3d", "chronon3d_core", "chronon3d_graph", "chronon3d_software", "chronon3d_registry", "chronon3d_cache", "chronon3d_effects", "chronon3d_io")
    add_packages("spdlog", "stb", "fmt", "toml++", {public = true})
    if has_config("profiling") then
        add_packages("tracy")
        add_defines("CHRONON_PROFILING", "TRACY_ON_DEMAND")
    end

-- IO Library
target("chronon3d_io")
    set_kind("static")
    add_files("src/io/*.cpp")
    add_deps("chronon3d")
    add_packages("stb")

-- Video Library
if has_config("video") then
    target("chronon3d_video")
        set_kind("static")
        add_files("src/video/*.cpp")
        add_deps("chronon3d", "chronon3d_io")
        add_packages("xxhash", "fmt", "ffmpeg", "spdlog", {public = true})
end

-- Pipeline Library (Interface)
target("chronon3d_pipeline")
    set_kind("headeronly")
    add_deps("chronon3d_renderer", "chronon3d_core", "chronon3d_graph", "chronon3d_software", "chronon3d_registry", "chronon3d_cache")
    add_packages("taskflow", "concurrentqueue", {public = true})
    
    if has_config("profiling") then
        add_packages("tracy", {public = true})
    end

if has_config("examples") then
    -- Examples Library (Host for auto-registration)
    target("chronon3d_examples_lib")
        set_kind("object")
        add_files("examples/proofs/**.cpp", "examples/demos/**.cpp", "examples/basics/**.cpp", "examples/debug/**.cpp")
        add_deps("chronon3d", "chronon3d_renderer")
        add_packages("meshoptimizer", "xxhash", "fmt")
end

-- CLI App
target("chronon3d_cli")
    set_kind("binary")
    add_files(
        "apps/chronon3d_cli/main.cpp",
        "apps/chronon3d_cli/proof_suites.cpp",
        "apps/chronon3d_cli/utils/cli_utils.cpp",
        "apps/chronon3d_cli/commands/command_list.cpp",
        "apps/chronon3d_cli/commands/command_info.cpp",
        "apps/chronon3d_cli/commands/command_render.cpp",
        "apps/chronon3d_cli/commands/command_batch.cpp",
        "apps/chronon3d_cli/commands/command_watch.cpp",
        "apps/chronon3d_cli/commands/command_bench.cpp",
        "apps/chronon3d_cli/commands/command_graph.cpp",
        "apps/chronon3d_cli/commands/command_proofs.cpp"
    )
    add_includedirs("apps/chronon3d_cli")
    add_deps("chronon3d_pipeline", "chronon3d_io")
    if has_config("examples") then
        add_deps("chronon3d_examples_lib")
    end
    add_packages("cli11", "spdlog", "fmt", "meshoptimizer", "xxhash", "toml++", "mimalloc")
    if has_config("video") then
        add_files("apps/chronon3d_cli/utils/ffmpeg_runner.cpp")
        add_files("apps/chronon3d_cli/commands/command_video.cpp")
        add_deps("chronon3d_video")
        add_packages("ffmpeg")
        add_defines("CHRONON_WITH_VIDEO")
    end
    set_rundir("$(projectdir)")

-- Tests
target("chronon3d_tests")
    set_kind("binary")
    set_rundir("$(projectdir)")
    add_files("tests/*.cpp")
    add_files("tests/core/*.cpp")
    add_files("tests/math/*.cpp")
    add_files("tests/geometry/*.cpp")
    add_files("tests/animation/*.cpp")
    add_files("tests/timeline/*.cpp")
    add_files("tests/scene/*.cpp")
    add_files("tests/renderer/*.cpp")
    add_files("tests/registry/*.cpp")
    add_files("tests/cache/*.cpp")
    add_files("tests/render_graph/*.cpp")
    add_files("tests/effects/*.cpp")
    add_files("tests/deterministic/*.cpp")
    add_files("tests/io/*.cpp")
    add_files("tests/assets/*.cpp")
    add_files("tests/description/*.cpp")
    add_files("tests/evaluation/*.cpp")
    if has_config("video") then
        add_files("tests/video/*.cpp")
        add_deps("chronon3d_video")
        add_packages("ffmpeg")
        add_defines("CHRONON_WITH_VIDEO")
    end
    add_deps("chronon3d_pipeline", "chronon3d_io")
    add_packages("doctest", "stb", "meshoptimizer", "xxhash")

-- Compile-time build guard: verifica che FFmpeg non sia nel percorso caldo
target("test_build_graph")
    set_kind("binary")
    set_default(false)
    add_files("tests/build/test_no_video_in_core.cpp")
    add_deps("chronon3d_pipeline")
    add_packages("fmt", "xxhash")
    -- NON aggiungere chronon3d_video né la define CHRONON_WITH_VIDEO
