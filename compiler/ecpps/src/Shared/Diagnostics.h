#pragma once

#include "Error.h"
#include <vector>

namespace ecpps
{
     struct Diagnostics
     {
          explicit Diagnostics(void) = default;
          Diagnostics(const Diagnostics&) = delete;
          Diagnostics(Diagnostics&&) = default;

          std::vector<diagnostics::DiagnosticsMessage> diagnosticsList{};
     };
} // namespace ecpps