#define CATCH_CONFIG_MAIN
#include <FileSystem/IncludeDirectory.h>

#include <catch_amalgamated.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace
{
     class TemporaryDirectory
     {
     public:
          TemporaryDirectory(void)
          {
               const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
               this->path = std::filesystem::temp_directory_path() / ("ecpps-path-test-" + std::to_string(stamp));
               std::filesystem::create_directories(this->path);
          }

          ~TemporaryDirectory(void)
          {
               std::error_code error;
               std::filesystem::remove_all(this->path, error);
          }

          std::filesystem::path path;
     };

     class TestPathProvider final : public ecpps::fs::IExecutablePathProvider
     {
     public:
          explicit TestPathProvider(ecpps::fs::PathResult result) : _result(std::move(result))
          {
          }

          [[nodiscard]] ecpps::fs::PathResult GetExecutablePath(void) const final
          {
               return this->_result;
          }

     private:
          ecpps::fs::PathResult _result;
     };

     void MakeFile(const std::filesystem::path& path)
     {
          std::filesystem::create_directories(path.parent_path());
          std::ofstream file(path);
          REQUIRE(file.is_open());
     }
} // namespace

// NOLINTNEXTLINE(bugprone-throwing-static-initialisation)
TEST_CASE("Include directory resolves ../include from the executable", "[unit][filesystem]")
{
     TemporaryDirectory temporary;
     const auto executable = temporary.path / "package" / "bin" / "ecpps";
     const auto include = temporary.path / "package" / "include";
     MakeFile(executable);
     std::filesystem::create_directories(include);

     const TestPathProvider provider{{.path = executable.parent_path() / ".." / "bin" / "ecpps"}};
     const auto result = ecpps::fs::DetectIncludeDirectory(provider);

     REQUIRE(result);
     CHECK((result.path == std::filesystem::canonical(include)));
     CHECK(result.path.is_absolute());

     CHECK(ecpps::fs::ValidateDirectory(include).empty());
     const auto normalised = ecpps::fs::NormalisePath(include / ".." / "include/");
     REQUIRE(normalised);
     CHECK((normalised.path == std::filesystem::canonical(include)));
}

#ifndef _WIN32
// NOLINTNEXTLINE(bugprone-throwing-static-initialisation)
TEST_CASE("Include directory resolves executable symlinks", "[unit][filesystem]")
{
     TemporaryDirectory temporary;
     const auto executable = temporary.path / "package" / "bin" / "ecpps";
     const auto include = temporary.path / "package" / "include";
     const auto alias = temporary.path / "alias" / "bin" / "ecpps";
     MakeFile(executable);
     std::filesystem::create_directories(include);
     std::filesystem::create_directories(alias.parent_path());
     std::filesystem::create_symlink(executable, alias);

     const TestPathProvider provider{{.path = alias}};
     const auto result = ecpps::fs::DetectIncludeDirectory(provider);

     REQUIRE(result);
     CHECK((result.path == std::filesystem::canonical(include)));
}
#endif

// NOLINTNEXTLINE(bugprone-throwing-static-initialisation)
TEST_CASE("Include directory reports lookup, invalid path, and missing directory errors", "[unit][filesystem]")
{
     const TestPathProvider lookupFailure{{.error = "test lookup failure"}};
     const auto lookupResult = ecpps::fs::DetectIncludeDirectory(lookupFailure);
     CHECK(lookupResult.error.contains("executable path lookup failed"));

     const TestPathProvider invalidPath{{.path = {}}};
     const auto invalidResult = ecpps::fs::DetectIncludeDirectory(invalidPath);
     CHECK(invalidResult.error.contains("invalid executable path"));

     TemporaryDirectory temporary;
     const auto executable = temporary.path / "package" / "bin" / "ecpps";
     MakeFile(executable);
     const TestPathProvider missingDirectory{{.path = executable}};
     const auto missingResult = ecpps::fs::DetectIncludeDirectory(missingDirectory);
     CHECK(missingResult.error.contains("directory is missing"));
     CHECK(!ecpps::fs::ValidateDirectory(executable).empty());
}

// NOLINTNEXTLINE(bugprone-throwing-static-initialisation)
TEST_CASE("Native executable path provider returns an absolute path", "[unit][filesystem]")
{
     const ecpps::fs::NativeExecutablePathProvider provider{};
     const auto result = provider.GetExecutablePath();

     REQUIRE(result);
     CHECK(result.path.is_absolute());
     CHECK(std::filesystem::is_regular_file(result.path));
}
