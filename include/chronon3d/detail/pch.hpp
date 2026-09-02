// SPDX-License-Identifier: MIT
// Precompiled header — includes heavy stable headers to speed up compilation
// This file is automatically included via target_precompile_headers in CMake.

#pragma once

// --- C++ Standard Library ---
#include <algorithm>
#include <array>
#include <atomic>
// FIX: GCC 14 + PCH + Unity build — <chrono> cascades through
// /usr/include/c++/14/chrono (transitively-visible headers like
// <ratio>/<version> lost across unity-aggregator boundaries,
// 'time_point' and friends fail to resolve). Same issue for the
// <fmt/format.h> formatting surface.  Removed from the PCH; the
// small number of TUs that need each include it directly (compiler
// cache makes the per-TU cost negligible).  Add back when the
// toolchain matures past the GCC-14 + C++20 + Unity + PCH
// interaction cascade.
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// --- spdlog (logging) ---
#include <spdlog/spdlog.h>

// --- glm (math) ---
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

// --- TBB (parallelism) ---
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
