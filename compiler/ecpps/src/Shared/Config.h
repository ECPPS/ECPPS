#pragma once
#include <bitset>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "Machine/Encoders/Context.h"

namespace ecpps
{
     enum struct CompilerStrategy : std::uint8_t
     {
          HighMemory = 0,
          Multithreaded = 1,
          LowMemory = 2
     };

     extern CompilerStrategy g_compilerStrategy;

     enum struct LinkerUsed : std::uint_fast8_t
     {
          Windows64,
          Windows32,
          Caosys,
          Undefined,
          Windows64Coff
     };

     constexpr LinkerUsed DefaultLinker =
#ifdef _WIN64
          LinkerUsed::Windows64;
#elif defined(_WIN32)
          LinkerUsed::Windows32;
#else
          LinkerUsed::Undefined;
#endif

     enum struct DiagnosticType : std::uint_fast8_t
     {
          FileNotFound
     };

     enum struct DiagnosticState : std::uint_fast8_t
     {
          Suppress,
          Warning,
          Error
     };

     enum struct VerboseFeature : std::uint16_t
     {
          Preprocessor = 1u << 0,
          Tokens = 1u << 1,
          AST = 1u << 2,
          IR = 1u << 3,
          VInst = 1u << 4,
          IInst = 1u << 5,
          PInst = 1u << 6,
          Emit = 1u << 7,
          FinalEmit = 1u << 8
     };

     using VerboseFeatures = std::uint16_t;

     constexpr VerboseFeatures VerboseFeatureMask(VerboseFeature feature) noexcept
     {
          return std::to_underlying(feature);
     }

     constexpr VerboseFeatures AllVerboseFeatures =
          VerboseFeatureMask(VerboseFeature::Preprocessor) | VerboseFeatureMask(VerboseFeature::Tokens) |
          VerboseFeatureMask(VerboseFeature::AST) | VerboseFeatureMask(VerboseFeature::IR) |
          VerboseFeatureMask(VerboseFeature::VInst) | VerboseFeatureMask(VerboseFeature::IInst) |
          VerboseFeatureMask(VerboseFeature::PInst) | VerboseFeatureMask(VerboseFeature::Emit) |
          VerboseFeatureMask(VerboseFeature::FinalEmit);

     enum struct StringPooling : std::uint8_t
     {
          None,
          Exact,
          Substring
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

          Count
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
          LongLong
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
               return verboseFeatures != 0;
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
