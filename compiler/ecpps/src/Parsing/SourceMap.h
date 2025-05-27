#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../Shared/Config.h"
#include "../Shared/Diagnostics.h"

namespace ecpps
{
     struct Location
     {
          std::size_t line{};
          std::size_t position{};
          std::size_t endPosition{};

          explicit Location(const std::size_t line, const std::size_t position, const std::size_t endPosition)
              : line(line), position(position), endPosition(endPosition)
          {
          }
     };
     struct SourceFile
     {
          std::string name{};
          std::string contents{};
          Diagnostics diagnostics{};
          explicit SourceFile(void) = default;
     };
     struct SourceMap
     {
          explicit SourceMap(CompilerConfig& config);
          std::vector<SourceFile> files{};

     private:
          std::reference_wrapper<CompilerConfig> _config;
     };
} // namespace ecpps