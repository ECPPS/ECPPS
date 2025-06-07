#include "ArithmeticTypes.h"
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
          // TODO: Assert otherIntegral != nullptr
          if (otherIntegral->_sign != this->_sign || otherIntegral->_kind != this->_kind)
               sequence.Push(ConversionSequence::ConversionKind::IntegralConversion);
          return ConversionSequence{sequence};
     }

     return ConversionSequence{std::nullopt};
}

ecpps::typeSystem::ConversionSequence ecpps::typeSystem::CharacterType::CompareTo(
    const std::shared_ptr<TypeBase>& other)
{
     if (!this->_isUnqualified || !IsCharacter(other)) return IntegralType::CompareTo(other);

     const auto otherCharacter = std::dynamic_pointer_cast<CharacterType>(other);
     // TODO: Assert otherCharacter != nullptr

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
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_char =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::Char, "char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_signedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::SignedChar, "signed char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::CharacterType> ecpps::typeSystem::g_unsignedChar =
    std::make_shared<typeSystem::CharacterType>(ecpps::typeSystem::CharacterSign::UnsignedChar, "unsigned char",
                                                ecpps::typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_short =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::Short, "short",
                                               typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_int = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Int, "int", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_long = std::make_shared<typeSystem::IntegralType>(
    typeSystem::Signedness::Signed, typeSystem::TypeKind::Long, "long", typeSystem::Qualifiers::None);
std::shared_ptr<ecpps::typeSystem::IntegralType> ecpps::typeSystem::g_longLong =
    std::make_shared<typeSystem::IntegralType>(typeSystem::Signedness::Signed, typeSystem::TypeKind::LongLong,
                                               "long long", typeSystem::Qualifiers::None);