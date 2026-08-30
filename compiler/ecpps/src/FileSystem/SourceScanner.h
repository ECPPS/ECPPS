#pragma once

#include <Shared/Config.h>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace ecpps::fs
{
     struct FileNotFoundException
     {
          std::string name;
     };
     struct IncludeCache
     {
          std::unordered_map<std::string, std::filesystem::path> cache;
     };
     enum struct IncludeType : bool
     {
          Local, // ""
          System // <>
     };
     class SourceScanner
     {
     public:
          [[nodiscard]] const std::filesystem::path& ResolveInclude(const std::filesystem::path& currentPath,
                                                                    const std::string& name, IncludeType type);
          [[nodiscard]] const std::string& GetFileContents(const std::filesystem::path& path);
          CompilerConfig* configuration{};

     private:
          std::unordered_map<std::filesystem::path, IncludeCache> _resolutionCache;
          std::unordered_map<std::filesystem::path, std::string> _contentsCache;
     };

     SourceScanner& GetSourceScanner(void);
} // namespace ecpps::fs
