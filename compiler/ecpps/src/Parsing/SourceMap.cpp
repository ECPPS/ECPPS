#include "SourceMap.h"
#include <fstream>
#include <print>

ecpps::SourceMap::SourceMap(CompilerConfig& config) : _config(std::ref(config))
{
     for (const auto& fileName : config.sourceFiles)
     {
          std::ifstream file{fileName, std::ios::binary | std::ios::ate};
          if (!file)
          {
               std::println("Error: Could not open {}", fileName);
               return;
          }

          const auto width = file.tellg();
          std::string content(static_cast<std::size_t>(width), '\0');

          file.seekg(0);
          file.read(content.data(), width);

          if (content.starts_with("\xEF\xBB\xBF")) content.erase(0, 3); // remove BOM

          SourceFile sourceFile{};
          sourceFile.name = fileName;
          sourceFile.contents = content;
          file.close();
          this->files.push_back(std::move(sourceFile));
     }
}