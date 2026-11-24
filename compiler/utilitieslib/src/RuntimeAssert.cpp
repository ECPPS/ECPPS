#include <RuntimeAssert.h>
#include <print>
#include <stdexcept>

void RuntimeAssert(bool condition, std::string_view conditionString, std::string message, std::string_view file,
                   std::size_t line, std::string_view function)
{
     if (condition) return;
     std::println("runtime_assert({}) failed: {} at {}:{} in {}", conditionString, message, file, line, function);

#ifdef _MSC_VER
     __debugbreak();
#endif
     throw std::runtime_error{message};
}
