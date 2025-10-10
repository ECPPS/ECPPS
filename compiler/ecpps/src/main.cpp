#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <print>
#include <ranges>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include "CodeGeneration/CodeEmitter.h"
#include "CodeGeneration/PseudoAssembly.h"
#include "Execution/IR.h"
#include "Linker/Linker.h"
#include "Linker/WindowsLinker.h"
#include "Parsing/AST.h"
#include "Parsing/Preprocessor.h"
#include "Parsing/SourceMap.h"
#include "Parsing/Tokeniser.h"
#include "Shared/Config.h"

int main(int argc, char* argv[])
{
     const auto start = std::chrono::steady_clock::now();

     ecpps::CompilerConfig config{argc, argv};
     ecpps::SourceMap sources{config};

     if (sources.files.empty())
     {
          std::println("\x1b[31mNo input files\x1b[0m");
          return -1;
     }

     auto emitter = ecpps::codegen::CodeEmitter::New(ecpps::abi::ABI::Current().Isa());
     if (emitter == nullptr)
     {
          std::println("\x1b[31mUnsupported architecture for the code generation\x1b[0m");
          return -1;
     }
     std::println("Target: {}", emitter->Name());

     std::vector<std::byte> generatedMachineCode{};
     std::vector<std::pair<std::string, std::size_t>> functions{};
     std::size_t mainOffset{};

     bool hadErrors = false;
     const bool isVerbose = config.verboseStatus == ecpps::VerboseStatus::Verbose ||
                            config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;
     const bool isExtraVerbose = config.verboseStatus == ecpps::VerboseStatus::ExtraVerbose;

     for (auto& source : sources.files)
     {
          std::println("Compiling {}...", source.name);
          // ECPPS pushed macros (& standard)

          std::vector<ecpps::MacroReplacement> macros{};
          macros.emplace_back("__cplusplus", std::nullopt, "202302", false); // TODO: 202302L
          // TODO: __DATE__
          // TODO: __FILE__
          macros.emplace_back("__LINE__", std::nullopt, "1", false);
          // TODO: __STDC_HOSTED__
          // TODO: __STDCPP_DEFAULT_NEW_ALIGNMENT__
          // TODO: __STDCPP_FLOAT16_T__
          // TODO: __STDCPP_FLOAT32_T__
          // TODO: __STDCPP_FLOAT64_T__
          // TODO: __STDCPP_FLOAT128_T__
          // TODO: __STDCPP_BFLOAT16_T__
          // TODO: __TIME__
          // TODO: feature-test macros
          // TODO: __STDCPP_THREADS__

          macros.emplace_back("__ecpps_stl_version", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_stl_version_minor", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_stl_version_patch", std::nullopt, "1", false);
          macros.emplace_back("__ecpps_version", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_version_minor", std::nullopt, "0", false);
          macros.emplace_back("__ecpps_version_patch", std::nullopt, "1", false);

          const auto ppTokens = ecpps::Preprocessor::Parse(source.contents, macros);
          const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("Tokens:");
          if (isExtraVerbose) ecpps::Tokeniser::Print(tokens);
          const auto ast = ecpps::ast::AST{tokens, source.diagnostics}.Parse();
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("AST:");
          if (isExtraVerbose)
               for (const auto& node : ast) std::println("{}", node->ToString(0));
          const auto ir = ecpps::ir::IR::Parse(source.diagnostics, ast);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("IR:");
          if (isExtraVerbose)
               for (const auto& node : ir) std::println("{}", node->ToString(0));
          ecpps::codegen::Compile(source, ir);
          if (isExtraVerbose) std::println();
          if (isExtraVerbose) std::println("Assembly:");

          std::unordered_map<std::string, std::size_t> routines{};
          routines.reserve(source.compiledRoutines.size());

          for (const auto& procedure : source.compiledRoutines)
          {
               const std::size_t offset = generatedMachineCode.size();
               if (procedure.name == "main") mainOffset = offset;

               functions.emplace_back(procedure.name, offset);

               if (isExtraVerbose)
               {
                    std::println("{}:", procedure.name);
                    for (const auto& instruction : procedure.instructions)
                    {
                         std::println("     {}", ecpps::codegen::ToString(instruction));
                    }
               }

               const auto machineCode = emitter->EmitRoutine(procedure, generatedMachineCode.size());
               routines.emplace(procedure.name, generatedMachineCode.size());
               generatedMachineCode.append_range(machineCode);
               if (!isExtraVerbose) continue;
          }
          emitter->PatchCalls(generatedMachineCode, routines);
          if (isExtraVerbose)
               for (auto it = routines.begin(); it != routines.end(); ++it)
               {
                    const auto& [routineName, routineOffset] = *it;
                    std::println("{}:", routineName);
                    // TODO: Assert

                    std::size_t size = (std::next(it) == routines.end()) ? generatedMachineCode.size() - routineOffset
                                                                         : std::next(it)->second - routineOffset;
                    const auto& machineCode =
                        generatedMachineCode | std::views::drop(routineOffset) | std::views::take(size);
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
                                   std::print("{:02x} ", static_cast<std::size_t>(machineCode[byteOffset]));
                         }
                         std::println("|");
                    }
               }

          for (const auto& diag : source.diagnostics.diagnosticsList)
          {
               ecpps::diagnostics::PrintDiagnostic(source.name, diag);

               if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error &&
                   (!config.warningsAreErrors || diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
                    break;
               hadErrors = true;
          }
     }

     if (hadErrors)
     {
          const auto end = std::chrono::steady_clock::now();

          std::println("Compilation failed. {} elapsed",
                       std::chrono::duration_cast<std::chrono::milliseconds>(end - start));

          return -1;
     }

     if (isVerbose) std::println("Linking objects...");

     std::vector<std::byte> codeSection{};

     std::vector<std::byte> imageBytes = ecpps::linker::Linker::SelectAndLink(
         config, generatedMachineCode, functions, mainOffset, emitter->linkerForwardedRelocations, codeSection);

     if (isExtraVerbose)
     {
          std::size_t size = codeSection.size();
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
     outFile.write(reinterpret_cast<const char*>(imageBytes.data()), imageBytes.size());
     outFile.close();

     const auto end = std::chrono::steady_clock::now();
     std::println("Fully linked {}", absolute(std::filesystem::path(config.outputImage)).string());
     std::println("Compilation successful. {} elapsed",
                  std::chrono::duration_cast<std::chrono::milliseconds>(end - start));

     return 0;
}