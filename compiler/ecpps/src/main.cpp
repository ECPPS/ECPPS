#include <chrono>
#include <cstddef>
#include <print>
#include "CodeGeneration/CodeEmitter.h"
#include "CodeGeneration/PseudoAssembly.h"
#include "Execution/IR.h"
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
          // TODO: Error
          return -1;
     }

     auto emitter = ecpps::codegen::CodeEmitter::New(ecpps::abi::ABI::Current().Isa());
     if (emitter == nullptr)
     {
          // TODO: Error
          return -1;
     }
     std::println("Target: {}", emitter->Name());

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
          const auto ir = ecpps::ir::IR::Parse(ast);
          std::println();
          std::println("IR:");
          for (const auto& node : ir) std::println("{}", node->ToString(0));
          ecpps::codegen::Compile(source, ir);
          std::println();
          std::println("Assembly:");

          for (const auto& procedure : source.compiledRoutines)
          {
               std::println("{}:", procedure.name);
               for (const auto& instruction : procedure.instructions)
               {
                    std::println("     {}", ecpps::codegen::ToString(instruction));
               }

               const auto machineCode = emitter->EmitRoutine(procedure);
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
     }

     const auto end = std::chrono::steady_clock::now();
     std::println("Compilation successful. {} elapsed",
                  std::chrono::duration_cast<std::chrono::milliseconds>(end - start));

     return 0;
}