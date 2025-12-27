#pragma once

#include <filesystem>
#include "../Shared/Config.h"

namespace ecpps::debugging
{
     struct Debugger
     {
          virtual ~Debugger(void) = default;
          [[nodiscard]] static int SelectAndDebug(CompilerConfig& configuration, std::filesystem::path program);

          [[nodiscard]] virtual int Debug(CompilerConfig& configuration, std::filesystem::path program) const = 0;
     };
} // namespace ecpps::debugging
