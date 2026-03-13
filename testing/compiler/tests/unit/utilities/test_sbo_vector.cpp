#define CATCH_CONFIG_MAIN
#include <SBOVector.h>
#include <TestHelpers.h>
#include <catch_amalgamated.hpp>
#include <string>
#include <vector>

using namespace ecpps;

TEST_CASE("SBOVector - Construction", "[utilities][sbo_vector]")
{
     SECTION("Default construction")
     {
          SBOVector<int> vec;
          REQUIRE((vec.Size() == 0));
     }

     SECTION("Construction with size")
     {
          SBOVector<int> vec(10);
          REQUIRE((vec.Size() == 10));
     }

     SECTION("Construction with size and value")
     {
          SBOVector<int> vec(5, 42);
          REQUIRE((vec.Size() == 5));
          for (std::size_t i = 0; i < vec.Size(); ++i) { REQUIRE((vec.begin()[i] == 42)); }
     }

     SECTION("Push elements to construct")
     {
          SBOVector<int> vec;
          vec.Push(1);
          vec.Push(2);
          vec.Push(3);
          vec.Push(4);
          vec.Push(5);

          REQUIRE((vec.Size() == 5));
          REQUIRE((vec.begin()[0] == 1));
          REQUIRE((vec.begin()[4] == 5));
     }
}

TEST_CASE("SBOVector - Capacity transitions", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;

     SECTION("Inline storage - stays on stack")
     {
          // Add elements up to inline capacity
          for (int i = 0; i < 8; ++i) { vec.Push(i); }
          REQUIRE((vec.Size() == 8));
          REQUIRE(vec.UseSBO()); // Should still use SBO
     }

     SECTION("Transition to heap")
     {
          // Add many elements to force heap allocation
          for (int i = 0; i < 100; ++i) { vec.Push(i); }
          REQUIRE((vec.Size() == 100));
          REQUIRE(!vec.UseSBO()); // Should have transitioned to heap

          // Verify all elements
          for (int i = 0; i < 100; ++i) { REQUIRE((vec.begin()[i] == i)); }
     }
}

TEST_CASE("SBOVector - Copy semantics", "[utilities][sbo_vector]")
{
     SECTION("Copy inline vector")
     {
          SBOVector<int> vec1;
          vec1.Push(1);
          vec1.Push(2);
          vec1.Push(3);

          SBOVector<int> vec2 = vec1;

          REQUIRE((vec2.Size() == 3));
          REQUIRE((vec2.begin()[0] == 1));
          REQUIRE((vec2.begin()[1] == 2));
          REQUIRE((vec2.begin()[2] == 3));

          // Modify vec2, vec1 should be unchanged
          vec2.begin()[0] = 99;
          REQUIRE((vec1.begin()[0] == 1));
     }

     SECTION("Copy heap vector")
     {
          SBOVector<int> vec1;
          for (int i = 0; i < 100; ++i) { vec1.Push(i); }

          SBOVector<int> vec2 = vec1;
          REQUIRE((vec2.Size() == 100));

          for (int i = 0; i < 100; ++i) { REQUIRE((vec2.begin()[i] == i)); }
     }

     SECTION("Copy assignment")
     {
          SBOVector<int> vec1;
          for (int i = 1; i <= 5; ++i) { vec1.Push(i); }

          SBOVector<int> vec2;
          vec2 = vec1;

          REQUIRE((vec2.Size() == 5));
          for (std::size_t i = 0; i < vec2.Size(); ++i) { REQUIRE((vec2.begin()[i] == vec1.begin()[i])); }
     }
}

TEST_CASE("SBOVector - Move semantics", "[utilities][sbo_vector]")
{
     SECTION("Move inline vector")
     {
          SBOVector<int> vec1;
          vec1.Push(1);
          vec1.Push(2);
          vec1.Push(3);

          SBOVector<int> vec2 = std::move(vec1);

          REQUIRE((vec2.Size() == 3));
          REQUIRE((vec2.begin()[0] == 1));
          REQUIRE((vec2.begin()[1] == 2));
          REQUIRE((vec2.begin()[2] == 3));
     }

     SECTION("Move heap vector")
     {
          SBOVector<int> vec1;
          for (int i = 0; i < 100; ++i) { vec1.Push(i); }

          SBOVector<int> vec2 = std::move(vec1);
          REQUIRE((vec2.Size() == 100));

          for (int i = 0; i < 100; ++i) { REQUIRE((vec2.begin()[i] == i)); }
     }

     SECTION("Move assignment")
     {
          SBOVector<int> vec1;
          for (int i = 1; i <= 5; ++i) { vec1.Push(i); }

          SBOVector<int> vec2;
          vec2 = std::move(vec1);

          REQUIRE((vec2.Size() == 5));
     }
}

TEST_CASE("SBOVector - Iterator correctness", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;
     vec.Push(10);
     vec.Push(20);
     vec.Push(30);
     vec.Push(40);
     vec.Push(50);

     SECTION("Range-based for loop")
     {
          std::vector<int> values;
          for (auto val : vec) { values.push_back(val); }
          REQUIRE((values.size() == 5));
          REQUIRE((values[0] == 10));
          REQUIRE((values[4] == 50));
     }

     SECTION("Const iteration")
     {
          const auto& constVec = vec;
          int count = 0;
          for (const auto* it = constVec.begin(); it != constVec.end(); ++it) { ++count; }
          REQUIRE((count == 5));
     }
}

TEST_CASE("SBOVector - Modifiers", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;

     SECTION("Push")
     {
          vec.Push(10);
          vec.Push(20);
          vec.Push(30);

          REQUIRE((vec.Size() == 3));
          REQUIRE((vec.begin()[0] == 10));
          REQUIRE((vec.begin()[1] == 20));
          REQUIRE((vec.begin()[2] == 30));
     }

     SECTION("EmplaceBack")
     {
          vec.EmplaceBack(100);
          vec.EmplaceBack(200);

          REQUIRE((vec.Size() == 2));
          REQUIRE((vec.begin()[0] == 100));
          REQUIRE((vec.begin()[1] == 200));
     }

     SECTION("Push many elements")
     {
          for (int i = 0; i < 50; ++i) { vec.Push(i * 2); }

          REQUIRE((vec.Size() == 50));

          // Verify elements
          for (int i = 0; i < 50; ++i) { REQUIRE((vec.begin()[i] == i * 2)); }
     }
}

TEST_CASE("SBOVector - Element access via iterators", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;
     vec.Push(100);
     vec.Push(200);
     vec.Push(300);
     vec.Push(400);
     vec.Push(500);

     SECTION("Access via begin()")
     {
          REQUIRE((vec.begin()[0] == 100));
          REQUIRE((vec.begin()[4] == 500));

          vec.begin()[2] = 999;
          REQUIRE((vec.begin()[2] == 999));
     }

     SECTION("First and last element")
     {
          REQUIRE((*vec.begin() == 100));
          REQUIRE((*(vec.end() - 1) == 500));

          *vec.begin() = 111;
          *(vec.end() - 1) = 555;

          REQUIRE((vec.begin()[0] == 111));
          REQUIRE((vec.begin()[4] == 555));
     }
}

TEST_CASE("SBOVector - Complex types", "[utilities][sbo_vector]")
{
     struct ComplexType
     {
          int value;
          std::string name;

          ComplexType(int v, std::string n) : value(v), name(std::move(n)) {}
     };

     SBOVector<ComplexType> vec;

     SECTION("EmplaceBack complex objects")
     {
          vec.EmplaceBack(1, "first");
          vec.EmplaceBack(2, "second");
          vec.EmplaceBack(3, "third");

          REQUIRE((vec.Size() == 3));
          REQUIRE((vec.begin()[0].value == 1));
          REQUIRE((vec.begin()[0].name == "first"));
          REQUIRE((vec.begin()[2].value == 3));
          REQUIRE((vec.begin()[2].name == "third"));
     }

     SECTION("Push complex objects")
     {
          ComplexType obj1(10, "ten");
          ComplexType obj2(20, "twenty");

          vec.Push(obj1);
          vec.Push(obj2);

          REQUIRE((vec.Size() == 2));
          REQUIRE((vec.begin()[0].value == 10));
          REQUIRE((vec.begin()[1].name == "twenty"));
     }
}

TEST_CASE("SBOVector - Size tracking", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;

     SECTION("Size increases with Push")
     {
          REQUIRE((vec.Size() == 0));

          vec.Push(1);
          REQUIRE((vec.Size() == 1));

          vec.Push(2);
          REQUIRE((vec.Size() == 2));

          for (int i = 0; i < 10; ++i) { vec.Push(i); }
          REQUIRE((vec.Size() == 12));
     }

     SECTION("Size accurate after construction")
     {
          SBOVector<int> vec2(25, 42);
          REQUIRE((vec2.Size() == 25));
     }
}

TEST_CASE("SBOVector - UseSBO flag", "[utilities][sbo_vector]")
{
     SBOVector<int> vec;

     SECTION("Small vectors use SBO")
     {
          vec.Push(1);
          vec.Push(2);
          vec.Push(3);

          REQUIRE(vec.UseSBO());
     }

     SECTION("Large vectors don't use SBO")
     {
          for (int i = 0; i < 200; ++i) { vec.Push(i); }

          REQUIRE(!vec.UseSBO());
     }
}
