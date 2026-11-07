#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "Shared/Error.h"

namespace ecpps::ir
{
     struct Context;

     class DiagnosticSink
     {
     public:
          static void Report(Context& context, diagnostics::DiagnosticsMessage diagnostic);

          template <typename TDiagnostics, typename... TArgs> static void Report(Context& context, TArgs&&... args)
          {
               Report(context, std::make_unique<TDiagnostics>(std::forward<TArgs>(args)...));
          }

          [[nodiscard]] static std::vector<diagnostics::DiagnosticsMessage>& List(Context& context);
          [[nodiscard]] static std::size_t Count(const Context& context);
     };
} // namespace ecpps::ir
