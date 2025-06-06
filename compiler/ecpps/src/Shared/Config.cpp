#include "Config.h"
#include <print>
#include <string_view>

ecpps::CompilerConfig::CompilerConfig(int argc, char* argv[])
{
     ecpps::abi::SimdFeatures simd{};
     switch (abi::ABI::Current().Isa())
     {
     case ecpps::abi::ISA::x86_32:
     case ecpps::abi::ISA::x86_64:
          simd = ecpps::abi::SimdFeatures::SSE; // default for SSE for x86
          break;
     case ecpps::abi::ISA::ARM32:
     case ecpps::abi::ISA::ARM64:
          simd = ecpps::abi::SimdFeatures::NEON; // default for NEON for ARM
          break;
     }

     for (std::size_t i = 1; i < argc; i++)
     {
          if (argv[i] == nullptr) break;

          std::string_view fullArgument{argv[i]};
          if (fullArgument.empty()) continue;
          if (fullArgument.starts_with('/')) // switch
          {
               auto flag = fullArgument.substr(1);
               auto value = flag.substr(flag.find(':') + 1);
               flag = flag.substr(0, flag.find(':'));

               if (flag == "/WX") this->warningsAreErrors = true;
               else if (flag == "/-WX")
                    this->warningsAreErrors = false;
               else
               {
                    // TODO: Error list of some sort on invalid flag
                    std::println("Unknown switch: `/{}:{}`", flag, value);
               }
          }
          else
               this->sourceFiles.emplace_back(fullArgument);
     }
     abi::ABI::Current().PushSIMDRegisters(simd);
}
