// SPDX-License-Identifier: MIT
// Precompiled header — includes heavy stable headers to speed up compilation
// This file is automatically included via target_precompile_headers in CMake.

#pragma once

// --- C++ Standard Library ---
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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

// --- fmt (formatting) ---
#include <fmt/core.h>
#include <fmt/format.h>

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
