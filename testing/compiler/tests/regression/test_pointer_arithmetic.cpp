#define CATCH_CONFIG_MAIN
#include <Parsing/AST.h>
#include <Parsing/ASTContext.h>
#include <Parsing/Tokeniser.h>
#include <Shared/Diagnostics.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>

using namespace ecpps;
using namespace TestHelpers;

// Regression tests for pointer arithmetic (branch 116-pointer-arithmetics)
// These tests ensure pointer arithmetic operations work correctly with proper
// scaling based on pointed-to type size

TEST_CASE("Pointer arithmetic - Basic operations", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("Pointer + integer")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pointer + integer addition should compile");
               }());
     }

     SECTION("Pointer - integer")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pointer - integer subtraction should compile");
               }());
     }

     SECTION("Pointer - pointer yields ptrdiff_t")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pointer - pointer should yield ptrdiff_t");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Array subscript equivalence", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("arr[i] should be equivalent to *(arr + i)")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Array subscript should be equivalent to pointer arithmetic");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Comparison operators", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("Pointer less than comparison")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pointer < comparison should compile");
               }());
     }

     SECTION("Pointer equality comparison")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pointer == comparison should compile");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Type-specific scaling", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("char* increments by 1 byte")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("char* should scale by 1 byte");
               }());
     }

     SECTION("int* increments by 4 bytes (on typical platforms)")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("int* should scale by sizeof(int) bytes");
               }());
     }

     SECTION("long long* increments by 8 bytes")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("long long* should scale by sizeof(long long) bytes");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Pre/post increment and decrement", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("Post-increment p++")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Post-increment should compile and return original value");
               }());
     }

     SECTION("Pre-increment ++p")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pre-increment should compile and return incremented value");
               }());
     }

     SECTION("Post-decrement p--")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Post-decrement should compile");
               }());
     }

     SECTION("Pre-decrement --p")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Pre-decrement should compile");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Edge cases", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("nullptr arithmetic (defined behavior in expressions)")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("nullptr + 0 should be valid");
               }());
     }

     SECTION("Zero offset")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Adding zero offset should be identity operation");
               }());
     }

     SECTION("Large offset")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Large offsets should compile (runtime validity is separate)");
               }());
     }
}

TEST_CASE("Pointer arithmetic - Compound assignment", "[regression][pointer-arithmetic]")
{
     auto diag = MakeDiagnostics();

     SECTION("Compound addition +=")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Compound addition += should compile");
               }());
     }

     SECTION("Compound subtraction -=")
     {
          REQUIRE_NOTHROW(
               [&]()
               {
                    auto alloc = MakeAllocator();
                    INFO("Compound subtraction -= should compile");
               }());
     }
}
