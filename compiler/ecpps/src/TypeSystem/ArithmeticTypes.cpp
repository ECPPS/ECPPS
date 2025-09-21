#include "ArithmeticTypes.h"
#include <Assert.h>
#include <format>
#include <string>
#include <utility>

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
std::string ecpps::typeSystem::IntegralType::RawName(void) const noexcept
{
     switch (this->_kind)
     {
     case TypeKind::Char: return this->_sign == Signedness::Signed ? "signed char" : "unsigned char";
     case TypeKind::Short: return this->_sign == Signedness::Signed ? "short" : "unsigned short";
     case TypeKind::Int: return this->_sign == Signedness::Signed ? "int" : "unsigned int";
     case TypeKind::Long: return this->_sign == Signedness::Signed ? "long" : "unsigned long";
     case TypeKind::LongLong: return this->_sign == Signedness::Signed ? "long long" : "unsigned long long";
     }

     return "__integer";
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::IntegralType::CompareTo(const std::shared_ptr<TypeBase>& other)
{
     SBOVector<ConversionSequence::ConversionKind> sequence{};

     if (IsIntegral(other))
     {
          const auto otherIntegral = std::dynamic_pointer_cast<IntegralType>(other);
          runtime_assert(otherIntegral != nullptr,
                         std::format("Integral type `{}` was not an integral type", other->RawName()));
          if (otherIntegral->_sign != this->_sign || otherIntegral->_kind != this->_kind)
               sequence.Push(ConversionSequence::ConversionKind::IntegralConversion);
          return ConversionSequence{sequence};
     }

     return ConversionSequence{std::nullopt};
}

std::shared_ptr<ecpps::typeSystem::TypeBase> ecpps::typeSystem::IntegralType::CommonWith(
    const std::shared_ptr<TypeBase>& other)
{
     if (other == nullptr) return nullptr;

     if (CompareTo(other).SameAs()) return other;
     if (!IsIntegral(other)) return nullptr;
     const auto otherIntegral = std::dynamic_pointer_cast<IntegralType>(other);

     if (otherIntegral->_sign != this->_sign)
     {
          const auto unsignedType =
              this->_sign == Signedness::Signed ? otherIntegral : std::make_shared<IntegralType>(*this);
          const auto signedType =
              this->_sign == Signedness::Signed ? std::make_shared<IntegralType>(*this) : otherIntegral;

          if (RankInteger(unsignedType) >= RankInteger(signedType)) return unsignedType;
          if (signedType->Size() >= unsignedType->Size()) return signedType;

          return std::make_shared<IntegralType>(Signedness::Unsigned, signedType->Kind(),
                                                "unsigned " + signedType->RawName(), signedType->qualifiers());
     }

     if (RankInteger(otherIntegral) > RankInteger(*this)) return otherIntegral;
     return std::make_shared<IntegralType>(*this);
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::CharacterType::CompareTo(
    const std::shared_ptr<TypeBase>& other)
{
     if (!this->_isUnqualified || !IsCharacter(other)) return IntegralType::CompareTo(other);

     const auto otherCharacter = std::dynamic_pointer_cast<CharacterType>(other);
     runtime_assert(otherCharacter != nullptr,
                    std::format("Character type `{}` was not a character type", other->RawName()));

     if (otherCharacter->_isUnqualified)
          return ConversionSequence{SBOVector<ConversionSequence::ConversionKind>{}}; // empty sequence = exact

     return IntegralType::CompareTo(other);
}

std::string ecpps::typeSystem::CharacterType::RawName(void) const noexcept
{
     if (this->_isUnqualified) return "char";
     return this->Sign() == Signedness::Signed ? "signed char" : "unsigned char";
}

// predefined builtin types
// void
std::shared_ptr<ecpps::typeSystem::VoidType> ecpps::typeSystem::g_void =
    std::make_shared<typeSystem::VoidType>("void", ecpps::typeSystem::Qualifiers::None);

// char
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_char =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char, "char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_signedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::SignedChar, "signed char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_unsignedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::UnsignedChar, "unsigned char",
                                                ecpps::typeSystem::Qualifiers::None);

// short
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_short =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::Short, "short",
                                               typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedShort =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Short,
                                               "unsigned short", typeSystem::Qualifiers::None);

// int
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_int = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Int, "int", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedInt =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Int,
                                               "unsigned int", typeSystem::Qualifiers::None);

// long
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_long = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Long, "long", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedLong =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::Long,
                                               "unsigned long", typeSystem::Qualifiers::None);

// long long
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_longLong =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::LongLong,
                                               "unsigned long long", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_unsignedLongLong =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Unsigned, typeSystem::TypeKind::LongLong,
                                               "long long", typeSystem::Qualifiers::None);

ecpps::typeSystem::IntegerConversionRank ecpps::typeSystem::RankInteger(const std::shared_ptr<IntegralType>& integer)
{
     switch (integer->Kind())
     {
     case TypeKind::Char: IntegerConversionRank::Char;
     case TypeKind::Short: IntegerConversionRank::Short;
     case TypeKind::Int: IntegerConversionRank::Int;
     case TypeKind::Long: IntegerConversionRank::Long;
     case TypeKind::LongLong: IntegerConversionRank::LongLong;
     }

     return IntegerConversionRank::Unknown;
}

ecpps::typeSystem::IntegerConversionRank ecpps::typeSystem::RankInteger(const IntegralType& integer)
{
     switch (integer.Kind())
     {
     case TypeKind::Char: IntegerConversionRank::Char;
     case TypeKind::Short: IntegerConversionRank::Short;
     case TypeKind::Int: IntegerConversionRank::Int;
     case TypeKind::Long: IntegerConversionRank::Long;
     case TypeKind::LongLong: IntegerConversionRank::LongLong;
     }

     return IntegerConversionRank::Unknown;
}

std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::PromoteInteger(
    const std::shared_ptr<IntegralType>& integer)
{
     if (IsBoolean(integer)) return g_int;

     const auto rank = RankInteger(integer);
     if (rank < IntegerConversionRank::Int) return integer->Sign() == Signedness::Signed ? g_int : g_unsignedInt;

     return integer;
}
