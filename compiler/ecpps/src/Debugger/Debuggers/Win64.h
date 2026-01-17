#pragma once
#include <filesystem>
#include "../Debugger.h"

namespace ecpps::debugging
{
     struct Win64Debugger final : Debugger
     {
          [[nodiscard]] int Debug(CompilerConfig& configuration, std::filesystem::path program) const final override;
     };
} // namespace ecpps::debugging
