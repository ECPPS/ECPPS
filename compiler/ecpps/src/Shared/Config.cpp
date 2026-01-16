#include "Config.h"
#include <cctype>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include "../Machine/ABI.h"
#include "../Machine/Machine.h"

ecpps::CompilerConfig::CompilerConfig(
    int argc, char* argv[]) // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
{
     using std::string_literals::operator""s;

     if (argc <= 1) PrintVersionAndExit();

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

     for (std::size_t i = 1; std::cmp_less(i, argc); i++)
     {
          if (argv[i] == nullptr) break;

          std::string_view fullArgument{argv[i]};
          if (fullArgument.empty()) continue;
          if (fullArgument.starts_with('/')) // switch
          {
               auto flag = fullArgument.substr(1);
               auto value = flag.find(':') == std::string::npos ? "" : flag.substr(flag.find(':') + 1);
               flag = flag.substr(0, flag.find(':'));
               const auto lowerFlag =
                   flag |
                   std::views::transform([](const char ch) -> char
                                         { return static_cast<char>(std::tolower(static_cast<int>(ch))); }) |
                   std::ranges::to<std::string>();
               const auto lowerValue =
                   value |
                   std::views::transform([](const char ch) -> char
                                         { return static_cast<char>(std::tolower(static_cast<int>(ch))); }) |
                   std::ranges::to<std::string>();

               if (flag == "WX") this->warningsAreErrors = true;
               else if (flag == "-WX")
                    this->warningsAreErrors = false;
               else if (lowerFlag == "verbose" || lowerFlag == "v")
               {
                    if (lowerValue == "1" || lowerValue == "true" || lowerValue == "on")
                         this->verboseStatus = VerboseStatus::Verbose;
                    else if (lowerValue == "2" || lowerValue == "extra" || lowerValue == "extraverbose" ||
                             lowerValue == "all" || lowerValue == "true" || lowerValue == "on")
                         this->verboseStatus = VerboseStatus::ExtraVerbose;
                    else if (lowerValue == "0" || lowerValue == "false" || lowerValue == "off")
                         this->verboseStatus = VerboseStatus::Default;
                    else
                         this->verboseStatus = VerboseStatus::Verbose;
               }
               else if (flag == "V") { this->verboseStatus = VerboseStatus::ExtraVerbose; }
               else if (lowerFlag == "output" || lowerFlag == "out")
               {
                    if (!this->outputImage.empty()) {} // TODO: Warning
                    this->outputImage = value;
               }
               else if (lowerFlag == "image")
               {
                    if (lowerValue == "win64" || lowerValue == "pe64" || lowerValue == "pe32p")
                         this->linker = LinkerUsed::Windows64;
                    else if (lowerValue == "win32" || lowerValue == "pe32")
                         this->linker = LinkerUsed::Windows32;
                    else if (lowerValue == "cao" || lowerValue == "caosys" || lowerValue == "cao64")
                         this->linker = LinkerUsed::Caosys;
                    else
                    {
                         // TODO: Error
                         std::println("Unknown linker: `{}`", value);
                    }
               }
               else if (flag == "D")
                    this->useDebugger = true;
               else if (flag == "?" || lowerFlag == "help")
                    PrintHelpAndExit();
               else if (std::filesystem::exists("/"s + flag.data()))
                    this->sourceFiles.emplace_back(fullArgument);
               else
               {
                    // TODO: Error list of some sort on invalid flag
                    std::println("Unknown switch: `/{}:{}`", flag, value);
               }
          }
          else
               this->sourceFiles.emplace_back(fullArgument);
     }

     switch (this->linker)
     {
     case LinkerUsed::Windows64:
     case LinkerUsed::Windows32:
          if (this->outputImage.empty()) this->outputImage = "output.exe";
          this->importedLibraries.emplace_back("KERNEL32.dll");
          this->importedLibraries.emplace_back("USER32.dll");
          break;
     case LinkerUsed::Caosys:
          if (this->outputImage.empty()) this->outputImage = "output.exe";
          this->importedLibraries.emplace_back("CAO.dll");
          break;
     default:
          if (this->outputImage.empty()) this->outputImage = "out";
          break;
     }

     abi::ABI::Current().PushSIMDRegisters(simd);
}

void ecpps::CompilerConfig::PrintVersionAndExit(void)
{
     std::println("ECPPS C++ Compiler pre-v0.0.1");
     std::println("Copyright (c) 2025 Tymi. All rights reserved.");
     std::exit(0);
}

void ecpps::CompilerConfig::PrintHelpAndExit(void)
{
     std::println("ECPPS C++ Compiler pre-v0.0.1");
     std::println("Copyright (c) 2025 Tymi. All rights reserved.");

     std::println("Usage:");
     std::println("  ecpps [options] <source files>");
     std::println();
     std::println("Options:");
     std::println("  /? | /help");
     std::println("      Show this help and exit.");
     std::println();
     std::println("  /WX");
     std::println("      Treat warnings as errors.");
     std::println();
     std::println("  /-WX");
     std::println("      Do not treat warnings as errors.");
     std::println();
     std::println("  /verbose[:level] | /v[:level]");
     std::println("      Enable verbose output.");
     std::println("      Levels:");
     std::println("        0, off        Default output");
     std::println("        1, on, true   Verbose output");
     std::println("        2, extra, all Extra verbose output");
     std::println();
     std::println("  /V");
     std::println("      Enable extra verbose output.");
     std::println();
     std::println("  /output:<file> | /out:<file>");
     std::println("      Specify output image name.");
     std::println();
     std::println("  /image:<type>");
     std::println("      Select output image / linker:");
     std::println("        win64, pe64, pe32p   Windows 64-bit");
     std::println("        win32, pe32         Windows 32-bit");
     std::println("        cao, caosys, cao64  CAOSYS image");
     std::println();
     std::println("  /D");
     std::println("      Enable debugger support.");
     std::println();
     std::println("Examples:");
     std::println("  ecpps /WX /image:win64 /out:app.exe main.cpp");
     std::println("  ecpps /verbose:2 test.cpp");
     std::println();

     std::exit(0);
}
