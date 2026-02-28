#pragma once

#include <Execution/NodeBase.h>
#include <Parsing/AST.h>
#include <Shared/BumpAllocator.h>
#include <Shared/Diagnostics.h>
#include <catch_amalgamated.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace TestHelpers
{

     /// Create a diagnostics instance for testing
     inline auto MakeDiagnostics() { return ecpps::Diagnostics{}; }

     /// Create a BumpAllocator with specified initial size
     inline auto MakeAllocator(std::size_t initialSize = 1024 * 64) { return ecpps::BumpAllocator{initialSize}; }

     /// Compare two byte sequences and return true if equal
     inline bool CompareBytes(std::span<const std::byte> left, std::span<const std::byte> right)
     {
          if (left.size() != right.size()) { return false; }

          return std::equal(left.begin(), left.end(), right.begin());
     }

     /// Enhanced byte comparison with detailed output on mismatch
     inline void RequireEqualBytes(std::span<const std::byte> actual, std::span<const std::byte> expected,
                                   std::string_view context = "")
     {
          INFO("Context: " << context);
          REQUIRE(actual.size() == expected.size());

          for (std::size_t i = 0; i < actual.size(); ++i)
          {
               if (actual[i] != expected[i])
               {
                    INFO("Byte mismatch at index " << i);
                    INFO("Expected: 0x" << std::hex << static_cast<int>(expected[i]));
                    INFO("Actual:   0x" << std::hex << static_cast<int>(actual[i]));
                    REQUIRE(actual[i] == expected[i]);
               }
          }
     }

     /// Create a temporary source file with given content, returns path
     inline std::filesystem::path CreateTempSourceFile(std::string_view content, std::string_view extension = ".cpp")
     {
          auto tempPath =
              std::filesystem::temp_directory_path() /
              ("ecpps_test_" + std::to_string(std::hash<std::string_view>{}(content)) + std::string{extension});

          std::ofstream file(tempPath);
          file << content;
          file.close();

          return tempPath;
     }

     /// RAII wrapper to ensure temp file cleanup
     class TempFile
     {
          std::filesystem::path path_;

     public:
          explicit TempFile(std::string_view content, std::string_view extension = ".cpp")
              : path_(CreateTempSourceFile(content, extension))
          {
          }

          ~TempFile()
          {
               std::error_code ec;
               std::filesystem::remove(path_, ec);
          }

          const std::filesystem::path& path() const { return path_; }
          operator std::filesystem::path() const { return path_; }
     };

     /// Compare two AST nodes recursively (basic implementation)
     template <typename NodeType> bool CompareAST(const NodeType* left, const NodeType* right)
     {
          if (left == right) return true;
          if (!left || !right) return false;

          // Base comparison - extend as needed for specific node types
          return typeid(*left) == typeid(*right);
     }

     /// Compare two IR nodes recursively (basic implementation)
     template <typename NodeType> bool CompareIR(const NodeType* left, const NodeType* right)
     {
          if (left == right) return true;
          if (!left || !right) return false;

          // Base comparison - extend as needed for specific node types
          return typeid(*left) == typeid(*right);
     }

     /// User-defined literal for byte arrays: 0x48_b, 0x89_b, etc.
     inline constexpr std::byte operator""_b(unsigned long long value) { return static_cast<std::byte>(value); }

     /// Helper to create a vector of bytes from initializer list
     inline std::vector<std::byte> MakeBytes(std::initializer_list<unsigned char> bytes)
     {
          std::vector<std::byte> result;
          result.reserve(bytes.size());
          for (auto b : bytes) { result.push_back(static_cast<std::byte>(b)); }
          return result;
     }

} // namespace TestHelpers
