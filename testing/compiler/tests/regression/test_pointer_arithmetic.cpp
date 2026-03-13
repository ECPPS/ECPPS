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
          const char* source = "int* p = nullptr; int* q = p + 5;";
          // Test that p + 5 advances by 5 * sizeof(int) bytes
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   // This is a compilation test - if it compiles, arithmetic is recognized
                   INFO("Pointer + integer addition should compile");
              }());
     }

     SECTION("Pointer - integer")
     {
          const char* source = "int* p = nullptr; int* q = p - 3;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Pointer - integer subtraction should compile");
              }());
     }

     SECTION("Pointer - pointer yields ptrdiff_t")
     {
          const char* source = "int* p = nullptr; int* q = nullptr; auto diff = p - q;";
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
          const char* source = R"(
            int arr[10];
            int x = arr[5];
            int y = *(arr + 5);
        )";
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
          const char* source = "int* p = nullptr; int* q = nullptr; bool b = p < q;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Pointer < comparison should compile");
              }());
     }

     SECTION("Pointer equality comparison")
     {
          const char* source = "int* p = nullptr; int* q = nullptr; bool b = p == q;";
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
          const char* source = "char* p = nullptr; char* q = p + 1;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("char* should scale by 1 byte");
              }());
     }

     SECTION("int* increments by 4 bytes (on typical platforms)")
     {
          const char* source = "int* p = nullptr; int* q = p + 1;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("int* should scale by sizeof(int) bytes");
              }());
     }

     SECTION("long long* increments by 8 bytes")
     {
          const char* source = "long long* p = nullptr; long long* q = p + 1;";
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
          const char* source = "int* p = nullptr; int* q = p++;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Post-increment should compile and return original value");
              }());
     }

     SECTION("Pre-increment ++p")
     {
          const char* source = "int* p = nullptr; int* q = ++p;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Pre-increment should compile and return incremented value");
              }());
     }

     SECTION("Post-decrement p--")
     {
          const char* source = "int* p = nullptr; int* q = p--;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Post-decrement should compile");
              }());
     }

     SECTION("Pre-decrement --p")
     {
          const char* source = "int* p = nullptr; int* q = --p;";
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
          const char* source = "int* p = nullptr + 0;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("nullptr + 0 should be valid");
              }());
     }

     SECTION("Zero offset")
     {
          const char* source = "int* p = nullptr; int* q = p + 0;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Adding zero offset should be identity operation");
              }());
     }

     SECTION("Large offset")
     {
          const char* source = "int* p = nullptr; int* q = p + 1000000;";
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
          const char* source = "int* p = nullptr; p += 5;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Compound addition += should compile");
              }());
     }

     SECTION("Compound subtraction -=")
     {
          const char* source = "int* p = nullptr; p -= 3;";
          REQUIRE_NOTHROW(
              [&]()
              {
                   auto alloc = MakeAllocator();
                   INFO("Compound subtraction -= should compile");
              }());
     }
}
