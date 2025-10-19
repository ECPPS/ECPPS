#pragma once

#include <cstddef>
#include <string>

void RuntimeAssert(bool condition, std::string_view conditionString, std::string message, std::string_view file,
                   std::size_t line, std::string_view function);

#ifdef NDEBUG
#define runtime_assert(condition, message)
template <typename T>
struct Empty
{
     explicit(false) constexpr Empty(auto o) requires std::is_constructible_v<T, decltype(o)> {}
     void operator=(auto o) requires std::is_copy_assignable_v<T, decltype(o)> {}
     void operator=(auto o) requires std::is_move_assignable_v<T, decltype(o)> {}    
};
template <typename T> using DebugOnlyImpl = Empty<T>;
#ifdef _MSC_VER
#define DebugOnly [[msvc::no_unique_address]] DebugOnlyImpl
#else
#define DebugOnly [[no_unique_address]] DebugOnlyImpl
#endif
#else
#define runtime_assert(condition, message) RuntimeAssert(condition, #condition, message, __FILE__, __LINE__, __func__)
template <typename T> using DebugOnly = T;
#endif