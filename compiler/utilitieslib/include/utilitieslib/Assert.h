#pragma once

#include <string>
#include <cstddef>

void RuntimeAssert(bool condition, std::string_view conditionString, std::string message, std::string_view file,
                   std::size_t line, std::string_view function);

#ifdef NDEBUG
#define runtime_assert(condition, message) 
#else
#define runtime_assert(condition, message) RuntimeAssert(condition, message, __FILE__, __LINE__, __func__)
#endif