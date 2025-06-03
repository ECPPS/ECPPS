#include <print>
#include "Execution/IR.h"
#include "Parsing/AST.h"
#include "Parsing/Preprocessor.h"
#include "Parsing/SourceMap.h"
#include "Parsing/Tokeniser.h"
#include "Shared/Config.h"
#include "CodeGeneration/PseudoAssembly.h"

int main(int argc, char* argv[])
{
     ecpps::CompilerConfig config{argc, argv};
     ecpps::SourceMap sources{config};
     

     if (sources.files.empty())
     {
          // TODO: Error
          return -1;
     }

     for (auto& source : sources.files)
     {
          const auto ppTokens = ecpps::Preprocessor::Parse(source.contents);
          std::println("Preprocessing tokens:");
          ecpps::Preprocessor::Print(ppTokens);
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
          }
     }

     return 0;
}