#include "Execution/Context.h"
#include "Machine/ABI.h"
#include "TypeSystem/TypeBase.h"
#ifdef _WIN32
#include <Windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <print>
#include <ranges>
#include <string>
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
     auto* const hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
     DWORD consoleMode{};
     GetConsoleMode(hConsoleOutput, &consoleMode);
     consoleMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
     SetConsoleMode(hConsoleOutput, consoleMode);
}
#endif

enum struct FileIterationStatus : bool
{
     Success = true,
     Failure = false
};

[[nodiscard]] static bool IsDiagnosticsCritical(const ecpps::diagnostics::DiagnosticsMessage& diagnostic,
                                                const ecpps::CompilerConfig& config)
{
     return diagnostic->Level() == ecpps::diagnostics::DiagnosticsLevel::Error ||
            (config.warningsAreErrors && diagnostic->Level() == ecpps::diagnostics::DiagnosticsLevel::Warning);
}

[[nodiscard]] static FileIterationStatus DoFileIteration(ecpps::SourceFile& source, ecpps::CompilerConfig& config,
                                                         bool isExtraVerbose,
                                                         std::vector<std::byte>& generatedMachineCode,
                                                         std::vector<std::pair<std::string, std::size_t>>& functions,
                                                         ecpps::codegen::CodeEmitter& emitter, std::size_t& mainOffset)
{
     ecpps::g_diagnosticsReferences.emplace(source.name, &source.diagnostics);

     try
     {
          if (config.verboseStatus != ecpps::VerboseStatus::Default) std::println("Compiling {}...", source.name);
          // ECPPS pushed macros (& standard)

          std::vector<ecpps::MacroReplacement> macros{};
          macros.emplace_back("__cplusplus", std::nullopt, "202302", false); // TODO: 202302L
          macros.emplace_back("__LINE__", std::nullopt, "1", false);
          macros.emplace_back("__FILE__", std::nullopt, "\"" + source.name + "\"", false);
          const auto now = std::chrono::system_clock::now();
          std::chrono::year_month_day ymd{std::chrono::floor<std::chrono::days>(now)};
          macros.emplace_back("__DATE__", std::nullopt, std::format("\"{:%b %e %Y}\"", ymd), false);
          std::chrono::hh_mm_ss hms{
              std::chrono::floor<std::chrono::seconds>(now - std::chrono::floor<std::chrono::days>(now))};
          macros.emplace_back("__TIME__", std::nullopt, std::format("\"{:%T}\"", hms), false);
          // TODO: __STDC_HOSTED__
          // TODO: __STDCPP_DEFAULT_NEW_ALIGNMENT__
          // TODO: __STDCPP_FLOAT16_T__
          // TODO: __STDCPP_FLOAT32_T__
          // TODO: __STDCPP_FLOAT64_T__
          // TODO: __STDCPP_FLOAT128_T__
          // TODO: __STDCPP_BFLOAT16_T__
          // TODO: feature-test macros
          // TODO: __STDCPP_THREADS__

          macros.emplace_back("__ecpps_stl_version", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_stl_version_minor", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_stl_version_patch", std::nullopt, "1", false);
          macros.emplace_back("__ecpps_version", std::nullopt, "000001", false);
          macros.emplace_back("__ecpps_version_minor", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_version_patch", std::nullopt, "1", false);
          std::set<std::filesystem::path> includedFiles;
          ecpps::Preprocessor preprocessor{};
          const auto ppTokens =
              preprocessor.Parse(source.contents, macros, source.name, includedFiles, config.includeDirectories);
          std::ranges::move(preprocessor.diagnostics, std::back_inserter(source.diagnostics.diagnosticsList));
          const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("Tokens:");
          if (isExtraVerbose) ecpps::Tokeniser::Print(tokens);
          ecpps::ast::ASTContext astContext{};
          ecpps::ast::AST astParser{tokens, source.diagnostics};
          auto ast = astParser.Parse(astContext);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("AST:");
          if (isExtraVerbose)
               for (const auto& node : ast) std::println("{}", node->ToString(0));
          ecpps::BumpAllocator irAllocator{};
          const auto ir = ecpps::ir::IR::Parse(source.diagnostics, irAllocator, ast);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("IR:");
          if (isExtraVerbose)
               for (const auto& node : ir) std::println("{}", node->ToString(0));
          ast.clear();
          astContext.Release();
          ecpps::codegen::Compile(config, source, ir);

          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("Assembly:");

          std::unordered_map<std::string, std::size_t> routines{};
          routines.reserve(source.compiledRoutines.size());

          for (const auto& procedure : source.compiledRoutines)
          {
               if (isExtraVerbose)
               {
                    std::println("{}:", procedure.name);
                    for (const auto& instruction : procedure.instructions)
                    {
                         std::println("     {}", ecpps::codegen::ToString(instruction));
                    }
               }

               const auto machineCode = emitter.EmitRoutine(procedure, generatedMachineCode.size());

               routines.emplace(procedure.name, generatedMachineCode.size());
               generatedMachineCode.append_range(machineCode);
               if (!isExtraVerbose) continue;
          }

          emitter.PatchCalls(generatedMachineCode, routines);

          for (const auto placemenent : emitter._stringRelocation)
          {
               auto bytes = std::span{generatedMachineCode.data() + placemenent, emitter._stringRelocationSize};
               auto* dword = std::bit_cast<std::uint32_t*>(bytes.data());
               *dword += 0x4000 - 0x1000;
               std::memcpy(bytes.data(), dword, sizeof(*dword));
          }

          for (const auto& [procedureName, procedurOffset] : routines)
          {
               if (procedureName == "_EntryPoint") mainOffset = procedurOffset;

               functions.emplace_back(procedureName, procedurOffset);
          }

          if (isExtraVerbose)
          {
               std::vector<std::pair<std::string, std::size_t>> ordered;
               ordered.reserve(routines.size());
               for (const auto& r : routines) ordered.emplace_back(r);

               std::ranges::sort(ordered, {}, &std::pair<std::string, std::size_t>::second);

               for (std::size_t i = 0; i < ordered.size(); i++)
               {
                    const auto& [routineName, routineOffset] = ordered[i];
                    std::println("{}:", routineName);

                    std::size_t start = std::min(routineOffset, generatedMachineCode.size());
                    std::size_t end = (i + 1 < ordered.size())
                                          ? std::min(ordered[i + 1].second, generatedMachineCode.size())
                                          : generatedMachineCode.size();

                    if (start >= end) continue;

                    auto machineCode =
                        std::ranges::subrange(generatedMachineCode.begin() + static_cast<std::ptrdiff_t>(start),
                                              generatedMachineCode.begin() + static_cast<std::ptrdiff_t>(end));

                    constexpr std::size_t RowSize = 8; // in bytes
                    const auto rows = (machineCode.size() + RowSize - 1) / RowSize;

                    std::println("Emitted {} bytes:", machineCode.size());
                    for (std::size_t row = 0; row < rows; row++)
                    {
                         std::print("| ");
                         const auto offset = row * RowSize;
                         for (std::size_t column = 0; column < RowSize; column++)
                         {
                              const auto byteOffset = offset + column;
                              if (byteOffset >= machineCode.size()) std::print("   ");
                              else
                                   std::print("{:02x} ", static_cast<std::size_t>(
                                                             machineCode[static_cast<std::ptrdiff_t>(byteOffset)]));
                         }
                         std::println("|");
                    }
               }
          }

          bool shouldFail = false;
          for (const auto& diagnostic : source.diagnostics.diagnosticsList)
          {
               ecpps::diagnostics::PrintDiagnostic(source.name, diagnostic);

               if (IsDiagnosticsCritical(diagnostic, config)) shouldFail = true;
          }
          if (shouldFail) return FileIterationStatus::Failure;
     }
     catch (const ecpps::TracedException& traceException)
     {
          try
          {
               for (const auto& diag : source.diagnostics.diagnosticsList)
                    ecpps::diagnostics::PrintDiagnostic(source.name, diag);
          }
          catch (const ecpps::TracedException& nestedTraceException)
          {
               ecpps::IssueICE(nestedTraceException);
          }
          catch (const std::exception& nestedException)
          {
               ecpps::IssueICE(nestedException.what());
          }

          ecpps::IssueICE(traceException);
     }
     catch (const std::exception& e)
     {
          try
          {
               for (const auto& diag : source.diagnostics.diagnosticsList)
                    ecpps::diagnostics::PrintDiagnostic(source.name, diag);
          }
          catch (const ecpps::TracedException& nestedTraceException)
          {
               ecpps::IssueICE(nestedTraceException);
          }
          catch (const std::exception& nestedException)
          {
               ecpps::IssueICE(nestedException.what());
          }

          ecpps::IssueICE(e.what());
     }
     catch (...)
     {
          try
          {
               for (const auto& diagnostic : source.diagnostics.diagnosticsList)
                    ecpps::diagnostics::PrintDiagnostic(source.name, diagnostic);
          }
          catch (const ecpps::TracedException& nestedTraceException)
          {
               ecpps::IssueICE(nestedTraceException);
          }
          catch (const std::exception& nestedException)
          {
               ecpps::IssueICE(nestedException.what());
          }

          ecpps::IssueICE("unknown");
     }
     return FileIterationStatus::Success;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
     EnableVirtualProcessing();
#endif
     ecpps::RegisterErrorCallbacks();

     try
     {
          std::vector<std::byte> strings{};

          auto startTime = std::chrono::steady_clock::now();

          ecpps::CompilerConfig config{argc, argv};
          ecpps::fs::GetSourceScanner().configuration = &config;
          ecpps::SourceMap sources{config};

          if (sources.files.empty())
          {
               std::println("\x1b[31mNo input files\x1b[0m");
               return -1;
          }

          constexpr auto translateSizes = [](ecpps::Size size)
          {
               using enum ecpps::Size;
               using ecpps::typeSystem::TypeKind;

               return size == Short  ? TypeKind::Short
                      : size == Int  ? TypeKind::Int
                      : size == Long ? TypeKind::Long
                                     : TypeKind::LongLong;
          };

          ecpps::ir::GetTypeContext().optimisations = config.optimisations;
          ecpps::abi::ABI::Current().sizeSize = translateSizes(config.sizeSize);
          ecpps::abi::ABI::Current().ptrdiffSize = translateSizes(config.ptrdiffSize);
          ecpps::abi::ABI::Current().intptrSize = translateSizes(config.intptrSize);

          auto emitter = ecpps::codegen::CodeEmitter::New(ecpps::abi::ABI::Current().Isa());
          if (emitter == nullptr)
          {
               std::println("\x1b[31mUnsupported architecture for the code generation\x1b[0m");
               return -1;
          }
          if (config.verboseStatus != ecpps::VerboseStatus::Default) std::println("Target: {}", emitter->Name());

          std::vector<std::byte> generatedMachineCode{};
          std::vector<std::pair<std::string, std::size_t>> functions{};
          std::size_t mainOffset{};

          bool hadErrors = false;
          const bool isVerbose = config.verboseStatus == ecpps::VerboseStatus::Verbose ||
                                 config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;
          const bool isExtraVerbose = config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;

          ecpps::g_diagnosticsReferences.reserve(sources.files.size());

          for (ecpps::SourceFile& source : sources.files)
          {
               hadErrors |= DoFileIteration(source, config, isExtraVerbose, generatedMachineCode, functions, *emitter,
                                            mainOffset) == FileIterationStatus::Failure;
          }

          if (hadErrors)
          {
               const auto end = std::chrono::steady_clock::now();

               std::println("Compilation failed. {} elapsed",
                            std::chrono::duration_cast<std::chrono::milliseconds>(end - startTime));

               return -1;
          }

          if (isExtraVerbose)
          {
               std::println();
               std::println("String Table Dump:");
               std::println("Size: {} bytes", config.stringArray.size());

               if (!config.stringArray.empty())
               {
                    constexpr std::size_t RowSize = 16;
                    const auto rows = (config.stringArray.size() + RowSize - 1) / RowSize;

                    for (std::size_t row = 0; row < rows; row++)
                    {
                         const auto offset = row * RowSize;

                         std::print("{:08x}: ", offset);

                         for (std::size_t column = 0; column < RowSize; column++)
                         {
                              const auto byteOffset = offset + column;
                              if (byteOffset >= config.stringArray.size()) std::print("   ");
                              else
                                   std::print("{:02x} ", static_cast<std::uint8_t>(config.stringArray[byteOffset]));

                              if (column == 7) std::print(" ");
                         }

                         std::print(" |");
                         for (std::size_t column = 0; column < RowSize; column++)
                         {
                              const auto byteOffset = offset + column;
                              if (byteOffset >= config.stringArray.size()) std::print(" ");
                              else
                              {
                                   const auto byte = static_cast<std::uint8_t>(config.stringArray[byteOffset]);
                                   if (byte >= 32 && byte < 127) std::print("{}", static_cast<char>(byte));
                                   else
                                        std::print(".");
                              }
                         }
                         std::println("|");
                    }
                    std::println();
               }
          }

          if (isVerbose) std::println("Linking objects...");

          std::vector<std::byte> codeSection{};
          config.stringArray.emplace_back(u8'\0');

          std::vector<std::byte> imageBytes = ecpps::linker::Linker::SelectAndLink(
              config, generatedMachineCode, functions, mainOffset, emitter->linkerForwardedRelocations, codeSection,
              emitter->_stringRelocation, 4,
              config.stringArray |
                  std::views::transform([](const char8_t character) { return static_cast<std::byte>(character); }) |
                  std::ranges::to<std::vector>());

          if (imageBytes.empty())
          {
               std::println("No linker selected.");
               return -1;
          }

          if (isExtraVerbose)
          {
               constexpr std::size_t RowSize = 8; // in bytes
               const auto rows = (codeSection.size() + RowSize - 1) / RowSize;

               std::println("Emitted {} bytes, of which {} are code:", imageBytes.size(), codeSection.size());
               for (std::size_t row = 0; row < rows; row++)
               {
                    std::print("| ");
                    const auto offset = row * RowSize;
                    for (std::size_t column = 0; column < RowSize; column++)
                    {
                         const auto byteOffset = offset + column;
                         if (byteOffset >= codeSection.size()) std::print("   ");
                         else
                              std::print("{:02x} ", static_cast<std::size_t>(codeSection[byteOffset]));
                    }
                    std::println("|");
               }
          }

          std::ofstream outFile(config.outputImage, std::ios::binary);
          if (!outFile.is_open())
          {
               std::println("Failed to open file: {}", config.outputImage);
               return -1;
          }

          outFile.write(reinterpret_cast<const char*>(imageBytes.data()),
                        static_cast<std::streamsize>(imageBytes.size()));
          if (!outFile)
          {
               std::println("Failed during write to: {}", config.outputImage);
               return -1;
          }

          const auto outputImagePath = absolute(std::filesystem::path(config.outputImage));
          const auto end = std::chrono::steady_clock::now();
          if (isVerbose) std::println("Fully linked {}", outputImagePath.string());
          if (isVerbose)
               std::println("Compilation successful. {}ms elapsed",
                            (std::chrono::duration_cast<std::chrono::microseconds>(end - startTime) / 1000.0).count());
          if (isExtraVerbose)
          {
               std::println("Instantiated \x1b[92m{}\x1b[0m types:", ecpps::ir::GetTypeContext().Count());
               const auto list = ecpps::ir::GetTypeContext().List();
               for (const auto& type : list)
               {
                    std::println("     \x1b[35m{}\x1b[0m \x1b[94m=> \x1b[92m{} \x1b[33mreference{}\x1b[0m",
                                 type.typePointer->RawName(), type.hitCount, type.hitCount == 1 ? "" : "s");
               }
               std::println("Created \x1b[92m{}\x1b[0m entities:", ecpps::ir::GetEntityStatistics().Count());
               for (const auto& [kind, entries] : ecpps::ir::GetEntityStatistics().Entries())
               {
                    std::println("     \x1b[35m{}\x1b[0m:", ecpps::ir::EntityKindToString(kind));
                    for (const auto& entry : entries)
                    {
                         std::println("       \x1b[35m{}\x1b[0m", entry.has_value() ? entry.value() : "<unnamed>");
                    }
               }
          }
          outFile.close();

          if (config.useDebugger) return ecpps::debugging::Debugger::SelectAndDebug(config, outputImagePath);

          return 0;
     }
     catch (const std::exception& e)
     {
          ecpps::IssueICE(e.what());
     }
     catch (...)
     {
          ecpps::IssueICE("unknown");
     }
}
