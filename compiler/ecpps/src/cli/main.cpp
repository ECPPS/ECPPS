#include "Execution/Context.h"
#include "Machine/ABI.h"
#include "Machine/Encoders/API/Target.h"
#include "Machine/Encoders/Context.h"
#include "Machine/Encoders/InstructionEncoder.h"
#include "Machine/Machine.h"
#include "TypeSystem/TypeBase.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <print>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <CodeGeneration/CodeEmitter.h>
#include <CodeGeneration/PseudoAssembly.h>
#include <Debugger/Debugger.h>
#include <Execution/IR.h>
#include <FileSystem/SourceScanner.h>
#include <Linker/Linker.h>
#include <Parsing/AST.h>
#include <Parsing/Preprocessor.h>
#include <Parsing/SourceMap.h>
#include <Parsing/Tokeniser.h>
#include <Shared/Config.h>
#include <Shared/Diagnostics.h>

#ifdef _WIN32
static void EnableVirtualProcessing(void)
{
     auto* const handle = GetStdHandle(STD_OUTPUT_HANDLE);
     if (handle == INVALID_HANDLE_VALUE) return;

     DWORD mode{};
     if (!GetConsoleMode(handle, &mode)) return;

     mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
     SetConsoleMode(handle, mode);
}
#endif

namespace
{
     using namespace ecpps;

     enum struct FileIterationStatus : bool
     {
          Success = true,
          Failure = false
     };

     [[nodiscard]] bool IsDiagnosticsCritical(const diagnostics::DiagnosticsMessage& diagnostic,
                                              const CompilerConfig& config)
     {
          return diagnostic->Level() == diagnostics::DiagnosticsLevel::Error ||
                 (config.warningsAreErrors && diagnostic->Level() == diagnostics::DiagnosticsLevel::Warning);
     }

     [[nodiscard]] bool HasFatalDiagnostics(const SourceFile& source, const CompilerConfig& config)
     {
          return std::ranges::any_of(source.diagnostics.diagnosticsList,
                                     [&config](const auto& diagnostic)
                                     {
                                          return IsDiagnosticsCritical(diagnostic, config);
                                     });
     }

     void PrintDiagnostics(SourceFile& source)
     {
          for (const auto& diagnostic : source.diagnostics.diagnosticsList)
               diagnostics::PrintDiagnostic(source.name, diagnostic);
     }

     void PrintHexDump(std::span<const std::byte> bytes, std::size_t rowSize = 8)
     {
          if (bytes.empty()) return;

          const auto rows = (bytes.size() + rowSize - 1) / rowSize;

          for (std::size_t row = 0; row < rows; ++row)
          {
               std::print("| ");

               const auto offset = row * rowSize;

               for (std::size_t column = 0; column < rowSize; ++column)
               {
                    const auto byteOffset = offset + column;

                    if (byteOffset >= bytes.size()) std::print("   ");
                    else
                         std::print("{:02x} ", std::to_integer<unsigned int>(bytes[byteOffset]));
               }

               std::println("|");
          }
     }

     void PrintStringTable(const CompilerConfig& config)
     {
          std::println();
          std::println("String Table Dump:");
          std::println("Size: {} bytes", config.stringArray.size());

          if (config.stringArray.empty()) return;

          constexpr std::size_t rowSize = 16;
          const auto rows = (config.stringArray.size() + rowSize - 1) / rowSize;

          for (std::size_t row = 0; row < rows; ++row)
          {
               const auto offset = row * rowSize;

               std::print("{:08x}: ", offset);

               for (std::size_t column = 0; column < rowSize; ++column)
               {
                    const auto byteOffset = offset + column;

                    if (byteOffset >= config.stringArray.size()) std::print("   ");
                    else
                         std::print("{:02x} ", static_cast<unsigned int>(config.stringArray[byteOffset]));

                    if (column == 7) std::print(" ");
               }

               std::print(" |");

               for (std::size_t column = 0; column < rowSize; ++column)
               {
                    const auto byteOffset = offset + column;

                    if (byteOffset >= config.stringArray.size())
                    {
                         std::print(" ");
                         continue;
                    }

                    const auto byte = static_cast<unsigned char>(config.stringArray[byteOffset]);

                    if (byte >= 32 && byte < 127) std::print("{}", static_cast<char>(byte));
                    else
                         std::print(".");
               }

               std::println("|");
          }

          std::println();
     }

     void PrintVirtualInstructions(const SourceFile& source)
     {
          std::println();
          std::println("Virtual Instructions:");

          for (const auto& routine : source.compiledRoutines)
          {
               std::println("  {}", routine.name);

               for (const auto& instruction : routine.virtualInstructions)
               {
                    std::string operands;

                    for (const auto operand : instruction.operands) operands += std::format("{}, ", operand.index);

                    if (!operands.empty())
                    {
                         operands.pop_back();
                         operands.pop_back();
                    }

                    std::println("    {} {}", ToString(instruction.type), operands);
               }
          }
     }

     void PrintPhysicalInstructions(const SourceFile& source, const abi::api::Target& target)
     {
          std::println();
          std::println("Physical Instructions:");

          for (const auto& routine : source.compiledRoutines)
          {
               std::println("  {}", routine.name);

               for (const auto& instruction : routine.physicalInstructions)
                    std::println("    {}", target.encoder->Stringify(instruction));
          }
     }

     void PrintEmittedRoutines(const std::vector<std::byte>& generatedMachineCode,
                               const std::vector<std::pair<std::string, std::size_t>>& functions)
     {
          if (functions.empty()) return;

          std::vector<std::pair<std::string, std::size_t>> ordered = functions;

          std::ranges::sort(ordered, {}, &std::pair<std::string, std::size_t>::second);

          std::println();

          for (std::size_t i = 0; i < ordered.size(); ++i)
          {
               const auto& [routineName, routineOffset] = ordered[i];

               std::println("{}:", routineName);

               const auto start = std::min(routineOffset, generatedMachineCode.size());

               const auto end = i + 1 < ordered.size() ? std::min(ordered[i + 1].second, generatedMachineCode.size())
                                                       : generatedMachineCode.size();

               if (start >= end) continue;

               const auto bytes = std::span{generatedMachineCode}.subspan(start, end - start);

               std::println("Emitted {} bytes:", bytes.size());
               PrintHexDump(bytes);
          }
     }

     void PrintTypeStatistics(void)
     {
          std::println("Instantiated \x1b[92m{}\x1b[0m types:", ir::GetTypeContext().Count());

          for (const auto& type : ir::GetTypeContext().List())
          {
               std::println("     \x1b[35m{}\x1b[0m "
                            "\x1b[94m=> \x1b[92m{} "
                            "\x1b[33mreference{}\x1b[0m",
                            type.typePointer->RawName(), type.hitCount, type.hitCount == 1 ? "" : "s");
          }

          std::println("Created \x1b[92m{}\x1b[0m entities:", ir::GetEntityStatistics().Count());

          for (const auto& [kind, entries] : ir::GetEntityStatistics().Entries())
          {
               std::println("     \x1b[35m{}\x1b[0m:", ir::EntityKindToString(kind));

               for (const auto& entry : entries)
               {
                    std::println("       \x1b[35m{}\x1b[0m", entry.has_value() ? entry.value() : "<unnamed>");
               }
          }
     }

     [[nodiscard]] FileIterationStatus DoFileIteration(SourceFile& source, CompilerConfig& config,
                                                       std::vector<std::byte>& generatedMachineCode,
                                                       std::vector<std::pair<std::string, std::size_t>>& functions,
                                                       codegen::CodeEmitter& emitter, std::size_t& mainOffset,
                                                       abi::api::Target& target)
     {
          g_diagnosticsReferences.emplace(source.name, &source.diagnostics);

          try
          {
               if (config.IsVerbose()) std::println("Compiling {}...", source.name);

               std::vector<MacroReplacement> macros;
               macros.reserve(16);

               macros.emplace_back("__cplusplus", std::nullopt, "202302", false);
               macros.emplace_back("__LINE__", std::nullopt, "1", false);
               macros.emplace_back("__FILE__", std::nullopt, "\"" + source.name + "\"", false);

               const auto now = std::chrono::system_clock::now();

               const std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(now)};

               macros.emplace_back("__DATE__", std::nullopt, std::format("\"{:%b %e %Y}\"", ymd), false);

               const std::chrono::hh_mm_ss hms{
                    std::chrono::floor<std::chrono::seconds>(now - std::chrono::floor<std::chrono::days>(now))};

               macros.emplace_back("__TIME__", std::nullopt, std::format("\"{:%T}\"", hms), false);
               macros.emplace_back("__ecpps_stl_version", std::nullopt, "0", false);
               macros.emplace_back("__ecpps_stl_version_minor", std::nullopt, "0", false);
               macros.emplace_back("__ecpps_stl_version_patch", std::nullopt, "1", false);
               macros.emplace_back("__ecpps_version", std::nullopt, "000001", false);
               macros.emplace_back("__ecpps_version_minor", std::nullopt, "0", false);
               macros.emplace_back("__ecpps_version_patch", std::nullopt, "1", false);

               std::set<std::filesystem::path> includedFiles;

               Preprocessor preprocessor;

               const auto ppTokens =
                    preprocessor.Parse(source.contents, macros, source.name, includedFiles, config.includeDirectories);

               std::ranges::move(preprocessor.diagnostics, std::back_inserter(source.diagnostics.diagnosticsList));

               if (config.IsVerbose(ecpps::VerboseFeature::Preprocessor))
               {
                    std::println();
                    std::println("Preprocessor output:");
                    ecpps::Preprocessor::Print(ppTokens);
               }

               const auto tokens = Tokeniser::Tokenise(ppTokens);

               if (config.IsVerbose(VerboseFeature::Tokens))
               {
                    std::println();
                    std::println("Tokens:");
                    Tokeniser::Print(tokens);
               }

               ast::ASTContext astContext;
               ast::AST astParser{tokens, source.diagnostics};
               auto ast = astParser.Parse(astContext);

               if (config.IsVerbose(VerboseFeature::AST))
               {
                    std::println();
                    std::println("AST:");

                    for (const auto& node : ast) std::println("{}", node->ToString(0));
               }

               BumpAllocator irAllocator;

               const auto ir = ir::IR::Parse(source.diagnostics, irAllocator, ast);

               if (config.IsVerbose(VerboseFeature::IR))
               {
                    std::println();
                    std::println("IR:");

                    for (const auto& node : ir) std::println("{}", node->ToString(0));
               }

               ast.clear();
               astContext.Release();

               codegen::Compile(config, source, ir, &target);

               if (config.IsVerbose(VerboseFeature::VInst)) PrintVirtualInstructions(source);

               for (auto& routine : source.compiledRoutines)
               {
                    routine.physicalInstructions = target.encoder->Encode(routine.virtualInstructions);

                    if (config.IsVerbose(VerboseFeature::IInst))
                    {
                         std::println();
                         std::println("Intermediate Instructions: {}", routine.name);

                         for (const auto& instruction : routine.physicalInstructions)
                         {
                              std::println("  {}", target.encoder->Stringify(instruction));
                         }
                    }

                    target.encoder->Finalise(routine.physicalInstructions);
               }

               if (config.IsVerbose(VerboseFeature::PInst)) PrintPhysicalInstructions(source, target);

               std::unordered_map<std::string, std::size_t> routines;
               routines.reserve(source.compiledRoutines.size());

               for (const auto& routine : source.compiledRoutines)
               {
                    const auto machineCode = emitter.EmitRoutine(routine, generatedMachineCode.size());

                    routines.emplace(routine.name, generatedMachineCode.size());

                    generatedMachineCode.append_range(machineCode);
               }

               for (const auto placement : emitter._stringRelocation)
               {
                    auto bytes = std::span{generatedMachineCode.data() + placement, emitter._stringRelocationSize};

                    auto* dword = std::bit_cast<std::uint32_t*>(bytes.data());

                    *dword += 0x4000 - 0x1000;

                    std::memcpy(bytes.data(), dword, sizeof(*dword));
               }

               for (const auto& [procedureName, procedureOffset] : routines)
               {
                    if (procedureName == "_EntryPoint") mainOffset = procedureOffset;

                    functions.emplace_back(procedureName, procedureOffset);
               }

               if (config.IsVerbose(VerboseFeature::Emit)) PrintEmittedRoutines(generatedMachineCode, functions);

               PrintDiagnostics(source);

               if (HasFatalDiagnostics(source, config)) return FileIterationStatus::Failure;
          }
          catch (const TracedException& exception)
          {
               try
               {
                    PrintDiagnostics(source);
               }
               catch (const TracedException& nested)
               {
                    IssueICE(nested);
               }
               catch (const std::exception& nested)
               {
                    IssueICE(nested.what());
               }

               IssueICE(exception);
          }
          catch (const std::exception& exception)
          {
               try
               {
                    PrintDiagnostics(source);
               }
               catch (const TracedException& nested)
               {
                    IssueICE(nested);
               }
               catch (const std::exception& nested)
               {
                    IssueICE(nested.what());
               }

               IssueICE(exception.what());
          }
          catch (...)
          {
               try
               {
                    PrintDiagnostics(source);
               }
               catch (const TracedException& nested)
               {
                    IssueICE(nested);
               }
               catch (const std::exception& nested)
               {
                    IssueICE(nested.what());
               }

               IssueICE("unknown");
          }

          return FileIterationStatus::Success;
     }

     [[nodiscard]] bool ConfigureABI(const CompilerConfig& config)
     {
          using typeSystem::TypeKind;

          constexpr auto TranslateSize = [](const Size size) constexpr
          {
               using enum Size;

               return size == Short  ? TypeKind::Short
                      : size == Int  ? TypeKind::Int
                      : size == Long ? TypeKind::Long
                                     : TypeKind::LongLong;
          };

          auto& abi = abi::ABI::Current();

          abi.sizeSize = TranslateSize(config.sizeSize);

          abi.ptrdiffSize = TranslateSize(config.ptrdiffSize);

          abi.intptrSize = TranslateSize(config.intptrSize);

          return true;
     }

     [[nodiscard]] std::unique_ptr<codegen::CodeEmitter> CreateEmitter(const CompilerConfig& config)
     {
          auto emitter = codegen::CodeEmitter::New(abi::ABI::Current().Isa());

          if (emitter == nullptr)
          {
               std::println("\x1b[31mUnsupported architecture for code generation\x1b[0m");

               return nullptr;
          }

          if (config.IsVerbose()) std::println("Code generator: {}", emitter->Name());

          return emitter;
     }

     [[nodiscard]] int WriteOutput(const CompilerConfig& config, const std::vector<std::byte>& imageBytes)
     {
          std::ofstream output(config.outputImage, std::ios::binary);

          if (!output.is_open())
          {
               std::println("\x1b[31mFailed to open output file: {}\x1b[0m", config.outputImage);

               return -1;
          }

          output.write(reinterpret_cast<const char*>(imageBytes.data()),
                       static_cast<std::streamsize>(imageBytes.size()));

          if (!output)
          {
               std::println("\x1b[31mFailed while writing output file: {}\x1b[0m", config.outputImage);

               return -1;
          }

          return 0;
     }
} // namespace

int main(int argc, char* argv[])
{
#ifdef _WIN32
     EnableVirtualProcessing();
#endif

     ecpps::RegisterErrorCallbacks();

     try
     {
          const auto startTime = std::chrono::steady_clock::now();

          ecpps::abi::BackendRegistry::RegisterTargets();

          ecpps::CompilerConfig config{argc, argv};

          ecpps::fs::GetSourceScanner().configuration = &config;

          ecpps::SourceMap sources{config};

          if (sources.files.empty())
          {
               std::println("\x1b[31mNo input files\x1b[0m");

               return -1;
          }

          const auto targetContext = config.target;

          if (config.IsVerbose())
          {
               std::println("ISA: {}", targetContext.isa);
               std::println("Platform: {}", targetContext.platform);
               std::println("SDK: {}", targetContext.sdk);
               std::println("CPU: {}", targetContext.cpu);
          }

          ecpps::ir::GetTypeContext().optimisations = config.optimisations;

          if (!ConfigureABI(config)) return -1;

          auto& target = ecpps::abi::BackendRegistry::Get(targetContext);

          if (target.encoder == nullptr)
          {
               std::println("\x1b[31mNo instruction encoder is available "
                            "for the selected target\x1b[0m");

               return -1;
          }

          if (target.platform == nullptr)
          {
               std::println("\x1b[31mNo platform backend is available "
                            "for the selected target\x1b[0m");

               return -1;
          }

          if (target.sdk == nullptr)
          {
               std::println("\x1b[31mNo SDK backend is available "
                            "for the selected target\x1b[0m");

               return -1;
          }

          auto emitter = CreateEmitter(config);

          if (emitter == nullptr) return -1;

          std::vector<std::byte> generatedMachineCode;
          std::vector<std::pair<std::string, std::size_t>> functions;

          std::size_t mainOffset{};

          generatedMachineCode.reserve(4096);
          functions.reserve(128);

          ecpps::g_diagnosticsReferences.reserve(sources.files.size());

          bool hadErrors = false;

          for (auto& source : sources.files)
          {
               const auto result =
                    DoFileIteration(source, config, generatedMachineCode, functions, *emitter, mainOffset, target);

               hadErrors |= result == FileIterationStatus::Failure;
          }

          if (hadErrors)
          {
               const auto endTime = std::chrono::steady_clock::now();

               std::println("Compilation failed. {} elapsed", endTime - startTime);

               return -1;
          }

          if (config.IsVerbose(VerboseFeature::FinalEmit)) PrintStringTable(config);

          if (config.IsVerbose()) std::println("Linking objects...");

          std::vector<std::byte> codeSection;

          config.stringArray.emplace_back(u8'\0');

          const auto stringBytes = config.stringArray |
                                   std::views::transform(
                                        [](const char8_t character)
                                        {
                                             return static_cast<std::byte>(character);
                                        }) |
                                   std::ranges::to<std::vector>();

          const auto imageBytes = ecpps::linker::Linker::SelectAndLink(
               config, generatedMachineCode, functions, mainOffset, emitter->linkerForwardedRelocations, codeSection,
               emitter->_stringRelocation, 4, stringBytes);

          if (imageBytes.empty())
          {
               std::println("\x1b[31mNo linker was selected or "
                            "the linker produced no output.\x1b[0m");

               return -1;
          }

          if (config.IsVerbose(VerboseFeature::FinalEmit))
          {
               std::println();
               std::println("Emitted {} bytes, of which {} are code:", imageBytes.size(), codeSection.size());

               PrintHexDump(codeSection);
          }

          if (WriteOutput(config, imageBytes) != 0) return -1;

          const auto outputPath = std::filesystem::absolute(config.outputImage);

          const auto endTime = std::chrono::steady_clock::now();

          if (config.IsVerbose())
          {
               std::println("Fully linked {}", outputPath.string());

               std::println("Compilation successful. {}ms elapsed",
                            static_cast<double>(
                                 std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count()) /
                                 1000.0);
          }

          if (config.IsVerbose(VerboseFeature::FinalEmit)) PrintTypeStatistics();

          if (config.useDebugger) return ecpps::debugging::Debugger::SelectAndDebug(config, outputPath);

          return 0;
     }
     catch (const std::exception& exception)
     {
          ecpps::IssueICE(exception.what());
     }
     catch (...)
     {
          ecpps::IssueICE("unknown");
     }
}
