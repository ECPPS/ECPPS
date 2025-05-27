#include <print>
#include "Parsing/Preprocessor.h"
#include "Parsing/SourceMap.h"
#include "Parsing/Tokeniser.h"
#include "Shared/Config.h"

int main(int argc, char* argv[])
{
     ecpps::CompilerConfig config{argc, argv};
     ecpps::SourceMap sources{config};

     if (sources.files.empty())
     {
          // TODO: Error
          return -1;
     }

     for (const auto& source : sources.files)
     {
          const auto ppTokens = ecpps::Preprocessor::Parse(source.contents);
          std::println("Preprocessing tokens:");
          ecpps::Preprocessor::Print(ppTokens);
          const auto tokens = ecpps::Tokeniser::Tokenise(ppTokens);
          std::println();
          std::println("Tokens:");
          ecpps::Tokeniser::Print(tokens);
     }

     return 0;
}