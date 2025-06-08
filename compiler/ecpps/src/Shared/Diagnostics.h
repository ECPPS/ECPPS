#pragma once

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
} // namespace ecpps