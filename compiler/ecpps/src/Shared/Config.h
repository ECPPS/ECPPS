#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecpps
{
     enum struct CompilerStrategy : std::uint8_t
     {
          HighMemory = 0, // the default
          Multithreaded = 1,
          LowMemory = 2
     };
     inline CompilerStrategy g_compilerStrategy{};

     enum struct LinkerUsed : std::uint_fast8_t
     {
          Windows64,
          Windows32,
          Caosys,
          Undefined
     };

     constexpr LinkerUsed DefaultLinker = LinkerUsed::
#ifdef _WIN64
         Windows64;
#elif defined(_WIN32)
         Windows32;
#else
         Undefined;
#endif

     enum struct DiagnosticType : std::uint_fast8_t
     {
          FileNotFound
     };
     enum struct DiagnosticState : std::uint_fast8_t
     {
          Surpress,
          Warning,
          Error
     };

     enum struct VerboseStatus : std::uint_fast8_t
     {
          Default,
          Verbose,
          ExtraVerbose
     };

     enum struct StringPooling : std::uint8_t
     {
          None,
          Exact,
          Substring
     };

     struct CompilerConfig
     {
          explicit CompilerConfig(int argc,
                                  char* argv[]); // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

          std::vector<std::string> sourceFiles{};
          std::unordered_map<DiagnosticType, DiagnosticState> diagnostics{};
          bool warningsAreErrors = false;
          std::string outputImage{};
          LinkerUsed linker = DefaultLinker;
          VerboseStatus verboseStatus = VerboseStatus::Default;
          std::vector<std::string> importedLibraries{};
          bool useDebugger = false;
          StringPooling stringPooling = StringPooling::Exact;
          std::vector<char8_t> stringArray{};
          std::uint32_t maxConstantEvaluationDepth = 8;

          [[noreturn]] static void PrintVersionAndExit(void);
          [[noreturn]] static void PrintHelpAndExit(void);
     };
} // namespace ecpps
