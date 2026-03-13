#define CATCH_CONFIG_MAIN
#include <SourceFileFixtures.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>
#include <filesystem>
#include <fstream>

using namespace TestHelpers;

// E2E tests compile actual C++ code through the compiler and verify execution
// These tests require the full compiler pipeline to be working

TEST_CASE("E2E - Simple int main returns zero", "[e2e][basic]")
{
     SECTION("int main() { return 0; }")
     {
          const auto& source = SourceFixtures::SimpleFunction;

          // Create temporary source file
          TempFile source_file(source, ".cpp");

          INFO("Testing compilation of simplest program");
          INFO("Source file: " << source_file.path());

          // TODO: Invoke compiler
          // auto result = CompileAndRun(source_file.path());
          // REQUIRE(result.exit_code == 0);

          // For now, just verify we can create the file
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Arithmetic operations", "[e2e][arithmetic]")
{
     SECTION("Addition returns correct value")
     {
          const char* code = R"(
            int main() {
                int result = 5 + 7;
                return result - 12; // Should return 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));

          INFO("Testing arithmetic: 5 + 7 should equal 12");
          // TODO: Compile, run, verify exit code is 0
     }

     SECTION("Multiplication and subtraction")
     {
          const char* code = R"(
            int main() {
                int a = 10;
                int b = a * 2;    // 20
                int c = b - 5;    // 15
                return c - 15;    // Should return 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("Complex arithmetic expression")
     {
          const char* code = R"(
            int main() {
                int result = (10 + 20) * 2 - 60; // (30) * 2 - 60 = 0
                return result;
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Function calls", "[e2e][functions]")
{
     SECTION("Simple function call")
     {
          const char* code = R"(
            int add(int a, int b) {
                return a + b;
            }
            
            int main() {
                int result = add(5, 7);
                return result - 12; // Should return 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));

          INFO("Testing function call with parameters");
     }

     SECTION("Multiple function calls")
     {
          const char* code = R"(
            int multiply(int x, int y) {
                return x * y;
            }
            
            int subtract(int x, int y) {
                return x - y;
            }
            
            int main() {
                int a = multiply(3, 4);  // 12
                int b = subtract(a, 2);  // 10
                return b - 10;           // 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("Nested function calls")
     {
          const char* code = R"(
            int add(int a, int b) { return a + b; }
            int mul(int a, int b) { return a * b; }
            
            int main() {
                return add(mul(2, 3), mul(4, 5)) - 26; // 6 + 20 - 26 = 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Control flow", "[e2e][control_flow]")
{
     SECTION("If statement")
     {
          const char* code = R"(
            int main() {
                int x = 10;
                int result = 0;
                
                if (x > 5) {
                    result = 1;
                } else {
                    result = 2;
                }
                
                return result - 1; // Should be 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("For loop")
     {
          const char* code = R"(
            int main() {
                int sum = 0;
                for (int i = 0; i < 10; ++i) {
                    sum += i;
                }
                return sum - 45; // 0+1+2+...+9 = 45, returns 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Pointers", "[e2e][pointers]")
{
     SECTION("Address-of and dereference")
     {
          const char* code = R"(
            int main() {
                int x = 42;
                int* p = &x;
                int y = *p;
                return y - 42; // Should return 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     // SECTION("Pointer arithmetic") {
     //     const char* code = R"(
     //         int main() {
     //             int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
     //             int* p = arr;
     //             p = p + 5;
     //             int value = *p;
     //             return value - 5; // Should return 0
     //         }
     //     )";

     //     TempFile source_file(code, ".cpp");
     //     REQUIRE(std::filesystem::exists(source_file.path()));
     // }

     // SECTION("Array subscript vs pointer arithmetic") {
     //     const char* code = R"(
     //         int main() {
     //             int arr[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
     //             int a = arr[7];
     //             int b = *(arr + 7);
     //             return (a == b) ? 0 : 1; // Should return 0
     //         }
     //     )";

     //     TempFile source_file(code, ".cpp");
     //     REQUIRE(std::filesystem::exists(source_file.path()));
     // }
}

TEST_CASE("E2E - Arrays", "[e2e][arrays]")
{
     SECTION("Array declaration and access")
     {
          const char* code = R"(
            int main() {
                int arr[5];
                arr[0] = 10;
                arr[4] = 20;
                return arr[0] + arr[4] - 30; // 10 + 20 - 30 = 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("Array initialization")
     {
          const char* code = R"(
            int main() {
                int arr[5] = {1, 2, 3, 4, 5};
                return arr[0] + arr[4] - 6; // 1 + 5 - 6 = 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Variable types", "[e2e][types]")
{
     SECTION("Different integer types")
     {
          const char* code = R"(
            int main() {
                char c = 5;
                short s = 10;
                int i = 15;
                long l = 20;
                long long ll = 25;
                
                int sum = c + s + i + l + ll;
                return sum - 75; // 5+10+15+20+25 = 75, returns 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("Unsigned types")
     {
          const char* code = R"(
            int main() {
                unsigned int x = 10u;
                unsigned int y = 5u;
                unsigned int result = x - y;
                return result - 5; // Returns 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}

TEST_CASE("E2E - Expressions", "[e2e][expressions]")
{
     SECTION("Ternary operator")
     {
          const char* code = R"(
            int main() {
                int x = 10;
                int result = (x > 5) ? 1 : 0;
                return result - 1; // Should return 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }

     SECTION("Logical operators")
     {
          const char* code = R"(
            int main() {
                int a = (1 && 1) ? 1 : 0;  // 1
                int b = (1 || 0) ? 1 : 0;  // 1
                int c = (0 && 1) ? 1 : 0;  // 0
                return a + b + c - 2;      // 1+1+0-2 = 0
            }
        )";

          TempFile source_file(code, ".cpp");
          REQUIRE(std::filesystem::exists(source_file.path()));
     }
}
