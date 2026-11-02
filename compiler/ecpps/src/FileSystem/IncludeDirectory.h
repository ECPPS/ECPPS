#pragma once

#include <filesystem>
#include <string>

namespace ecpps::fs
{
     struct PathResult
     {
          std::filesystem::path path{};
          std::string error{};

          [[nodiscard]] explicit operator bool(void) const noexcept
          {
               return this->error.empty() && !this->path.empty();
          }
     };

     class IExecutablePathProvider
     {
     public:
          virtual ~IExecutablePathProvider(void) = default;
          [[nodiscard]] virtual PathResult GetExecutablePath(void) const = 0;
     };

     class NativeExecutablePathProvider final : public IExecutablePathProvider
     {
     public:
          [[nodiscard]] PathResult GetExecutablePath() const final;
     };

     [[nodiscard]] PathResult NormalisePath(const std::filesystem::path& path);
     [[nodiscard]] std::string ValidateDirectory(const std::filesystem::path& path);
     [[nodiscard]] PathResult DetectIncludeDirectory(const IExecutablePathProvider& provider);
     [[nodiscard]] PathResult DetectIncludeDirectory(void);
} // namespace ecpps::fs
