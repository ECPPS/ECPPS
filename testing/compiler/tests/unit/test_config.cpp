#include <Shared/Config.h>

#include <catch_amalgamated.hpp>

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace
{
     [[nodiscard]] ecpps::CompilerConfig MakeConfig(std::initializer_list<std::string_view> arguments)
     {
          std::vector<std::string> storage;
          storage.reserve(arguments.size());

          for (const auto argument : arguments) storage.emplace_back(argument);

          std::vector<char*> argv;
          argv.reserve(storage.size());

          for (auto& argument : storage) argv.push_back(argument.data());

          return ecpps::CompilerConfig{static_cast<int>(argv.size()), argv.data()};
     }
} // namespace

TEST_CASE("Compiler config accepts absolute input paths and recognized options", "[unit][config]")
{
     const auto config = MakeConfig({"ecpps", "/out:result.bin", "/tmp/input.cpp"});

     CHECK((config.outputImage == "result.bin"));
     REQUIRE(confi(g.sourceFiles.size() == 1));
     CHECK((config.sourceFiles.front() == "/tmp/input.cpp"));
}
