#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "../Machine/Machine.h"

namespace ecpps
{
     enum struct DiagnosticType
     {
          FileNotFound
     };
     enum struct DiagnosticState
     {
          Surpress,
          Warning,
          Error
     };

     struct CompilerConfig
     {
          explicit CompilerConfig(int argc, char* argv[]);

          std::vector<std::string> sourceFiles{};
          std::unordered_map<DiagnosticType, DiagnosticState> diagnostics{};
          bool warningsAreErrors = false;
          abi::CPUFeatures cpuFeatures;
     };
} // namespace ecpps