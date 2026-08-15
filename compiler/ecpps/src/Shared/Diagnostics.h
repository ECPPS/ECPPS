#pragma once

#include <platformlib/platformlib.h>
#include <exception>
#include <stacktrace>
#include <string>
#include <unordered_map>
#include <vector>
#include "Error.h"

namespace ecpps
{
     struct Diagnostics
     {
          explicit Diagnostics(void) = default;
          Diagnostics(const Diagnostics&) = delete;
          Diagnostics(Diagnostics&&) = default;

          std::vector<diagnostics::DiagnosticsMessage> diagnosticsList{};
     };

     class TracedException final : public std::exception
     {
     public:
          explicit TracedException(std::string message);
          explicit TracedException(std::string message, const std::exception_ptr& inner);
          explicit TracedException(const std::exception& exception)
              : _message(exception.what()), _trace(std::stacktrace::current(1)),
                _inner(std::make_exception_ptr(exception))
          {
          }

          [[nodiscard]] const char* what(void) const noexcept override
          {
               return this->_message.c_str();
          }
          [[nodiscard]] const std::stacktrace& Trace(void) const noexcept
          {
               return this->_trace;
          }
          [[nodiscard]] const std::exception_ptr& Inner(void) const noexcept
          {
               return this->_inner;
          }

     private:
          std::string _message;
          std::stacktrace _trace;
          std::exception_ptr _inner;
     };

     [[noreturn]] void IssueICE(const TracedException& exception);
     [[noreturn]] void IssueICE(std::string_view message);
     [[noreturn]] void IssueICE(std::string_view message, const std::stacktrace& trace);
     [[noreturn]] void IssueICE(std::string_view message, void* implementationDefined);

     extern std::unordered_map<std::string, ecpps::Diagnostics*> g_diagnosticsReferences;

     void RegisterErrorCallbacks(void);
} // namespace ecpps

using ecpps::TracedException;
