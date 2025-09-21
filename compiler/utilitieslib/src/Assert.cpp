#include <Assert.h>
#include <print>

void RuntimeAssert(bool condition, std::string_view conditionString, std::string message, std::string_view file,
                   std::size_t line, std::string_view function)
{
     if (condition) return;
     std::println("runtime_assert({}) failed: {} at {}:{} in {}", conditionString, std::move(message), file, line,
                  function);
}