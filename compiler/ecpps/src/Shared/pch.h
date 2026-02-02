#pragma once

// NOLINTBEGIN()
#ifdef __clang__
#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wall"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Weverything"
#endif

#ifdef _WIN32
#include <corecrt.h> // NOLINT
#endif
#include <algorithm>     // NOLINT
#include <chrono>        // NOLINT
#include <concepts>      // NOLINT
#include <cstdarg>       // NOLINT
#include <cstddef>       // NOLINT
#include <cstdint>       // NOLINT
#include <cstdlib>       // NOLINT
#include <cstring>       // NOLINT
#include <ctime>         // NOLINT
#include <filesystem>    // NOLINT
#include <format>        // NOLINT
#include <fstream>       // NOLINT
#include <functional>    // NOLINT
#include <iostream>      // NOLINT
#include <map>           // NOLINT
#include <memory>        // NOLINT
#include <print>         // NOLINT
#include <ranges>        // NOLINT
#include <stdexcept>     // NOLINT
#include <string>        // NOLINT
#include <string_view>   // NOLINT
#include <type_traits>   // NOLINT
#include <unordered_map> // NOLINT
#include <unordered_set> // NOLINT
#include <utility>       // NOLINT
#include <variant>       // NOLINT
#include <vector>        // NOLINT

#ifdef _WIN32
#include <Windows.h>

#include <DbgHelp.h>
#endif

// NOLINTEND()
#ifdef __clang__
#pragma GCC diagnostic pop
#endif
