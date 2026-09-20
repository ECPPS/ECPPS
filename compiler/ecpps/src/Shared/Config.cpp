#include "Config.h"

#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

ecpps::CompilerStrategy ecpps::g_compilerStrategy{};

namespace
{
     using ecpps::Optimisation;
     using ecpps::VerboseFeature;
     using ecpps::VerboseFeatures;
     using ecpps::abi::ArchitectureExtensionFeatures;
     using ecpps::abi::ISA;
     using ecpps::abi::encoding::MicroArch;
     using ecpps::abi::encoding::Platform;
     using ecpps::abi::encoding::SDK;
     struct CommandLineOption
     {
          std::string_view original{};
          std::string_view name{};
          std::string_view value{};
          bool hasValue = false;
          std::size_t index{};
     };

     struct HostTarget
     {
          ISA isa = ISA::x86_64;
          Platform platform = Platform::None;
          SDK sdk = SDK::Unknown;
          ecpps::LinkerUsed linker = ecpps::LinkerUsed::Undefined;
     };

     [[nodiscard]] std::string ToLower(std::string_view value)
     {
          std::string result;
          result.reserve(value.size());

          for (const char ch : value) result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

          return result;
     }

     [[nodiscard]] bool EqualsInsensitive(std::string_view lhs, std::string_view rhs) noexcept
     {
          if (lhs.size() != rhs.size()) return false;

          for (std::size_t i = 0; i < lhs.size(); ++i)
          {
               const auto a = static_cast<unsigned char>(lhs[i]);
               const auto b = static_cast<unsigned char>(rhs[i]);

               if (std::tolower(a) != std::tolower(b)) return false;
          }

          return true;
     }

     [[nodiscard]] CommandLineOption ParseOption(std::string_view argument, std::size_t index)
     {
          CommandLineOption result{
               .original = argument,
               .index = index,
          };

          if (!argument.starts_with('/')) return result;

          argument.remove_prefix(1);

          const auto colon = argument.find(':');

          if (colon == std::string_view::npos)
          {
               result.name = argument;
               return result;
          }

          result.name = argument.substr(0, colon);
          result.value = argument.substr(colon + 1);
          result.hasValue = true;

          return result;
     }

     [[noreturn]] void CommandLineError(std::size_t index, std::string_view argument, std::string_view message)
     {
          std::println(stderr, "ecpps: error: argument {} (`{}`): {}", index, argument, message);

          std::exit(1);
     }

     void CommandLineWarning(std::size_t index, std::string_view argument, std::string_view message)
     {
          std::println(stderr, "ecpps: warning: argument {} (`{}`): {}", index, argument, message);
     }

     void RequireValue(const CommandLineOption& option)
     {
          if (!option.hasValue || option.value.empty())
          {
               CommandLineError(option.index, option.original,
                                std::format("option '/{}' requires a value", option.name));
          }
     }

     [[nodiscard]] std::vector<std::string_view> SplitCommaSeparated(std::string_view value)
     {
          std::vector<std::string_view> result;

          while (true)
          {
               const auto comma = value.find(',');

               if (comma == std::string_view::npos)
               {
                    if (!value.empty()) result.emplace_back(value);
                    break;
               }

               const auto element = value.substr(0, comma);

               if (!element.empty()) result.emplace_back(element);

               value.remove_prefix(comma + 1);
          }

          return result;
     }

     [[nodiscard]] bool ParseUnsigned(std::string_view value, std::uint32_t& destination)
     {
          if (value.empty()) return false;

          const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), destination);

          return ec == std::errc{} && ptr == value.data() + value.size();
     }

     void WarnIfAlreadySet(bool alreadySet, const CommandLineOption& option, std::string_view description)
     {
          if (!alreadySet) return;

          CommandLineWarning(option.index, option.original, std::format("{} overrides an earlier value", description));
     }

     [[nodiscard]] bool ParseOptimisationName(std::string_view value, Optimisation& optimisation)
     {
          const auto name = ToLower(value);

          if (name == "constant-fold-arithmetic" || name == "constfold")
               optimisation = Optimisation::ConstantFoldArithmetic;
          else if (name == "constant-fold-array-indirections" || name == "constfold-array")
               optimisation = Optimisation::ConstantFoldArrayIndirections;
          else if (name == "inline-regular-functions" || name == "inline")
               optimisation = Optimisation::InlineRegularFunctions;
          else if (name == "use-dec-inc" || name == "decinc")
               optimisation = Optimisation::UseDecInc;
          else if (name == "remove-redundant-movs" || name == "rm-movs")
               optimisation = Optimisation::RemoveRedundantMovs;
          else if (name == "omit-calling-frame" || name == "omit-frame")
               optimisation = Optimisation::OmitCallingFrame;
          else if (name == "tail-jmp" || name == "tailjmp")
               optimisation = Optimisation::TailJmp;
          else if (name == "xor-to-zero" || name == "xorzero")
               optimisation = Optimisation::XorToZero;
          else if (name == "encoder")
               optimisation = Optimisation::EncoderOptimisations;
          else if (name == "aggressive-encoder")
               optimisation = Optimisation::AggressiveEncoderOptimisations;
          else
               return false;

          return true;
     }

     void ApplyOptimisationPreset(ecpps::OptimisationFeatureSets& optimisations, std::string_view preset)
     {
          optimisations.Reset();

          if (preset == "0") return;

          if (preset == "1")
          {
               optimisations.Enable(Optimisation::ConstantFoldArithmetic);
               optimisations.Enable(Optimisation::ConstantFoldArrayIndirections);
               optimisations.Enable(Optimisation::RemoveRedundantMovs);
               optimisations.Enable(Optimisation::XorToZero);
               return;
          }

          if (preset == "2")
          {
               ApplyOptimisationPreset(optimisations, "1");

               optimisations.Enable(Optimisation::InlineRegularFunctions);
               optimisations.Enable(Optimisation::UseDecInc);
               optimisations.Enable(Optimisation::OmitCallingFrame);
               optimisations.Enable(Optimisation::TailJmp);
               optimisations.Enable(Optimisation::EncoderOptimisations);
               return;
          }

          if (preset == "3")
          {
               for (std::size_t i : std::views::iota(0uz, std::to_underlying(Optimisation::Count)))
                    optimisations.Enable(static_cast<Optimisation>(i));

               return;
          }

          if (EqualsInsensitive(preset, "s"))
          {
               optimisations.Enable(Optimisation::ConstantFoldArithmetic);
               optimisations.Enable(Optimisation::ConstantFoldArrayIndirections);
               optimisations.Enable(Optimisation::RemoveRedundantMovs);
               optimisations.Enable(Optimisation::XorToZero);
               optimisations.Enable(Optimisation::TailJmp);
               return;
          }

          std::println(stderr, "ecpps: internal error: unknown optimisation preset '{}'", preset);

          std::exit(1);
     }

     [[nodiscard]] bool ParseVerboseFeature(std::string_view value, VerboseFeature& feature)
     {
          const auto name = ToLower(value);

          if (name == "preprocessor" || name == "pp") feature = VerboseFeature::Preprocessor;
          else if (name == "tokens" || name == "token")
               feature = VerboseFeature::Tokens;
          else if (name == "ast")
               feature = VerboseFeature::AST;
          else if (name == "ir")
               feature = VerboseFeature::IR;
          else if (name == "vinst")
               feature = VerboseFeature::VInst;
          else if (name == "iinst")
               feature = VerboseFeature::IInst;
          else if (name == "pinst")
               feature = VerboseFeature::PInst;
          else if (name == "emit")
               feature = VerboseFeature::Emit;
          else if (name == "femit" || name == "final-emit")
               feature = VerboseFeature::FinalEmit;
          else
               return false;

          return true;
     }

     void ParseVerboseFeatures(ecpps::CompilerConfig& config, const CommandLineOption& option)
     {
          RequireValue(option);

          if (EqualsInsensitive(option.value, "all"))
          {
               config.verboseFeatures = ecpps::AllVerboseFeatures;
               return;
          }

          if (EqualsInsensitive(option.value, "none"))
          {
               config.verboseFeatures = 0;
               return;
          }

          for (const auto featureName : SplitCommaSeparated(option.value))
          {
               VerboseFeature feature{};

               if (!ParseVerboseFeature(featureName, feature))
               {
                    CommandLineError(option.index, option.original,
                                     std::format("unknown verbose feature '{}'", featureName));
               }

               config.EnableVerbose(feature);
          }
     }

     [[nodiscard]] bool ParseISA(std::string_view value, ISA& isa)
     {
          if (EqualsInsensitive(value, "x86") || EqualsInsensitive(value, "x86-32") ||
              EqualsInsensitive(value, "i386") || EqualsInsensitive(value, "i686"))
          {
               isa = ISA::x86_32;
               return true;
          }

          if (EqualsInsensitive(value, "x64") || EqualsInsensitive(value, "x86-64") ||
              EqualsInsensitive(value, "amd64"))
          {
               isa = ISA::x86_64;
               return true;
          }

          if (EqualsInsensitive(value, "arm") || EqualsInsensitive(value, "arm32"))
          {
               isa = ISA::ARM32;
               return true;
          }

          if (EqualsInsensitive(value, "arm64") || EqualsInsensitive(value, "aarch64"))
          {
               isa = ISA::ARM64;
               return true;
          }

          return false;
     }

     [[nodiscard]] bool ParsePlatform(std::string_view value, Platform& platform)
     {
          if (EqualsInsensitive(value, "none") || EqualsInsensitive(value, "freestanding"))
          {
               platform = Platform::None;
               return true;
          }

          if (EqualsInsensitive(value, "windows") || EqualsInsensitive(value, "win"))
          {
               platform = Platform::Windows;
               return true;
          }

          if (EqualsInsensitive(value, "linux"))
          {
               platform = Platform::Linux;
               return true;
          }

          return false;
     }

     [[nodiscard]] bool ParseSDK(std::string_view value, SDK& sdk)
     {
          if (EqualsInsensitive(value, "unknown") || EqualsInsensitive(value, "none"))
          {
               sdk = SDK::Unknown;
               return true;
          }

          if (EqualsInsensitive(value, "windows-10") || EqualsInsensitive(value, "windows-sdk-10") ||
              EqualsInsensitive(value, "win10"))
          {
               sdk = SDK::WindowsSDK10;
               return true;
          }

          if (EqualsInsensitive(value, "linux-kernel-7") || EqualsInsensitive(value, "linux-kernel-7.x") ||
              EqualsInsensitive(value, "linux-7"))
          {
               sdk = SDK::LinuxKernel7x;
               return true;
          }

          if (EqualsInsensitive(value, "linux-kernel-7.14") || EqualsInsensitive(value, "linux-7.14"))
          {
               sdk = SDK::LinuxKernel7014;
               return true;
          }

          return false;
     }

     [[nodiscard]] bool ParseMicroArch(std::string_view value, MicroArch& cpu)
     {
          if (EqualsInsensitive(value, "unknown") || EqualsInsensitive(value, "generic"))
          {
               cpu = MicroArch::Unknown;
               return true;
          }

          if (EqualsInsensitive(value, "haswell"))
          {
               cpu = MicroArch::Haswell;
               return true;
          }

          if (EqualsInsensitive(value, "skylake"))
          {
               cpu = MicroArch::Skylake;
               return true;
          }

          return false;
     }

     [[nodiscard]] bool ParseExtension(std::string_view value, ArchitectureExtensionFeatures& extension)
     {
          const auto name = ToLower(value);

          if (name == "sse") extension = ArchitectureExtensionFeatures::SSE;
          else if (name == "sse2")
               extension = ArchitectureExtensionFeatures::SSE2;
          else if (name == "sse3")
               extension = ArchitectureExtensionFeatures::SSE3;
          else if (name == "ssse3")
               extension = ArchitectureExtensionFeatures::SSSE3;
          else if (name == "sse4.1" || name == "sse4_1")
               extension = ArchitectureExtensionFeatures::SSE4_1;
          else if (name == "sse4.2" || name == "sse4_2")
               extension = ArchitectureExtensionFeatures::SSE4_2;
          else if (name == "avx")
               extension = ArchitectureExtensionFeatures::AVX;
          else if (name == "avx2")
               extension = ArchitectureExtensionFeatures::AVX2;
          else if (name == "fma")
               extension = ArchitectureExtensionFeatures::FMA;
          else if (name == "avx512f")
               extension = ArchitectureExtensionFeatures::AVX512F;
          else if (name == "avx512bw")
               extension = ArchitectureExtensionFeatures::AVX512BW;
          else if (name == "avx512dq")
               extension = ArchitectureExtensionFeatures::AVX512DQ;
          else if (name == "avx512vl")
               extension = ArchitectureExtensionFeatures::AVX512VL;
          else if (name == "bmi1")
               extension = ArchitectureExtensionFeatures::BMI1;
          else if (name == "bmi2")
               extension = ArchitectureExtensionFeatures::BMI2;
          else if (name == "lzcnt")
               extension = ArchitectureExtensionFeatures::LZCNT;
          else if (name == "popcnt")
               extension = ArchitectureExtensionFeatures::POPCNT;
          else if (name == "neon")
               extension = ArchitectureExtensionFeatures::NEON;
          else if (name == "asimd")
               extension = ArchitectureExtensionFeatures::ASIMD;
          else if (name == "aes")
               extension = ArchitectureExtensionFeatures::AES;
          else if (name == "sha1")
               extension = ArchitectureExtensionFeatures::SHA1;
          else if (name == "sha2")
               extension = ArchitectureExtensionFeatures::SHA2;
          else if (name == "sve")
               extension = ArchitectureExtensionFeatures::SVE;
          else if (name == "sve2")
               extension = ArchitectureExtensionFeatures::SVE2;
          else if (name == "fp16")
               extension = ArchitectureExtensionFeatures::FP16;
          else if (name == "dotprod")
               extension = ArchitectureExtensionFeatures::DOTPROD;
          else
               return false;

          return true;
     }

     void ParseExtensions(ecpps::CompilerConfig& config, const CommandLineOption& option)
     {
          RequireValue(option);

          if (EqualsInsensitive(option.value, "none"))
          {
               config.target.extensions = ArchitectureExtensionFeatures::None;
               return;
          }

          for (const auto extensionName : SplitCommaSeparated(option.value))
          {
               ArchitectureExtensionFeatures extension{};

               if (!ParseExtension(extensionName, extension))
               {
                    CommandLineError(option.index, option.original,
                                     std::format("unknown target extension '{}'", extensionName));
               }

               config.target.extensions |= extension;
          }
     }

     [[nodiscard]] HostTarget GetHostTarget(void)
     {
          HostTarget result{};

#if defined(_M_ARM64) || defined(__aarch64__)
          result.isa = ISA::ARM64;
#elif defined(_M_ARM) || defined(__arm__)
          result.isa = ISA::ARM32;
#elif defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
          result.isa = ISA::x86_64;
#elif defined(_WIN32) || defined(__i386__) || defined(_M_IX86)
          result.isa = ISA::x86_32;
#else
          result.isa = ISA::x86_64;
#endif

#if defined(_WIN32)
          result.platform = Platform::Windows;

          if (result.isa == ISA::x86_64)
          {
               result.sdk = SDK::WindowsSDK10;
               result.linker = ecpps::LinkerUsed::Windows64;
          }
          else if (result.isa == ISA::x86_32)
          {
               result.sdk = SDK::WindowsSDK10;
               result.linker = ecpps::LinkerUsed::Windows32;
          }
          else
          {
               result.sdk = SDK::WindowsSDK10;
               result.linker = ecpps::LinkerUsed::Undefined;
          }
#elif defined(__linux__)
          result.platform = Platform::Linux;
          result.sdk = SDK::LinuxKernel7x;
          result.linker = ecpps::LinkerUsed::Undefined;
#else
          result.platform = Platform::None;
          result.sdk = SDK::Unknown;
          result.linker = ecpps::LinkerUsed::Undefined;
#endif

          return result;
     }

     void ApplyHostDefaults(ecpps::CompilerConfig& config)
     {
          const auto host = GetHostTarget();

          config.target.isa = host.isa;
          config.target.platform = host.platform;
          config.target.sdk = host.sdk;
          config.target.cpu = MicroArch::Unknown;
          config.target.extensions = ArchitectureExtensionFeatures::None;

          config.linker = host.linker;

          using ecpps::Size;

          switch (host.isa)
          {
          case ISA::x86_32:
          case ISA::ARM32:
               config.sizeSize = Size::Int;
               config.intptrSize = Size::Int;
               config.ptrdiffSize = Size::Int;
               break;

          case ISA::x86_64:
          case ISA::ARM64:
               config.sizeSize = Size::LongLong;
               config.intptrSize = Size::LongLong;
               config.ptrdiffSize = Size::LongLong;
               break;

          default:
               config.sizeSize = Size::Int;
               config.intptrSize = Size::Int;
               config.ptrdiffSize = Size::Int;
               break;
          }
     }

     [[nodiscard]] bool ApplyImagePreset(std::string_view value, ecpps::CompilerConfig& config)
     {
          const auto preset = ToLower(value);

          if (preset == "windows-x64" || preset == "win64" || preset == "pe64" || preset == "pe32p")
          {
               config.linker = ecpps::LinkerUsed::Windows64;

               config.target.isa = ISA::x86_64;
               config.target.platform = Platform::Windows;
               config.target.sdk = SDK::WindowsSDK10;
               config.target.cpu = MicroArch::Unknown;
               config.target.extensions = ArchitectureExtensionFeatures::None;

               config.sizeSize = ecpps::Size::LongLong;
               config.intptrSize = ecpps::Size::LongLong;
               config.ptrdiffSize = ecpps::Size::LongLong;

               return true;
          }

          if (preset == "windows-x64-coff" || preset == "win64-coff" || preset == "coff64")
          {
               config.linker = ecpps::LinkerUsed::Windows64Coff;

               config.target.isa = ISA::x86_64;
               config.target.platform = Platform::Windows;
               config.target.sdk = SDK::WindowsSDK10;
               config.target.cpu = MicroArch::Unknown;
               config.target.extensions = ArchitectureExtensionFeatures::None;

               config.sizeSize = ecpps::Size::LongLong;
               config.intptrSize = ecpps::Size::LongLong;
               config.ptrdiffSize = ecpps::Size::LongLong;

               return true;
          }

          if (preset == "windows-x86" || preset == "win32" || preset == "pe32")
          {
               config.linker = ecpps::LinkerUsed::Windows32;

               config.target.isa = ISA::x86_32;
               config.target.platform = Platform::Windows;
               config.target.sdk = SDK::WindowsSDK10;
               config.target.cpu = MicroArch::Unknown;
               config.target.extensions = ArchitectureExtensionFeatures::None;

               config.sizeSize = ecpps::Size::Int;
               config.intptrSize = ecpps::Size::Int;
               config.ptrdiffSize = ecpps::Size::Int;

               return true;
          }

          if (preset == "caosys" || preset == "cao" || preset == "cao64")
          {
               config.linker = ecpps::LinkerUsed::Caosys;

               config.target.isa = ISA::x86_64;
               config.target.platform = Platform::None;
               config.target.sdk = SDK::Unknown;
               config.target.cpu = MicroArch::Unknown;
               config.target.extensions = ArchitectureExtensionFeatures::None;

               config.sizeSize = ecpps::Size::LongLong;
               config.intptrSize = ecpps::Size::LongLong;
               config.ptrdiffSize = ecpps::Size::LongLong;

               return true;
          }

          if (preset == "linux-x64" || preset == "linux-amd64")
          {
               config.linker = ecpps::LinkerUsed::Undefined;

               config.target.isa = ISA::x86_64;
               config.target.platform = Platform::Linux;
               config.target.sdk = SDK::LinuxKernel7x;
               config.target.cpu = MicroArch::Unknown;
               config.target.extensions = ArchitectureExtensionFeatures::None;

               config.sizeSize = ecpps::Size::LongLong;
               config.intptrSize = ecpps::Size::LongLong;
               config.ptrdiffSize = ecpps::Size::LongLong;

               return true;
          }

          return false;
     }

     void ConfigureOutputDefaults(ecpps::CompilerConfig& config)
     {
          switch (config.linker)
          {
          case ecpps::LinkerUsed::Windows64:
          case ecpps::LinkerUsed::Windows32:
          case ecpps::LinkerUsed::Windows64Coff:
               if (config.outputImage.empty()) config.outputImage = "output.exe";

               config.importedLibraries.emplace_back("KERNEL32.dll");
               config.importedLibraries.emplace_back("USER32.dll");
               break;

          case ecpps::LinkerUsed::Caosys:
               if (config.outputImage.empty()) config.outputImage = "output.exe";

               config.importedLibraries.emplace_back("CAO.dll");
               break;

          default:
               if (config.outputImage.empty()) config.outputImage = "out";
               break;
          }
     }

     void ValidateConfig(const ecpps::CompilerConfig& config)
     {
          if (config.sourceFiles.empty()) return;

          if (config.target.platform == Platform::Windows && config.target.sdk == SDK::Unknown)
          {
               std::println(stderr, "ecpps: warning: Windows target has no SDK selected");
          }

          if (config.target.platform == Platform::Linux && config.target.sdk == SDK::Unknown)
          {
               std::println(stderr, "ecpps: warning: Linux target has no kernel SDK selected");
          }

          if (config.target.isa == ISA::x86_32 &&
              config.target.extensions & (ArchitectureExtensionFeatures::AVX | ArchitectureExtensionFeatures::AVX2 |
                                          ArchitectureExtensionFeatures::AVX512F))
          {
               std::println(stderr, "ecpps: warning: x86-32 target uses x86-64-only extensions");
          }
     }
} // namespace

ecpps::CompilerConfig::CompilerConfig(
     int argc,
     char* argv[]) // NOLINT(cppcoreguidelines-avoid-c-arrays, modernize-avoid-c-arrays)
{
     ApplyHostDefaults(*this);

     if (argc <= 1)
     {
          PrintVersionAndExit(false);
     }

     bool outputWasSpecified = false;
     bool imageWasSpecified = false;

     for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
     {
          if (argv[argumentIndex] == nullptr) break;

          const std::string_view argument{argv[argumentIndex]};

          if (argument.empty()) continue;

          if (!argument.starts_with('/'))
          {
               sourceFiles.emplace_back(argument);
               continue;
          }

          const auto option = ParseOption(argument, static_cast<std::size_t>(argumentIndex));

          if (option.name == "?" || EqualsInsensitive(option.name, "help"))
          {
               PrintHelpAndExit();
          }

          if (EqualsInsensitive(option.name, "version"))
          {
               PrintVersionAndExit(false);
          }

          if (option.name == "v")
          {
               PrintVersionAndExit(true);
          }

          if (option.name == "WX")
          {
               warningsAreErrors = true;
               continue;
          }

          if (option.name == "-WX")
          {
               warningsAreErrors = false;
               continue;
          }

          if (EqualsInsensitive(option.name, "warnings"))
          {
               RequireValue(option);

               const auto value = ToLower(option.value);

               if (value == "errors" || value == "error")
               {
                    warningsAreErrors = true;
                    continue;
               }

               if (value == "default")
               {
                    warningsAreErrors = false;
                    continue;
               }

               if (value == "none")
               {
                    for (auto& [type, state] : diagnostics) state = DiagnosticState::Suppress;

                    continue;
               }

               if (value == "all") continue;

               constexpr std::string_view errorPrefix = "errors:";
               constexpr std::string_view disablePrefix = "no-";

               if (value.starts_with(errorPrefix))
               {
                    const auto warning = value.substr(errorPrefix.size());

                    if (warning.empty())
                    {
                         CommandLineError(option.index, option.original, "missing warning name after 'errors:'");
                    }

                    // TODO: map warning name -> DiagnosticType.
                    continue;
               }

               if (value.starts_with(disablePrefix))
               {
                    const auto warning = value.substr(disablePrefix.size());

                    if (warning.empty())
                    {
                         CommandLineError(option.index, option.original, "missing warning name after 'no-'");
                    }

                    // TODO: map warning name -> DiagnosticType.
                    continue;
               }

               CommandLineError(option.index, option.original, std::format("unknown warnings mode '{}'", option.value));
          }

          if (EqualsInsensitive(option.name, "verbose"))
          {
               if (!option.hasValue)
               {
                    verboseFeatures |= VerboseFeatureMask(VerboseFeature::Preprocessor) |
                                       VerboseFeatureMask(VerboseFeature::Tokens) |
                                       VerboseFeatureMask(VerboseFeature::AST) | VerboseFeatureMask(VerboseFeature::IR);

                    continue;
               }

               ParseVerboseFeatures(*this, option);
               continue;
          }

          if (EqualsInsensitive(option.name, "out") || EqualsInsensitive(option.name, "output"))
          {
               RequireValue(option);

               WarnIfAlreadySet(outputWasSpecified, option, "output file");

               outputWasSpecified = true;
               outputImage = std::string(option.value);
               continue;
          }

          if (option.name == "I" || EqualsInsensitive(option.name, "include"))
          {
               RequireValue(option);
               includeDirectories.emplace_back(option.value);
               continue;
          }

          if (EqualsInsensitive(option.name, "image"))
          {
               RequireValue(option);

               if (imageWasSpecified)
               {
                    CommandLineWarning(option.index, option.original,
                                       "target image preset overrides an earlier target preset");
               }

               if (!ApplyImagePreset(option.value, *this))
               {
                    CommandLineError(option.index, option.original,
                                     std::format("unknown image preset '{}'", option.value));
               }

               imageWasSpecified = true;
               continue;
          }

          if (EqualsInsensitive(option.name, "isa"))
          {
               RequireValue(option);

               ISA isa{};

               if (!ParseISA(option.value, isa))
               {
                    CommandLineError(option.index, option.original, std::format("unknown ISA '{}'", option.value));
               }

               target.isa = isa;
               continue;
          }

          if (EqualsInsensitive(option.name, "platform"))
          {
               RequireValue(option);

               Platform platform{};

               if (!ParsePlatform(option.value, platform))
               {
                    CommandLineError(option.index, option.original, std::format("unknown platform '{}'", option.value));
               }

               target.platform = platform;
               continue;
          }

          if (EqualsInsensitive(option.name, "sdk"))
          {
               RequireValue(option);

               SDK sdk{};

               if (!ParseSDK(option.value, sdk))
               {
                    CommandLineError(option.index, option.original, std::format("unknown SDK '{}'", option.value));
               }

               target.sdk = sdk;
               continue;
          }

          if (EqualsInsensitive(option.name, "cpu"))
          {
               RequireValue(option);

               MicroArch cpu{};

               if (!ParseMicroArch(option.value, cpu))
               {
                    CommandLineError(option.index, option.original,
                                     std::format("unknown microarchitecture '{}'", option.value));
               }

               target.cpu = cpu;
               continue;
          }

          if (EqualsInsensitive(option.name, "extensions"))
          {
               ParseExtensions(*this, option);
               continue;
          }

          if (option.name == "D")
          {
               useDebugger = true;
               continue;
          }

          if (option.name == "O0" || option.name == "O1" || option.name == "O2" || option.name == "O3" ||
              option.name == "Os")
          {
               ApplyOptimisationPreset(optimisations, option.name.substr(1));

               continue;
          }

          if (option.name == "O")
          {
               RequireValue(option);

               auto value = option.value;
               bool disable = false;

               if (value.starts_with("no-"))
               {
                    disable = true;
                    value.remove_prefix(3);
               }

               Optimisation optimisation{};

               if (!ParseOptimisationName(value, optimisation))
               {
                    CommandLineError(option.index, option.original, std::format("unknown optimisation '{}'", value));
               }

               if (disable) optimisations.Disable(optimisation);
               else
                    optimisations.Enable(optimisation);

               continue;
          }

          if (EqualsInsensitive(option.name, "Oconst-depth"))
          {
               RequireValue(option);

               std::uint32_t depth{};

               if (!ParseUnsigned(option.value, depth))
               {
                    CommandLineError(option.index, option.original,
                                     std::format("invalid constant-evaluation depth '{}'", option.value));
               }

               optimisations.maxConstantEvaluationDepth = depth;
               continue;
          }

          CommandLineError(option.index, option.original, std::format("unknown option '/{}'", option.name));
     }

     currentTarget = target.MakeId();

     ConfigureOutputDefaults(*this);
     ValidateConfig(*this);
}

void ecpps::CompilerConfig::PrintVersionAndExit(bool extended)
{
     std::println("ECPPS C++ Compiler pre-v0.0.1");
     std::println("Copyright (c) 2026 Tymi. All rights reserved.");

     if (!extended) std::exit(0);

     std::println();
     std::println("Build information:");

#if defined(_WIN64)
     std::println("  Host architecture: x86-64");
#elif defined(_WIN32)
     std::println("  Host architecture: x86");
#elif defined(__aarch64__)
     std::println("  Host architecture: AArch64");
#elif defined(__arm__)
     std::println("  Host architecture: ARM32");
#else
     std::println("  Host architecture: unknown");
#endif

#if defined(_WIN32)
     std::println("  Host platform: Windows");
#elif defined(__linux__)
     std::println("  Host platform: Linux");
#else
     std::println("  Host platform: unknown");
#endif

#if defined(NDEBUG)
     std::println("  Build configuration: Release");
#else
     std::println("  Build configuration: Debug");
#endif

     std::println("  Compiler strategy: {}",
                  []
                  {
                       switch (g_compilerStrategy)
                       {
                       case CompilerStrategy::HighMemory: return "high-memory";
                       case CompilerStrategy::Multithreaded: return "multithreaded";
                       case CompilerStrategy::LowMemory: return "low-memory";
                       }

                       return "unknown";
                  }());

     std::println("  Default target ISA: {}",
                  []
                  {
                       switch (GetHostTarget().isa)
                       {
                       case ISA::x86_32: return "x86-32";

                       case ISA::x86_64: return "x86-64";

                       case ISA::ARM32: return "ARM32";

                       case ISA::ARM64: return "ARM64";
                       }

                       return "unknown";
                  }());

     std::println("  Default target platform: {}",
                  []
                  {
                       switch (GetHostTarget().platform)
                       {
                       case Platform::None: return "freestanding";

                       case Platform::Windows: return "Windows";

                       case Platform::Linux: return "Linux";
                       }

                       return "unknown";
                  }());

     std::println("  Default target SDK: {}",
                  []
                  {
                       switch (GetHostTarget().sdk)
                       {
                       case SDK::Unknown: return "unknown";

                       case SDK::WindowsSDK10: return "Windows SDK 10";

                       case SDK::LinuxKernel7x: return "Linux kernel 7.x";

                       case SDK::LinuxKernel7014: return "Linux kernel 7.14";
                       }

                       return "unknown";
                  }());

     std::exit(0);
}

void ecpps::CompilerConfig::PrintHelpAndExit(void)
{
     std::println("ECPPS C++ Compiler pre-v0.0.1");
     std::println("Copyright (c) 2026 Tymi. All rights reserved.");

     std::println();
     std::println("Usage:");
     std::println("  ecpps [options] <source files>");

     std::println();
     std::println("General:");
     std::println("  /?");
     std::println("  /help");
     std::println("      Show this help message.");
     std::println();
     std::println("  /version");
     std::println("      Show the compiler version.");
     std::println();
     std::println("  /v");
     std::println("      Show the compiler version and extended build information.");

     std::println();
     std::println("Warnings:");
     std::println("  /warnings:<mode>");
     std::println("      Configure warning handling.");
     std::println("      Modes:");
     std::println("        default       Default warning handling");
     std::println("        all           Enable all warnings");
     std::println("        errors        Treat warnings as errors");
     std::println("        none          Suppress warnings");
     std::println();
     std::println("  /WX");
     std::println("      Alias for /warnings:errors.");
     std::println();
     std::println("  /-WX");
     std::println("      Restore default warning handling.");

     std::println();
     std::println("Diagnostics:");
     std::println("  /verbose");
     std::println("      Enable the default verbose pipeline diagnostics.");
     std::println();
     std::println("  /verbose:<features>");
     std::println("      Enable selected compiler pipeline diagnostics.");
     std::println();
     std::println("      Features:");
     std::println("        preprocessor");
     std::println("        tokens");
     std::println("        ast");
     std::println("        ir");
     std::println("        vinst");
     std::println("        iinst");
     std::println("        pinst");
     std::println("        emit");
     std::println("        femit");
     std::println();
     std::println("      Special values:");
     std::println("        all");
     std::println("        none");
     std::println();
     std::println("      Example:");
     std::println("        /verbose:tokens,ast,ir");

     std::println();
     std::println("Target:");
     std::println("  /image:<preset>");
     std::println("      Select a complete compilation target preset.");
     std::println("      Presets:");
     std::println("        windows-x64");
     std::println("        windows-x64-coff");
     std::println("        windows-x86");
     std::println("        linux-x64");
     std::println("        caosys");
     std::println();
     std::println("      If no target options are specified, the host target");
     std::println("      is selected automatically.");
     std::println();
     std::println("  /isa:<isa>");
     std::println("      Select the target instruction-set architecture.");
     std::println("      Values:");
     std::println("        x86, x86-32, i386");
     std::println("        x64, x86-64, amd64");
     std::println("        arm, arm32");
     std::println("        arm64, aarch64");
     std::println();
     std::println("  /platform:<platform>");
     std::println("      Select the target platform.");
     std::println("      Values: none, windows, linux");
     std::println();
     std::println("  /sdk:<sdk>");
     std::println("      Select the target SDK.");
     std::println("      Values:");
     std::println("        windows-10");
     std::println("        linux-kernel-7");
     std::println("        linux-kernel-7.14");
     std::println("        unknown");
     std::println();
     std::println("  /cpu:<microarchitecture>");
     std::println("      Select the target microarchitecture.");
     std::println("      Values: generic, haswell, skylake");
     std::println();
     std::println("  /extensions:<extensions>");
     std::println("      Select target architecture extensions.");
     std::println("      Multiple extensions may be comma-separated.");

     std::println();
     std::println("Output:");
     std::println("  /out:<file>");
     std::println("  /output:<file>");
     std::println("      Specify the output image name.");

     std::println();
     std::println("Includes:");
     std::println("  /I:<directory>");
     std::println("  /include:<directory>");
     std::println("      Add an include directory.");

     std::println();
     std::println("Debugging:");
     std::println("  /D");
     std::println("      Enable debugger support.");

     std::println();
     std::println("Optimisation:");
     std::println("  /O0");
     std::println("      Disable all optimisations.");
     std::println();
     std::println("  /O1");
     std::println("      Enable the efficient optimisation preset.");
     std::println();
     std::println("  /O2");
     std::println("      Enable the very efficient optimisation preset.");
     std::println();
     std::println("  /O3");
     std::println("      Enable all optimisations.");
     std::println();
     std::println("  /Os");
     std::println("      Enable the size-oriented optimisation preset.");
     std::println();
     std::println("  /O:<optimisation>");
     std::println("      Enable an individual optimisation.");
     std::println();
     std::println("  /O:no-<optimisation>");
     std::println("      Disable an individual optimisation.");
     std::println();
     std::println("  /Oconst-depth:<depth>");
     std::println("      Set the maximum constant-evaluation depth.");

     std::println();
     std::println("Examples:");
     std::println("  ecpps /WX /image:windows-x64 /out:app.exe main.cpp");
     std::println("  ecpps /O2 /O:no-inline-regular-functions main.cpp");
     std::println("  ecpps /O3 /O:no-aggressive-encoder main.cpp");
     std::println("  ecpps /verbose:tokens,ast,ir test.cpp");
     std::println("  ecpps /image:windows-x64 /cpu:haswell main.cpp");
     std::println("  ecpps /image:windows-x64 /extensions:sse4.2,avx2 main.cpp");

     std::exit(0);
}
