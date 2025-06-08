#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <print>
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

     for (auto& source : sources.files)
     {
          std::println("Compiling {}...", source.name);

          const auto ppTokens = ecpps::Preprocessor::Parse(source.contents);
          const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          std::println();
          std::println("Tokens:");
          ecpps::Tokeniser::Print(tokens);
          const auto ast = ecpps::ast::AST{tokens, source.diagnostics}.Parse();
          std::println();
          std::println("AST:");
          for (const auto& node : ast) std::println("{}", node->ToString(0));
          const auto ir = ecpps::ir::IR::Parse(source.diagnostics, ast);
          std::println();
          std::println("IR:");
          for (const auto& node : ir) std::println("{}", node->ToString(0));
          ecpps::codegen::Compile(source, ir);
          std::println();
          std::println("Assembly:");

          for (const auto& procedure : source.compiledRoutines)
          {
               const std::size_t offset = generatedMachineCode.size();
               if (procedure.name == "main") mainOffset = offset;

               functions.emplace_back(procedure.name, offset);

               std::println("{}:", procedure.name);
               for (const auto& instruction : procedure.instructions)
               {
                    std::println("     {}", ecpps::codegen::ToString(instruction));
               }

               const auto machineCode = emitter->EmitRoutine(procedure);
               generatedMachineCode.append_range(machineCode);

               std::println("Emitted {} bytes:", machineCode.size());
               constexpr std::size_t RowSize = 8; // in bytes
               const auto rows = (machineCode.size() + RowSize - 1) / RowSize;
               for (std::size_t row = 0; row < rows; row++)
               {
                    std::print("| ");
                    const auto offset = row * RowSize;
                    for (std::size_t column = 0; column < RowSize; column++)
                    {
                         const auto byteOffset = offset + column;
                         if (byteOffset >= machineCode.size()) std::print("   ");
                         else
                              std::print("{:02x} ", static_cast<std::size_t>(machineCode.at(byteOffset)));
                    }
                    std::println("|");
               }
          }

          for (const auto& diag : source.diagnostics.diagnosticsList)
          {
               ecpps::diagnostics::PrintDiagnostic(source.name, diag);

               if (diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Error && (!config.warningsAreErrors || diag->Level() != ecpps::diagnostics::DiagnosticsLevel::Warning))
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

     std::println("Linking objects...");

     std::vector<std::byte> imageBytes =
         ecpps::linker::Linker::SelectAndLink(config, generatedMachineCode, functions, mainOffset);

     std::ofstream outFile(config.outputImage, std::ios::binary);
     outFile.write(reinterpret_cast<const char*>(imageBytes.data()), imageBytes.size());
     outFile.close();

     const auto end = std::chrono::steady_clock::now();
     std::println("Compilation successful. {} elapsed",
                  std::chrono::duration_cast<std::chrono::milliseconds>(end - start));

     return 0;
}