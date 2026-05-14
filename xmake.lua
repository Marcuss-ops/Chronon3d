-- Project configuration
set_project("Chronon3d")
set_version("0.1.0")

-- Rules and modes
add_rules("mode.debug", "mode.release")
set_languages("c++20")

-- Policies
set_policy("build.warning", true)
set_policy("build.ccache", true)

-- Options
option("profiling")
    set_default(false)
    set_showmenu(true)
option_end()

-- Dependencies
add_requires("glm", "spdlog", "stb", "cli11", "highway", "taskflow", "concurrentqueue", "meshoptimizer", "xxhash", "fmt", "doctest", "toml++")
add_requires("ffmpeg", {configs = {shared = false, avdevice = false, avfilter = false, avformat = true, avcodec = true, swresample = false, swscale = true, postproc = false}})
add_requires("enkits", "mimalloc", "robin-hood-hashing", "tinyexpr")

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

-- Effects Library
target("chronon3d_effects")
    set_kind("static")
    add_files("src/effects/*.cpp")
    add_deps("chronon3d")

-- Renderer Library
target("chronon3d_renderer")
    set_kind("static")
    add_files("src/renderer/software/**.cpp")
    add_files("src/renderer/text/**.cpp")
    add_files("src/renderer/assets/**.cpp")
    add_files("src/renderer/video/**.cpp")
    add_files("src/scene/**.cpp")
    add_files("src/evaluation/**.cpp")
    add_files("src/layout/**.cpp")
    add_files("src/render_graph/**.cpp")
    add_files("src/specscene/**.cpp")

    add_deps("chronon3d", "chronon3d_registry", "chronon3d_cache", "chronon3d_effects", "chronon3d_video")
    add_packages("spdlog", "stb", "highway", "meshoptimizer", "fmt", "xxhash", "toml++", "enkits", "tinyexpr", {public = true})
    
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
target("chronon3d_video")
    set_kind("static")
    add_files("src/video/*.cpp")
    add_deps("chronon3d", "chronon3d_io")
    add_packages("xxhash", "fmt", "ffmpeg", "spdlog", {public = true})

-- Pipeline Library (Interface)
target("chronon3d_pipeline")
    set_kind("headeronly")
    add_deps("chronon3d_renderer", "chronon3d_registry", "chronon3d_cache")
    add_packages("taskflow", "concurrentqueue", {public = true})
    
    if has_config("profiling") then
        add_packages("tracy", {public = true})
    end

-- Examples Library (Host for auto-registration)
target("chronon3d_examples_lib")
    set_kind("static")
    add_files("examples/proofs/**.cpp", "examples/demos/**.cpp", "examples/basics/**.cpp", "examples/debug/**.cpp")
    add_deps("chronon3d", "chronon3d_renderer")
    add_packages("meshoptimizer", "xxhash", "fmt")

-- CLI App
target("chronon3d_cli")
    set_kind("binary")
    add_files("apps/chronon3d_cli/**.cpp")
    add_includedirs("apps/chronon3d_cli")
    add_deps("chronon3d_pipeline", "chronon3d_io", "chronon3d_examples_lib", "chronon3d_video")
    add_packages("cli11", "spdlog", "fmt", "meshoptimizer", "xxhash", "toml++", "mimalloc", "ffmpeg")
    set_rundir("$(projectdir)")

    -- Handle auto-registration link issues by forcing whole archive for examples
    if is_plat("windows") then
        add_ldflags("/WHOLEARCHIVE:chronon3d_examples_lib.lib", {force = true})
    else
        -- chronon3d_renderer must appear after --no-whole-archive so the linker
        -- can resolve references made by the force-extracted examples objects.
        add_ldflags("-Wl,--whole-archive", "-lchronon3d_examples_lib", "-Wl,--no-whole-archive", "-lchronon3d_renderer", "-lchronon3d_video", "-lchronon3d_io", "-lxxhash", "-lavdevice", "-lavfilter", "-lavformat", "-lavcodec", "-lswresample", "-lswscale", "-lavutil", "-ltinyexpr", "-lenkiTS", {force = true})
    end

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
    add_files("tests/video/*.cpp")
    add_deps("chronon3d_pipeline", "chronon3d_io", "chronon3d_video")
    add_packages("doctest", "stb", "meshoptimizer", "xxhash")
