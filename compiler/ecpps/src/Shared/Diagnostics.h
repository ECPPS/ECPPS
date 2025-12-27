#pragma once

#include <Windows.h>
#include <array>
#include <string>
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

     struct TracedException : std::exception
     {
          std::string msg;
          std::vector<void*> trace;
          std::shared_ptr<std::exception> inner;

          explicit TracedException(std::string message) : msg(std::move(message)) { CaptureStack(); }

          explicit TracedException(const std::exception& ex)
              : msg(ex.what()), inner(std::make_unique<std::decay_t<decltype(ex)>>(ex))
          {
               CaptureStack();
          }

          [[nodiscard]] const char* what(void) const noexcept override { return msg.c_str(); }

     private:
          void CaptureStack(void)
          {
               constexpr ULONG MaxFrames = 64;
               std::array<void*, MaxFrames> frames{};
               USHORT captured = CaptureStackBackTrace(0, MaxFrames, frames.data(), nullptr);
               trace.assign(frames.data(), frames.data() + captured);
          }
     };

     void IssueICE(const TracedException& ex);
     void IssueICE(std::string_view message, CONTEXT* defaultContext);
} // namespace ecpps

using ecpps::TracedException;