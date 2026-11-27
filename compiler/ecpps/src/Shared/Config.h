#pragma once
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "Machine/Encoders/Context.h"

namespace ecpps
{
     enum struct CompilerStrategy : std::uint8_t
     {
          HighMemory = 0,
          Multithreaded = 1,
          LowMemory = 2,
     };

     extern CompilerStrategy g_compilerStrategy;

     enum struct LinkerUsed : std::uint_fast8_t
     {
          Windows64,
          Windows32,
          Caosys,
          Undefined,
          Windows64Coff,
          Linux64ELF,
     };

     constexpr LinkerUsed DefaultLinker =
#ifdef _WIN64
          LinkerUsed::Windows64;
#elifdef _WIN32
          LinkerUsed::Windows32;
#elifdef __linux__
          LinkerUsed::Linux64ELF;
#else
          LinkerUsed::Undefined;
#endif

     enum struct DiagnosticType : std::uint_fast8_t
     {
          FileNotFound,
     };

     enum struct DiagnosticState : std::uint_fast8_t
     {
          Suppress,
          Warning,
          Error,
     };

     enum struct VerboseFeature : std::uint16_t
     {
          Preprocessor = 1uz << 0uz,
          Tokens = 1uz << 1uz,
          AST = 1uz << 2uz,
          IR = 1uz << 3uz,
          VInst = 1uz << 4uz,
          IInst = 1uz << 5uz,
          PInst = 1uz << 6uz,
          Emit = 1uz << 7uz,
          FinalEmit = 1uz << 8uz,
          Time = 1uz << 9uz,
     };

     using VerboseFeatures = std::uint16_t;

     constexpr VerboseFeatures VerboseFeatureMask(VerboseFeature feature) noexcept
     {
          return std::to_underlying(feature);
     }

     constexpr VerboseFeatures AllVerboseFeatures =
          static_cast<VerboseFeatures>(static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::Preprocessor)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::Tokens)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::AST)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::IR)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::VInst)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::IInst)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::PInst)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::Emit)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::FinalEmit)) |
                                       static_cast<std::uint32_t>(VerboseFeatureMask(VerboseFeature::Time)));

     enum struct StringPooling : std::uint8_t
     {
          None,
          Exact,
          Substring,
     };

     enum struct Optimisation : std::uint64_t // NOLINT
     {
          ConstantFoldArithmetic,
          ConstantFoldArrayIndirections,
          InlineRegularFunctions,
          UseDecInc,
          RemoveRedundantMovs,
          OmitCallingFrame,
          TailJmp,
          XorToZero,
          EncoderOptimisations,
          AggressiveEncoderOptimisations,
          OptimiseDeadJumps,

          Count,
     };

     struct OptimisationFeatureSets
     {
          std::uint32_t maxConstantEvaluationDepth = 0x1000;

          [[nodiscard]]
          constexpr bool IsEnabled(Optimisation optimisation) const noexcept
          {
               return features.test(std::to_underlying(optimisation));
          }

          constexpr void Enable(Optimisation optimisation) noexcept
          {
               features.set(std::to_underlying(optimisation));
          }

          constexpr void Disable(Optimisation optimisation) noexcept
          {
               features.reset(std::to_underlying(optimisation));
          }

          constexpr void Reset(void) noexcept
          {
               features.reset();
          }

          std::bitset<std::to_underlying(Optimisation::Count)> features{};
     };

     enum struct Size : std::uint_fast8_t
     {
          Short,
          Int,
          Long,
          LongLong,
     };

     struct CompilerConfig
     {
          explicit CompilerConfig(int argc,
                                  char* argv[]); // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)

          std::vector<std::string> sourceFiles{};
          std::vector<std::string> includeDirectories{};

          std::unordered_map<DiagnosticType, DiagnosticState> diagnostics{};

          bool warningsAreErrors = false;

          std::string outputImage{};

          LinkerUsed linker = DefaultLinker;

          VerboseFeatures verboseFeatures = 0;

          std::optional<VerboseFeature> firstVerboseFeature{};

          bool dumpToFile = false;
          VerboseFeature dumpFeature = VerboseFeature::Preprocessor;
          std::FILE* dumpFile = nullptr;

          std::vector<std::string> importedLibraries{};

          bool useDebugger = false;

          StringPooling stringPooling = StringPooling::Exact;

          std::vector<char8_t> stringArray{};

          OptimisationFeatureSets optimisations{};

          abi::encoding::CompilationContext target{};
          abi::encoding::CompilationId currentTarget{};

          Size sizeSize{};
          Size ptrdiffSize{};
          Size boolSize{};
          Size intptrSize{};

          [[nodiscard]] bool IsVerbose(VerboseFeature feature) const noexcept
          {
               return (verboseFeatures & VerboseFeatureMask(feature)) != 0;
          }

          [[nodiscard]] bool IsVerbose(void) const noexcept
          {
               return verboseFeatures != 0 && !dumpToFile;
          }

          [[nodiscard]] bool StopsAfter(VerboseFeature feature) const noexcept
          {
               return dumpToFile && dumpFeature == feature;
          }

          void EnableVerbose(VerboseFeature feature) noexcept
          {
               verboseFeatures |= VerboseFeatureMask(feature);
          }

          void DisableVerbose(VerboseFeature feature) noexcept
          {
               verboseFeatures &= static_cast<VerboseFeatures>(~VerboseFeatureMask(feature));
          }

          [[noreturn]] static void PrintVersionAndExit(bool extended = false);

          [[noreturn]] static void PrintHelpAndExit(void);
     };
} // namespace ecpps
