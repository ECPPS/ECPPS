#include "SourceScanner.h"
#include <fstream>

const std::filesystem::path& ecpps::fs::SourceScanner::ResolveInclude(const std::filesystem::path& currentPath,
                                                                      const std::string& name, IncludeType type)
{
     auto& cache = this->_resolutionCache.try_emplace(currentPath).first->second.cache;
     if (cache.contains(name)) return cache.at(name);

     if (type == IncludeType::Local)
     {
          const auto& localPath = currentPath.parent_path() / name;
          if (std::filesystem::exists(localPath)) return cache.try_emplace(name, canonical(localPath)).first->second;
     }
     if (this->configuration != nullptr)
          for (const auto& includeDir : this->configuration->includeDirectories)
          {
               const auto& systemPath = std::filesystem::path(includeDir) / name;
               if (std::filesystem::exists(systemPath))
                    return cache.try_emplace(name, canonical(systemPath)).first->second;
          }
     if (type == IncludeType::System)
     {
          const auto& localPath = currentPath.parent_path() / name;
          if (std::filesystem::exists(localPath)) return cache.try_emplace(name, canonical(localPath)).first->second;
     }
     throw std::runtime_error("Include file not found: " + name);
}

const std::string& ecpps::fs::SourceScanner::GetFileContents(const std::filesystem::path& path)
{
     if (this->_contentsCache.contains(path)) return this->_contentsCache.at(path);
     std::ifstream file(path);
     if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path.string());
     std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
     if (contents.starts_with("\xEF\xBB\xBF")) contents.erase(0, 3); // remove BOM
     return this->_contentsCache.try_emplace(path, std::move(contents)).first->second;
}

ecpps::fs::SourceScanner& ecpps::fs::GetSourceScanner(void)
{
     static SourceScanner instance{};
     return instance;
}
