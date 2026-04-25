#include "SourceMap.h"
#include <FileSystem/SourceScanner.h>
#include <fstream>
#include <print>

ecpps::SourceMap::SourceMap(CompilerConfig& config) : _config(std::ref(config))
{
     for (const auto& fileName : config.sourceFiles)
     {
          const auto& content = ecpps::fs::GetSourceScanner().GetFileContents(fileName);

          SourceFile sourceFile{};
          sourceFile.name = fileName;
          sourceFile.contents = content;

          this->files.push_back(std::move(sourceFile));
     }
}
