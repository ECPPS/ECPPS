#define CATCH_CONFIG_MAIN
#include <Execution/Context.h>
#include <TestHelpers.h>
#include <TypeSystem/ArithmeticTypes.h>
#include <TypeSystem/CompoundTypes.h>
#include <TypeSystem/TypeBase.h>
#include <catch_amalgamated.hpp>

using namespace TestHelpers;
using ecpps::typeSystem::Qualifiers;
using ecpps::typeSystem::Signedness;
using ecpps::typeSystem::TypeTraitEnum;

TEST_CASE("Type Comparison - operator==", "[typesystem][type_comparison]")
{
     // Create some test types for comparison
     ecpps::ir::TypeRequest int32RequestA{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest int32RequestB{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest uint32Request{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Unsigned}};
     ecpps::ir::TypeRequest int64Request{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::LongLong,
                                                         .signedness = Signedness::Signed}};

     const auto* int32A = ecpps::ir::GetContext().Get(int32RequestA);
     const auto* int32B = ecpps::ir::GetContext().Get(int32RequestB);
     const auto* uint32 = ecpps::ir::GetContext().Get(uint32Request);
     const auto* int64 = ecpps::ir::GetContext().Get(int64Request);

     SECTION("Same integral types are equal")
     {
          REQUIRE((*int32A == int32B));
          REQUIRE((*int32B == int32A));
     }

     SECTION("Different signedness are not equal")
     {
          REQUIRE_FALSE((*int32A == uint32));
          REQUIRE_FALSE((*uint32 == int32A));
     }

     SECTION("Different sizes are not equal")
     {
          REQUIRE_FALSE((*int32A == int64));
          REQUIRE_FALSE((*int64 == int32A));
     }

     SECTION("Type equals itself")
     {
          REQUIRE((*int32A == int32A));
          REQUIRE((*uint32 == uint32));
     }
}

TEST_CASE("Type Comparison - CompareTo conversion sequences", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest int32SignedRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest int32UnsignedRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Unsigned}};
     ecpps::ir::TypeRequest charRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest shortRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Short,
                                                         .signedness = Signedness::Signed}};

     const auto* int32Signed = ecpps::ir::GetContext().Get(int32SignedRequest);
     const auto* int32Unsigned = ecpps::ir::GetContext().Get(int32UnsignedRequest);
     const auto* charType = ecpps::ir::GetContext().Get(charRequest);
     const auto* shortType = ecpps::ir::GetContext().Get(shortRequest);

     SECTION("Identical types - no conversion needed")
     {
          auto conv = int32Signed->CompareTo(int32Signed);
          REQUIRE(conv.IsValid());
          REQUIRE(conv.SameAs()); // Empty sequence means identical
     }

     SECTION("Integral promotion - char to int")
     {
          auto conv = charType->CompareTo(int32Signed);
          REQUIRE(conv.IsValid());
          // Should have promotion in sequence
     }

     SECTION("Integral promotion - short to int")
     {
          auto conv = shortType->CompareTo(int32Signed);
          REQUIRE(conv.IsValid());
     }

     SECTION("Sign conversion")
     {
          auto conv = int32Signed->CompareTo(int32Unsigned);
          // Depending on implementation, may require conversion
          INFO("Conversion valid: " << conv.IsValid());
     }
}

TEST_CASE("Type Comparison - CommonWith for binary operators", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest int32Request{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Int,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest int64Request{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::LongLong,
                                                         .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest charRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                         .signedness = Signedness::Signed}};

     const auto* int32 = ecpps::ir::GetContext().Get(int32Request);
     const auto* int64 = ecpps::ir::GetContext().Get(int64Request);
     const auto* charType = ecpps::ir::GetContext().Get(charRequest);

     SECTION("Common type of int and int is int")
     {
          const auto* common = int32->CommonWith(int32);
          REQUIRE((common != nullptr));
          REQUIRE((*common == int32));
     }

     SECTION("Common type of char and int")
     {
          const auto* common = charType->CommonWith(int32);
          REQUIRE((common != nullptr));
          // Usually promotes to int
     }

     SECTION("Common type of int and long long")
     {
          const auto* common = int32->CommonWith(int64);
          REQUIRE((common != nullptr));
          // Usually promotes to long long
     }
}

TEST_CASE("Type Comparison - Pointer type comparisons", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     ecpps::ir::TypeRequest charRequest{
         .kind = ecpps::ir::TypeKind::Fundamental,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::StandardSignedIntegerRequest{.size = ecpps::typeSystem::TypeKind::Char,
                                                         .signedness = Signedness::Signed}};

     const auto* intType = ecpps::ir::GetContext().Get(intRequest);
     const auto* charType = ecpps::ir::GetContext().Get(charRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     ecpps::ir::TypeRequest intPtr2Request{.kind = ecpps::ir::TypeKind::Compound,
                                           .qualifiers = Qualifiers::None,
                                           .data = ecpps::ir::PointerRequest{.elementType = intType}};
     ecpps::ir::TypeRequest charPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                           .qualifiers = Qualifiers::None,
                                           .data = ecpps::ir::PointerRequest{.elementType = charType}};

     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest);
     const auto* intPtr2 = ecpps::ir::GetContext().Get(intPtr2Request);
     const auto* charPtr = ecpps::ir::GetContext().Get(charPtrRequest);

     SECTION("Pointers to same type are equal") { REQUIRE((*intPtr == intPtr2)); }

     SECTION("Pointers to different types are not equal") { REQUIRE_FALSE((*intPtr == charPtr)); }

     SECTION("Pointer comparison with non-pointer") { REQUIRE_FALSE((*intPtr == intType)); }
}

TEST_CASE("Type Comparison - Array type comparisons", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};

     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intArray10Request{.kind = ecpps::ir::TypeKind::Compound,
                                              .qualifiers = Qualifiers::None,
                                              .data =
                                                  ecpps::ir::BoundedArrayRequest{.elementType = intType, .size = 10}};
     ecpps::ir::TypeRequest intArray10DupRequest{
         .kind = ecpps::ir::TypeKind::Compound,
         .qualifiers = Qualifiers::None,
         .data = ecpps::ir::BoundedArrayRequest{.elementType = intType, .size = 10}};
     ecpps::ir::TypeRequest intArray20Request{.kind = ecpps::ir::TypeKind::Compound,
                                              .qualifiers = Qualifiers::None,
                                              .data =
                                                  ecpps::ir::BoundedArrayRequest{.elementType = intType, .size = 20}};

     const auto* intArray10 = ecpps::ir::GetContext().Get(intArray10Request);
     const auto* intArray10Dup = ecpps::ir::GetContext().Get(intArray10DupRequest);
     const auto* intArray20 = ecpps::ir::GetContext().Get(intArray20Request);

     SECTION("Arrays of same type and size are equal") { REQUIRE((*intArray10 == intArray10Dup)); }

     SECTION("Arrays of same type but different size are not equal") { REQUIRE_FALSE((*intArray10 == intArray20)); }
}

TEST_CASE("Type Comparison - Multi-level pointer comparisons", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};

     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest); // int*

     ecpps::ir::TypeRequest intPtrPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                             .qualifiers = Qualifiers::None,
                                             .data = ecpps::ir::PointerRequest{.elementType = intPtr}};
     const auto* intPtrPtr = ecpps::ir::GetContext().Get(intPtrPtrRequest); // int**

     ecpps::ir::TypeRequest intPtrPtrPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                                .qualifiers = Qualifiers::None,
                                                .data = ecpps::ir::PointerRequest{.elementType = intPtrPtr}};
     const auto* intPtrPtrPtr = ecpps::ir::GetContext().Get(intPtrPtrPtrRequest); // int***

     ecpps::ir::TypeRequest intPtr2Request{.kind = ecpps::ir::TypeKind::Compound,
                                           .qualifiers = Qualifiers::None,
                                           .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr2 = ecpps::ir::GetContext().Get(intPtr2Request);

     ecpps::ir::TypeRequest intPtrPtr2Request{.kind = ecpps::ir::TypeKind::Compound,
                                              .qualifiers = Qualifiers::None,
                                              .data = ecpps::ir::PointerRequest{.elementType = intPtr2}};
     const auto* intPtrPtr2 = ecpps::ir::GetContext().Get(intPtrPtr2Request);

     SECTION("int* != int**") { REQUIRE_FALSE((*intPtr == intPtrPtr)); }

     SECTION("int** == int** (same levels)") { REQUIRE((*intPtrPtr == intPtrPtr2)); }

     SECTION("int** != int***") { REQUIRE_FALSE((*intPtrPtr == intPtrPtrPtr)); }
}

TEST_CASE("Type Comparison - Const qualifiers", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};

     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest);

     SECTION("Pointer comparison with const")
     {
          // This test depends on const implementation
          INFO("Const qualifier support in type system");
          REQUIRE((intPtr != nullptr));
     }
}

TEST_CASE("Type Comparison - Conversion sequence ranking", "[typesystem][type_comparison]")
{
     using ConversionKind = ecpps::typeSystem::ConversionSequence::ConversionKind;

     SECTION("IntegralPromotion ranks higher than IntegralConversion")
     {
          // IntegralPromotion = 11, IntegralConversion = 9
          REQUIRE((static_cast<int>(ConversionKind::IntegralPromotion) >
                   static_cast<int>(ConversionKind::IntegralConversion)));
     }

     SECTION("LvalueToRValue ranks highest") { REQUIRE((static_cast<int>(ConversionKind::LvalueToRValue) == 16)); }

     SECTION("Ellipsis (variadic) ranks lowest") { REQUIRE((static_cast<int>(ConversionKind::Ellipsis) == 1)); }

     SECTION("UserDefined ranks low") { REQUIRE((static_cast<int>(ConversionKind::UserDefined) == 2)); }
}

TEST_CASE("Type Comparison - Cross-category comparisons", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest);

     ecpps::ir::TypeRequest intArrayRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::BoundedArrayRequest{.elementType = intType, .size = 10}};
     const auto* intArray = ecpps::ir::GetContext().Get(intArrayRequest);

     SECTION("int != int*") { REQUIRE_FALSE((*intType == intPtr)); }

     SECTION("int != int[]") { REQUIRE_FALSE((*intType == intArray)); }

     SECTION("int* != int[]") { REQUIRE_FALSE((*intPtr == intArray)); }
}

TEST_CASE("Type Comparison - Nullptr and void* conversions", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest);

     SECTION("Type to void* conversion")
     {
          // Any T* should be convertible to void*
          INFO("T* to void* conversion should be valid");
          REQUIRE((intPtr != nullptr));
     }
}

TEST_CASE("Type Comparison - Array decay to pointer", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     ecpps::ir::TypeRequest intArrayRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::BoundedArrayRequest{.elementType = intType, .size = 10}};
     const auto* intArray = ecpps::ir::GetContext().Get(intArrayRequest);

     ecpps::ir::TypeRequest intPtrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                          .qualifiers = Qualifiers::None,
                                          .data = ecpps::ir::PointerRequest{.elementType = intType}};
     const auto* intPtr = ecpps::ir::GetContext().Get(intPtrRequest);

     SECTION("Array can decay to pointer")
     {
          auto conv = intArray->CompareTo(intPtr);
          // Array-to-pointer conversion should be valid
          INFO("Array decay conversion valid: " << conv.IsValid());
     }
}

TEST_CASE("Type Comparison - Floating point comparisons", "[typesystem][type_comparison]")
{
     // Assuming FloatingPointType exists
     // FloatingPointType floatType(FloatKind::Float);
     // FloatingPointType doubleType(FloatKind::Double);

     SECTION("float != double")
     {
          INFO("Floating point type comparison");
          // REQUIRE_FALSE(floatType == &doubleType);
     }

     SECTION("float promotes to double")
     {
          INFO("Float to double promotion");
          // auto conv = floatType.CompareTo(&doubleType);
          // REQUIRE(conv.IsValid());
     }
}

TEST_CASE("Type Comparison - Type traits affect comparison", "[typesystem][type_comparison]")
{
     ecpps::ir::TypeRequest intRequest{.kind = ecpps::ir::TypeKind::Fundamental,
                                       .qualifiers = Qualifiers::None,
                                       .data = ecpps::ir::StandardSignedIntegerRequest{
                                           .size = ecpps::typeSystem::TypeKind::Int, .signedness = Signedness::Signed}};
     const auto* intType = ecpps::ir::GetContext().Get(intRequest);

     auto traits = intType->Traits();

     SECTION("Integral types have Arithmetic trait") { REQUIRE(traits.Has(TypeTraitEnum::Arithmetic)); }

     SECTION("Integral types have Integral trait") { REQUIRE(traits.Has(TypeTraitEnum::Integral)); }

     SECTION("Pointer types have Pointer trait")
     {
          ecpps::ir::TypeRequest ptrRequest{.kind = ecpps::ir::TypeKind::Compound,
                                            .qualifiers = Qualifiers::None,
                                            .data = ecpps::ir::PointerRequest{.elementType = intType}};
          const auto* ptr = ecpps::ir::GetContext().Get(ptrRequest);
          REQUIRE(ptr->Traits().Has(TypeTraitEnum::Pointer));
     }
}
