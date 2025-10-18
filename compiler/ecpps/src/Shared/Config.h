#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecpps
{
     enum struct LinkerUsed : std::uint_fast8_t
     {
          Windows64,
          Windows32,
          Caosys
     };

     constexpr LinkerUsed DefaultLinker = LinkerUsed::
#ifdef _WIN64
         Windows64;
#elif defined(_WIN32)
         Windows32;
#else
         Windows64; // fallback to x64 Windows
#endif

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

     enum struct VerboseStatus : std::uint8_t
     {
          Default,
          Verbose,
          ExtraVerbose
     };

     struct CompilerConfig
     {
          explicit CompilerConfig(int argc, char* argv[]);

          std::vector<std::string> sourceFiles{};
          std::unordered_map<DiagnosticType, DiagnosticState> diagnostics{};
          bool warningsAreErrors = false;
          std::string outputImage{};
          LinkerUsed linker = DefaultLinker;
          VerboseStatus verboseStatus = VerboseStatus::Default;
          std::vector<std::string> importedLibraries{};

          [[noreturn]] void PrintVersionAndExit(void) const;
     };
} // namespace ecpps