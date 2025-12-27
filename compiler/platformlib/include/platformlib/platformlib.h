#pragma once
#include <stdexcept>
#include <string>

namespace ecpps::platformlib
{
     struct NativeException : std::runtime_error
     {
          explicit NativeException(const std::string& message) : runtime_error(message) {}
     };
} // namespace ecpps::platformlib