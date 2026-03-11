#include "ArithmeticTypes.h"
#include <RuntimeAssert.h>
#include "TypeBase.h"
#ifndef NDEBUG
#include <format>
#endif
#include <TypeSystem/CompoundTypes.h>
#include <string>
#include <utility>
#include "../Machine/ABI.h"

#include <Execution/Context.h>

std::size_t ecpps::typeSystem::IntegralType::Size(void) const noexcept
{
     switch (this->_kind)
     {
     case TypeKind::Char: return std::to_underlying(TypeSizes::Char);
     case TypeKind::Short: return std::to_underlying(TypeSizes::Short);
     case TypeKind::Int: return std::to_underlying(TypeSizes::Int);
     case TypeKind::Long: return std::to_underlying(TypeSizes::Long);
     case TypeKind::LongLong: return std::to_underlying(TypeSizes::LongLong);
     }

     return 0;
}
std::string ecpps::typeSystem::IntegralType::RawName(void) const
{
     std::string built{};
     if (this->IsConst()) built += "const ";
     if (this->IsVolatile()) built += "volatile ";

     switch (this->_kind)
     {
     case TypeKind::Char: return built + (this->_sign == Signedness::Signed ? "signed char" : "unsigned char");
     case TypeKind::Short: return built + (this->_sign == Signedness::Signed ? "short" : "unsigned short");
     case TypeKind::Int: return built + (this->_sign == Signedness::Signed ? "int" : "unsigned int");
     case TypeKind::Long: return built + (this->_sign == Signedness::Signed ? "long" : "unsigned long");
     case TypeKind::LongLong: return built + (this->_sign == Signedness::Signed ? "long long" : "unsigned long long");
     }

     return "__integer";
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::IntegralType::CompareTo(NonowningTypePointer other) const
{
     if (other == nullptr) return ConversionSequence{std::nullopt};
     SBOVector<ConversionSequence::ConversionKind> sequence{};

     if (IsIntegral(other))
     {
          const auto* const otherIntegral = other->CastTo<IntegralType>();
          runtime_assert(otherIntegral != nullptr,
                         std::format("Integral type `{}` was not an integral type", other->RawName()));
          if (otherIntegral->_sign != this->_sign || otherIntegral->_kind != this->_kind)
               sequence.Push(ConversionSequence::ConversionKind::IntegralConversion);
          return ConversionSequence{sequence};
     }

     return ConversionSequence{std::nullopt};
}

ecpps::typeSystem::NonowningTypePointer ecpps::typeSystem::IntegralType::CommonWith(
    const ecpps::typeSystem::NonowningTypePointer other) const
{
     if (other == nullptr) return nullptr;

     if (CompareTo(other).SameAs()) return other;
     if (!IsIntegral(other)) return nullptr;
     const auto* const otherIntegral = other->CastTo<IntegralType>();

     if (otherIntegral->_sign != this->_sign)
     {
          const auto* const unsignedType = this->_sign == Signedness::Signed ? otherIntegral : this;
          const auto* const signedType = this->_sign == Signedness::Signed ? this : otherIntegral;

          if (RankInteger(unsignedType) >= RankInteger(signedType)) return unsignedType;
          if (signedType->Size() >= unsignedType->Size()) return signedType;

          ir::TypeRequest request{};
          request.kind = ir::TypeKind::Fundamental;
          request.qualifiers = signedType->qualifiers();
          request.data =
              ir::StandardSignedIntegerRequest{.size = signedType->Kind(), .signedness = Signedness::Unsigned};

          return ir::GetContext().Get(request);
          // return ecpps::ir::GetContext().Get(ecpps::ir::GetContext().PushType(
          //     std::make_unique<IntegralType>(Signedness::Unsigned, signedType->Kind(),
          //                                    "unsigned " + signedType->RawName(), signedType->qualifiers())));
     }

     if (RankInteger(otherIntegral) > RankInteger(*this)) return otherIntegral;
     return this;
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::CharacterType::CompareTo(
    const NonowningTypePointer other) const
{
     if (!this->_isUnqualified || !IsCharacter(other)) return IntegralType::CompareTo(other);

     const auto* const otherCharacter = other->CastTo<CharacterType>();
     runtime_assert(otherCharacter != nullptr,
                    std::format("Character type `{}` was not a character type", other->RawName()));

     if (otherCharacter->_isUnqualified)
          return ConversionSequence{SBOVector<ConversionSequence::ConversionKind>{}}; // empty sequence = exact

     return IntegralType::CompareTo(other);
}

std::string ecpps::typeSystem::CharacterType::RawName(void) const noexcept
{
     std::string built{};
     if (this->IsConst()) built += "const ";
     if (this->IsVolatile()) built += "volatile ";
     if (this->_isUnqualified) built += "char";
     else
          built += this->Sign() == Signedness::Signed ? "signed char" : "unsigned char";
     return built;
}

// predefined builtin types
// void
std::unique_ptr<ecpps::typeSystem::VoidType> ecpps::typeSystem::g_void =
    std::make_unique<typeSystem::VoidType>("void", ecpps::typeSystem::Qualifiers::None);

// char
std::unique_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_char =
    std::make_unique<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char, "char",
                                                ecpps::typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_signedChar =
    std::make_unique<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::SignedChar, "signed char",
                                                ecpps::typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_unsignedChar =
    std::make_unique<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::UnsignedChar, "unsigned char",
                                                ecpps::typeSystem::Qualifiers::None);

// short
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_short =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::Short, "short",
                                               typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedShort =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Short,
                                               "unsigned short", typeSystem::Qualifiers::None);

// int
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_int = std::make_unique<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Int, "int", typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedInt =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Int,
                                               "unsigned int", typeSystem::Qualifiers::None);

// long
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_long = std::make_unique<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Long, "long", typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedLong =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Long,
                                               "unsigned long", typeSystem::Qualifiers::None);

// long long
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_longLong =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::LongLong,
                                               "long long", typeSystem::Qualifiers::None);
std::unique_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedLongLong =
    std::make_unique<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::LongLong,
                                               "unsigned long long", typeSystem::Qualifiers::None);

// commonly used types
std::unique_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_constChar =
    std::make_unique<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char, "const char",
                                                ecpps::typeSystem::Qualifiers::Const);

ecpps::typeSystem::IntegerConversionRank ecpps::typeSystem::RankInteger(const IntegralType* integer)
{
     switch (integer->Kind())
     {
     case TypeKind::Char: return IntegerConversionRank::Char;
     case TypeKind::Short: return IntegerConversionRank::Short;
     case TypeKind::Int: return IntegerConversionRank::Int;
     case TypeKind::Long: return IntegerConversionRank::Long;
     case TypeKind::LongLong: return IntegerConversionRank::LongLong;
     }

     return IntegerConversionRank::Unknown;
}

ecpps::typeSystem::IntegerConversionRank ecpps::typeSystem::RankInteger(const IntegralType& integer)
{
     switch (integer.Kind())
     {
     case TypeKind::Char: return IntegerConversionRank::Char;
     case TypeKind::Short: return IntegerConversionRank::Short;
     case TypeKind::Int: return IntegerConversionRank::Int;
     case TypeKind::Long: return IntegerConversionRank::Long;
     case TypeKind::LongLong: return IntegerConversionRank::LongLong;
     }

     return IntegerConversionRank::Unknown;
}

const ecpps::typeSystem::IntegralType* ecpps::typeSystem::PromoteInteger(const IntegralType* integer)
{
     if (IsBoolean(integer)) return g_int.get();

     const auto rank = RankInteger(integer);
     if (rank < IntegerConversionRank::Int)
          return integer->Sign() == Signedness::Signed ? g_int.get() : g_unsignedInt.get();

     return integer;
}

std::size_t ecpps::typeSystem::PointerType::Size(void) const noexcept { return abi::ABI::Current().PointerSize(); }

std::size_t ecpps::typeSystem::PointerType::Alignment(void) const noexcept { return abi::ABI::Current().PointerSize(); }

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::PointerType::CompareTo(NonowningTypePointer other) const
{
     if (IsArray(other))
     {
          const auto* const otherArray = other->CastTo<ArrayType>();
          const auto* elementType = otherArray->ElementType();
          runtime_assert(otherArray != nullptr, "Invalid array type");

          if (IsObject(elementType))
          {
               bool castedAwayQualifiers = false;
               const auto* const qualifiedType = elementType->CastTo<QualifiedType>();
               const auto* const thisType = this->BaseType()->CastTo<QualifiedType>();

               runtime_assert(qualifiedType != nullptr, "unqualifiable object type!");
               runtime_assert(thisType != nullptr, "unqualifiable object type!");

               if (qualifiedType->IsConst() && !thisType->IsConst()) castedAwayQualifiers = true;
               if (qualifiedType->IsVolatile() && !thisType->IsVolatile()) castedAwayQualifiers = true;
               if (castedAwayQualifiers)
               {
                    return ConversionSequence{std::nullopt,
                                              "converting arrays to pointers cannot cast away qualifiers"};
               }
          }

          const auto elementComparison = this->BaseType()->CompareTo(elementType);
          if (!elementComparison.SameAs()) return ConversionSequence{std::nullopt};
          return ConversionSequence{SBOVector{1, ConversionSequence::ConversionKind::ArrayToPointer}};
     }
     if (!IsPointer(other)) return ConversionSequence{std::nullopt};

     const auto* const otherPointer = other->CastTo<PointerType>();
     runtime_assert(otherPointer != nullptr, std::format("Pointer type `{}` was not a pointer type", other->RawName()));

     bool castedAwayQualifiers = false;
     const auto* const qualifiedType = otherPointer->BaseType()->CastTo<QualifiedType>();
     const auto* const thisType = this->BaseType()->CastTo<QualifiedType>();

     runtime_assert(qualifiedType != nullptr, "unqualifiable object type!");
     runtime_assert(thisType != nullptr, "unqualifiable object type!");

     if (qualifiedType->IsConst() && !thisType->IsConst()) castedAwayQualifiers = true;
     if (qualifiedType->IsVolatile() && !thisType->IsVolatile()) castedAwayQualifiers = true;
     if (castedAwayQualifiers)
     {
          return ConversionSequence{std::nullopt, "converting arrays to pointers cannot cast away qualifiers"};
     }

     if (this->_baseType->CastTo<VoidType>() != nullptr)
          return ConversionSequence{SBOVector{1, ConversionSequence::ConversionKind::PointerConversion}};

     const auto subobjectComparison = otherPointer->_baseType->CompareTo(this->_baseType);

     if (subobjectComparison.SameAs()) return ConversionSequence{SBOVector<ConversionSequence::ConversionKind>{}};

     const auto* thisQualified = this->_baseType->CastTo<QualifiedType>();
     const auto* otherQualified = otherPointer->_baseType->CastTo<QualifiedType>();

     if (thisQualified && otherQualified)
     {
          const bool addingConst = thisQualified->IsConst() && !otherQualified->IsConst();
          const bool addingVolatile = thisQualified->IsVolatile() && !otherQualified->IsVolatile();

          if ((addingConst || addingVolatile) && !castedAwayQualifiers)
          {
               const auto* thisIntegral = thisQualified->CastTo<IntegralType>();
               const auto* otherIntegral = otherQualified->CastTo<IntegralType>();

               if (thisIntegral && otherIntegral && thisIntegral->Kind() == otherIntegral->Kind())
                    return ConversionSequence{SBOVector{1, ConversionSequence::ConversionKind::QualifierConversion}};
          }
     }

     return subobjectComparison.IsValid() ? subobjectComparison : ConversionSequence{std::nullopt};
}

ecpps::typeSystem::TypeTraits ecpps::typeSystem::PointerType::Traits(void) const noexcept
{
     return TypeTraits{TypeTraitEnum::Scalar,           TypeTraitEnum::Literal, TypeTraitEnum::TriviallyCopyable,
                       TypeTraitEnum::ImplicitLifetime, TypeTraitEnum::Pointer, TypeTraitEnum::Object};
}

ecpps::typeSystem::NonowningTypePointer ecpps::typeSystem::PointerType::CommonWith(
    [[maybe_unused]] NonowningTypePointer other) const
{
     // TODO: Implement
     return nullptr;
}

std::size_t ecpps::typeSystem::ReferenceType::Size(void) const noexcept { return abi::ABI::Current().PointerSize(); }

std::size_t ecpps::typeSystem::ReferenceType::Alignment(void) const noexcept
{
     return abi::ABI::Current().PointerSize();
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::ReferenceType::CompareTo(
    [[maybe_unused]] NonowningTypePointer other) const
{
     // TODO: Implement
     throw nullptr;
}

ecpps::typeSystem::TypeTraits ecpps::typeSystem::ReferenceType::Traits(void) const noexcept
{
     return TypeTraits{TypeTraitEnum::Reference, TypeTraitEnum::Literal};
}

ecpps::typeSystem::NonowningTypePointer ecpps::typeSystem::ReferenceType::CommonWith(
    [[maybe_unused]] NonowningTypePointer other) const
{
     // TODO: Implement
     return nullptr;
}
