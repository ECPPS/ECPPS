#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../CodeGeneration/Nodes.h"
#include "../Shared/Config.h"
#include "../Shared/Diagnostics.h"

namespace ecpps
{
     struct SourceFile
     {
          std::string name{};
          std::string contents{};
          Diagnostics diagnostics{};
          std::vector<codegen::Routine> compiledRoutines{};

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
