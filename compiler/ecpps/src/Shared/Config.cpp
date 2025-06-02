#include "Config.h"
#include <print>
#include <string_view>

ecpps::CompilerConfig::CompilerConfig(int argc, char* argv[])
     : cpuFeatures(abi::CPUFeatures::CurrentISA(), abi::SimdFeatures::None)
{
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
}
