#include <Execution/Context.h>
#include <TestHelpers.h>
#include <TypeSystem/ArithmeticTypes.h>
#include <TypeSystem/CompoundTypes.h>
#define CATCH_CONFIG_MAIN
#include <catch_amalgamated.hpp>

using namespace TestHelpers;
using ecpps::typeSystem::Qualifiers;
using ecpps::typeSystem::Signedness;
using ecpps::typeSystem::TypeTraitEnum;

TEST_CASE("ArithmeticTypes - Integral type sizes", "[typesystem][arithmetic_types]")
{
     SECTION("Char type - 1 byte")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Signed}};
          const auto* charType = ecpps::ir::GetContext().Get(request);
          REQUIRE((charType->Size() == 1));
     }

     SECTION("Short type - 2 bytes")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Short,
                                                              .signedness = Signedness::Signed}};
          const auto* shortType = ecpps::ir::GetContext().Get(request);
          REQUIRE((shortType->Size() == 2));
     }

     SECTION("Int type - 4 bytes")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          const auto* intType = ecpps::ir::GetContext().Get(request);
          REQUIRE((intType->Size() == 4));
     }

     SECTION("Long type - 4 bytes (Windows x64)")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Long,
                                                              .signedness = Signedness::Signed}};
          const auto* longType = ecpps::ir::GetContext().Get(request);
          REQUIRE((longType->Size() == 4));
     }

     SECTION("LongLong type - 8 bytes")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::LongLong,
                                                              .signedness = Signedness::Signed}};
          const auto* longlongType = ecpps::ir::GetContext().Get(request);
          REQUIRE((longlongType->Size() == 8));
     }
}

TEST_CASE("ArithmeticTypes - Signedness", "[typesystem][arithmetic_types]")
{
     SECTION("Signed int")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          const auto* signedInt = ecpps::ir::GetContext().Get(request);
          auto name = signedInt->RawName();
          INFO("Signed int name: " << name);
          REQUIRE((name.find("int") != std::string::npos));
     }

     SECTION("Unsigned int")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Unsigned}};
          const auto* unsignedInt = ecpps::ir::GetContext().Get(request);
          auto name = unsignedInt->RawName();
          INFO("Unsigned int name: " << name);
          REQUIRE((name.find("unsigned") != std::string::npos || name.find("uint") != std::string::npos));
     }

     SECTION("Signed vs unsigned are different types")
     {
          ecpps::ir::TypeRequest signedRequest{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          ecpps::ir::TypeRequest unsignedRequest{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Unsigned}};
          const auto* signedInt = ecpps::ir::GetContext().Get(signedRequest);
          const auto* unsignedInt = ecpps::ir::GetContext().Get(unsignedRequest);

          REQUIRE_FALSE((*signedInt == unsignedInt));
     }
}

TEST_CASE("ArithmeticTypes - Alignment", "[typesystem][arithmetic_types]")
{
     SECTION("Char alignment")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Signed}};
          const auto* charType = ecpps::ir::GetContext().Get(request);
          REQUIRE((charType->Alignment() >= 1));
     }

     SECTION("Int alignment")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          const auto* intType = ecpps::ir::GetContext().Get(request);
          REQUIRE((intType->Alignment() >= 4));
     }

     SECTION("LongLong alignment")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::LongLong,
                                                              .signedness = Signedness::Signed}};
          const auto* longlongType = ecpps::ir::GetContext().Get(request);
          REQUIRE((longlongType->Alignment() >= 8));
     }
}

TEST_CASE("ArithmeticTypes - Type traits", "[typesystem][arithmetic_types]")
{
     ecpps::ir::TypeRequest request{.kind = ecpps::ir::TypeKind::Fundamental,
                                    .qualifiers = Qualifiers::None,
                                    .data = ecpps::ir::StandardSignedIntegerRequest{
                                        .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(request);
     auto traits = intType->Traits();

     SECTION("Has Arithmetic trait") { REQUIRE(traits.Has(TypeTraitEnum::Arithmetic)); }

     SECTION("Has Integral trait") { REQUIRE(traits.Has(TypeTraitEnum::Integral)); }

     SECTION("Has Scalar trait") { REQUIRE(traits.Has(TypeTraitEnum::Scalar)); }

     SECTION("Does not have FloatingPoint trait") { REQUIRE_FALSE(traits.Has(TypeTraitEnum::FloatingPoint)); }

     SECTION("Does not have Pointer trait") { REQUIRE_FALSE(traits.Has(TypeTraitEnum::Pointer)); }
}

TEST_CASE("ArithmeticTypes - Character types", "[typesystem][arithmetic_types]")
{
     SECTION("char type")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Signed,
                                                              .isCharWithoutSign = true}};
          const auto* charType = ecpps::ir::GetContext().Get(request);
          REQUIRE((charType->Size() == 1));
          REQUIRE(charType->Traits().Has(TypeTraitEnum::Character));
     }

     SECTION("signed char")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Signed}};
          const auto* signedChar = ecpps::ir::GetContext().Get(request);
          REQUIRE((signedChar->Size() == 1));
     }

     SECTION("unsigned char")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Unsigned}};
          const auto* unsignedChar = ecpps::ir::GetContext().Get(request);
          REQUIRE((unsignedChar->Size() == 1));
     }
}

TEST_CASE("ArithmeticTypes - Pointer types", "[typesystem][arithmetic_types]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     SECTION("Pointer size matches platform")
     {
          ecpps::ir::TypeRequest ptrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::PointerRequest{.elementType = intType}};
          const auto* intPtr = ecpps::ir::GetContext().Get(ptrRequest);

// On x64, pointers should be 8 bytes
#ifdef _WIN64
          REQUIRE((intPtr->Size() == 8));
#else
          REQUIRE((intPtr->Size() >= 4));
#endif
     }

     SECTION("Pointer to char")
     {
          ecpps::ir::TypeRequest charRequest{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                              .signedness = Signedness::Signed,
                                                              .isCharWithoutSign = true}};
          const auto* charType = ecpps::ir::GetContext().Get(charRequest);
          ecpps::ir::TypeRequest ptrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::PointerRequest{.elementType = charType}};
          const auto* charPtr = ecpps::ir::GetContext().Get(ptrRequest);

          REQUIRE(charPtr->Traits().Has(TypeTraitEnum::Pointer));
          REQUIRE(charPtr->Traits().Has(TypeTraitEnum::Scalar));
     }

     SECTION("Pointer to pointer (double indirection)")
     {
          ecpps::ir::TypeRequest ptr1Request{.kind = ecpps::ir::TypeKind::Compound,
                                             .qualifiers = Qualifiers::None,
                                             .data = ecpps::ir::PointerRequest{.elementType = intType}};
          const auto* intPtr = ecpps::ir::GetContext().Get(ptr1Request);
          ecpps::ir::TypeRequest ptr2Request{.kind = ecpps::ir::TypeKind::Compound,
                                             .qualifiers = Qualifiers::None,
                                             .data = ecpps::ir::PointerRequest{.elementType = intPtr}};
          const auto* intPtrPtr = ecpps::ir::GetContext().Get(ptr2Request);

#ifdef _WIN64
          REQUIRE((intPtrPtr->Size() == 8));
#endif
     }
}
TEST_CASE("ArithmeticTypes - Type names", "[typesystem][arithmetic_types]")
{
     SECTION("Int type name")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          const auto* intType = ecpps::ir::GetContext().Get(request);
          auto name = intType->RawName();
          REQUIRE(!name.empty());
          INFO("Int type name: " << name);
     }

     SECTION("Unsigned int type name")
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Unsigned}};
          const auto* uintType = ecpps::ir::GetContext().Get(request);
          auto name = uintType->RawName();
          REQUIRE(!name.empty());
          INFO("Unsigned int type name: " << name);
     }

     SECTION("Pointer type name")
     {
          ecpps::ir::TypeRequest intRequest{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                              .signedness = Signedness::Signed}};
          const auto* intType = ecpps::ir::GetContext().Get(intRequest);
          ecpps::ir::TypeRequest ptrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::PointerRequest{.elementType = intType}};
          const auto* intPtr = ecpps::ir::GetContext().Get(ptrRequest);
          auto name = intPtr->RawName();
          REQUIRE(!name.empty());
          REQUIRE((name.find('*') != std::string::npos));
          INFO("Pointer type name: " << name);
     }
}

TEST_CASE("ArithmeticTypes - All integral type combinations", "[typesystem][arithmetic_types]")
{
     struct TypeSpec
     {
          ecpps::typeSystem::TypeKind kind;
          Signedness sign;
     };

     constexpr auto types =
         std::to_array<TypeSpec>({{.kind = ecpps::typeSystem::TypeKind::Char, .sign = Signedness::Signed},
                                  {.kind = ecpps::typeSystem::TypeKind::Char, .sign = Signedness::Unsigned},
                                  {.kind = ecpps::typeSystem::TypeKind::Short, .sign = Signedness::Signed},
                                  {.kind = ecpps::typeSystem::TypeKind::Short, .sign = Signedness::Unsigned},
                                  {.kind = ecpps::typeSystem::TypeKind::Int, .sign = Signedness::Signed},
                                  {.kind = ecpps::typeSystem::TypeKind::Int, .sign = Signedness::Unsigned},
                                  {.kind = ecpps::typeSystem::TypeKind::Long, .sign = Signedness::Signed},
                                  {.kind = ecpps::typeSystem::TypeKind::Long, .sign = Signedness::Unsigned},
                                  {.kind = ecpps::typeSystem::TypeKind::LongLong, .sign = Signedness::Signed},
                                  {.kind = ecpps::typeSystem::TypeKind::LongLong, .sign = Signedness::Unsigned}});

     for (const auto& spec : types)
     {
          ecpps::ir::TypeRequest request{
              .kind = ecpps::ir::TypeKind::Fundamental,
              .qualifiers = Qualifiers::None,
              .data = ecpps::ir::StandardSignedIntegerRequest{.size = spec.kind, .signedness = spec.sign}};
          const auto* type = ecpps::ir::GetContext().Get(request);

          INFO("Testing " << type->RawName());
          REQUIRE((type->Size() > 0));
          REQUIRE((type->Alignment() > 0));
          REQUIRE((type->Alignment() <= type->Size()));
          REQUIRE(type->Traits().Has(TypeTraitEnum::Arithmetic));
          REQUIRE(type->Traits().Has(TypeTraitEnum::Integral));
     }
}
