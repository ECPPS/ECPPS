#pragma once
#include <stdexcept>
#include <string>

namespace ecpps::platformlib
{
     struct NativeException : std::runtime_error
     {
          explicit NativeException(std::string message) : runtime_error(std::move(message)) {}
     };
} // namespace ecpps::platformlib