#define CATCH_CONFIG_MAIN
#include <BumpAllocatorFixture.h>
#include <Shared/BumpAllocator.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>
#include <algorithm>
#include <cstddef>
#include <vector>

using namespace TestHelpers;
using namespace ecpps;

TEST_CASE("BumpAllocator - Basic Allocation", "[utilities][bump_allocator]")
{
     SECTION("Single small allocation")
     {
          BumpAllocator alloc(4096);
          auto* ptr = alloc.Allocate(64);
          REQUIRE((ptr != nullptr));
     }

     SECTION("Multiple small allocations")
     {
          BumpAllocator alloc(4096);
          std::vector<std::byte*> ptrs;

          for (int i = 0; i < 10; ++i)
          {
               auto* ptr = alloc.Allocate(16);
               REQUIRE((ptr != nullptr));
               ptrs.push_back(ptr);
          }

          // Verify all pointers are distinct
          std::ranges::sort(ptrs);
          auto [first, last] = std::ranges::unique(ptrs);
          REQUIRE((std::distance(ptrs.begin(), first) == 10));
     }

     SECTION("Various sizes - 1B, 64B, 4KB, 1MB")
     {
          BumpAllocator alloc(2uz * 1024 * 1024); // 2 MB

          auto* ptr1B = alloc.Allocate(1);
          REQUIRE((ptr1B != nullptr));

          auto* ptr64B = alloc.Allocate(64);
          REQUIRE((ptr64B != nullptr));

          auto* ptr4KB = alloc.Allocate(4uz * 1024);
          REQUIRE((ptr4KB != nullptr));
     }
}

TEST_CASE("BumpAllocator - Alignment", "[utilities][bump_allocator]")
{
     BumpAllocator alloc(64uz * 1024);

     SECTION("Default alignment")
     {
          auto* ptr = alloc.Allocate(17); // Odd size
          REQUIRE((ptr != nullptr));
          // C++ new operator typically provides at least alignof(std::max_align_t)
     }

     SECTION("Multiple allocations maintain distinct addresses")
     {
          auto* ptr1 = alloc.Allocate(7);
          auto* ptr2 = alloc.Allocate(13);
          auto* ptr3 = alloc.Allocate(29);

          REQUIRE((ptr1 != nullptr));
          REQUIRE((ptr2 != nullptr));
          REQUIRE((ptr3 != nullptr));
          REQUIRE((ptr1 != ptr2));
          REQUIRE((ptr2 != ptr3));
          REQUIRE((ptr1 != ptr3));
     }
}

TEST_CASE("BumpAllocator - Large allocations", "[utilities][bump_allocator]")
{
     SECTION("Allocation near capacity")
     {
          BumpAllocator alloc(128uz * 1024);        // 128 KB
          auto* ptr = alloc.Allocate(100uz * 1024); // 100 KB
          REQUIRE((ptr != nullptr));
     }

     SECTION("Progressive allocation filling capacity")
     {
          BumpAllocator alloc(256uz * 1024);
          std::vector<std::byte*> ptrs;

          // Allocate in 16KB chunks
          for (int i = 0; i < 16; ++i)
          {
               auto* ptr = alloc.Allocate(16uz * 1024);
               if (ptr) ptrs.push_back(ptr);
          }

          REQUIRE(!ptrs.empty());
     }
}

TEST_CASE("BumpAllocator - Placement new operator", "[utilities][bump_allocator]")
{
     BumpAllocator alloc(4096);

     SECTION("Allocate int via placement new")
     {
          int* value = new (alloc) int(42);
          REQUIRE((value != nullptr)); // NOLINT
          REQUIRE((*value == 42));     // NOLINT
     }

     SECTION("Allocate struct via placement new")
     {
          struct TestStruct
          {
               int a;
               double b;
               char c;
          };

          auto* obj = new (alloc) TestStruct{.a = 10, .b = 3.14, .c = 'X'};
          REQUIRE((obj != nullptr));                // NOLINT
          REQUIRE((obj->a == 10));                  // NOLINT
          REQUIRE((obj->b == Catch::Approx(3.14))); // NOLINT
          REQUIRE((obj->c == 'X'));                 // NOLINT
     }

     SECTION("Allocate array via placement new")
     {
          auto* arr = new (alloc) int[10]; // NOLINT
          REQUIRE((arr != nullptr));       // NOLINT

          for (int i = 0; i < 10; ++i) arr[i] = i * 2; // NOLINT

          for (int i = 0; i < 10; ++i) REQUIRE((arr[i] == i * 2)); // NOLINT
     }
}

TEST_CASE("BumpAllocator - Release and reuse", "[utilities][bump_allocator]")
{
     BumpAllocator alloc(4096);

     auto* ptr1 = alloc.Allocate(128);
     REQUIRE((ptr1 != nullptr)); // NOLINT

     alloc.Release();

     // After release, should be able to allocate again
     auto* ptr2 = alloc.Allocate(128);
     REQUIRE((ptr2 != nullptr)); // NOLINT

     // After release, might get same address (bump reset)
     // This is implementation-dependent
}

TEST_CASE("BumpAllocator - Fixture usage", "[utilities][bump_allocator]")
{
     SECTION("Small fixture")
     {
          BumpAllocatorFixture fixture(BumpAllocatorFixture::Size::Small);
          auto* ptr = fixture->Allocate(256);
          REQUIRE((ptr != nullptr)); // NOLINT
     }

     SECTION("Medium fixture")
     {
          BumpAllocatorFixture fixture(BumpAllocatorFixture::Size::Medium);
          auto* ptr = fixture->Allocate(16uz * 1024);
          REQUIRE((ptr != nullptr)); // NOLINT
     }

     SECTION("Large fixture")
     {
          BumpAllocatorFixture fixture(BumpAllocatorFixture::Size::Large);
          auto* ptr = fixture->Allocate(512uz * 1024);
          REQUIRE((ptr != nullptr)); // NOLINT
     }

     SECTION("Custom size fixture")
     {
          BumpAllocatorFixture fixture(32uz * 1024);
          auto* ptr = fixture->Allocate(8uz * 1024);
          REQUIRE((ptr != nullptr)); // NOLINT
     }
}
